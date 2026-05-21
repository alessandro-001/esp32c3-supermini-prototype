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
static uint8_t      gSensorType = SENSOR_TYPE_DEFAULT;

static IPAddress    gBrokerIp;
static uint16_t     gBrokerPort = LOCAL_MQTT_PORT;

// ── Helpers ───────────────────────────────────────────────────────────────────

static String getDeviceSuffix() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return mac.length() >= 4 ? mac.substring(mac.length() - 4) : mac;
}

static const char* sensorTypePrefix(uint8_t type) {
    switch (type) {
        case 1:  return "ENV_";
        case 2:  return "SOIL_";
        case 3:  return "MIN_";
        default: return "ENV_";
    }
}

static String getDeviceId() {
    return String(sensorTypePrefix(gSensorType)) + getDeviceSuffix();
}

static String buildTopic() {
    return "IESWIC3A/" + String(gSensorType) + "/data";
}

static const char* sensorTypeLabel(uint8_t type) {
    switch (type) {
        case 1:  return "environment";
        case 2:  return "soil";
        case 3:  return "mineral";
        default: return "environment";
    }
}

// ── Sensor Type NVS ──────────────────────────────────────────────────────────

void localMqttSetSensorType(uint8_t type) {
    if (type < 1 || type > 3) type = SENSOR_TYPE_DEFAULT;
    gSensorType = type;
    Preferences p;
    p.begin("device", false);
    p.putUChar("sensor_type", type);
    p.end();
    Serial.printf("[LocalMQTT] Sensor type set → %d (%s)\n", type, sensorTypeLabel(type));
}

uint8_t localMqttGetSensorType() {
    return gSensorType;
}

static void loadSensorType() {
    Preferences p;
    p.begin("device", true);
    gSensorType = p.getUChar("sensor_type", SENSOR_TYPE_DEFAULT);
    p.end();
    if (gSensorType < 1 || gSensorType > 3) gSensorType = SENSOR_TYPE_DEFAULT;
    Serial.printf("[LocalMQTT] Sensor type loaded → %d (%s)\n", gSensorType, sensorTypeLabel(gSensorType));
}

// ── Connection ────────────────────────────────────────────────────────────────

static void localMqttConnect() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[LocalMQTT] WiFi not connected, skip MQTT connect. status=%d\n", WiFi.status());
        return;
    }
    if (localMqtt.connected()) return;

    String clientId = "IESWIC3A-" + WiFi.macAddress();
    clientId.replace(":", "");

    Serial.printf("[LocalMQTT] Connecting to %s:%u as %s...",
                  gBrokerIp.toString().c_str(),
                  gBrokerPort,
                  clientId.c_str());

    if (localMqtt.connect(clientId.c_str())) {
        Serial.println(" connected!");
        logPush("[LocalMQTT] connected! topic=" + buildTopic());
    } else {
        Serial.printf(" failed (rc=%d) WiFi=%d RSSI=%d IP=%s\n",
                      localMqtt.state(),
                      WiFi.status(),
                      WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());
        logPush("[LocalMQTT] failed (rc=" + String(localMqtt.state()) + ")");

        // CHANGE: close any half-open TCP socket before next reconnect attempt.
        localWifiClient.stop();
    }
}

void localMqttSetBroker(const String& ip, uint16_t port) {
    Preferences p;
    p.begin("broker", false);
    p.putString("ip",   ip);
    p.putUShort("port", port);
    p.end();

    localMqtt.disconnect();

    // CHANGE: close the underlying TCP socket when changing broker.
    localWifiClient.stop();

    gBrokerPort = port;

    // OLD CODE:
    // localMqtt.setServer(ip.c_str(), port);

    // CHANGE: parse the String once into static IPAddress storage.
    if (!gBrokerIp.fromString(ip)) {
        Serial.printf("[LocalMQTT] Invalid broker IP → %s\n", ip.c_str());
        logPush("[LocalMQTT] Invalid broker IP: " + ip);
        return;
    }

    localMqtt.setServer(gBrokerIp, gBrokerPort);
    Serial.printf("[LocalMQTT] Broker updated → %s:%u\n", gBrokerIp.toString().c_str(), gBrokerPort);
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
    loadSensorType();
    String   ip   = localMqttGetBrokerIP();
    uint16_t port = localMqttGetBrokerPort();

    gBrokerPort = port;

    // OLD CODE:
    // localMqtt.setServer(ip.c_str(), port);

    // CHANGE: avoid giving PubSubClient a pointer from a temporary/local String.
    if (!gBrokerIp.fromString(ip)) {
        Serial.printf("[LocalMQTT] Invalid broker IP → %s\n", ip.c_str());
        logPush("[LocalMQTT] Invalid broker IP: " + ip);
        return;
    }

    localMqtt.setServer(gBrokerIp, gBrokerPort);

    // OLD CODE:
    // localMqtt.setBufferSize(512);
    // localMqtt.setKeepAlive(60);
    // localMqtt.setSocketTimeout(15);

    // CHANGE: payload can exceed 512 with topic/header overhead; keep timeout short
    // so failed TCP connects do not stall the firmware for too long.
    localMqtt.setBufferSize(1024);
    localMqtt.setKeepAlive(30);
    localMqtt.setSocketTimeout(5);

    localMqttConnect();
    Serial.printf("[LocalMQTT] Initialised → %s:%u  topic=%s\n",
                  gBrokerIp.toString().c_str(), gBrokerPort, buildTopic().c_str());
}

