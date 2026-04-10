#pragma once
#include <Arduino.h>

//! ── Local MQTT (Raspberry Pi pipeline) ──────────────────────────────────────
void localMqttInit();           // call once in setup()
void localMqttHandle();         // call in loop()
void localMqttPublish();        // publish full sensor payload
bool localMqttIsConnected();    // connection status