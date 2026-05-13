#include "local_mqtt.h"
#include "config.h"
#include "sensors.h"
#include "web_server.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <Preferences.h>

//* Local MQTT client for Raspberry Pi pipeline

static WiFiClient   localWifiClient;
static PubSubClient localMqtt(localWifiClient);

static String getDeviceId() {
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
        logPush("[LocalMQTT] connected!");
    } else {
        Serial.printf(" failed (rc=%d)\n", localMqtt.state());
        logPush("[LocalMQTT] failed (rc=" + String(localMqtt.state()) + ")");
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
    String   ip   = localMqttGetBrokerIP();
    uint16_t port = localMqttGetBrokerPort();
    localMqtt.setServer(ip.c_str(), port);
    localMqtt.setBufferSize(512);
    localMqtt.setKeepAlive(60);
    localMqtt.setSocketTimeout(15);
    localMqttConnect();
    Serial.printf("[LocalMQTT] Initialised → %s:%u\n", ip.c_str(), port);
}

void localMqttHandle() {
    if (WiFi.status() != WL_CONNECTED) return;  
    if (!localMqtt.loop()) {
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 5000) {
            lastReconnect = millis();
            Serial.println("[LocalMQTT] Connection lost — reconnecting...");
            logPush("[LocalMQTT] Lost (rc=" + String(localMqtt.state()) + ") — reconnecting...");
            // localMqtt.disconnect();
            // delay(100);
            localMqttConnect();
        }
    }
}

bool localMqttIsConnected() {
    return localMqtt.connected();
}

void localMqttPublish() {
    if (!localMqtt.connected()) return;

    String deviceId = getDeviceId();

    char payload[512];
    if (sensorCO2 == 0) {
        snprintf(payload, sizeof(payload),
            "{"
            "\"device_id\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"temperature\":%.2f,"
            "\"humidity\":%.2f,"
            "\"co2\":null,"
            "\"co2_label\":null,"
            "\"alert_temp\":%s,\"alert_temp_num\":%d,"
            "\"alert_hum\":%s,\"alert_hum_num\":%d,"
            "\"alert_co2\":false,\"alert_co2_num\":0,"
            "\"light_on\":%s,\"light_on_num\":%d"
            "}",
            deviceId.c_str(), FIRMWARE_VERSION, WiFi.RSSI(),
            sensorTemp, sensorHum,
            alertTemp ? "true" : "false", alertTemp ? 1 : 0,
            alertHum  ? "true" : "false", alertHum  ? 1 : 0,
            ldrLightOn ? "true" : "false", ldrLightOnNum()
        );
    } else {
        snprintf(payload, sizeof(payload),
            "{"
            "\"device_id\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"temperature\":%.2f,"
            "\"humidity\":%.2f,"
            "\"co2\":%d,"
            "\"co2_label\":\"%s\","
            "\"alert_temp\":%s,\"alert_temp_num\":%d,"
            "\"alert_hum\":%s,\"alert_hum_num\":%d,"
            "\"alert_co2\":%s,\"alert_co2_num\":%d,"
            "\"light_on\":%s,\"light_on_num\":%d"
            "}",
            deviceId.c_str(), FIRMWARE_VERSION, WiFi.RSSI(),
            sensorTemp, sensorHum,
            sensorCO2, co2Label(sensorCO2),
            alertTemp ? "true" : "false", alertTemp ? 1 : 0,
            alertHum  ? "true" : "false", alertHum  ? 1 : 0,
            alertCO2  ? "true" : "false", alertCO2  ? 1 : 0,
            ldrLightOn ? "true" : "false", ldrLightOnNum()
        );
    }

    localMqtt.publish(LOCAL_MQTT_TOPIC, payload);
    Serial.printf("[LocalMQTT] Published: %s\n", payload);
    logPush("[LocalMQTT] Published T:" + String(sensorTemp,1) + " H:" + String(sensorHum,1) + " CO2:" + String(sensorCO2));
}

void localMqttPublishConfig(float tempHigh, float tempLow,
                             float humHigh,  float humLow,
                             float co2High) {
    if (!localMqtt.connected()) return;

    String deviceId = getDeviceId();
    String topic    = "IESWIC3A/" + deviceId + "/config";

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"temp_high\":%.2f,"
        "\"temp_low\":%.2f,"
        "\"hum_high\":%.2f,"
        "\"hum_low\":%.2f,"
        "\"co2_high\":%.0f"
        "}",
        deviceId.c_str(),
        tempHigh,
        tempLow,
        humHigh,
        humLow,
        co2High
    );

    localMqtt.publish(topic.c_str(), payload, true);
    Serial.printf("[LocalMQTT] Config published on %s: %s\n", topic.c_str(), payload);
}