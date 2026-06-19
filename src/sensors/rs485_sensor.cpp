#include "rs485_sensor.h"
#include "config.h"
#include "web_server.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>

//* RS485 Modbus RTU driver — CWT Water pH/EC + Halisense Soil 7-in-1
//* Single shared Modbus transport, per-sensor register maps & scaling.
//* Datasheets are the source of truth for register maps and scaling.

// ── Shared sensor state ──────────────────────────────────────────────────────
float waterPh   = 0.0f;
float waterEc   = 0.0f;
float waterTemp = 0.0f;
bool  waterOK   = false;
bool  alertWaterPh = false;
bool  alertWaterEc = false;

float    soilMoist = 0.0f;
float    soilTemp  = 0.0f;
float    soilEc    = 0.0f;
float    soilPh    = 0.0f;
uint16_t soilN     = 0;
uint16_t soilP     = 0;
uint16_t soilK     = 0;
bool     soilOK    = false;
bool     alertSoilMoist = false;
bool     alertSoilEc    = false;
bool     alertSoilPh    = false;

uint32_t rs485PollCount = 0;
uint32_t rs485FailCount = 0;

// ── Thresholds (defined in web_server.cpp, NVS-persisted there) ─────────────
extern float threshWaterPhLow;
extern float threshWaterPhHigh;
extern float threshWaterEcHigh;
extern float threshSoilMoistLow;
extern float threshSoilMoistHigh;
extern float threshSoilEcHigh;
extern float threshSoilPhLow;
extern float threshSoilPhHigh;

// ── Module-private state ─────────────────────────────────────────────────────
static HardwareSerial RS485(1);          // UART1 routed to TXD0/RXD0 pads via GPIO matrix

static uint8_t  _activeType    = 1;      // 1=env(idle), 2=soil, 3=mineral(water)
static bool     _uartReady     = false;
static uint32_t _lastPollMs    = 0;
static uint8_t  _consecFails   = 0;

// ── CRC-16/Modbus (poly 0xA001, init 0xFFFF, transmitted low byte first) ────
static uint16_t modbusCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

// ── UART / transceiver helpers ───────────────────────────────────────────────
static void rs485BeginUart(uint32_t baud) {
    RS485.end();
    delay(10);
    RS485.begin(baud, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    RS485.setTimeout(RS485_TIMEOUT_MS);
    _uartReady = true;
    Serial.printf("[RS485] UART1 up — %lu,N,8,1 (TX=GPIO%d RX=GPIO%d DE/RE=GPIO%d)\n",
                  (unsigned long)baud, RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN);
}

static void rs485Flush() {
    while (RS485.available()) RS485.read();
}

// ── Core Modbus transaction: FC 0x03 Read Holding Registers ─────────────────
// Returns true and fills `out[count]` (big-endian words decoded) on success.
static bool modbusReadHolding(uint8_t addr, uint16_t startReg, uint16_t count, uint16_t* out) {
    if (!_uartReady || count == 0 || count > 16) return false;

    uint8_t req[8];
    req[0] = addr;
    req[1] = 0x03;
    req[2] = (uint8_t)(startReg >> 8);
    req[3] = (uint8_t)(startReg & 0xFF);
    req[4] = (uint8_t)(count >> 8);
    req[5] = (uint8_t)(count & 0xFF);
    uint16_t crc = modbusCrc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);   // CRC low byte first
    req[7] = (uint8_t)(crc >> 8);

    rs485Flush();

    // Transmit (DE/RE high)
    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(100);                 // transceiver enable settle
    RS485.write(req, sizeof(req));
    RS485.flush();                          // wait until shifted out of UART
    delayMicroseconds(100);                 // last stop bit margin
    digitalWrite(RS485_DE_PIN, LOW);        // back to receive

    // Receive: addr + fc + bytecount + 2*count data + 2 CRC
    const size_t expected = 5 + (size_t)count * 2;
    uint8_t buf[5 + 2 * 16];
    size_t  got   = 0;
    uint32_t t0   = millis();

    while (got < expected && (millis() - t0) < RS485_TIMEOUT_MS) {
        int avail = RS485.available();
        while (avail-- > 0 && got < sizeof(buf)) {
            buf[got++] = (uint8_t)RS485.read();
        }
        if (got < expected) delay(2);
    }

    if (got == 0) {
        Serial.println("[RS485] timeout — no response (sensor missing / wiring / baud?)");
        return false;
    }

    // Modbus exception frame? addr, fc|0x80, code, crc(2)
    if (got >= 5 && buf[0] == addr && buf[1] == (0x80 | 0x03)) {
        Serial.printf("[RS485] Modbus exception 0x%02X from addr %u\n", buf[2], addr);
        return false;
    }

    if (got < expected) {
        Serial.printf("[RS485] short frame: got %u of %u bytes\n",
                      (unsigned)got, (unsigned)expected);
        return false;
    }

    if (buf[0] != addr || buf[1] != 0x03 || buf[2] != (uint8_t)(count * 2)) {
        Serial.printf("[RS485] bad header: %02X %02X %02X (want %02X 03 %02X)\n",
                      buf[0], buf[1], buf[2], addr, count * 2);
        return false;
    }

    uint16_t rxCrc   = (uint16_t)buf[expected - 2] | ((uint16_t)buf[expected - 1] << 8);
    uint16_t calcCrc = modbusCrc16(buf, expected - 2);
    if (rxCrc != calcCrc) {
        Serial.printf("[RS485] CRC fail: rx=0x%04X calc=0x%04X — frame discarded\n",
                      rxCrc, calcCrc);
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        out[i] = ((uint16_t)buf[3 + i * 2] << 8) | buf[4 + i * 2];
    }
    return true;
}

