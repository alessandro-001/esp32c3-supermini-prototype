#pragma once
#include <Arduino.h>

// ── MQTT ─────────────────────────────────────────────────────────────────────

void mqttInit(const String& token);     // accepts dynamic token
void mqttHandle();                      // call in loop()
void mqttPublish();                     // publish telemetry
void mqttSetToken(const String& token); // update token (after provisioning)
void mqttPublishAttributes();           // publish device attributes
bool mqttIsConnected();                 // connection status
void mqttDisconnect();                  // clean disconnect (for factory reset)