#include "scd40.h"
#include "config.h"
#include "sensors.h"
#include "web_server.h"

#include <Wire.h>
#include <Arduino.h>

//* SCD40-D-R2 — CO2, Temperature & Humidity
//* Raw I2C implementation, no external SCD40 library dependency.

// ── Shared sensor state ──────────────────────────────────────────────────────
float    sensorTemp = 0.0f;
float    sensorHum  = 0.0f;
uint16_t sensorCO2  = 0;
bool     sensorOK   = false;
bool     alertTemp  = false;
bool     alertHum   = false;
bool     alertCO2   = false;

// ── SCD40 I2C address and commands ───────────────────────────────────────────
static constexpr uint8_t  SCD40_ADDR = 0x62;

static constexpr uint16_t CMD_START_PERIODIC_MEASUREMENT = 0x21B1;
static constexpr uint16_t CMD_READ_MEASUREMENT           = 0xEC05;
static constexpr uint16_t CMD_STOP_PERIODIC_MEASUREMENT  = 0x3F86;
static constexpr uint16_t CMD_GET_DATA_READY_STATUS      = 0xE4B8;
static constexpr uint16_t CMD_GET_SERIAL_NUMBER          = 0x3682;
static constexpr uint16_t CMD_GET_SENSOR_VARIANT         = 0x202F;

static constexpr uint16_t CMD_SET_ASC_ENABLED            = 0x2416;

// ── Timing ───────────────────────────────────────────────────────────────────
static constexpr uint32_t MIN_READ_INTERVAL_MS = 5200;
static constexpr uint32_t POLL_INTERVAL_MS     = 1000;
static constexpr uint16_t STOP_DELAY_MS        = 500;
static constexpr uint16_t CMD_READ_DELAY_MS    = 2;

// ── Module-private state ─────────────────────────────────────────────────────
static uint32_t _measurementStartMs = 0;
static uint32_t _lastPollMs         = 0;
static uint32_t _lastReadMs         = 0;
static uint16_t _lastDataReadyRaw   = 0;
static uint16_t _zeroCO2Count       = 0;

extern float threshTemp;
extern float threshTempLow;
extern float threshHum;
extern float threshHumLow;
extern float threshCO2;

// ── CRC-8 for SCD4x words ────────────────────────────────────────────────────
static uint8_t scd40Crc8(uint8_t msb, uint8_t lsb) {
    uint8_t crc = 0xFF;
    uint8_t data[2] = { msb, lsb };

    for (uint8_t i = 0; i < 2; i++) {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x31);
            } else {
                crc = static_cast<uint8_t>(crc << 1);
            }
        }
    }

    return crc;
}

// ── I2C helpers ──────────────────────────────────────────────────────────────
static bool sendCommand(uint16_t cmd) {
    Wire.beginTransmission(SCD40_ADDR);
    Wire.write(static_cast<uint8_t>(cmd >> 8));
    Wire.write(static_cast<uint8_t>(cmd & 0xFF));

    uint8_t result = Wire.endTransmission(true);

    if (result != 0) {
        Serial.printf("[SCD40] sendCommand 0x%04X failed, i2c_error=%u\n", cmd, result);
        return false;
    }

    return true;
}

static bool sendCommandWithArg(uint16_t cmd, uint16_t arg) {
    uint8_t msb = static_cast<uint8_t>(arg >> 8);
    uint8_t lsb = static_cast<uint8_t>(arg & 0xFF);

    Wire.beginTransmission(SCD40_ADDR);
    Wire.write(static_cast<uint8_t>(cmd >> 8));
    Wire.write(static_cast<uint8_t>(cmd & 0xFF));
    Wire.write(msb);
    Wire.write(lsb);
    Wire.write(scd40Crc8(msb, lsb));

    uint8_t result = Wire.endTransmission(true);

    if (result != 0) {
        Serial.printf("[SCD40] sendCommandWithArg 0x%04X failed, i2c_error=%u\n", cmd, result);
        return false;
    }

    return true;
}

