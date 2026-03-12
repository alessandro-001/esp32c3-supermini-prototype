#pragma once

#include "secrets.h"

// ── Access Point ──────────────────────────────────────────────
#define AP_SSID             "ESP32C3_Hotspot"  //* http://192.168.4.1

// ── Hardware Pins ─────────────────────────────────────────────
#define NEOPIXEL_PIN        3
#define NUM_LEDS            12
#define BRIGHTNESS          60  // RGB brightness (0-255)
#define I2C_SDA             8   // GPIO8 esp32-c3 
#define I2C_SCL             9   // GPIO9 esp32-c3 

// ── Sensor & LED Timing ───────────────────────────────────────
#define SENSOR_INTERVAL     2000
#define LED_INTERVAL        20

// ── Temperature Range ─────────────────────────────────────────
#define TEMP_MIN            15.0f
#define TEMP_MAX            35.0f

// ── ThingsBoard ───────────────────────────────────────────────
// #define TB_HOST             "thingsboard.cloud"
// #define TB_PORT             1883