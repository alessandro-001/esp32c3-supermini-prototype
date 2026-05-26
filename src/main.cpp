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
#include "local_mqtt.h"
#include "factory_reset.h"
#include <esp_wifi.h>

//* ESP32C3 Smart Monitor — MAIN

// ── NeoPixel ──────────────────────────────────────────────────────────────────
Adafruit_NeoPixel ring(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t lastLedUpdate = 0;

// ── Feature flags ─────────────────────────────────────────────────────────────
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

void setCommissionedPublic() { setCommissioned(); }

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
    WiFi.mode(WIFI_AP_STA);   
    delay(200); 
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String apSsid = String(AP_SSID) + "_" + mac.substring(8);

    // Must be called BEFORE softAP() to configure DHCP pool correctly
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );
    bool ok = WiFi.softAP(apSsid.c_str(), AP_PASSWORD, 6);
    Serial.printf("[AP] softAP returned: %s\n", ok ? "OK" : "FAILED");
    esp_wifi_set_ps(WIFI_PS_NONE);
    delay(500);
    Serial.printf("[AP] Started: %s @ %s\n",
                  apSsid.c_str(), WiFi.softAPIP().toString().c_str());
                  
    Serial.printf("[AP] Station count: %d\n", WiFi.softAPgetStationNum());
    Serial.printf("[AP] AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
    Serial.printf("[AP] SSID visible: %s\n", WiFi.softAPSSID().c_str());
}