// ── Sensor 3: CWT-OYS-PHEC Water pH/EC ───────────────────────────────────────
// Regs 0x0000..0x0002: pH(/100), EC(raw uS/cm), Temp(/10)
static bool readWaterSensor() {
    uint16_t w[3];
    if (!modbusReadHolding(WATER_SENSOR_ADDR, 0x0000, 3, w)) return false;

    float ph = w[0] / 100.0f;
    float ec = (float)w[1];
    float t  = (int16_t)w[2] / 10.0f;

    // Sanity envelope (datasheet: pH 0-14, EC 0-20000, T 0-60C)
    if (ph < 0.0f || ph > 14.0f || ec < 0.0f || ec > 20000.0f || t < -10.0f || t > 80.0f) {
        Serial.printf("[RS485:Water] out-of-range reading rejected pH=%.2f EC=%.0f T=%.1f\n",
                      ph, ec, t);
        return false;
    }

    waterPh = ph; waterEc = ec; waterTemp = t;

    alertWaterPh = (waterPh < threshWaterPhLow) || (waterPh > threshWaterPhHigh);
    alertWaterEc = (waterEc > threshWaterEcHigh);

    Serial.printf("[RS485:Water] pH:%.2f  EC:%.0f uS/cm  T:%.1f degC%s%s\n",
                  waterPh, waterEc, waterTemp,
                  alertWaterPh ? "  [pH ALERT]" : "",
                  alertWaterEc ? "  [EC ALERT]" : "");
    logPush("[Water] pH:" + String(waterPh, 2) +
            " EC:" + String(waterEc, 0) + "uS/cm T:" + String(waterTemp, 1) + "\u00b0C");
    return true;
}

// ── Sensor 2: Halisense Soil 7-in-1 ──────────────────────────────────────────
// Regs 0x0000..0x0006: Hum(/10), Temp(/10 signed), EC(raw), pH(/10), N, P, K (raw)
static bool readSoilSensor() {
    uint16_t w[7];
    if (!modbusReadHolding(SOIL_SENSOR_ADDR, 0x0000, 7, w)) return false;

    float moist = w[0] / 10.0f;
    float t     = (int16_t)w[1] / 10.0f;     // signed: range -40..80 degC
    float ec    = (float)w[2];
    float ph    = w[3] / 10.0f;

    // Sanity envelope (datasheet ranges)
    if (moist < 0.0f || moist > 100.0f || t < -45.0f || t > 85.0f ||
        ec < 0.0f || ec > 20000.0f || ph < 0.0f || ph > 14.0f) {
        Serial.printf("[RS485:Soil] out-of-range reading rejected M=%.1f T=%.1f EC=%.0f pH=%.1f\n",
                      moist, t, ec, ph);
        return false;
    }

    soilMoist = moist; soilTemp = t; soilEc = ec; soilPh = ph;
    soilN = w[4]; soilP = w[5]; soilK = w[6];

    alertSoilMoist = (soilMoist < threshSoilMoistLow) || (soilMoist > threshSoilMoistHigh);
    alertSoilEc    = (soilEc > threshSoilEcHigh);
    alertSoilPh    = (soilPh < threshSoilPhLow) || (soilPh > threshSoilPhHigh);

    Serial.printf("[RS485:Soil] M:%.1f%%  T:%.1f degC  EC:%.0f uS/cm  pH:%.1f  N:%u P:%u K:%u mg/kg\n",
                  soilMoist, soilTemp, soilEc, soilPh, soilN, soilP, soilK);
    logPush("[Soil] M:" + String(soilMoist, 1) + "% T:" + String(soilTemp, 1) +
            "\u00b0C EC:" + String(soilEc, 0) + " pH:" + String(soilPh, 1) +
            " NPK:" + String(soilN) + "/" + String(soilP) + "/" + String(soilK));
    return true;
}

