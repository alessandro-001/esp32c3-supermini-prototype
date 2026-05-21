#pragma once
#include <Arduino.h>

void        scd40Init();
void        scd40Read();
bool        scd40IsOK();
const char* co2Label(uint16_t co2);