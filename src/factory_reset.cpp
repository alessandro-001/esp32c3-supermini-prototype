#include "factory_reset.h"
#include "config.h"
#include <Preferences.h>
#include <Arduino.h>

//* Factory Reset — GPIO4 hold 5s or software trigger

static const unsigned long HOLD_MS      = 5000;
static unsigned long       _pressStart  = 0;
static bool                _holding     = false;
static bool                _armed       = false; // debounce: button was LOW first

void factoryResetInit() {
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    Serial.printf("[Reset] Factory reset on GPIO%d (hold %lus) ✓ connected!\n",
                  FACTORY_RESET_PIN, HOLD_MS / 1000);
}

void factoryResetHandle() {
    bool pressed = (digitalRead(FACTORY_RESET_PIN) == LOW);

    if (pressed && !_holding) {
        // Debounce — confirm still LOW after 50ms before arming
        delay(50);
        if (digitalRead(FACTORY_RESET_PIN) != LOW) return;
        _holding    = true;
        _armed      = true;
        _pressStart = millis();
        Serial.println("[Reset] Button held — keep holding for factory reset...");
    }

    if (!pressed && _holding) {
        _holding = false;
        _armed   = false;
        Serial.println("[Reset] Button released — hold cancelled");
    }

    if (_holding && _armed && (millis() - _pressStart >= HOLD_MS)) {
        Serial.println("[Reset] ✓ Hold threshold reached — executing factory reset");
        factoryResetExecute();
    }
}

void factoryResetExecute() {
    Serial.println("[Reset] Clearing all NVS namespaces...");

    // Clear every namespace used by the firmware
    const char* namespaces[] = { "provision", "netcfg", "thresholds", "device", nullptr };
    for (int i = 0; namespaces[i] != nullptr; i++) {
        Preferences p;
        p.begin(namespaces[i], false);
        p.clear();
        p.end();
        Serial.printf("[Reset]   cleared: %s\n", namespaces[i]);
    }

    Serial.println("[Reset] Done — rebooting in 1s...");
    delay(1000);
    ESP.restart();
}