static bool readBytes(uint8_t *buffer, uint8_t length) {
    uint8_t received = Wire.requestFrom(
        static_cast<uint8_t>(SCD40_ADDR),
        length,
        static_cast<uint8_t>(true)
    );

    if (received != length) {
        Serial.printf("[SCD40] I2C read length mismatch: expected=%u got=%u\n", length, received);

        while (Wire.available()) {
            Wire.read();
        }

        return false;
    }

    for (uint8_t i = 0; i < length; i++) {
        buffer[i] = static_cast<uint8_t>(Wire.read());
    }

    return true;
}

static bool readOneWordAfterCommand(uint16_t cmd, uint16_t delayMs, uint16_t &value) {
    if (!sendCommand(cmd)) {
        return false;
    }

    delay(delayMs);

    uint8_t buf[3] = {0};

    if (!readBytes(buf, 3)) {
        return false;
    }

    uint8_t expectedCrc = scd40Crc8(buf[0], buf[1]);

    if (expectedCrc != buf[2]) {
        Serial.printf("[SCD40] CRC fail cmd=0x%04X data=%02X %02X crc=%02X expected=%02X\n",
                      cmd,
                      buf[0],
                      buf[1],
                      buf[2],
                      expectedCrc);
        return false;
    }

    value = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    return true;
}

// ── I2C scanner ──────────────────────────────────────────────────────────────
static bool scanI2CBus() {
    Serial.println("[I2C Scanner] Scanning...");

    uint8_t found = 0;
    bool foundSCD40 = false;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);

        if (Wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X\n", addr);
            found++;

            if (addr == SCD40_ADDR) {
                foundSCD40 = true;
            }
        }
    }

    if (found == 0) {
        Serial.println("  No I2C devices found!");
    }

    if (!foundSCD40) {
        Serial.println("[SCD40] 0x62 NOT found on I2C scan");
    }

    return foundSCD40;
}

// ── Public label helper ──────────────────────────────────────────────────────
const char* co2Label(uint16_t co2) {
    if      (co2 == 0)    return "Unavailable";
    else if (co2 < 400)   return "Low/Warming";
    else if (co2 < 600)   return "Excellent";
    else if (co2 < 800)   return "Good";
    else if (co2 < 1000)  return "Moderate";
    else if (co2 < 1500)  return "Poor";
    else                  return "Unhealthy";
}

bool scd40IsOK() {
    return sensorOK;
}

// ── Stop/start helpers ───────────────────────────────────────────────────────
static void stopPeriodicMeasurement() {
    Serial.println("[SCD40] Sending stop_periodic_measurement...");

    if (!sendCommand(CMD_STOP_PERIODIC_MEASUREMENT)) {
        Serial.println("[SCD40] stop_periodic_measurement warning, continuing");
    }

    delay(STOP_DELAY_MS);
}

static bool startPeriodicMeasurement() {
    Serial.println("[SCD40] Sending start_periodic_measurement...");

    if (!sendCommand(CMD_START_PERIODIC_MEASUREMENT)) {
        Serial.println("[SCD40] start_periodic_measurement failed");
        return false;
    }

    _measurementStartMs = millis();
    _lastPollMs = 0;
    _lastReadMs = 0;
    _lastDataReadyRaw = 0;
    _zeroCO2Count = 0;

    Serial.println("[SCD40] start_periodic_measurement OK");
    return true;
}

// ── Serial number check ──────────────────────────────────────────────────────
static bool readSerialNumber() {
    if (!sendCommand(CMD_GET_SERIAL_NUMBER)) {
        Serial.println("[SCD40] get_serial_number command failed");
        return false;
    }

    delay(CMD_READ_DELAY_MS);

    uint8_t buf[9] = {0};

    if (!readBytes(buf, 9)) {
        Serial.println("[SCD40] serial number read failed");
        return false;
    }

    bool crc0 = (scd40Crc8(buf[0], buf[1]) == buf[2]);
    bool crc1 = (scd40Crc8(buf[3], buf[4]) == buf[5]);
    bool crc2 = (scd40Crc8(buf[6], buf[7]) == buf[8]);

    if (!crc0 || !crc1 || !crc2) {
        Serial.println("[SCD40] serial number CRC failed");
        return false;
    }

    uint16_t w0 = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    uint16_t w1 = (static_cast<uint16_t>(buf[3]) << 8) | buf[4];
    uint16_t w2 = (static_cast<uint16_t>(buf[6]) << 8) | buf[7];

    Serial.printf("[SCD40] Serial number: %04X-%04X-%04X\n", w0, w1, w2);
    return true;
}

