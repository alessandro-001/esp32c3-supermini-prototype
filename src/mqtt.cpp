#include "mqtt.h"
#include "sensors/shtc3.cpp"
#include "config.h"
#include "provisioning.h"
#include <PubSubClient.h>
#include <WiFi.h>

static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static String       gToken;

//! ── MQTT ─────────────────────────────────────────────────────────────────────

static void mqttConnect() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) return;
    if (gToken.isEmpty()) {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            Serial.println("[MQTT] Waiting for provisioning...");
            warnedOnce = true;
        }
        return;
    }

    String clientId = "ESP32-" + provisioningDeviceId();
    Serial.printf("[MQTT] Connecting as %s...\n", clientId.c_str());
    
    if (mqtt.connect(clientId.c_str(), gToken.c_str(), NULL)) {
        Serial.println("[MQTT] ✅ Connected to ThingsBoard");
        
        // Subscribe to attribute updates and RPC
        mqtt.subscribe("v1/devices/me/attributes");
        mqtt.subscribe("v1/devices/me/rpc/request/+");
    } else {
        Serial.printf("[MQTT] ❌ Failed (rc=%d), will retry\n", mqtt.state());
    }
}

/// Initialise MQTT with a dynamic token from NVS/provisioning
void mqttInit(const String& token) {
    gToken = token;
    mqtt.setServer(TB_SERVER, TB_PORT);
    mqtt.setBufferSize(512);
    mqttConnect();
}

/// Update token (used after provisioning completes)
void mqttSetToken(const String& token) {
    if (gToken != token) {
        gToken = token;
        if (mqtt.connected()) {
            mqtt.disconnect();
        }
        if (!gToken.isEmpty()) {
            mqttConnect();
        }
    }
}

/// Call mqtt.loop() to maintain connection and handle incoming messages
void mqttHandle() {
    if (gToken.isEmpty()) return;
    
    mqtt.loop();
    
    // Non-blocking reconnect
    static unsigned long lastReconnect = 0;
    if (!mqtt.connected() && millis() - lastReconnect > 5000) {
        lastReconnect = millis();
        mqttConnect();
    }
}

/// Check if MQTT is connected
bool mqttIsConnected() {
    return mqtt.connected();
}

/// Publish sensor data and alert status to ThingsBoard
void mqttPublish() {
    if (!mqtt.connected()) return;

    char payload[128];
    snprintf(payload, sizeof(payload),
        "{\"temperature\":%.2f,\"humidity\":%.2f,\"alert_temp\":%s,\"alert_hum\":%s}",
        sensorTemp, sensorHum,
        alertTemp ? "true" : "false",
        alertHum  ? "true" : "false"
    );
    mqtt.publish("v1/devices/me/telemetry", payload);
    Serial.printf("[MQTT] Published: %s\n", payload);
}

/// Publish device attributes to ThingsBoard
void mqttPublishAttributes() {
    if (!mqtt.connected()) return;

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"mac\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"firmware\":\"%s\"}",
        provisioningDeviceId().c_str(),
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        FIRMWARE_VERSION
    );
    mqtt.publish("v1/devices/me/attributes", payload);
    Serial.printf("[MQTT] Attributes: %s\n", payload);
}

/// Cleanly disconnect from MQTT broker
void mqttDisconnect() {
    if (mqtt.connected()) {
        Serial.println("[MQTT] Disconnecting...");
        mqtt.disconnect();
        delay(100);  // Allow disconnect to complete
        Serial.println("[MQTT] ✓ Disconnected");
    }
}