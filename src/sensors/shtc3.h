#pragma once

/// ── SHTC3 Temperature & Humidity Sensor ───────────────────────────────────────────

enum { SHTC3_OK = 0, SHTC3_ERROR = -1 };

extern float sensorTemp;
extern float sensorHum;
extern bool  sensorOK;
extern bool  alertTemp;
extern bool  alertHum;

void shtc3Init();
void shtc3Read();
