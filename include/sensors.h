#pragma once
#include <Arduino.h>

//! ── SCD40 shared state ──────────────────────────────────────────────────────
extern float    sensorTemp;
extern float    sensorHum;
extern uint16_t sensorCO2;
extern bool     sensorOK;
extern bool     alertTemp;
extern bool     alertHum;
extern bool     alertCO2;

//! ── LDR shared state ────────────────────────────────────────────────────────
extern bool ldrLightOn;
extern bool ldrOK;

inline int ldrLightOnNum() {
    return ldrLightOn ? 1 : 0;
}

//! ── SCD40 API ────────────────────────────────────────────────────────────────
void        scd40Init();
void        scd40Read();
const char* co2Label(uint16_t co2);

//! ── LDR API ──────────────────────────────────────────────────────────────────
void ldrInit();
void ldrRead();