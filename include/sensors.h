#pragma once

// ── Shared sensor state ──────────────────────────────────────────────────────
extern float sensorTemp;        // Current temperature reading (°C)
extern float sensorHum;         // Current humidity reading (%)
extern bool  sensorOK;          // True if at least one sensor initialised OK

// ── SHTC3 — Temperature & Humidity ───────────────────────────────────────────
void shtc3Init();               // Initialise I2C and SHTC3 sensor
void shtc3Read();               // Read temp/humidity into sensorTemp, sensorHum

// ── Air Quality (future) ─────────────────────────────────────────────────────
// void airQualityInit();
// void airQualityRead();
