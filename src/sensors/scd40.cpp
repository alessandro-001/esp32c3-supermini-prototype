#include "scd40.h"
#include "config.h"
#include "sensors.h"
#include "web_server.h"
#include <SensirionI2cScd4x.h>
#include <Wire.h>

//* SCD40-D-R2 — CO2, Temperature & Humidity (I2C, 0x62)

// ── All shared state definitions (previously split across shtc3.cpp/ens160.cpp)
float    sensorTemp = 0.0f;
float    sensorHum  = 0.0f;
uint16_t sensorCO2  = 0;
bool     sensorOK   = false;
bool     alertTemp  = false;
bool     alertHum   = false;
bool     alertCO2   = false;

// ── Module-private ────────────────────────────────────────────────────────────
static SensirionI2cScd4x _scd40;
static uint32_t          _lastRead = 0;
static bool              _co2Ready = false;
static uint8_t           _badReadCount = 0;

extern float threshTemp;
extern float threshTempLow;
extern float threshHum;
extern float threshHumLow;
extern float threshCO2;

// ── CO2 label ─────────────────────────────────────────────────────────────────
const char* co2Label(uint16_t co2) {
    if      (co2 == 0)   return "–";   
    if      (co2 < 600)  return "Excellent";
    else if (co2 < 800)  return "Good";
    else if (co2 < 1000) return "Moderate";
    else if (co2 < 1500) return "Poor";
    else                 return "Unhealthy";
}

// ── Public API ────────────────────────────────────────────────────────────────

void scd40Init() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("✓ I2C on SDA=GPIO8, SCL=GPIO9");

    Serial.println("[I2C Scanner] Scanning...");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) Serial.println("  No I2C devices found! Check wiring.");

    Serial.print("[SCD40] Initialising at 0x62... ");
    _scd40.begin(Wire, SCD40_I2C_ADDR_62);

    uint16_t err = _scd40.stopPeriodicMeasurement();
    delay(500);

    err = _scd40.startPeriodicMeasurement();
    if (err) {
        Serial.printf("FAILED (0x%04X) — check wiring\n", err);
        sensorOK = false;
        return;
    }

    sensorOK = true;
    Serial.println("OK! First reading in ~5s, stable CO2 after ~60s");
}

void scd40Read() {
    if (!sensorOK) return;

    uint32_t now = millis();
    if (now - _lastRead < SENSOR_INTERVAL) return;
    _lastRead = now;

    bool isReady = false;
    uint16_t err = _scd40.getDataReadyStatus(isReady);
    if (err || !isReady) return;

    float    t   = 0.0f;
    float    h   = 0.0f;
    uint16_t co2 = 0;

    err = _scd40.readMeasurement(co2, t, h);
    if (err) {
        Serial.printf("[SCD40] readMeasurement error: 0x%04X\n", err);
        return;
    }

    // ── Temperature & Humidity ────────────────────────────────────────────────
    if (t > -40.0f && t < 120.0f && h >= 0.0f && h <= 100.0f) {
        sensorTemp = t;
        sensorHum  = h;
        alertTemp  = (sensorTemp > threshTemp)  || (sensorTemp < threshTempLow);
        alertHum   = (sensorHum  > threshHum)   || (sensorHum  < threshHumLow);
    } else {
        Serial.printf("[SCD40] Bad T/H — T:%.1f H:%.1f skipped\n", t, h);
    }

    // ── CO2 ───────────────────────────────────────────────────────────────────
    if (co2 == 0) {
        if (!_co2Ready) {
            Serial.println("[SCD40] CO2=0 — warming up");
        } else {
            _badReadCount++;
            Serial.printf("[SCD40] CO2=0 — bad read after warmup (%d)\n", _badReadCount);
            if (_badReadCount >= 5) {
                Serial.println("[SCD40] Restarting periodic measurement...");
                _scd40.stopPeriodicMeasurement();
                delay(500);
                _scd40.startPeriodicMeasurement();
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
        Serial.printf("[SCD40] Bad CO2: %d ppm — skipped\n", co2);
        return;
    }

    // reset counter on valid read
    _badReadCount = 0;

    Serial.printf("[SCD40] T:%.1f°C  H:%.1f%%  CO2:%d ppm (%s)\n",
                  sensorTemp, sensorHum, sensorCO2, co2Label(sensorCO2));    logPush("T:" + String(sensorTemp,1) + "\u00b0C  H:" + String(sensorHum,1) + "%  CO2:" + String(sensorCO2) + " ppm (" + co2Label(sensorCO2) + ")");}