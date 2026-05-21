#include "scd40.h"
#include "config.h"
#include "sensors.h"
#include "web_server.h"

#include <Wire.h>
#include <SCD40.h>

//* SCD40-D-R2 — CO2, Temperature & Humidity, I2C address 0x62

// ── Shared sensor state ──────────────────────────────────────────────────────
float    sensorTemp = 0.0f;
float    sensorHum  = 0.0f;
uint16_t sensorCO2  = 0;
bool     sensorOK   = false;
bool     alertTemp  = false;
bool     alertHum   = false;
bool     alertCO2   = false;

// ── Module-private ───────────────────────────────────────────────────────────
static SCD40    _scd40(Wire);
static uint32_t _lastRead = 0;
static bool     _co2Ready = false;
static uint8_t  _badReadCount = 0;

extern float threshTemp;
extern float threshTempLow;
extern float threshHum;
extern float threshHumLow;
extern float threshCO2;

// ── Optional I2C scanner ─────────────────────────────────────────────────────
static void scanI2CBus() {
    Serial.println("[I2C Scanner] Scanning...");

    uint8_t found = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X\n", addr);
            found++;
        }
    }

    if (found == 0) {
        Serial.println("  No I2C devices found! Check PCBA power, SDA/SCL, and pullups.");
    }
}

// ── CO2 label ────────────────────────────────────────────────────────────────
const char* co2Label(uint16_t co2) {
    if      (co2 == 0)    return "–";
    if      (co2 < 600)   return "Excellent";
    else if (co2 < 800)   return "Good";
    else if (co2 < 1000)  return "Moderate";
    else if (co2 < 1500)  return "Poor";
    else                  return "Unhealthy";
}

bool scd40IsOK() {
    return sensorOK;
}

// ── Init ─────────────────────────────────────────────────────────────────────
void scd40Init() {
    sensorOK = false;

    delay(30); // SCD40 power-up delay

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    Serial.printf("✓ I2C on SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA, I2C_SCL);

    scanI2CBus();

    Serial.print("[SCD40] Initialising at 0x62... ");

    if (!_scd40.begin()) {
    Serial.printf("FAILED begin, error=%u\n", _scd40.getLastError());
    return;
    }

    uint64_t serial = _scd40.getSerialNumber();
    Serial.printf("serial=%llu... ", serial);

    // Make sure sensor is idle before settings.
    if (!_scd40.stopMeasurement()) {
        Serial.printf("stop warning, error=%u... ", _scd40.getLastError());
    }

    // Give the sensor a clean idle window.
    delay(100);

    // ASC is enabled by default, but this makes it explicit.
    if (!_scd40.enableASC(true)) {
        Serial.printf("ASC warning, error=%u... ", _scd40.getLastError());
    }

    delay(100);

    if (!_scd40.startMeasurement()) {
        Serial.printf("FAILED start, error=%u\n", _scd40.getLastError());
        return;
    }

    _lastRead = millis();
    _co2Ready = false;
    _badReadCount = 0;
    sensorOK = true;

    Serial.println("OK! First reading in ~5s, CO2 response stabilizes over ~60s");
}

// ── Read ─────────────────────────────────────────────────────────────────────
void scd40Read() {
    if (!sensorOK) return;

    const uint32_t now = millis();

    if (now - _lastRead < SENSOR_INTERVAL) {
        return;
    }

    _lastRead = now;

    if (!_scd40.isDataReady()) {
        uint8_t err = _scd40.getLastError();

        if (err != SCD40::ERROR_NOT_READY) {
            Serial.printf("[SCD40] isDataReady error=%u\n", err);
        }

        return;
    }

    float co2Float = 0.0f;
    float t        = 0.0f;
    float h        = 0.0f;

    if (!_scd40.readMeasurement(co2Float, t, h)) {
        Serial.printf("[SCD40 DEBUG] readMeasurement FAILED error=%u co2Float=%.1f T=%.2f H=%.2f\n",
                    _scd40.getLastError(), co2Float, t, h);
        return;
    }

    Serial.printf("[SCD40 DEBUG] readMeasurement OK co2Float=%.1f T=%.2f H=%.2f\n",
                co2Float, t, h);

    uint16_t co2 = static_cast<uint16_t>(co2Float + 0.5f);

    // ── Temperature & Humidity ───────────────────────────────────────────────
    if (t > -40.0f && t < 120.0f && h >= 0.0f && h <= 100.0f) {
        sensorTemp = t;
        sensorHum  = h;

        alertTemp = (sensorTemp > threshTemp) || (sensorTemp < threshTempLow);
        alertHum  = (sensorHum  > threshHum)  || (sensorHum  < threshHumLow);
    } else {
        Serial.printf("[SCD40] Bad T/H — T:%.1f H:%.1f skipped\n", t, h);
    }

    // ── CO2 ──────────────────────────────────────────────────────────────────
    if (co2 == 0) {
    _badReadCount++;

    if (!_co2Ready) {
        Serial.printf("[SCD40] CO2=0 — waiting for first CO2 sample (%u)\n", _badReadCount);

        if (_badReadCount >= 12) {
            Serial.println("[SCD40] CO2 still 0 after ~60s — not normal, keeping measurement running for debug");
            _badReadCount = 0;
        }
    } else {
            _badReadCount++;
            Serial.printf("[SCD40] CO2=0 — bad read after warmup (%u)\n", _badReadCount);

            if (_badReadCount >= 5) {
                Serial.println("[SCD40] Restarting periodic measurement...");

                _scd40.stopMeasurement();
                _scd40.startMeasurement();

                _badReadCount = 0;
                _co2Ready = false;
            }
        }

        return;
    }

    if (co2 >= 400 && co2 <= 40000) {
        _co2Ready = true;
        sensorCO2 = co2;
        alertCO2  = (sensorCO2 > threshCO2);
    } else {
        Serial.printf("[SCD40] Bad CO2: %u ppm — skipped\n", co2);
        return;
    }

    _badReadCount = 0;

    Serial.printf("[SCD40] T:%.1f°C  H:%.1f%%  CO2:%u ppm (%s)\n",
                  sensorTemp,
                  sensorHum,
                  sensorCO2,
                  co2Label(sensorCO2));

    logPush(
        "T:" + String(sensorTemp, 1) + "\u00b0C  H:" +
        String(sensorHum, 1) + "%  CO2:" +
        String(sensorCO2) + " ppm (" +
        co2Label(sensorCO2) + ")"
    );
}