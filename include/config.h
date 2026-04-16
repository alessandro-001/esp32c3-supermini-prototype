#pragma once

#include "secrets.h"

//! ── Access Point ────────────────────────────────────────────────────────────────────────────────────────────
#define AP_SSID             "ESP32C3_Hotspot"  //* http://192.168.4.1

//! ── Hardware Pins ESP32C3 ───────────────────────────────────────────────────────────────────────────────────────────
#define NEOPIXEL_PIN        3
#define NUM_LEDS            12
#define BRIGHTNESS          80  // RGB brightness (0-255)

// I2C — SHTC3 Temperature & Humidity
#define I2C_SDA             8   // GPIO8
#define I2C_SCL             9   // GPIO9

// SPI — ENS160 Air Quality (default SPI bus)
#define ENS160_SCK_PIN      4   // GPIO4  (SCK)
#define ENS160_MOSI_PIN     6   // GPIO6  (SDI / MOSI)
#define ENS160_MISO_PIN     5   // GPIO5  (SDO / MISO)
#define ENS160_CS_PIN       7   // GPIO7  (CS  / SS)
// #define ENS160_INT_PIN   10  // GPIO10 (INT — optional, not used)

// LDR — Light Detection
#define LDR_PIN         2     // GPIO2
#define LDR_THRESHOLD   2900  // above = light ON, adjust to environment

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