// ── Sensor variant check ─────────────────────────────────────────────────────
static bool readSensorVariant() {
    uint16_t variant = 0;

    Serial.print("[SCD40] Reading sensor variant... ");

    if (!readOneWordAfterCommand(CMD_GET_SENSOR_VARIANT, CMD_READ_DELAY_MS, variant)) {
        Serial.println("FAILED, continuing anyway");
        return false;
    }

    uint8_t topNibble = (variant >> 12) & 0x0F;

    const char* variantText = "Unknown";

    if (topNibble == 0x0) {
        variantText = "SCD40";
    } else if (topNibble == 0x1) {
        variantText = "SCD41";
    } else if (topNibble == 0x5) {
        variantText = "SCD43";
    }

    Serial.printf("%s raw=0x%04X\n", variantText, variant);
    return true;
}

// ── Data ready ───────────────────────────────────────────────────────────────
static bool checkDataReady(bool &ready) {
    uint16_t raw = 0;

    if (!readOneWordAfterCommand(CMD_GET_DATA_READY_STATUS, CMD_READ_DELAY_MS, raw)) {
        ready = false;
        return false;
    }

    _lastDataReadyRaw = raw;
    ready = ((raw & 0x07FF) != 0);

    return true;
}

// ── Measurement frame ────────────────────────────────────────────────────────
struct MeasurementFrame {
    bool     allCrcOK;
    bool     co2CrcOK;
    bool     tempCrcOK;
    bool     humCrcOK;
    uint16_t rawCO2;
    uint16_t rawTemp;
    uint16_t rawHum;
    float    temperature;
    float    humidity;
    uint16_t co2;
};

static bool readMeasurementFrame(MeasurementFrame &f) {
    f = {};

    if (!sendCommand(CMD_READ_MEASUREMENT)) {
        Serial.println("[SCD40] read_measurement command failed");
        return false;
    }

    delay(CMD_READ_DELAY_MS);

    uint8_t buf[9] = {0};

    if (!readBytes(buf, 9)) {
        Serial.println("[SCD40] measurement frame read failed");
        return false;
    }

    Serial.printf("[SCD40 RAW] %02X %02X %02X  %02X %02X %02X  %02X %02X %02X\n",
                  buf[0],
                  buf[1],
                  buf[2],
                  buf[3],
                  buf[4],
                  buf[5],
                  buf[6],
                  buf[7],
                  buf[8]);

    f.co2CrcOK  = (scd40Crc8(buf[0], buf[1]) == buf[2]);
    f.tempCrcOK = (scd40Crc8(buf[3], buf[4]) == buf[5]);
    f.humCrcOK  = (scd40Crc8(buf[6], buf[7]) == buf[8]);
    f.allCrcOK  = f.co2CrcOK && f.tempCrcOK && f.humCrcOK;

    f.rawCO2  = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    f.rawTemp = (static_cast<uint16_t>(buf[3]) << 8) | buf[4];
    f.rawHum  = (static_cast<uint16_t>(buf[6]) << 8) | buf[7];

    f.co2 = f.rawCO2;
    f.temperature = -45.0f + 175.0f * (static_cast<float>(f.rawTemp) / 65535.0f);
    f.humidity    = 100.0f * (static_cast<float>(f.rawHum) / 65535.0f);

    return true;
}

