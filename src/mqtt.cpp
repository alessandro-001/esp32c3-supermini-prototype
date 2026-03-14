#include "mqtt.h"
//#include "sensors/shtc3.h"
#include "sensors.h"
#include "sensors/ens160.h"
#include "config.h"
#include "provisioning.h"
#include <PubSubClient.h>
#include <WiFi.h>

// Threshold values defined in web_server.cpp
extern float threshTemp;
extern float threshHum;
extern float threshTvoc;
extern float threshEco2;

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
        mqtt.subscribe("v1/devices/me/attributes");
        mqtt.subscribe("v1/devices/me/rpc/request/+");
    } else {
        Serial.printf("[MQTT] ❌ Failed (rc=%d), will retry\n", mqtt.state());
    }
}

void mqttInit(const String& token) {
    gToken = token;
    mqtt.setServer(TB_SERVER, TB_PORT);
    mqtt.setBufferSize(512);
    mqttConnect();
}

void mqttSetToken(const String& token) {
    if (gToken != token) {
        gToken = token;
        if (mqtt.connected()) mqtt.disconnect();
        if (!gToken.isEmpty()) mqttConnect();
    }
}

void mqttHandle() {
    if (gToken.isEmpty()) return;
    mqtt.loop();

    static unsigned long lastReconnect = 0;
    if (!mqtt.connected() && millis() - lastReconnect > 5000) {
        lastReconnect = millis();
        mqttConnect();
    }
}

bool mqttIsConnected() {
    return mqtt.connected();
}

/// Publish sensor telemetry to ThingsBoard
/// Includes: temperature, humidity, alerts, AQI, TVOC, eCO2
void mqttPublish() {
    if (!mqtt.connected()) return;

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"alert_temp\":%s,"
        "\"alert_hum\":%s,"
        "\"aqi\":%d,"
        "\"aqi_label\":\"%s\","
        "\"tvoc\":%d,"
        "\"eco2\":%d,"
        "\"air_quality_status\":\"%s\""
        "}",
        sensorTemp,
        sensorHum,
        alertTemp ? "true" : "false",
        alertHum  ? "true" : "false",
        ens160AQI,
        ens160AQILabel(ens160AQI),
        ens160TVOC,
        ens160eCO2,
        ens160Status.c_str()
    );

    mqtt.publish("v1/devices/me/telemetry", payload);
    Serial.printf("[MQTT] Published: %s\n", payload);
}

/// Publish device attributes to ThingsBoard
void mqttPublishAttributes() {
    if (!mqtt.connected()) return;

    char payload[384];
    snprintf(payload, sizeof(payload),
        "{"
        "\"mac\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"firmware\":\"%s\","
        "\"highTempThreshold\":%.1f,"
        "\"highHumThreshold\":%.1f,"
        "\"highTvocThreshold\":%.0f,"
        "\"highEco2Threshold\":%.0f"
        "}",
        provisioningDeviceId().c_str(),
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        FIRMWARE_VERSION,
        threshTemp,
        threshHum,
        threshTvoc,
        threshEco2
    );

    mqtt.publish("v1/devices/me/attributes", payload);
    Serial.printf("[MQTT] Attributes: %s\n", payload);
}

void mqttDisconnect() {
    if (mqtt.connected()) {
        Serial.println("[MQTT] Disconnecting...");
        mqtt.disconnect();
        delay(100);
        Serial.println("[MQTT] ✓ Disconnected");
    }
}