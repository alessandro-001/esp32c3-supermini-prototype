#pragma once
#include <Arduino.h>

//! ── Local MQTT (Raspberry Pi pipeline) ──────────────────────────────────────
void localMqttInit();           // call once in setup()
void localMqttHandle();         // call in loop()
void localMqttPublish();        // publish full sensor payload
void localMqttPublishConfig(float tempHigh, float tempLow,
							float humHigh,  float humLow,
							int aqiHigh,    float co2High,
							float tvocHigh); // publish numeric config payload
bool localMqttIsConnected();    // connection status


// Broker configuration (saved to NVS, changeable via web UI)
void   localMqttSetBroker(const String& ip, uint16_t port);
String localMqttGetBrokerIP();
uint16_t localMqttGetBrokerPort();