// ── Init ─────────────────────────────────────────────────────────────────────
void scd40Init() {
    Serial.println();
    Serial.println("### SCD40 RAW DRIVER INIT ###");

    sensorOK = false;
    sensorTemp = 0.0f;
    sensorHum = 0.0f;
    sensorCO2 = 0;
    alertTemp = false;
    alertHum = false;
    alertCO2 = false;

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
    Wire.setTimeOut(100);

    Serial.printf("✓ I2C on SDA=GPIO%d, SCL=GPIO%d @ 100kHz\n", I2C_SDA, I2C_SCL);

    delay(50);

    bool foundSCD40 = scanI2CBus();

    if (!foundSCD40) {
        Serial.println("[SCD40] Init failed: 0x62 not detected");
        sensorOK = false;
        return;
    }

    stopPeriodicMeasurement();

    Serial.print("[SCD40] Reading serial number... ");
    if (readSerialNumber()) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED, continuing anyway");
    }

    readSensorVariant();

    Serial.println("[SCD40] ASC untouched");

    delay(20);

    if (!startPeriodicMeasurement()) {
        Serial.println("[SCD40] Init failed: could not start measurement");
        sensorOK = false;
        return;
    }

    sensorOK = true;

    Serial.println("[SCD40] Init complete");
    Serial.println("[SCD40] First valid frame expected after ~5.2s");
}

// ── Read ─────────────────────────────────────────────────────────────────────
void scd40Read() {
    if (!sensorOK) {
        return;
    }

    const uint32_t now = millis();

    if (now - _lastPollMs < POLL_INTERVAL_MS) {
        return;
    }

    _lastPollMs = now;

    bool ready = false;

    if (!checkDataReady(ready)) {
        Serial.println("[SCD40] data-ready check failed");
        return;
    }

    Serial.printf("[SCD40] DataReady raw=0x%04X ready=%s\n",
                  _lastDataReadyRaw,
                  ready ? "YES" : "NO");

    if (!ready) {
        return;
    }

    if ((now - _measurementStartMs) < MIN_READ_INTERVAL_MS) {
        Serial.println("[SCD40] Waiting for first 5.2s interval");
        return;
    }

    if (_lastReadMs != 0 && (now - _lastReadMs) < MIN_READ_INTERVAL_MS) {
        Serial.println("[SCD40] Waiting for next 5.2s interval");
        return;
    }

    MeasurementFrame f;

    if (!readMeasurementFrame(f)) {
        Serial.println("[SCD40] readMeasurementFrame failed");
        return;
    }

    _lastReadMs = now;

    Serial.printf("[SCD40] CRC CO2=%s TEMP=%s HUM=%s\n",
                  f.co2CrcOK ? "OK" : "FAIL",
                  f.tempCrcOK ? "OK" : "FAIL",
                  f.humCrcOK ? "OK" : "FAIL");

    Serial.printf("[SCD40] Decoded rawCO2=%u rawT=%u rawH=%u -> CO2=%u T=%.2f H=%.2f\n",
                  f.rawCO2,
                  f.rawTemp,
                  f.rawHum,
                  f.co2,
                  f.temperature,
                  f.humidity);

    if (!f.allCrcOK) {
        Serial.println("[SCD40] CRC failed, frame discarded");
        return;
    }

    extern float tempOffset;
    extern float humOffset;
    extern float co2Offset;
    sensorTemp = f.temperature + tempOffset;
    sensorHum  = f.humidity + humOffset;
    float rawCo2f = (float)f.co2 + co2Offset;
    sensorCO2  = (uint16_t)(rawCo2f < 0.0f ? 0 : rawCo2f);

    alertTemp = (sensorTemp > threshTemp) || (sensorTemp < threshTempLow);
    alertHum  = (sensorHum  > threshHum)  || (sensorHum  < threshHumLow);
    alertCO2  = (sensorCO2 > threshCO2);

    if (sensorCO2 == 0) {
        _zeroCO2Count++;
        Serial.printf("[SCD40] CO2 raw is still 0 after %u valid frames\n", _zeroCO2Count);
    } else {
        Serial.printf("[SCD40] CO2 received: %u ppm\n", sensorCO2);
    }

    Serial.printf("[SCD40] T:%.1f°C H:%.1f%% CO2:%u ppm (%s)\n",
                  sensorTemp,
                  sensorHum,
                  sensorCO2,
                  co2Label(sensorCO2));

    logPush(
        "T:" + String(sensorTemp, 1) + "\u00b0C H:" +
        String(sensorHum, 1) + "% CO2:" +
        String(sensorCO2) + " ppm (" +
        co2Label(sensorCO2) + ")"
    );
}