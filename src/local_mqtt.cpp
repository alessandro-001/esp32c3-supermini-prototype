#include "local_mqtt.h"

#include "config.h"
#include "sensors.h"
#include "web_server.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

// Firmware MQTT target:
// ESP32-C3 -> Raspberry Pi MQTT broker -> mqtt_to_influx.py -> InfluxDB
//
// Docker bridge expects topics:
// sensors/+/telemetry
// sensors/+/attributes
// sensors/+/soil
// sensors/+/mineral

static WiFiClient localWifiClient;
static PubSubClient localMqtt(localWifiClient);

static uint8_t gSensorType = SENSOR_TYPE_DEFAULT;
static String gBrokerHost = LOCAL_MQTT_SERVER;
static uint16_t gBrokerPort = LOCAL_MQTT_PORT;

static unsigned long lastReconnectAttempt = 0;
static unsigned long lastAttributesPublish = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static String macNoColon() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return mac;
}

static String getDeviceSuffix() {
  String mac = macNoColon();
  return mac.length() >= 4 ? mac.substring(mac.length() - 4) : mac;
}

static const char* sensorTypePrefix(uint8_t type) {
  switch (type) {
    case 1: return "ENV_";
    case 2: return "SOIL_";
    case 3: return "MIN_";
    default: return "ENV_";
  }
}

static const char* sensorTypeLabel(uint8_t type) {
  switch (type) {
    case 1: return "environment";
    case 2: return "soil";
    case 3: return "mineral";
    default: return "environment";
  }
}

static const char* measurementFromSensorType(uint8_t type) {
  switch (type) {
    case 1: return "telemetry";
    case 2: return "soil";
    case 3: return "mineral";
    default: return "telemetry";
  }
}

static String getDeviceId() {
  return String(sensorTypePrefix(gSensorType)) + getDeviceSuffix();
}

static String buildDataTopic() {
  return "sensors/" + getDeviceId() + "/" + String(measurementFromSensorType(gSensorType));
}

static String buildAttributesTopic() {
  return "sensors/" + getDeviceId() + "/attributes";
}

static const char* boolText(bool v) {
  return v ? "true" : "false";
}

static int boolNum(bool v) {
  return v ? 1 : 0;
}

static const char* co2LabelLocal(int co2) {
  if (co2 <= 0) return "Unavailable";
  if (co2 < 800) return "Good";
  if (co2 < 1200) return "Moderate";
  if (co2 < 2000) return "Poor";
  return "Very Poor";
}

