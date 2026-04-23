#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config.h"
#include "sensors.h"
#include "wifi_config.h"
#include "web_server.h"
#include "provisioning.h"
#include "mqtt.h"
#include "cloud/google_sheets.h"
#include "local_mqtt.h"

//* ESP32C3 Smart Monitor Prototype - MAIN *//

// ── NeoPixel RGB ──────────────────────────────────────────────────────────────
Adafruit_NeoPixel ring(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ── LED state ─────────────────────────────────────────────────────────────────
static uint32_t lastLedUpdate = 0;
static uint16_t targetHue     = 32768;
static uint16_t rainbowHue    = 0;

// ── Feature flags ─────────────────────────────────────────────────────────────
bool googleSheetsEnabled              = true;
static const bool thingsBoardEnabled  = false;

// ── Commissioning state ───────────────────────────────────────────────────────
// "commissioned" = device has joined a WiFi network at least once.
// When commissioned: AP hotspot is hidden (STA only).
// On factory reset: NVS cleared → AP reappears on next boot.
static const char* DEVICE_NVS_NS    = "device";
static const char* COMMISSIONED_KEY = "commissioned";

static bool isCommissioned() {
    Preferences p;
    p.begin(DEVICE_NVS_NS, true);
    bool v = p.getBool(COMMISSIONED_KEY, false);
    p.end();
    return v;
}

static void setCommissioned() {
    Preferences p;
    p.begin(DEVICE_NVS_NS, false);
    p.putBool(COMMISSIONED_KEY, true);
    p.end();
}

// ── mDNS ─────────────────────────────────────────────────────────────────────
// Advertises as bossfarm-{MAC4}.local on the LAN
static void startMDNS() {
    String mac      = WiFi.macAddress();
    mac.replace(":", "");
    String hostname = String(MDNS_PREFIX) + "-" + mac.substring(8);
    hostname.toLowerCase();

    if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] Advertising as http://%s.local\n", hostname.c_str());
    } else {
        Serial.println("[mDNS] Failed to start");
    }
}

// ── AP helpers ────────────────────────────────────────────────────────────────
static String buildApSsid() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return String(AP_SSID) + "_" + mac.substring(8);
}