void localMqttHandle() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!localMqtt.loop()) {
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 5000) {
            lastReconnect = millis();
            Serial.printf("[LocalMQTT] Connection lost rc=%d — reconnecting...\n", localMqtt.state());
            logPush("[LocalMQTT] Lost (rc=" + String(localMqtt.state()) + ") — reconnecting...");

            // CHANGE: clear stale socket before trying MQTT reconnect.
            localWifiClient.stop();
            localMqttConnect();
        }
    }
}

bool localMqttIsConnected() {
    return localMqtt.connected();
}

// ── Publish ───────────────────────────────────────────────────────────────────

void localMqttPublish() {
    if (!localMqtt.connected()) return;

    String deviceId = getDeviceId();
    String topic    = buildTopic();

    char payload[576];
    if (sensorCO2 == 0) {
        snprintf(payload, sizeof(payload),
            "{"
            "\"device_id\":\"%s\","
            "\"sensor_type\":%d,"
            "\"sensor_type_label\":\"%s\","
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
            deviceId.c_str(),
            gSensorType, sensorTypeLabel(gSensorType),
            FIRMWARE_VERSION, WiFi.RSSI(),
            sensorTemp, sensorHum,
            alertTemp ? "true" : "false", alertTemp ? 1 : 0,
            alertHum  ? "true" : "false", alertHum  ? 1 : 0,
            ldrLightOn ? "true" : "false", ldrLightOnNum()
        );
    } else {
        snprintf(payload, sizeof(payload),
            "{"
            "\"device_id\":\"%s\","
            "\"sensor_type\":%d,"
            "\"sensor_type_label\":\"%s\","
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
            deviceId.c_str(),
            gSensorType, sensorTypeLabel(gSensorType),
            FIRMWARE_VERSION, WiFi.RSSI(),
            sensorTemp, sensorHum,
            sensorCO2, co2Label(sensorCO2),
            alertTemp ? "true" : "false", alertTemp ? 1 : 0,
            alertHum  ? "true" : "false", alertHum  ? 1 : 0,
            alertCO2  ? "true" : "false", alertCO2  ? 1 : 0,
            ldrLightOn ? "true" : "false", ldrLightOnNum()
        );
    }

    // OLD CODE:
    // localMqtt.publish(topic.c_str(), payload);
    // Serial.printf("[LocalMQTT] Published to %s: %s\n", topic.c_str(), payload);

    // CHANGE: check publish() result so failed sends are visible.
    bool ok = localMqtt.publish(topic.c_str(), payload);
    if (ok) {
        Serial.printf("[LocalMQTT] Published to %s: %s\n", topic.c_str(), payload);
    } else {
        Serial.printf("[LocalMQTT] Publish failed topic=%s payloadLen=%u state=%d\n",
                      topic.c_str(),
                      strlen(payload),
                      localMqtt.state());
        localWifiClient.stop();
    }

    logPush("[LocalMQTT] T:" + String(sensorTemp,1) +
            " H:" + String(sensorHum,1) +
            " CO2:" + String(sensorCO2) +
            " type:" + String(gSensorType));
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
        "\"sensor_type\":%d,"
        "\"temp_high\":%.2f,"
        "\"temp_low\":%.2f,"
        "\"hum_high\":%.2f,"
        "\"hum_low\":%.2f,"
        "\"co2_high\":%.0f"
        "}",
        deviceId.c_str(),
        gSensorType,
        tempHigh, tempLow,
        humHigh,  humLow,
        co2High
    );

    // CHANGE: check retained config publish result too.
    bool ok = localMqtt.publish(topic.c_str(), payload, true);
    if (ok) {
        Serial.printf("[LocalMQTT] Config published on %s: %s\n", topic.c_str(), payload);
    } else {
        Serial.printf("[LocalMQTT] Config publish failed topic=%s payloadLen=%u state=%d\n",
                      topic.c_str(),
                      strlen(payload),
                      localMqtt.state());
        localWifiClient.stop();
    }
}