static String isoTimestampUtc() {
  time_t now = time(nullptr);

  // If NTP is not configured, return empty.
  // mqtt_to_influx.py will use server time when timestamp is missing.
  if (now < 1700000000) {
    return "";
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sensor Type NVS
// ─────────────────────────────────────────────────────────────────────────────

void localMqttSetSensorType(uint8_t type) {
  if (type < 1 || type > 3) type = SENSOR_TYPE_DEFAULT;

  gSensorType = type;

  Preferences p;
  p.begin("device", false);
  p.putUChar("sensor_type", type);
  p.end();

  Serial.printf("[LocalMQTT] Sensor type set -> %u (%s)\n",
                gSensorType,
                sensorTypeLabel(gSensorType));
}

uint8_t localMqttGetSensorType() {
  return gSensorType;
}

static void loadSensorType() {
  Preferences p;
  p.begin("device", true);
  gSensorType = p.getUChar("sensor_type", SENSOR_TYPE_DEFAULT);
  p.end();

  if (gSensorType < 1 || gSensorType > 3) {
    gSensorType = SENSOR_TYPE_DEFAULT;
  }

  Serial.printf("[LocalMQTT] Sensor type loaded -> %u (%s)\n",
                gSensorType,
                sensorTypeLabel(gSensorType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Broker settings
// ─────────────────────────────────────────────────────────────────────────────

void localMqttSetBroker(const String& hostOrIp, uint16_t port) {
  if (hostOrIp.length() == 0) {
    Serial.println("[LocalMQTT] Empty broker host ignored");
    return;
  }

  gBrokerHost = hostOrIp;
  gBrokerPort = port;

  Preferences p;
  p.begin("broker", false);
  p.putString("host", gBrokerHost);
  p.putUShort("port", gBrokerPort);
  p.end();

  localMqtt.disconnect();
  localWifiClient.stop();

  // PubSubClient accepts hostname or IP string here.
  localMqtt.setServer(gBrokerHost.c_str(), gBrokerPort);

  Serial.printf("[LocalMQTT] Broker saved -> %s:%u\n",
                gBrokerHost.c_str(),
                gBrokerPort);
}

String localMqttGetBrokerIP() {
  Preferences p;
  p.begin("broker", true);

  // Support both new key "host" and older key "ip".
  String host = p.getString("host", "");
  if (host.length() == 0) {
    host = p.getString("ip", LOCAL_MQTT_SERVER);
  }

  p.end();
  return host;
}

uint16_t localMqttGetBrokerPort() {
  Preferences p;
  p.begin("broker", true);
  uint16_t port = p.getUShort("port", LOCAL_MQTT_PORT);
  p.end();
  return port;
}

static void loadBroker() {
  gBrokerHost = localMqttGetBrokerIP();
  gBrokerPort = localMqttGetBrokerPort();

  if (gBrokerHost.length() == 0) {
    gBrokerHost = LOCAL_MQTT_SERVER;
  }

  localMqtt.setServer(gBrokerHost.c_str(), gBrokerPort);

  Serial.printf("[LocalMQTT] Broker loaded -> %s:%u\n",
                gBrokerHost.c_str(),
                gBrokerPort);
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection
// ─────────────────────────────────────────────────────────────────────────────

static void localMqttConnect() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (localMqtt.connected()) {
    return;
  }

  String clientId = "ESP32C3-" + macNoColon();

  Serial.printf("[LocalMQTT] Connecting to %s:%u as %s ... ",
                gBrokerHost.c_str(),
                gBrokerPort,
                clientId.c_str());

  bool ok = localMqtt.connect(clientId.c_str());

  if (ok) {
    Serial.println("connected");
    Serial.printf("[LocalMQTT] Data topic: %s\n", buildDataTopic().c_str());
    Serial.printf("[LocalMQTT] Attr topic: %s\n", buildAttributesTopic().c_str());

    logPush("[LocalMQTT] connected " + gBrokerHost + ":" + String(gBrokerPort));
    logPush("[LocalMQTT] topic " + buildDataTopic());

    // Force attributes after reconnect.
    lastAttributesPublish = 0;
  } else {
    Serial.printf("failed rc=%d wifi=%d rssi=%d ip=%s\n",
                  localMqtt.state(),
                  WiFi.status(),
                  WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());

    logPush("[LocalMQTT] connect failed rc=" + String(localMqtt.state()));
    localWifiClient.stop();
  }
}

void localMqttInit() {
  loadSensorType();
  loadBroker();

  localMqtt.setBufferSize(1024);
  localMqtt.setKeepAlive(30);
  localMqtt.setSocketTimeout(5);

  localMqttConnect();

  Serial.printf("[LocalMQTT] Initialised -> %s:%u\n",
                gBrokerHost.c_str(),
                gBrokerPort);
}

void localMqttHandle() {
  if (WiFi.status() != WL_CONNECTED) {
    if (localMqtt.connected()) {
      localMqtt.disconnect();
    }
    return;
  }

  if (localMqtt.connected()) {
    localMqtt.loop();

    // Publish attributes every 60 seconds.
    // This lets the API endpoint /api/devices/{device_id}/thresholds_from_device
    // find threshold-like fields in the "attributes" measurement.
    unsigned long now = millis();
    if (now - lastAttributesPublish >= 60000UL) {
      lastAttributesPublish = now;
      localMqttPublishConfig(30.0f, 10.0f, 80.0f, 40.0f, 1000.0f);
    }

    return;
  }

  unsigned long now = millis();
  if (now - lastReconnectAttempt >= 5000UL) {
    lastReconnectAttempt = now;
    localWifiClient.stop();
    localMqttConnect();
  }
}

bool localMqttIsConnected() {
  return localMqtt.connected();
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish telemetry / soil / mineral data
// ─────────────────────────────────────────────────────────────────────────────

void localMqttPublish() {
  if (!localMqtt.connected()) {
    return;
  }

  String deviceId = getDeviceId();
  String topic = buildDataTopic();
  String timestamp = isoTimestampUtc();

  char payload[900];

  if (gSensorType == 1) {
    // Environment sensor.
    if (timestamp.length() > 0) {
      snprintf(
        payload,
        sizeof(payload),
        "{"
          "\"device_id\":\"%s\","
          "\"timestamp\":\"%s\","
          "\"reading\":{"
            "\"sensor_type\":%u,"
            "\"sensor_type_label\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"temperature\":%.2f,"
            "\"humidity\":%.2f,"
            "\"co2\":%d,"
            "\"eco2\":%d,"
            "\"co2_label\":\"%s\","
            "\"co2_valid\":%s,"
            "\"alert_temp\":%s,"
            "\"alert_temp_num\":%d,"
            "\"alert_hum\":%s,"
            "\"alert_hum_num\":%d,"
            "\"alert_co2\":%s,"
            "\"alert_co2_num\":%d,"
            "\"light_on\":%s,"
            "\"light_on_num\":%d"
          "}"
        "}",
        deviceId.c_str(),
        timestamp.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        sensorTemp,
        sensorHum,
        sensorCO2,
        sensorCO2,
        co2LabelLocal(sensorCO2),
        boolText(sensorCO2 > 0),
        boolText(alertTemp),
        boolNum(alertTemp),
        boolText(alertHum),
        boolNum(alertHum),
        boolText(alertCO2),
        boolNum(alertCO2),
        boolText(ldrLightOn),
        boolNum(ldrLightOn)
      );
    } else {
      snprintf(
        payload,
        sizeof(payload),
        "{"
          "\"device_id\":\"%s\","
          "\"reading\":{"
            "\"sensor_type\":%u,"
            "\"sensor_type_label\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"temperature\":%.2f,"
            "\"humidity\":%.2f,"
            "\"co2\":%d,"
            "\"eco2\":%d,"
            "\"co2_label\":\"%s\","
            "\"co2_valid\":%s,"
            "\"alert_temp\":%s,"
            "\"alert_temp_num\":%d,"
            "\"alert_hum\":%s,"
            "\"alert_hum_num\":%d,"
            "\"alert_co2\":%s,"
            "\"alert_co2_num\":%d,"
            "\"light_on\":%s,"
            "\"light_on_num\":%d"
          "}"
        "}",
        deviceId.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        sensorTemp,
        sensorHum,
        sensorCO2,
        sensorCO2,
        co2LabelLocal(sensorCO2),
        boolText(sensorCO2 > 0),
        boolText(alertTemp),
        boolNum(alertTemp),
        boolText(alertHum),
        boolNum(alertHum),
        boolText(alertCO2),
        boolNum(alertCO2),
        boolText(ldrLightOn),
        boolNum(ldrLightOn)
      );
    }
  } else if (gSensorType == 2) {
    // Soil sensor.
    //* Replace these placeholder values with your real soil sensor readings.
    float soilEc = 0.0f;
    float soilRh = 0.0f;

    if (timestamp.length() > 0) {
      snprintf(
        payload,
        sizeof(payload),
        "{"
          "\"device_id\":\"%s\","
          "\"timestamp\":\"%s\","
          "\"reading\":{"
            "\"sensor_type\":%u,"
            "\"sensor_type_label\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"ec\":%.2f,"
            "\"rh\":%.2f"
          "}"
        "}",
        deviceId.c_str(),
        timestamp.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        soilEc,
        soilRh
      );
    } else {
      snprintf(
        payload,
        sizeof(payload),
        "{"
          "\"device_id\":\"%s\","
          "\"reading\":{"
            "\"sensor_type\":%u,"
            "\"sensor_type_label\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"ec\":%.2f,"
            "\"rh\":%.2f"
          "}"
        "}",
        deviceId.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        soilEc,
        soilRh
      );
    }
  } else {
    // Mineral sensor.
    //* Replace these placeholder values with your real mineral sensor readings.
    float mineralEc = 0.0f;
    float mineralN = 0.0f;
    float mineralP = 0.0f;
    float mineralK = 0.0f;

    if (timestamp.length() > 0) {
      snprintf(
        payload,
        sizeof(payload),
        "{"
          "\"device_id\":\"%s\","
          "\"timestamp\":\"%s\","
          "\"reading\":{"
            "\"sensor_type\":%u,"
            "\"sensor_type_label\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"ec\":%.2f,"
            "\"n\":%.2f,"
            "\"p\":%.2f,"
            "\"k\":%.2f"
          "}"
        "}",
        deviceId.c_str(),
        timestamp.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        mineralEc,
        mineralN,
        mineralP,
        mineralK
      );
    } else {
      snprintf(
        payload,
        sizeof(payload),
        "{"
          "\"device_id\":\"%s\","
          "\"reading\":{"
            "\"sensor_type\":%u,"
            "\"sensor_type_label\":\"%s\","
            "\"firmware\":\"%s\","
            "\"rssi\":%d,"
            "\"ec\":%.2f,"
            "\"n\":%.2f,"
            "\"p\":%.2f,"
            "\"k\":%.2f"
          "}"
        "}",
        deviceId.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        mineralEc,
        mineralN,
        mineralP,
        mineralK
      );
    }
  }

  bool ok = localMqtt.publish(topic.c_str(), payload);

  if (ok) {
    Serial.printf("[LocalMQTT] Published to %s: %s\n",
                  topic.c_str(),
                  payload);
  } else {
    Serial.printf("[LocalMQTT] Publish failed topic=%s payloadLen=%u state=%d\n",
                  topic.c_str(),
                  strlen(payload),
                  localMqtt.state());

    logPush("[LocalMQTT] publish failed rc=" + String(localMqtt.state()));
    localWifiClient.stop();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish attributes / thresholds
// ─────────────────────────────────────────────────────────────────────────────

void localMqttPublishConfig(float tempHigh,
                            float tempLow,
                            float humHigh,
                            float humLow,
                            float co2High) {
  if (!localMqtt.connected()) {
    return;
  }

  String deviceId = getDeviceId();
  String topic = buildAttributesTopic();
  String timestamp = isoTimestampUtc();

  char payload[700];

  if (timestamp.length() > 0) {
    snprintf(
      payload,
      sizeof(payload),
      "{"
        "\"device_id\":\"%s\","
        "\"timestamp\":\"%s\","
        "\"reading\":{"
          "\"mac\":\"%s\","
          "\"ip\":\"%s\","
          "\"rssi\":%d,"
          "\"firmware\":\"%s\","
          "\"sensor_type\":%u,"
          "\"sensor_type_label\":\"%s\","
          "\"highTempThreshold\":%.2f,"
          "\"lowTempThreshold\":%.2f,"
          "\"highHumThreshold\":%.2f,"
          "\"lowHumThreshold\":%.2f,"
          "\"highEco2Threshold\":%.2f"
        "}"
      "}",
      deviceId.c_str(),
      timestamp.c_str(),
      macNoColon().c_str(),
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI(),
      FIRMWARE_VERSION,
      gSensorType,
      sensorTypeLabel(gSensorType),
      tempHigh,
      tempLow,
      humHigh,
      humLow,
      co2High
    );
  } else {
    snprintf(
      payload,
      sizeof(payload),
      "{"
        "\"device_id\":\"%s\","
        "\"reading\":{"
          "\"mac\":\"%s\","
          "\"ip\":\"%s\","
          "\"rssi\":%d,"
          "\"firmware\":\"%s\","
          "\"sensor_type\":%u,"
          "\"sensor_type_label\":\"%s\","
          "\"highTempThreshold\":%.2f,"
          "\"lowTempThreshold\":%.2f,"
          "\"highHumThreshold\":%.2f,"
          "\"lowHumThreshold\":%.2f,"
          "\"highEco2Threshold\":%.2f"
        "}"
      "}",
      deviceId.c_str(),
      macNoColon().c_str(),
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI(),
      FIRMWARE_VERSION,
      gSensorType,
      sensorTypeLabel(gSensorType),
      tempHigh,
      tempLow,
      humHigh,
      humLow,
      co2High
    );
  }

  bool ok = localMqtt.publish(topic.c_str(), payload, true);

  if (ok) {
    Serial.printf("[LocalMQTT] Attributes published to %s: %s\n",
                  topic.c_str(),
                  payload);
  } else {
    Serial.printf("[LocalMQTT] Attributes publish failed topic=%s payloadLen=%u state=%d\n",
                  topic.c_str(),
                  strlen(payload),
                  localMqtt.state());

    logPush("[LocalMQTT] attributes publish failed rc=" + String(localMqtt.state()));
    localWifiClient.stop();
  }
}