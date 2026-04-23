#pragma once
#include <Arduino.h>

//! ── Factory Reset ─────────────────────────────────────────────────────────────
// Physical: hold GPIO4 for 5 seconds → clears NVS → reboots
// Software: call factoryResetExecute() directly (used by web endpoint in dev)

void factoryResetInit();    // call once in setup()
void factoryResetHandle();  // call in loop() — monitors button hold
void factoryResetExecute(); // clears all NVS namespaces and reboots