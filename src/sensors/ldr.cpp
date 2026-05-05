#include "ldr.h"
#include "config.h"
#include "sensors.h"
#include "web_server.h"
#include <Arduino.h>

bool ldrLightOn = false;
bool ldrOK      = false;
static uint32_t _lastRead = 0;

//* LDR — Light Detection Sensor

void ldrInit() {
    pinMode(LDR_PIN, INPUT);
    ldrOK = false; // will be set on first valid read
    Serial.printf("✓ LDR on GPIO%d\n", LDR_PIN);
}

void ldrRead() {
    uint32_t now = millis();
    if (now - _lastRead < SENSOR_INTERVAL) return;
    _lastRead = now;

    int raw = analogRead(LDR_PIN);
    ldrOK = true;  // sensor is present — extremes (0/4095) are valid in dark/bright conditions
    ldrLightOn = (raw > LDR_THRESHOLD);
    Serial.printf("[LDR] raw=%d  light=%s\n", raw, ldrLightOn ? "ON" : "OFF");
    logPush("[LDR] raw=" + String(raw) + " thr=" + String(LDR_THRESHOLD) + " → " + (ldrLightOn ? "ON" : "OFF"));
}