#pragma once

#include "secrets.h"

//! ── Access Point ─────────────────────────────────────────────────────────────
#define AP_SSID             "ESP32C3_Hotspot"

//! ── Hardware Pins ESP32-C3 ───────────────────────────────────────────────────
#define NEOPIXEL_PIN        3
#define NUM_LEDS            12
#define BRIGHTNESS          10

#define I2C_SDA             7
#define I2C_SCL             6

#define LDR_PIN             0
#define LDR_THRESHOLD       50
#define LDR_ENABLED         1

#define FACTORY_RESET_PIN   4

//! ── Sensor & LED Timing ──────────────────────────────────────────────────────
#define SENSOR_INTERVAL     5000
#define LED_INTERVAL        20

//! ── Temperature Range ────────────────────────────────────────────────────────
#define TEMP_MIN            15.0f
#define TEMP_MAX            35.0f

//! ── ThingsBoard API ──────────────────────────────────────────────────────────
#define TB_SERVER           "mqtt.thingsboard.cloud"
#define TB_PORT             1883
#define TB_HTTP_HOST        "https://thingsboard.cloud"

//! ── Device Identity ──────────────────────────────────────────────────────────
#define DEVICE_GROUP        "PROTO_BF_DEVICES"
#define FIRMWARE_VERSION    "1.0.0"

//! ── Sensor Type ──────────────────────────────────────────────────────────────
// 1 = environment, 2 = soil, 3 = mineral
#define SENSOR_TYPE_DEFAULT 1

//! ── Local MQTT (Raspberry Pi / Docker) ──────────────────────────────────────
#define LOCAL_MQTT_SERVER   "weedsync.local"
#define LOCAL_MQTT_PORT     1883

//! ── mDNS ─────────────────────────────────────────────────────────────────────
#define MDNS_PREFIX         "bossfarm"