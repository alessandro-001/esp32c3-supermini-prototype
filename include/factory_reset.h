#pragma once
#include <Arduino.h>

//! ── Factory Reset ─────────────────────────────────────────────────────────────
void factoryResetInit();    // call once in setup()
void factoryResetHandle();  // call in loop() — monitors button hold
void factoryResetExecute(); // clears all NVS namespaces and reboots