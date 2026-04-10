#include "local_mqtt.h"
#include "config.h"
#include "sensors.h"
#include <PubSubClient.h>
#include <WiFi.h>

static WiFiClient   localWifiClient;
static PubSubClient localMqtt(localWifiClient);

// Local MQTT client for Raspberry Pi pipeline (e.g., Node-RED)

static void localMqttConnect() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (localMqtt.connected()) return;

    String clientId = "IESWIC3A-" + WiFi.macAddress();
    clientId.replace(":", "");

    Serial.print("[LocalMQTT] Connecting...");
    if (localMqtt.connect(clientId.c_str())) {
        Serial.println(" connected!");
    } else {
        Serial.printf(" failed (rc=%d)\n", localMqtt.state());
    }
}

void localMqttInit() {
    localMqtt.setServer(LOCAL_MQTT_SERVER, LOCAL_MQTT_PORT);
    localMqtt.setBufferSize(512);
    localMqttConnect();
    Serial.println("[LocalMQTT] Initialised → " LOCAL_MQTT_SERVER);
}

void localMqttHandle() {
    localMqtt.loop();

    static unsigned long lastReconnect = 0;
    if (!localMqtt.connected() && millis() - lastReconnect > 5000) {
        lastReconnect = millis();
        localMqttConnect();
    }
}

bool localMqttIsConnected() {
    return localMqtt.connected();
}

void localMqttPublish() {
    if (!localMqtt.connected()) return;

    char payload[512];
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"IESWIC3A_%s\","
        "\"firmware\":\"%s\","
        "\"rssi\":%d,"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"alert_temp\":%s,"
        "\"alert_hum\":%s,"
        "\"aqi\":%d,"
        "\"aqi_label\":\"%s\","
        "\"tvoc\":%d,"
        "\"eco2\":%d,"
        "\"air_quality_status\":\"%s\","
        "\"light_on\":%s"
        "}",
        WiFi.macAddress().substring(12).c_str(),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        sensorTemp,
        sensorHum,
        alertTemp  ? "true" : "false",
        alertHum   ? "true" : "false",
        ens160AQI,
        ens160AQILabel(ens160AQI),
        ens160TVOC,
        ens160eCO2,
        ens160Status.c_str(),
        ldrLightOn ? "true" : "false"
    );

    localMqtt.publish(LOCAL_MQTT_TOPIC, payload);
    Serial.printf("[LocalMQTT] Published: %s\n", payload);
}