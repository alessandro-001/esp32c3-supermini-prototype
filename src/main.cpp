#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "sensors.h"
#include "sensors/ens160.h"
#include "wifi_config.h"
#include "web_server.h"
#include <WiFi.h>
#include "provisioning.h"
#include "mqtt.h"

//* ESP32C3 Smart Monitor Prototype - MAIN *//

// ── NeoPixel RGB ──────────────────────────────────────────────────────────────
Adafruit_NeoPixel ring(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ── LED state ─────────────────────────────────────────────────────────────────
static uint32_t lastLedUpdate = 0;
static float    breathAngle   = 0.0f;
static uint16_t targetHue     = 32768;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== BOSS FARM Smart Monitor ===");

    // NeoPixel init
    ring.begin();
    ring.setBrightness(BRIGHTNESS);
    ring.clear();
    ring.show();
    Serial.println("✓ NeoPixel on GPIO3");

    // Sensor init
    shtc3Init();   // I2C — temp & humidity (GPIO8/9)
    ens160Init();  // SPI — air quality (GPIO4/5/6/7)

    // Start AP (always available for configuration)
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD, 6);
    Serial.printf("[AP] Started: %s @ %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Load and connect to saved WiFi
    wifiConfigBegin(HOME_SSID, HOME_PASSWORD);
    bool wifiConnected = wifiConfigConnect(10000);

    // Automatic provisioning on boot
    String token = provisioningInit();
    if (token.isEmpty() && wifiConnected) {
        token = provisioningRequest();
        if (!token.isEmpty()) {
            mqttInit(token);
        }
    } else {
        mqttInit(token);
    }

    webServerInit();
}

void loop() {
    uint32_t now = millis();

    // ── Sensor reads ─────────────────────────────────────────────────────────
    shtc3Read();
    ens160Read();

    // ── Sensor warnings (printed once per session) ────────────────────────────
    static bool warnedSHTC3  = false;
    static bool warnedENS160 = false;
    if (!sensorOK  && !warnedSHTC3)  { Serial.println("⚠️ SHTC3 not found — temp/humidity unavailable"); warnedSHTC3  = true; }
    if (!ens160OK  && !warnedENS160) { Serial.println("⚠️ ENS160 not found — air quality unavailable");  warnedENS160 = true; }

    // ── Web server ───────────────────────────────────────────────────────────
    webServerHandle();

    // ── Hue update from latest sensorTemp ────────────────────────────────────
    if (sensorOK) {
        float norm = constrain((sensorTemp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN), 0.0f, 1.0f);
        targetHue = (uint16_t)((1.0f - norm) * 43690); // Blue(cold) → Red(hot)
    }

    // ── LED update (breathing effect) ────────────────────────────────────────
    if (now - lastLedUpdate >= LED_INTERVAL) {
        lastLedUpdate = now;

        if (sensorOK) {
            breathAngle += 0.05f;
            if (breathAngle > TWO_PI) breathAngle -= TWO_PI;
            uint8_t bri = (uint8_t)(155 + 100 * sin(breathAngle));
            ring.fill(ring.gamma32(ring.ColorHSV(targetHue, 255, bri)));
        } else {
            ring.clear();
            static bool warnedLed = false;
            if (!warnedLed) { Serial.println("⚠️ Sensor not found — LEDs off"); warnedLed = true; }
        }
        ring.show();
    }

    // ── MQTT telemetry publish every 5s ──────────────────────────────────────
    static uint32_t lastMqttPublish = 0;
    static bool     sentAttributes  = false;
    static bool     wasConnected    = false;

    bool isConnected = mqttIsConnected();

    // Detect fresh connection (including reconnects) → re-send attributes
    if (!wasConnected && isConnected) {
        sentAttributes = false;
    }
    wasConnected = isConnected;

    if (now - lastMqttPublish >= 5000) {
        lastMqttPublish = now;
        if (isConnected) {
            mqttPublish();
            if (!sentAttributes) {
                mqttPublishAttributes();
                sentAttributes = true;
            }
        }
    }

    provisioningHandle();
    mqttHandle();
}