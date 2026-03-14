#pragma once
#include <Arduino.h>

//! ── SHTC3 shared state ───────────────────────────────────────────────────────
extern float sensorTemp;        // Current temperature reading (°C)
extern float sensorHum;         // Current humidity reading (%)
extern bool  sensorOK;          // True if SHTC3 initialised OK
extern bool  alertTemp;         // True if temperature exceeds threshold
extern bool  alertHum;          // True if humidity exceeds threshold

//! ── ENS160 shared state ──────────────────────────────────────────────────────
extern uint8_t  ens160AQI;      // Air Quality Index (1–5)
extern uint16_t ens160TVOC;     // Total VOC in ppb
extern uint16_t ens160eCO2;     // Equivalent CO2 in ppm
extern bool     ens160OK;       // True if ENS160 initialised OK
extern String   ens160Status;   // Human-readable status string

//! ── SHTC3 API ────────────────────────────────────────────────────────────────
void        shtc3Init();
void        shtc3Read();

//! ── ENS160 API ───────────────────────────────────────────────────────────────
void        ens160Init();
void        ens160Read();
void        ens160SetCompensation(float temp, float hum);
const char* ens160AQILabel(uint8_t aqi);