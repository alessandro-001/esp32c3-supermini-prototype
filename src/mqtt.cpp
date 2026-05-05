#include "mqtt.h"
#include "sensors.h"
#include "config.h"
#include "provisioning.h"
#include <PubSubClient.h>
#include <WiFi.h>

extern float threshTemp;
extern float threshTempLow;
extern float threshHum;
extern float threshHumLow;
extern float threshCO2;

static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static String       gToken;

static void mqttConnect() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) return;
    if (gToken.isEmpty()) {
        static bool warnedOnce = false;
        if (!warnedOnce) { Serial.println("[MQTT] Waiting for provisioning..."); warnedOnce = true; }
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

bool mqttIsConnected() { return mqtt.connected(); }

void mqttPublish() {
    if (!mqtt.connected()) return;
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"co2\":%d,"
        "\"co2_label\":\"%s\","
        "\"alert_temp\":%s,"
        "\"alert_hum\":%s,"
        "\"alert_co2\":%s,"
        "\"light_on\":%s"
        "}",
        sensorTemp, sensorHum,
        sensorCO2, co2Label(sensorCO2),
        alertTemp  ? "true" : "false",
        alertHum   ? "true" : "false",
        alertCO2   ? "true" : "false",
        ldrLightOn ? "true" : "false"
    );
    mqtt.publish("v1/devices/me/telemetry", payload);
    Serial.printf("[MQTT] Published: %s\n", payload);
}

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
        "\"lowTempThreshold\":%.1f,"
        "\"highHumThreshold\":%.1f,"
        "\"lowHumThreshold\":%.1f,"
        "\"highCO2Threshold\":%.0f"
        "}",
        provisioningDeviceId().c_str(),
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        FIRMWARE_VERSION,
        threshTemp, threshTempLow,
        threshHum,  threshHumLow,
        threshCO2
    );
    mqtt.publish("v1/devices/me/attributes", payload);
    Serial.printf("[MQTT] Attributes: %s\n", payload);
}

void mqttDisconnect() {
    if (mqtt.connected()) {
        mqtt.disconnect();
        delay(100);
        Serial.println("[MQTT] ✓ Disconnected");
    }
}