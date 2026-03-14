#include "ens160.h"
#include "config.h"
#include "sensors.h"
#include <DFRobot_ENS160.h>
#include <Wire.h>

//* Fermion ENS160 Air Quality sensor module (I2C)
//* Shares I2C bus with SHTC3 (GPIO8=SDA, GPIO9=SCL)
//* ENS160 I2C address: 0x53 (default), SHTC3: 0x70 — no conflict

// ── Shared sensor values (definitions) ───────────────────────────────────────
uint8_t  ens160AQI    = 0;
uint16_t ens160TVOC   = 0;
uint16_t ens160eCO2   = 0;
bool     ens160OK     = false;
String   ens160Status = "Initialising";

// ── Module-private objects ────────────────────────────────────────────────────
// 0x53 = default I2C address (ADDR pin floating or LOW)
// 0x52 = alternate address (ADDR pin HIGH)
static DFRobot_ENS160_I2C _ens160(&Wire, 0x53);
static uint32_t _lastRead  = 0;

// ── AQI label helper ─────────────────────────────────────────────────────────
const char* ens160AQILabel(uint8_t aqi) {
    switch (aqi) {
        case 1: return "Excellent";
        case 2: return "Good";
        case 3: return "Moderate";
        case 4: return "Poor";
        case 5: return "Unhealthy";
        default: return "Unknown";
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void ens160Init() {
    // Always init I2C here — safe to call even if shtc3Init already called it
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(50); // let bus settle

    Serial.print("[ENS160] Initialising on I2C (0x53)... ");
    if (_ens160.begin() != NO_ERR) {
        Serial.println("FAILED — check wiring/address");
        ens160OK     = false;
        ens160Status = "Error";
        return;
    }

    _ens160.setPWRMode(ENS160_STANDARD_MODE);
    _ens160.setTempAndHum(25.0f, 50.0f); // initial compensation, updated on first read

    ens160OK     = true;
    ens160Status = "Warming up";
    Serial.println("OK!");
    Serial.println("[ENS160] Warming up (~3 mins for accurate readings)");
}

void ens160SetCompensation(float temp, float hum) {
    if (!ens160OK) return;
    _ens160.setTempAndHum(temp, hum);
}

void ens160Read() {
    if (!ens160OK) return;

    uint32_t now = millis();
    if (now - _lastRead < SENSOR_INTERVAL) return;
    _lastRead = now;

    // Update compensation with latest SHTC3 readings
    if (sensorOK) {
        _ens160.setTempAndHum(sensorTemp, sensorHum);
    }

    // Check operating status
    // 0 = Normal operation
    // 1 = Warm-Up phase (first 3 mins after power-on)
    // 2 = Initial Start-Up phase (first full hour after first ever power-on)
    // 3 = Invalid output
    uint8_t status = _ens160.getENS160Status();

    if (status == 0) {
        ens160Status = "Normal";
    } else if (status == 1) {
        ens160Status = "Warming up";
        Serial.println("[ENS160] Still warming up...");
        return;
    } else if (status == 2) {
        ens160Status = "Initialising";
        Serial.println("[ENS160] Initial start-up phase...");
        return;
    } else {
        ens160Status = "Invalid";
        Serial.println("[ENS160] Invalid output");
        return;
    }

    // Read air quality values
    uint8_t  aqi  = _ens160.getAQI();
    uint16_t tvoc = _ens160.getTVOC();
    uint16_t eco2 = _ens160.getECO2();

    // Basic sanity check
    if (aqi >= 1 && aqi <= 5 && eco2 >= 400 && eco2 <= 65000) {
        ens160AQI  = aqi;
        ens160TVOC = tvoc;
        ens160eCO2 = eco2;
        Serial.printf("[ENS160] AQI:%d (%s)  TVOC:%d ppb  eCO2:%d ppm\n",
                      aqi, ens160AQILabel(aqi), tvoc, eco2);
    } else {
        Serial.println("[ENS160] Bad reading — skipping");
    }
}