// ── Type management ──────────────────────────────────────────────────────────
static void invalidateReadings() {
    waterOK = false;
    soilOK  = false;
    alertWaterPh = alertWaterEc = false;
    alertSoilMoist = alertSoilEc = alertSoilPh = false;
    _consecFails = 0;
}

void rs485ApplySensorType(uint8_t type) {
    if (type < 1 || type > 3) type = 1;
    _activeType = type;
    invalidateReadings();

    switch (_activeType) {
        case 2:
            rs485BeginUart(SOIL_SENSOR_BAUD);
            Serial.println("[RS485] Active driver: Soil 7-in-1 (Halisense)");
            logPush("[RS485] driver -> Soil 7-in-1 @4800");
            break;
        case 3:
            rs485BeginUart(WATER_SENSOR_BAUD);
            Serial.println("[RS485] Active driver: Water pH/EC (CWT-OYS-PHEC)");
            logPush("[RS485] driver -> Water pH/EC @9600");
            break;
        default:
            RS485.end();
            _uartReady = false;
            Serial.println("[RS485] No RS485 sensor selected (environment type) — UART idle");
            break;
    }

    _lastPollMs = 0;   // force immediate first poll on next loop pass
}

uint8_t rs485ActiveType() {
    return _activeType;
}

const char* rs485StatusLabel() {
    if (_activeType == 2) return soilOK  ? "ok" : "no response";
    if (_activeType == 3) return waterOK ? "ok" : "no response";
    return "idle";
}

// ── Init ─────────────────────────────────────────────────────────────────────
void rs485SensorInit() {
    Serial.println();
    Serial.println("### RS485 MODBUS DRIVER INIT ###");

    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);   // receive mode ASAP (FC may float high at boot)

    // Load sensor type directly from NVS — independent of MQTT init order,
    // so the driver also works on a not-yet-commissioned device in AP mode.
    Preferences p;
    p.begin("device", true);
    uint8_t type = p.getUChar("sensor_type", SENSOR_TYPE_DEFAULT);
    p.end();

    rs485ApplySensorType(type);
}

// ── Read (call from loop, self-throttled) ────────────────────────────────────
void rs485SensorRead() {
    if (_activeType != 2 && _activeType != 3) return;
    if (!_uartReady) return;

    uint32_t now = millis();
    if (_lastPollMs != 0 && (now - _lastPollMs) < RS485_POLL_INTERVAL) return;
    _lastPollMs = now;

    rs485PollCount++;

    bool ok = (_activeType == 2) ? readSoilSensor() : readWaterSensor();

    if (ok) {
        _consecFails = 0;
        if (_activeType == 2) soilOK  = true;
        else                  waterOK = true;
    } else {
        rs485FailCount++;
        if (_consecFails < 255) _consecFails++;

        // Keep last good values visible for transient glitches; flag the
        // sensor unavailable only after RS485_MAX_FAILS consecutive failures.
        if (_consecFails >= RS485_MAX_FAILS) {
            if (_activeType == 2 && soilOK) {
                soilOK = false;
                logPush("[RS485] Soil sensor unavailable (" + String(_consecFails) + " fails)");
            }
            if (_activeType == 3 && waterOK) {
                waterOK = false;
                logPush("[RS485] Water sensor unavailable (" + String(_consecFails) + " fails)");
            }
        }
    }
}
