#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "sensors.h"
#include "wifi_config.h"
#include "web_server.h"
#include <WiFi.h>

//* ESP32C3 Smart Monitor Prototype - MAIN

// ── NeoPixel RGB ──────────────────────────────────────────────────────────────
Adafruit_NeoPixel ring(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ── LED state ─────────────────────────────────────────────────────────────────
static uint32_t lastLedUpdate = 0;
static float    breathAngle   = 0.0f;
static uint16_t targetHue     = 32768;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== SHTC3 + NeoPixel Ring ===");

  // NeoPixel init
  ring.begin();
  ring.setBrightness(BRIGHTNESS);
  ring.clear();
  ring.show();
  Serial.println("✓ NeoPixel on GPIO3");

  // Sensor init
  shtc3Init();


  // Start AP (always available for configuration)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD, 6); // Use channel 6 for reliability
  Serial.printf("[AP] Started: %s @ %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // Load and connect to saved WiFi credentials (defaults from secrets.h)
  wifiConfigBegin(HOME_SSID, HOME_PASSWORD);
  bool wifiConnected = wifiConfigConnect(10000); // 10 seconds timeout

  Serial.println("\nTemp (C)\tHumidity (%)");
  Serial.println("─────────────────────────────────────");

  webServerInit();
}

void loop() {
  uint32_t now = millis();

  // ── Sensor read ──────────────────────────────────────────────────────────
  shtc3Read();

  // ── Web server ───────────────────────────────────────────────────────────
  webServerHandle();

  // ── Hue update from latest sensorTemp ────────────────────────────────────
  if (sensorOK) {
    float norm = constrain((sensorTemp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN), 0.0f, 1.0f);
    targetHue = (uint16_t)((1.0f - norm) * 43690); // Blue(cold) → Red(hot)
  }

  // ── LED update (breathing effect) ─────────────────────────────────────────
  if (now - lastLedUpdate >= LED_INTERVAL) {
    lastLedUpdate = now;

    if (sensorOK) {
      breathAngle += 0.05f;
      if (breathAngle > TWO_PI) breathAngle -= TWO_PI;
      uint8_t bri = (uint8_t)(155 + 100 * sin(breathAngle));
      ring.fill(ring.gamma32(ring.ColorHSV(targetHue, 255, bri)));
    } else {
      ring.clear();
      static bool warned = false;
      if (!warned) { Serial.println("⚠ Sensor not found — LEDs off"); warned = true; }
    }
    ring.show();
  }
}