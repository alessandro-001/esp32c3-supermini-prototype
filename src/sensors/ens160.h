#pragma once
#include <Arduino.h>

void ens160Init();
void ens160Read();
void ens160SetCompensation(float temp, float hum);
const char* ens160AQILabel(uint8_t aqi);