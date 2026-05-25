#pragma once
#include <Arduino.h>

void localMqttInit();
void localMqttHandle();
void localMqttPublish();
void localMqttPublishConfig(float tempHigh, float tempLow,
                             float humHigh,  float humLow,
                             float co2High);
bool localMqttIsConnected();

void     localMqttSetBroker(const String& ip, uint16_t port);
String   localMqttGetBrokerIP();
uint16_t localMqttGetBrokerPort();

void    localMqttSetSensorType(uint8_t type);
uint8_t localMqttGetSensorType();