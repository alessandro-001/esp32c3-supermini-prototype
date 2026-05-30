#include "provisioning.h"
#include "config.h"
#include "secrets.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

//! ── Device Provisioning ───────────────────────────────────────────────────────

static Preferences      provPrefs;
static WiFiClient       provWifiClient;
static PubSubClient     provMqtt(provWifiClient);
static ProvisioningState provState    = PROV_STATE_IDLE;
static ProvisioningCallback provCallback = nullptr;
static String           pendingToken;
static unsigned long    provStartTime = 0;
static const unsigned long PROV_TIMEOUT_MS = 30000;

static const char* PROV_REQUEST_TOPIC  = "/provision/request";
static const char* PROV_RESPONSE_TOPIC = "/provision/response";

// ── Device Identity ───────────────────────────────────────────────────────────

/// Get unique device ID from MAC address (e.g. "A4CF12AABBCC")
String provisioningDeviceId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[13];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(id);
}

/// Generate device name following pattern: ESP32-{MAC}
static String generateDeviceName() {
    return "ESP32-" + provisioningDeviceId();
}

// ── NVS helpers ───────────────────────────────────────────────────────────────

bool provisioningHasToken() {
    provPrefs.begin("provision", true);
    String token = provPrefs.getString("token", "");
    provPrefs.end();
    return token.length() > 0;
}

bool provisioningNeeded() {
    return !provisioningHasToken();
}

void provisioningClearToken() {
    provPrefs.begin("provision", false);
    provPrefs.clear();
    provPrefs.end();
    Serial.println("[Prov] Token cleared from NVS");
}

ProvisioningState provisioningGetState() {
    return provState;
}

void provisioningSetCallback(ProvisioningCallback cb) {
    provCallback = cb;
}



// ══════════════════════════════════════════════════════════════════════════════
// HTTP Provisioning
// ══════════════════════════════════════════════════════════════════════════════

String provisioningRequest() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Prov] Cannot provision: WiFi not connected");
        return "";
    }

    String deviceId   = provisioningDeviceId();
    String deviceName = "ESP32-" + deviceId;

    Serial.printf("[Prov] Requesting token for device: %s\n", deviceName.c_str());

    StaticJsonDocument<256> doc;
    doc["deviceName"]            = deviceName;

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    http.addHeader("Content-Type", "application/json");

    int    httpCode = http.POST(payload);
    String response = http.getString();
    http.end();

    Serial.printf("[Prov] HTTP %d: %s\n", httpCode, response.c_str());

    if (httpCode != 200) {
        Serial.printf("[Prov] Failed: HTTP %d\n", httpCode);
        return "";
    }

    StaticJsonDocument<256> resp;
    DeserializationError err = deserializeJson(resp, response);
    if (err) {
        Serial.printf("[Prov] JSON parse error: %s\n", err.c_str());
        return "";
    }

    const char* status = resp["status"];
    if (!status || String(status) != "SUCCESS") {
        Serial.printf("[Prov] status: %s\n", status ? status : "null");
        return "";
    }

    const char* token = resp["credentialsValue"];
    if (!token || strlen(token) == 0) {
        Serial.println("[Prov] No token in response");
        return "";
    }

    // Save token to NVS
    provPrefs.begin("provision", false);
    provPrefs.putString("token", token);
    provPrefs.putString("device_name", deviceName);
    provPrefs.end();

    Serial.printf("[Prov] ✅ Token saved: %s\n", token);
    return String(token);
}

// ══════════════════════════════════════════════════════════════════════════════
// MQTT Provisioning
// ══════════════════════════════════════════════════════════════════════════════