static void stopAP() {
    WiFi.softAPdisconnect(true);
    delay(200);
    Serial.println("[AP] Hotspot stopped");
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

    factoryResetInit();

    scd40Init();
    ldrInit();

    // ── Radio init: AP first, always ─────────────────────────────────────────
    WiFi.persistent(false);
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    delay(300);
    startAP();          // AP up before any STA attempt
    delay(500);

    wifiConfigBegin(HOME_SSID, HOME_PASSWORD);

    if (deviceIsCommissioned()) {
        bool ok = wifiConfigConnect(15000);
        if (ok) {
            stopAP();
            startMDNS();
            localMqttInit();
            Serial.println("[Boot] Restored — device online");
            logPush("[Boot] WiFi OK — IP: " + WiFi.localIP().toString());
        } else {
            // AP already running — just log it
            Serial.println("[Boot] WiFi unavailable — AP visible for reconfiguration");
            logPush("[Boot] WiFi failed — AP staying on");
        }
    } else {
#ifdef DEV_MODE
        bool ok = wifiConfigConnect(15000);
        if (ok) {
            setCommissionedPublic();
            stopAP();
            startMDNS();
            localMqttInit();
            Serial.println("[Boot] DEV_MODE: Auto-commissioned");
            logPush("[Boot] DEV_MODE online");
        } else {
            Serial.println("[Boot] DEV_MODE: WiFi failed — AP on");
        }
#else
        Serial.println("[Boot] Not commissioned — waiting for Register Device");
#endif
    }

    webServerInit();
    Serial.println("====== Setup complete ======\n");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    webServerHandle();
    localMqttHandle();

    if (thingsBoardEnabled) {
        provisioningHandle();
        mqttHandle();
    }

    factoryResetHandle();

    // Short delay to prevent watchdog and allow background tasks to run.
    delay(1);

    // Sensor reads every 2s, staggered to avoid doing them both at the same time.
    scd40Read();

    static uint32_t lastLdrCheck = 0;
    if (now - lastLdrCheck >= SENSOR_INTERVAL) {
        lastLdrCheck = now;

        webServerHandle();
        localMqttHandle();

        ring.clear();
        ring.show();
        delay(20);

        webServerHandle();
        localMqttHandle();

        ldrRead();
    }

    static bool warnedSCD40 = false;
    if (!sensorOK && !warnedSCD40) {
        Serial.println("WARNING: SCD40 not found - all sensors unavailable");
        warnedSCD40 = true;
    }

    // NeoPixel update every LED_INTERVAL ms.
    // States (priority order):
    //   BLUE   — not commissioned (device never registered)
    //   RED    — commissioned but WiFi disconnected
    //   YELLOW — WiFi up but MQTT broker disconnected
    //   GREEN  — WiFi up and MQTT broker connected (data flowing)
    if (now - lastLedUpdate >= LED_INTERVAL) {
        lastLedUpdate = now;

        uint32_t colour;
        bool commissioned = deviceIsCommissioned();
        bool wifiUp       = (WiFi.status() == WL_CONNECTED);
        bool mqttUp       = localMqttIsConnected();

        if (!commissioned) {
            colour = ring.Color(0, 0, 40);      // Blue  — not registered
        } else if (!wifiUp) {
            colour = ring.Color(40, 0, 0);      // Red   — no WiFi
        } else if (!mqttUp) {
            colour = ring.Color(40, 20, 0);     // Amber — WiFi OK, no broker
        } else {
            colour = ring.Color(0, 40, 0);      // Green — fully connected
        }

        ring.fill(colour);
        ring.show();
    }

    // Telemetry publish every 5s.
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
                if (!sentAttributes) {
                    mqttPublishAttributes();
                    sentAttributes = true;
                }
            }
        }

        localMqttPublish();
    }

    // Health log every 30s.
    static uint32_t lastHealthLog = 0;
    if (now - lastHealthLog >= 30000) {
        lastHealthLog = now;
        logPush("[Sys] heap=" + String(ESP.getFreeHeap()) +
                " up=" + String(now / 1000) + "s" +
                " wifi=" + String(WiFi.status() == WL_CONNECTED ? "OK" : "X") +
                " mqtt=" + String(localMqttIsConnected() ? "OK" : "X") +
                " rssi=" + String(WiFi.RSSI()));

        Serial.printf("[Sys] heap=%u up=%lus wifi=%d mqtt=%d rssi=%d\n",
                      ESP.getFreeHeap(),
                      now / 1000,
                      WiFi.status(),
                      localMqttIsConnected(),
                      WiFi.RSSI());
    }

    // Wi-Fi and MQTT connection management.
    static uint32_t wifiLostAt   = 0;
    static bool     apReEnabled  = false;
    static uint32_t lastStaRetry = 0;
    static const uint32_t AP_FALLBACK_MS = 30000;
    static const uint32_t STA_RETRY_MS   = 120000;

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiLostAt == 0) {
            wifiLostAt = now;
            logPush("[WiFi] Lost connection (status=" + String(WiFi.status()) + ")");
        }

        if (!apReEnabled && deviceIsCommissioned() && (now - wifiLostAt > AP_FALLBACK_MS)) {
            Serial.println("[WiFi] Lost for 30s - re-enabling AP");
            logPush("[WiFi] Lost 30s - AP re-enabled");
            startAP();
            apReEnabled = true;
        }

        if (deviceIsCommissioned() && wifiConfigHasCredentials()
            && (now - lastStaRetry > STA_RETRY_MS)
            && !localMqttIsConnected()) {

            lastStaRetry = now;
            Serial.println("[WiFi] Retrying STA connection...");
            logPush("[WiFi] Retry attempt...");

            
            bool ok = wifiConfigConnect(10000);
            if (ok) {
                if (apReEnabled) {
                    stopAP();
                    apReEnabled = false;
                }
                startMDNS();
                wifiLostAt = 0;
                Serial.println("[WiFi] Reconnected!");
                logPush("[WiFi] Reconnected! IP: " + WiFi.localIP().toString());
                localMqttInit();
            } else {
                logPush("[WiFi] Retry failed (status=" + String(WiFi.status()) + ")");
            }
        }
    } else {
        if (apReEnabled) {
            stopAP();
            startMDNS();
            apReEnabled = false;
        }
        wifiLostAt   = 0;
        lastStaRetry = 0;
    }
}
