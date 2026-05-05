#pragma once

#include "secrets.h"

//! ── Access Point ────────────────────────────────────────────────────────────────────────────────────────────
#define AP_SSID             "ESP32C3_Hotspot"  //* http://192.168.4.1

//! ── Hardware Pins ESP32-C3 ───────────────────────────────────────────────────────────────────────────────────────────
// NeoPixel RGB ring (WS2812B)
#define NEOPIXEL_PIN        3
#define NUM_LEDS            12
#define BRIGHTNESS          80  // RGB brightness (0-255)

// I2C — SHTC3 & ENS160 Temperature & Humidity + Air Quality (shared bus) now SCD40 only
#define I2C_SDA             7   // mine is 8 // Hin is 7
#define I2C_SCL             6   // mine is 9 // Hin is 6

// LDR — Light Detection
#define LDR_PIN         0     // mine is 2 // Hin is 0
#define LDR_THRESHOLD   2900  // above = light ON, adjust to environment

// Factory Reset button — active LOW (internal pullup)
#define FACTORY_RESET_PIN   4       // GPIO4 — SMD button to GND

//! ── Sensor & LED Timing ─────────────────────────────────────────────────────────────────────────────────────
#define SENSOR_INTERVAL     2000    // Sensor read interval (ms)
#define LED_INTERVAL        20      // LED animation update interval (ms)

//! ── Temperature Range ───────────────────────────────────────────────────────────────────────────────────────
#define TEMP_MIN            15.0f   // Minimum expected temperature (°C) for color mapping
#define TEMP_MAX            35.0f   // Maximum expected temperature (°C) for color mapping

//! ── ThingsBoard API ─────────────────────────────────────────────────────────────────────────────────────────
#define TB_SERVER     "mqtt.thingsboard.cloud"          // ThingsBoard MQTT broker
#define TB_PORT       1883                              // MQTT port
#define TB_HTTP_HOST  "https://thingsboard.cloud"       // for provisioning HTTP

//! ── Device Identity ─────────────────────────────────────────────────────────────────────────────────────────
#define DEVICE_GROUP        "PROTO_BF_DEVICES"          // Device group for ThingsBoard provisioning
#define FIRMWARE_VERSION    "1.0.0"                     // Firmware version for ThingsBoard provisioning

//! ── Local MQTT (Raspberry Pi / Docker) ─────────────────────────────────────
#define LOCAL_MQTT_SERVER   "192.168.0.16"             // Pi's static IP .16 //or my macbook .240
#define LOCAL_MQTT_PORT     1883                        // MQTT port
#define LOCAL_MQTT_TOPIC    "IESWIC3A/data"             // Topic for local MQTT (e.g., for Node-RED)

//! ── mDNS ─────────────────────────────────────────────────────────────────────
#define MDNS_PREFIX         "bossfarm"