static void provMqttCallback(char* topic, byte* payload, unsigned int length) {
    if (String(topic) != PROV_RESPONSE_TOPIC) return;

    String response;
    for (unsigned int i = 0; i < length; i++) response += (char)payload[i];

    Serial.printf("[Prov] MQTT Response: %s\n", response.c_str());

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
        Serial.printf("[Prov] JSON parse error: %s\n", err.c_str());
        provState = PROV_STATE_FAILED;
        return;
    }

    const char* status = doc["status"];
    if (!status) { provState = PROV_STATE_FAILED; return; }

    if (String(status) == "SUCCESS") {
        const char* credentialsValue = doc["credentialsValue"];
        if (credentialsValue && strlen(credentialsValue) > 0) {
            pendingToken = String(credentialsValue);

            provPrefs.begin("provision", false);
            provPrefs.putString("token", pendingToken);
            provPrefs.putString("device_name", generateDeviceName());
            provPrefs.end();

            Serial.printf("[Prov] ✅ MQTT Token saved: %s\n", pendingToken.c_str());
            provState = PROV_STATE_SUCCESS;
        } else {
            provState = PROV_STATE_FAILED;
        }
    } else {
        Serial.printf("[Prov] ❌ MQTT Provisioning failed: %s\n", status);
        provState = PROV_STATE_FAILED;
    }
}

String provisioningInit() {
    provPrefs.begin("provision", true);
    String token = provPrefs.getString("token", "");
    provPrefs.end();

    if (token.length() > 0) {
        Serial.printf("[Prov] Token loaded from NVS: %.8s...\n", token.c_str());
    } else {
        Serial.println("[Prov] No token in NVS — provisioning required");
    }

    // provMqtt.setServer(...) removed (platform-specific)
    provMqtt.setCallback(provMqttCallback);
    provMqtt.setBufferSize(512);

    return token;
}

bool provisioningStartMqtt() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Prov] Cannot start MQTT: WiFi not connected");
        return false;
    }
    if (provisioningHasToken()) {
        Serial.println("[Prov] Already provisioned");
        return false;
    }
    provState     = PROV_STATE_CONNECTING;
    provStartTime = millis();
    Serial.println("[Prov] Starting MQTT provisioning...");
    return true;
}

void provisioningHandle() {
    if (provState == PROV_STATE_IDLE) return;

    if (provState == PROV_STATE_SUCCESS) {
        provMqtt.disconnect();
        if (provCallback) provCallback(true, pendingToken);
        provState = PROV_STATE_IDLE;
        return;
    }

    if (provState == PROV_STATE_FAILED) {
        provMqtt.disconnect();
        if (provCallback) provCallback(false, "");
        provState = PROV_STATE_IDLE;
        return;
    }

    if (millis() - provStartTime > PROV_TIMEOUT_MS) {
        Serial.println("[Prov] ❌ Timeout");
        provState = PROV_STATE_FAILED;
        return;
    }

    switch (provState) {
        case PROV_STATE_CONNECTING: {
        static unsigned long lastConnectAttempt = 0;
        if (!provMqtt.connected()) {
            if (millis() - lastConnectAttempt < 1000) break; // non-blocking wait
            lastConnectAttempt = millis();
            Serial.println("[Prov] Connecting to broker as 'provision'...");
            if (provMqtt.connect("provision")) {
                Serial.println("[Prov] ✅ Connected to broker");
                provMqtt.subscribe(PROV_RESPONSE_TOPIC);
                provState = PROV_STATE_REQUESTING;
            } else {
                Serial.printf("[Prov] Connection failed: %d, retrying...\n", provMqtt.state());
            }
        }
        break;
}

        case PROV_STATE_REQUESTING: {
            StaticJsonDocument<256> doc;
            doc["deviceName"]            = generateDeviceName();
            // provision keys removed

            String payload;
            serializeJson(doc, payload);
            Serial.printf("[Prov] Sending MQTT request: %s\n", payload.c_str());

            if (provMqtt.publish(PROV_REQUEST_TOPIC, payload.c_str())) {
                provState = PROV_STATE_WAITING_RESPONSE;
            } else {
                Serial.println("[Prov] MQTT publish failed");
            }
            break;
        }

        case PROV_STATE_WAITING_RESPONSE:
            provMqtt.loop();
            break;

        default:
            break;
    }
}
