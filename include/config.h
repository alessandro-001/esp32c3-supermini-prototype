#pragma once

#include "secrets.h"

//! ── Access Point ─────────────────────────────────────────────────────────────
#define AP_SSID             "ESP32C3_Hotspot"

//! ── Hardware Pins ESP32-C3 ───────────────────────────────────────────────────
#define NEOPIXEL_PIN        20 // 3 (old)
#define NUM_LEDS            12
#define BRIGHTNESS          10

#define I2C_SDA             6 // 7(C3) 6(C6)
#define I2C_SCL             7 // 6(C3) 7(C6)

#define LDR_PIN             0
#define LDR_THRESHOLD       50
#define LDR_ENABLED         1

#define FACTORY_RESET_PIN   5 // 4 (old)

//! ── Sensor & LED Timing ──────────────────────────────────────────────────────
#define SENSOR_INTERVAL     5000
#define LED_INTERVAL        20

//! ── Temperature Range ────────────────────────────────────────────────────────
#define TEMP_MIN            15.0f
#define TEMP_MAX            35.0f

//! (Provisioning host and platform-specific defines removed)

//! ── Device Identity ──────────────────────────────────────────────────────────
#define DEVICE_GROUP        "PROTO_BF_DEVICES"
#define FIRMWARE_VERSION    "2.0.0"
#define TEMP_OFFSET_DEFAULT  0.0f   // °C — negative to reduce reading

//! ── Sensor Type ──────────────────────────────────────────────────────────────
// 1 = environment, 2 = soil, 3 = mineral
#define SENSOR_TYPE_DEFAULT 1

//! ── Local MQTT (Raspberry Pi / Docker) ──────────────────────────────────────
#define LOCAL_MQTT_SERVER   "weedsync.local"
#define LOCAL_MQTT_PORT     1883

//! ── mDNS ─────────────────────────────────────────────────────────────────────
#define MDNS_PREFIX         "bossfarm"

//! ── RS485 / Modbus RTU (MAX3485, see schematic) ─────────────────────────────
#define RS485_TX_PIN          16    // TXD0 pad -> MAX3485 DI  (driven as UART1)
#define RS485_RX_PIN          17    // RXD0 pad <- MAX3485 RO  (driven as UART1)
#define RS485_DE_PIN          14    // RS485_FC -> DE+RE, HIGH=TX LOW=RX (module pin 19)

#define RS485_TIMEOUT_MS      400   // per-transaction response timeout
#define RS485_POLL_INTERVAL   SENSOR_INTERVAL   // poll every 5s
#define RS485_MAX_FAILS       3     // consecutive failures before flagged unavailable

#define WATER_SENSOR_ADDR     1     // CWT-OYS-PHEC default slave ID
#define WATER_SENSOR_BAUD     9600  // CWT default: 9600,N,8,1
#define SOIL_SENSOR_ADDR      1     // Halisense default slave ID
#define SOIL_SENSOR_BAUD      4800  // Halisense default: 4800,N,8,1