static void startAP() {
    String apSsid = buildApSsid();
    WiFi.softAP(apSsid.c_str(), AP_PASSWORD, 1);
    Serial.printf("[AP] Started: %s @ %s\n",
                  apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

static void stopAP() {
    WiFi.softAPdisconnect(true);
    Serial.println("[AP] Hotspot hidden — device in STA-only mode");
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n====== BOSS FARM Smart Monitor ======");

    // NeoPixel init
    ring.begin();
    ring.setBrightness(BRIGHTNESS);
    ring.clear();
    ring.show();
    Serial.println("✓ NeoPixel on GPIO3");

    // Sensor init
    shtc3Init();   // I2C — temp & humidity (GPIO8/9)
    ens160Init();  // I2C — air quality (same bus, 0x53)

    // LDR init
    ldrInit();

    // WiFi — always start in AP_STA so we can connect while AP is up
    WiFi.mode(WIFI_AP_STA);
    startAP();

    wifiConfigBegin(HOME_SSID, HOME_PASSWORD);
    bool wifiConnected = wifiConfigConnect(10000);

    if (wifiConnected) {
        if (!isCommissioned()) {
            setCommissioned();
            Serial.println("[Commissioning] First join complete — hiding AP");
        }
        stopAP();
        startMDNS();
    } else {
        Serial.println("[Commissioning] No WiFi — AP remains visible for setup");
    }

    // Google Sheets init
    googleSheetsInit();

    if (wifiConnected) {
        String bootMsg = "{\"event\":\"boot\",\"rssi\":" + String(WiFi.RSSI()) + "}";
        googleSheetsSend(bootMsg);
    }

    if (thingsBoardEnabled) {
        String token = provisioningInit();
        if (token.isEmpty() && wifiConnected) {
            token = provisioningRequest();
            if (!token.isEmpty()) mqttInit(token);
        } else {
            mqttInit(token);
        }
    }

    webServerInit();
    localMqttInit();

    Serial.println("====== Setup complete ======\n");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // ── Sensor reads ─────────────────────────────────────────────────────────
    shtc3Read();
    ens160Read();
    ldrRead();

    // ── Sensor warnings (once per session) ───────────────────────────────────
    static bool warnedSHTC3  = false;
    static bool warnedENS160 = false;
    if (!sensorOK  && !warnedSHTC3)  { Serial.println("⚠️ SHTC3 not found — temp/humidity unavailable"); warnedSHTC3  = true; }
    if (!ens160OK  && !warnedENS160) { Serial.println("⚠️ ENS160 not found — air quality unavailable");  warnedENS160 = true; }

    // ── Web server ───────────────────────────────────────────────────────────
    webServerHandle();

    // ── LED update (smooth colour fade) ──────────────────────────────────────
    if (now - lastLedUpdate >= LED_INTERVAL) {
        lastLedUpdate = now;

        if (sensorOK) {
            float norm      = constrain((sensorTemp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN), 0.0f, 1.0f);
            uint16_t newHue = (uint16_t)((1.0f - norm) * 43690);
            if (targetHue < newHue)      targetHue += min((uint16_t)20, (uint16_t)(newHue - targetHue));
            else if (targetHue > newHue) targetHue -= min((uint16_t)20, (uint16_t)(targetHue - newHue));
            ring.fill(ring.gamma32(ring.ColorHSV(targetHue, 255, 200)));
        } else {
            rainbowHue += 128;
            ring.fill(ring.gamma32(ring.ColorHSV(rainbowHue, 255, 200)));
        }
        ring.show();
    }

    // ── Google Sheets publish every 60s ──────────────────────────────────────
    static uint32_t lastSheetPublish = 0;
    if (googleSheetsEnabled && now - lastSheetPublish >= 60000) {
        lastSheetPublish = now;
        if (isWiFiConnected()) {
            String deviceId = "SM_" + WiFi.macAddress().substring(12);
            deviceId.replace(":", "");

            String data = "{";
            data += "\"device\":\"" + deviceId + "\",";
            data += "\"temp\":"  + String(sensorTemp, 1) + ",";
            data += "\"hum\":"   + String(sensorHum,  1) + ",";
            data += "\"aqi\":"   + String(ens160AQI)     + ",";
            data += "\"tvoc\":"  + String(ens160TVOC)    + ",";
            data += "\"eco2\":"  + String(ens160eCO2);
            data += "}";
            googleSheetsSend(data);
        }
    }

    // ── MQTT publish every 5s ────────────────────────────────────────────────
    static uint32_t lastMqttPublish = 0;
    static bool     sentAttributes  = false;
    static bool     wasConnected    = false;

    if (now - lastMqttPublish >= 5000) {
        lastMqttPublish = now;

        if (thingsBoardEnabled) {
            bool isConnected = mqttIsConnected();
            if (!wasConnected && isConnected) sentAttributes = false;
            wasConnected = isConnected;
            if (isConnected) {
                mqttPublish();
                if (!sentAttributes) { mqttPublishAttributes(); sentAttributes = true; }
            }
        }

        localMqttPublish();
    }

    // ── AP re-enable if WiFi drops after commissioning ───────────────────────
    // If we lose WiFi for >30s, re-show AP so the device can be reconfigured.
    static uint32_t wifiLostAt  = 0;
    static bool     apReEnabled = false;
    static const uint32_t AP_FALLBACK_MS = 30000;

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiLostAt == 0) wifiLostAt = now;
        if (!apReEnabled && (now - wifiLostAt > AP_FALLBACK_MS)) {
            Serial.println("[WiFi] Lost for 30s — re-enabling AP for reconfiguration");
            startAP();
            apReEnabled = true;
        }
    } else {
        if (apReEnabled) {
            stopAP();
            startMDNS();
            apReEnabled = false;
        }
        wifiLostAt = 0;
    }

    // ── Service handles ──────────────────────────────────────────────────────
    if (thingsBoardEnabled) {
        provisioningHandle();
        mqttHandle();
    }
    localMqttHandle();
}