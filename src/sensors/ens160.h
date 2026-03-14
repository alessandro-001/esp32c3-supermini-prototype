#pragma once
#include <Arduino.h>

/// ── ENS160 Air Quality Sensor ─────────────────────────────────────────────────

// Shared sensor values (defined in ens160.cpp)
extern uint8_t  ens160AQI;       // Air Quality Index (1=Excellent, 2=Good, 3=Moderate, 4=Poor, 5=Unhealthy)
extern uint16_t ens160TVOC;      // Total VOC in ppb
extern uint16_t ens160eCO2;      // Equivalent CO2 in ppm
extern bool     ens160OK;        // True if sensor initialised successfully
extern String   ens160Status;    // Human-readable status string

// AQI level labels (for display/MQTT)
const char* ens160AQILabel(uint8_t aqi);

// ── Public API ────────────────────────────────────────────────────────────────
void ens160Init();                              // Init SPI and ENS160, call once in setup()
void ens160Read();                              // Read air quality data, call in loop()
void ens160SetCompensation(float temp, float hum); // Feed SHTC3 data for accuracy