#include "local_mqtt.h"
#include "config.h"
#include "sensors.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <Preferences.h>

//* Local MQTT client for Raspberry Pi pipeline (e.g., Node-RED)

static WiFiClient   localWifiClient;
static PubSubClient localMqtt(localWifiClient);

static String getTelemetryDeviceId() {
    return "IESWIC3A_" + WiFi.macAddress().substring(12);
}

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

void localMqttSetBroker(const String& ip, uint16_t port) {
    Preferences p;
    p.begin("broker", false);
    p.putString("ip",   ip);
    p.putUShort("port", port);
    p.end();
    localMqtt.disconnect();
    localMqtt.setServer(ip.c_str(), port);
    Serial.printf("[LocalMQTT] Broker updated → %s:%d\n", ip.c_str(), port);
}

String localMqttGetBrokerIP() {
    Preferences p;
    p.begin("broker", true);
    String ip = p.getString("ip", LOCAL_MQTT_SERVER);
    p.end();
    return ip;
}

uint16_t localMqttGetBrokerPort() {
    Preferences p;
    p.begin("broker", true);
    uint16_t port = p.getUShort("port", LOCAL_MQTT_PORT);
    p.end();
    return port;
}

void localMqttInit() {
    String brokerIp = localMqttGetBrokerIP();
    uint16_t brokerPort = localMqttGetBrokerPort();

    localMqtt.setServer(brokerIp.c_str(), brokerPort);
    localMqtt.setBufferSize(512);
    localMqttConnect();
    Serial.printf("[LocalMQTT] Initialised → %s:%u\n", brokerIp.c_str(), brokerPort);
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

    String deviceId = getTelemetryDeviceId();

    char payload[512];
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"firmware\":\"%s\","
        "\"rssi\":%d,"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"alert_temp\":%s,"
        "\"alert_temp_num\":%d,"
        "\"alert_hum\":%s,"
        "\"alert_hum_num\":%d,"
        "\"aqi\":%d,"
        "\"aqi_label\":\"%s\","
        "\"tvoc\":%d,"
        "\"eco2\":%d,"
        "\"air_quality_status\":\"%s\","
        "\"light_on\":%s,"
        "\"light_on_num\":%d"
        "}",
        deviceId.c_str(),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        sensorTemp,
        sensorHum,
        alertTemp  ? "true" : "false",
        alertTemp  ? 1 : 0,
        alertHum   ? "true" : "false",
        alertHum   ? 1 : 0,
        ens160AQI,
        ens160AQILabel(ens160AQI),
        ens160TVOC,
        ens160eCO2,
        ens160Status.c_str(),
        ldrLightOn ? "true" : "false",
        ldrLightOnNum()
    );

    localMqtt.publish(LOCAL_MQTT_TOPIC, payload);
    Serial.printf("[LocalMQTT] Published: %s\n", payload);
}

void localMqttPublishConfig(float tempHigh, float tempLow,
                            float humHigh,  float humLow,
                            int aqiHigh,    float co2High,
                            float tvocHigh) {
    if (!localMqtt.connected()) return;

    String deviceId = getTelemetryDeviceId();
    String topic = "IESWIC3A/" + deviceId + "/config";

    char payload[320];
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"temp_high\":%.2f,"
        "\"temp_low\":%.2f,"
        "\"hum_high\":%.2f,"
        "\"hum_low\":%.2f,"
        "\"aqi_high\":%d,"
        "\"co2_high\":%.0f,"
        "\"tvoc_high\":%.0f"
        "}",
        deviceId.c_str(),
        tempHigh,
        tempLow,
        humHigh,
        humLow,
        aqiHigh,
        co2High,
        tvocHigh
    );

    // Retain the latest config so backend consumers can recover state after reconnect.
    localMqtt.publish(topic.c_str(), payload, true);
    Serial.printf("[LocalMQTT] Config published on %s: %s\n", topic.c_str(), payload);
}