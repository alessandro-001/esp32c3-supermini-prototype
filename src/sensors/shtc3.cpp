#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SHTC3.h"
#include "config.h"
#include "sensors.h"

//* SHTC3 sensor module for temperature and humidity

// ── Module-private objects & state ───────────────────────────────────────────
static Adafruit_SHTC3 _shtc3;
static uint32_t       _lastRead = 0;

// ── Shared sensor values (definitions) ───────────────────────────────────────
float sensorTemp = 0.0f;
float sensorHum  = 0.0f;
bool  sensorOK   = false;
bool  alertTemp  = false;
bool  alertHum   = false;

// ── I2C scan (debug utility) ──────────────────────────────────────────────────
static void i2cScan() {
  Serial.println("\n[I2C Scanner] Scanning...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Found device at 0x");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) Serial.println("  No I2C devices found! Check wiring.");
}

// ── Public API ────────────────────────────────────────────────────────────────
void shtc3Init() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("✓ I2C on SDA=GPIO8, SCL=GPIO9");

  i2cScan();

  Serial.print("  Connecting to SHTC3... ");
  if (_shtc3.begin()) {
    sensorOK = true;
    Serial.println("OK! (0x70)");
  } else {
    Serial.println("FAILED");
  }
}

void shtc3Read() { 
  if (!sensorOK) return;

  uint32_t now = millis();
  if (now - _lastRead < SENSOR_INTERVAL) return;
  _lastRead = now;

  sensors_event_t temp, humidity;
  _shtc3.getEvent(&humidity, &temp);

  float t = temp.temperature;
  float h = humidity.relative_humidity;

  if (t > -40 && t < 120 && h >= 0 && h <= 100) {
    sensorTemp = t;
    sensorHum  = h;
    Serial.print(t, 1); Serial.print(" C\t\t");
    Serial.print(h, 1); Serial.println(" %");
  } else {
    Serial.println("SHTC3: bad reading — skipping");
  }
}
