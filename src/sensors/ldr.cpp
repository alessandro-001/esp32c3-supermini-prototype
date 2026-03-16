#include "ldr.h"
#include "config.h"
#include "sensors.h"
#include <Arduino.h>

bool ldrLightOn = false;
static uint32_t _lastRead = 0;

//* LDR — Light Detection Sensor (photoresistor on GPIO2)

void ldrInit() {
    pinMode(LDR_PIN, INPUT);
    Serial.println("✓ LDR on GPIO2");
}

void ldrRead() {
    uint32_t now = millis();
    if (now - _lastRead < SENSOR_INTERVAL) return;
    _lastRead = now;

    int raw = analogRead(LDR_PIN);
    ldrLightOn = (raw > LDR_THRESHOLD);

    //Serial.printf("[LDR] raw=%d  light=%s\n", raw, ldrLightOn ? "ON" : "OFF");
}