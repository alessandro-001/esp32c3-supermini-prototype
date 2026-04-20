#pragma once
#include <Arduino.h>

//! ── SHTC3 /T&H/ shared state ───────────────────────────────────────────────────────
extern float sensorTemp;        // Current temperature reading (°C)
extern float sensorHum;         // Current humidity reading (%)
extern bool  sensorOK;          // True if SHTC3 initialised OK
extern bool  alertTemp;         // True if temperature exceeds threshold
extern bool  alertHum;          // True if humidity exceeds threshold

//! ── ENS160 /AQI/ shared state ──────────────────────────────────────────────────────
extern uint8_t  ens160AQI;      // Air Quality Index (1–5)
extern uint16_t ens160TVOC;     // Total VOC in ppb
extern uint16_t ens160eCO2;     // Equivalent CO2 in ppm
extern bool     ens160OK;       // True if ENS160 initialised OK
extern String   ens160Status;   // Human-readable status string

//! ── LDR shared state ────────────────────────────────────────────────────────────────
extern bool ldrLightOn;     // true if light is detected above threshold
extern bool ldrOK;      // true if LDR is connected and reading valid

// Shared helper to keep all outbound payloads consistent for light state.
inline int ldrLightOnNum() {
	return ldrLightOn ? 1 : 0;
}

//! ── SHTC3 API ────────────────────────────────────────────────────────────────
void        shtc3Init();
void        shtc3Read();

//! ── ENS160 API ───────────────────────────────────────────────────────────────
void        ens160Init();
void        ens160Read();
void        ens160SetCompensation(float temp, float hum);
const char* ens160AQILabel(uint8_t aqi);


//! ── LDR API ──────────────────────────────────────────────────────────────────
void        ldrInit();
void        ldrRead();