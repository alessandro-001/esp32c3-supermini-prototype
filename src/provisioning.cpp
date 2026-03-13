#include "provisioning.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

//! ── Device Provisioning ───────────────────────────────────────────────────────

static Preferences provPrefs;
static WiFiClient provWifiClient;
static PubSubClient provMqtt(provWifiClient);
static ProvisioningState provState = PROV_STATE_IDLE;
static ProvisioningCallback provCallback = nullptr;
static String pendingToken;
static unsigned long provStartTime = 0;
static const unsigned long PROV_TIMEOUT_MS = 30000;

// ThingsBoard MQTT provisioning topics
static const char* PROV_REQUEST_TOPIC = "/provision/request";
static const char* PROV_RESPONSE_TOPIC = "/provision/response";

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

/// Returns true if a valid token is stored in NVS
bool provisioningHasToken() {
    provPrefs.begin("provision", true);  // read-only
    String token = provPrefs.getString("token", "");
    provPrefs.end();
    return token.length() > 0;
}

/// Check if device needs provisioning
bool provisioningNeeded() {
    return !provisioningHasToken();
}

/// Clear saved token from NVS
void provisioningClearToken() {
    provPrefs.begin("provision", false);
    provPrefs.clear();
    provPrefs.end();
    Serial.println("[Prov] Token cleared from NVS");
}

/// Get current provisioning state
ProvisioningState provisioningGetState() {
    return provState;
}

/// Set callback for provisioning completion
void provisioningSetCallback(ProvisioningCallback cb) {
    provCallback = cb;
}

//! ══════════════════════════════════════════════════════════════════════════════
//! HTTP Provisioning (for web interface - REQUIRED by webserver.cpp)
//! ══════════════════════════════════════════════════════════════════════════════

/// Send HTTP provisioning request to ThingsBoard
/// Returns token on success, empty string on failure
/// THIS FUNCTION IS CALLED BY webserver.cpp handleProvision()
String provisioningRequest() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Prov] Cannot provision: WiFi not connected");
        return "";
    }

    String deviceId   = provisioningDeviceId();
    String deviceName = "ESP32-" + deviceId;  // e.g. "ESP32-A4CF12AABBCC"

    Serial.printf("[Prov] Requesting token for device: %s\n", deviceName.c_str());

    // Build JSON payload
    StaticJsonDocument<256> doc;
    doc["deviceName"]            = deviceName;
    doc["provisionDeviceKey"]    = TB_PROVISION_KEY;
    doc["provisionDeviceSecret"] = TB_PROVISION_SECRET;

    String payload;
    serializeJson(doc, payload);

    // Send HTTP POST to ThingsBoard provisioning endpoint
    HTTPClient http;
    String url = String(TB_HTTP_HOST) + "/api/v1/provision";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(payload);
    String response = http.getString();
    http.end();

    Serial.printf("[Prov] HTTP %d: %s\n", httpCode, response.c_str());

    if (httpCode != 200) {
        Serial.printf("[Prov] Failed: HTTP %d\n", httpCode);
        return "";
    }

    // Parse JSON response
    StaticJsonDocument<256> resp;
    DeserializationError err = deserializeJson(resp, response);
    if (err) {
        Serial.printf("[Prov] JSON parse error: %s\n", err.c_str());
        return "";
    }

    // Check provisioning status
    const char* status = resp["status"];
    if (!status || String(status) != "SUCCESS") {
        Serial.printf("[Prov] TB status: %s\n", status ? status : "null");
        return "";
    }

    // Extract token
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

//! ══════════════════════════════════════════════════════════════════════════════
//! MQTT Provisioning (for automatic provisioning on boot)
//! ══════════════════════════════════════════════════════════════════════════════

/// MQTT callback for provisioning response
static void provMqttCallback(char* topic, byte* payload, unsigned int length) {
    if (String(topic) != PROV_RESPONSE_TOPIC) return;
    
    String response;
    for (unsigned int i = 0; i < length; i++) {
        response += (char)payload[i];
    }
    
    Serial.printf("[Prov] MQTT Response: %s\n", response.c_str());
    
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
        Serial.printf("[Prov] JSON parse error: %s\n", err.c_str());
        provState = PROV_STATE_FAILED;
        return;
    }
    
    const char* status = doc["status"];
    if (!status) {
        provState = PROV_STATE_FAILED;
        return;
    }
    
    if (String(status) == "SUCCESS") {
        const char* credentialsValue = doc["credentialsValue"];
        
        if (credentialsValue && strlen(credentialsValue) > 0) {
            pendingToken = String(credentialsValue);
            
            // Save to NVS
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

/// Load token from NVS on boot
String provisioningInit() {
    provPrefs.begin("provision", true);
    String token = provPrefs.getString("token", "");
    provPrefs.end();
    
    if (token.length() > 0) {
        Serial.printf("[Prov] Token loaded from NVS: %.8s...\n", token.c_str());
    } else {
        Serial.println("[Prov] No token in NVS — provisioning required");
    }
    
    // Setup MQTT client for provisioning
    provMqtt.setServer(TB_SERVER, TB_PORT);
    provMqtt.setCallback(provMqttCallback);
    provMqtt.setBufferSize(512);
    
    return token;
}

/// Start MQTT provisioning flow
bool provisioningStartMqtt() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Prov] Cannot start MQTT: WiFi not connected");
        return false;
    }
    
    if (provisioningHasToken()) {
        Serial.println("[Prov] Already provisioned");
        return false;
    }
    
    provState = PROV_STATE_CONNECTING;
    provStartTime = millis();
    Serial.println("[Prov] Starting MQTT provisioning...");
    
    return true;
}

/// State machine handler - call in loop()
void provisioningHandle() {
    if (provState == PROV_STATE_IDLE) return;
    
    // Handle success/failure states
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
    
    // Timeout check
    if (millis() - provStartTime > PROV_TIMEOUT_MS) {
        Serial.println("[Prov] ❌ Timeout");
        provState = PROV_STATE_FAILED;
        return;
    }
    
    switch (provState) {
        case PROV_STATE_CONNECTING:
            if (!provMqtt.connected()) {
                Serial.println("[Prov] Connecting to broker as 'provision'...");
                if (provMqtt.connect("provision")) {
                    Serial.println("[Prov] ✅ Connected to broker");
                    provMqtt.subscribe(PROV_RESPONSE_TOPIC);
                    provState = PROV_STATE_REQUESTING;
                } else {
                    Serial.printf("[Prov] Connection failed: %d\n", provMqtt.state());
                    delay(1000);
                }
            }
            break;
            
        case PROV_STATE_REQUESTING: {
            // Build provisioning request
            StaticJsonDocument<256> doc;
            doc["deviceName"]            = generateDeviceName();
            doc["provisionDeviceKey"]    = TB_PROVISION_KEY;
            doc["provisionDeviceSecret"] = TB_PROVISION_SECRET;
            
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