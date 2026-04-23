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
#include "factory_reset.h"

//* ESP32C3 Smart Monitor Prototype - MAIN *//

// ── NeoPixel ──────────────────────────────────────────────────────────────────
Adafruit_NeoPixel ring(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t lastLedUpdate = 0;
static uint16_t targetHue     = 32768;
static uint16_t rainbowHue    = 0;

// ── Feature flags ─────────────────────────────────────────────────────────────
bool googleSheetsEnabled             = true;
static const bool thingsBoardEnabled = false;

// ── Commissioning ─────────────────────────────────────────────────────────────
static const char* DEVICE_NVS_NS    = "device";
static const char* COMMISSIONED_KEY = "commissioned";

bool deviceIsCommissioned() {
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

void setCommissionedPublic() {
    setCommissioned();
}

// ── mDNS ─────────────────────────────────────────────────────────────────────
static void startMDNS() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String hostname = String(MDNS_PREFIX) + "-" + mac.substring(8);
    hostname.toLowerCase();
    if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] http://%s.local\n", hostname.c_str());
    } else {
        Serial.println("[mDNS] Failed to start");
    }
}

// ── AP helpers ────────────────────────────────────────────────────────────────
static void startAP() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String apSsid = String(AP_SSID) + "_" + mac.substring(8);
    WiFi.softAP(apSsid.c_str(), AP_PASSWORD, 1);
    Serial.printf("[AP] Started: %s @ %s\n",
                  apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

static void stopAP() {
    WiFi.softAPdisconnect(true);
    Serial.println("[AP] Hotspot hidden — STA only");
}

// ── Register Device ───────────────────────────────────────────────────────────
bool registerDevice() {
    Serial.println("[Register] Starting registration...");
    bool ok = wifiConfigConnect(15000);
    if (!ok) {
        Serial.println("[Register] WiFi connection failed");
        return false;
    }
    setCommissioned();
    stopAP();
    startMDNS();
    localMqttInit();
    Serial.println("[Register] ✓ Device registered and visible on network");
    return true;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n====== BOSS FARM Smart Monitor ======");

    ring.begin();
    ring.setBrightness(BRIGHTNESS);
    ring.clear();
    ring.show();
    Serial.println("✓ NeoPixel on GPIO3");

    // factoryResetInit(); // enable when board assembled

    shtc3Init();
    ens160Init();
    ldrInit();

    WiFi.mode(WIFI_AP_STA);
    startAP();

    if (deviceIsCommissioned()) {
        wifiConfigBegin(HOME_SSID, HOME_PASSWORD);
        bool ok = wifiConfigConnect(10000);
        if (ok) {
            stopAP();
            startMDNS();
            localMqttInit();
            Serial.println("[Boot] Restored — device online");
        } else {
            Serial.println("[Boot] WiFi unavailable — AP visible for reconfiguration");
        }
    } else {
        wifiConfigBegin(HOME_SSID, HOME_PASSWORD);
        Serial.println("[Boot] Not commissioned — waiting for Register Device");
    }

    googleSheetsInit();

    webServerInit();
    Serial.println("====== Setup complete ======\n");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // factoryResetHandle(); // enable when board assembled

    shtc3Read();
    ens160Read();
    ldrRead();

    static bool warnedSHTC3  = false;
    static bool warnedENS160 = false;
    if (!sensorOK  && !warnedSHTC3)  { Serial.println("⚠️ SHTC3 not found — temp/humidity unavailable"); warnedSHTC3  = true; }
    if (!ens160OK  && !warnedENS160) { Serial.println("⚠️ ENS160 not found — air quality unavailable");  warnedENS160 = true; }

    webServerHandle();

    // ── LED ──────────────────────────────────────────────────────────────────
    if (now - lastLedUpdate >= LED_INTERVAL) {
        lastLedUpdate = now;
        if (sensorOK) {
            float norm      = constrain((sensorTemp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN), 0.0f, 1.0f);
            uint16_t newHue = (uint16_t)((1.0f - norm) * 43690);
            if      (targetHue < newHue) targetHue += min((uint16_t)20, (uint16_t)(newHue - targetHue));
            else if (targetHue > newHue) targetHue -= min((uint16_t)20, (uint16_t)(targetHue - newHue));
            ring.fill(ring.gamma32(ring.ColorHSV(targetHue, 255, 200)));
        } else {
            rainbowHue += 128;
            ring.fill(ring.gamma32(ring.ColorHSV(rainbowHue, 255, 200)));
        }
        ring.show();
    }

    // ── Google Sheets every 60s ───────────────────────────────────────────────
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

    // ── AP fallback after 30s WiFi loss ──────────────────────────────────────
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
        if (apReEnabled) { stopAP(); startMDNS(); apReEnabled = false; }
        wifiLostAt = 0;
    }

    // ── Service handles ──────────────────────────────────────────────────────
    if (thingsBoardEnabled) {
        provisioningHandle();
        mqttHandle();
    }
    localMqttHandle();
}