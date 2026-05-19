#include "ldr.h"
#include "config.h"
#include "sensors.h"
#include "web_server.h"
#include "driver/gpio.h"
#include <Arduino.h>

bool ldrLightOn = false;
bool ldrOK      = false;
static uint32_t _lastRead = 0;

void ldrInit() {
    gpio_reset_pin((gpio_num_t)LDR_PIN);
    gpio_set_direction((gpio_num_t)LDR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)LDR_PIN, GPIO_PULLDOWN_ONLY);
    analogSetPinAttenuation(LDR_PIN, ADC_11db);
    delay(100);
    for (int i = 0; i < 5; i++) { analogRead(LDR_PIN); delay(5); }
    ldrOK = false;
    Serial.printf("✓ LDR on GPIO%d\n", LDR_PIN);
}

void ldrRead() {
    uint32_t now = millis();
    if (now - _lastRead < SENSOR_INTERVAL) return;
    _lastRead = now;

    int samples[5];
    for (int i = 0; i < 5; i++) {
        samples[i] = analogRead(LDR_PIN);
        delay(5);
    }

    int minVal     = 4095;
    int validCount = 0;
    for (int i = 0; i < 5; i++) {
        if (samples[i] < 4090) {
            if (samples[i] < minVal) minVal = samples[i];
            validCount++;
        }
    }

    if (validCount == 0) {
        // strapping spike — hold previous reading silently
        Serial.printf("[LDR] holding previous → light=%s\n", ldrLightOn ? "ON" : "OFF");
        return;
    }

    ldrOK      = true;
    ldrLightOn = (minVal > LDR_THRESHOLD);  // high value = lit, low value = dark
    Serial.printf("[LDR] min=%d  valid=%d/5  thr=%d  light=%s\n",
                  minVal, validCount, LDR_THRESHOLD, ldrLightOn ? "ON" : "OFF");
    logPush("[LDR] min=" + String(minVal) + " thr=" + String(LDR_THRESHOLD) +
            " → " + (ldrLightOn ? "ON" : "OFF"));
}