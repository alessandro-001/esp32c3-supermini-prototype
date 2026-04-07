# Smart Farm Monitor — Firmware

![ESP32-C3](https://img.shields.io/badge/ESP32--C3-Lolin_C3_Mini-blue?style=flat-square)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-orange?style=flat-square)
![ThingsBoard](https://img.shields.io/badge/ThingsBoard-Cloud_MQTT-00a6d6?style=flat-square)
![SHTC3](https://img.shields.io/badge/SHTC3-Temp_%26_Humidity-green?style=flat-square)
![ENS160](https://img.shields.io/badge/ENS160-Air_Quality-yellowgreen?style=flat-square)
![LDR](https://img.shields.io/badge/LDR-Light_Detection-yellow?style=flat-square)
![NeoPixel](https://img.shields.io/badge/NeoPixel-WS2812B-purple?style=flat-square)
![Firmware](https://img.shields.io/badge/Firmware-v1.0.0-lightgrey?style=flat-square)

ESP32-C3 based environmental monitoring device. Reads temperature, humidity, air quality and light, publishes telemetry to ThingsBoard Cloud via MQTT, and exposes a local web UI for configuration.

---

## Table of Contents

- [Project Overview](#project-overview)
- [Hardware Setup & Wiring](#hardware-setup--wiring)
- [Firmware Architecture](#firmware-architecture)
- [Web Endpoints Reference](#web-endpoints-reference)
- [ThingsBoard Setup Guide](#thingsboard-setup-guide)
- [Build, Flash & Test](#build-flash--test)

---

## Project Overview

Each unit is a self-contained monitor that:

- Reads sensors on a 2-second interval
- Breathes an RGB LED ring whose hue maps to temperature (blue = cold, red = hot)
- Publishes telemetry to ThingsBoard Cloud every 5 seconds via MQTT
- Hosts a local web UI accessible via WiFi AP for configuration
- Provisions itself automatically to ThingsBoard on first boot

### Tech Stack

| Layer | Technology |
|---|---|
| MCU | ESP32-C3 (Lolin C3 Mini) |
| Framework | Arduino via PlatformIO |
| Cloud | ThingsBoard Cloud (MQTT) |
| Sensors | SHTC3 (I2C), ENS160 (I2C), LDR (ADC) |
| LED | Adafruit NeoPixel (12-LED ring) |

### Prototype Sensor Split (Debug Only)

During development three units run in parallel, each with a subset of sensors. In the final product all sensors connect to a single unit.

| Unit | Sensors |
|---|---|
| Unit 1 | SHTC3 — temperature & humidity |
| Unit 2 | ENS160 — air quality |
| Unit 3 | LDR — light detection |

---

## Hardware Setup & Wiring

### ESP32-C3 Pin Assignment

```
GPIO2   — LDR (ADC input)
GPIO3   — NeoPixel data
GPIO8   — I2C SDA (SHTC3 + ENS160)
GPIO9   — I2C SCL (SHTC3 + ENS160)
```

> Both SHTC3 and ENS160 share the I2C bus. SHTC3 is at address `0x70`, ENS160 is at `0x53` (or `0x52` if ADDR pin is HIGH). No address conflict.

### SHTC3 — Temperature & Humidity

| SHTC3 Pin | ESP32-C3 Pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |

### ENS160 — Air Quality (AQI, TVOC, eCO2)

| ENS160 Pin | ESP32-C3 Pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |
| ADDR | GND → address `0x53` / 3.3V → address `0x52` |

### LDR — Light Detection

Wire as a voltage divider with a 10kΩ pull-down resistor:

```
3.3V ── LDR ── GPIO2 ── 10kΩ ── GND
```

Adjust `LDR_THRESHOLD` in `config.h` to suit your environment. Default is `2900` — ADC values above this are read as light ON.

### NeoPixel Ring (12 LEDs)

| NeoPixel Pin | ESP32-C3 Pin |
|---|---|
| VCC | 5V |
| GND | GND |
| DIN | GPIO3 |

---

## Firmware Architecture

```
├── include/
│   ├── config.h          — Pin definitions, timing, thresholds, TB server config
│   ├── secrets.h         — WiFi credentials, ThingsBoard provision keys (gitignored)
│   ├── sensors.h         — Shared sensor state (single source of truth for all sensors)
│   ├── mqtt.h            — MQTT public API
│   ├── provisioning.h    — Provisioning public API and state machine enum
│   ├── web_server.h      — Web server public API
│   └── wifi_config.h     — WiFi configuration public API
│
├── src/
│   ├── main.cpp          — setup() and loop(): sensor reads, LED, MQTT publish
│   ├── mqtt.cpp          — MQTT connection, telemetry and attribute publishing
│   ├── provisioning.cpp  — HTTP and MQTT provisioning flows, NVS token storage
│   ├── web_server.cpp    — HTTP server, HTML web UI, threshold persistence
│   ├── wifi_config.cpp   — WiFi credential storage, AP+STA connection management
│   └── sensors/
│       ├── shtc3.cpp/h   — SHTC3 temperature & humidity driver
│       ├── ens160.cpp/h  — ENS160 air quality driver
│       └── ldr.cpp/h     — LDR light detection driver
│
└── test/
    └── native/           — Host-native unit tests 
```

### Key Design Decisions

**`sensors.h` is the single source of truth for all sensor state.** No module should include `sensors/shtc3.h` or `sensors/ens160.h` directly — all shared variables are declared as `extern` in `sensors.h` and defined in their respective `.cpp` files.

**WiFi runs in `WIFI_AP_STA` mode at all times.** The AP is always available for configuration even when connected to a router.

**AP SSID is unique per unit** — derived from the last 4 characters of the MAC address (e.g. `ESP32C3_Hotspot_61D4`). This allows multiple units on the same network to be distinguished.

**MQTT token is stored in NVS** under the `provision` namespace. On boot, `provisioningInit()` loads it. If empty and WiFi is connected, `provisioningRequest()` runs HTTP provisioning automatically.

**Attributes are re-sent on every fresh MQTT connection** (including reconnects) using the `wasConnected` edge detection pattern in `main.cpp`.

### Data Flow

```
Sensors (2s interval)
    │
    ▼
Global state in sensors.h
    │
    ├──► LED ring (20ms interval) — hue from sensorTemp
    │
    ├──► Web server /sensors endpoint — polled every 3s by browser
    │
    └──► MQTT telemetry (5s interval) ──► ThingsBoard Cloud
              │
              └──► MQTT attributes (on connect) ──► ThingsBoard Cloud
```

### MQTT Payloads

**Telemetry** (`v1/devices/me/telemetry`):
```json
{
  "temperature": 22.50,
  "humidity": 55.00,
  "alert_temp": false,
  "alert_hum": false,
  "aqi": 2,
  "aqi_label": "Good",
  "tvoc": 120,
  "eco2": 650,
  "air_quality_status": "Normal",
  "light_on": true
}
```

**Attributes** (`v1/devices/me/attributes`):
```json
{
  "mac": "8856A67561D4",
  "ip": "192.168.1.42",
  "rssi": -65,
  "firmware": "1.0.0",
  "highTempThreshold": 30.0,
  "highHumThreshold": 80.0,
  "highTvocThreshold": 500,
  "highEco2Threshold": 1000
}
```

---

## Web Endpoints Reference

All endpoints served on port 80. Accessible at `192.168.4.1` via AP or at the device LAN IP.

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/` | Web UI (single-page HTML) |
| `GET` | `/sensors` | Live sensor data as JSON |
| `GET` | `/wifi` | Current SSID and connection status |
| `GET` | `/scan` | Scan available WiFi networks |
| `POST` | `/set_wifi` | Save WiFi credentials and connect |
| `GET` | `/device_info` | Device MAC and name |
| `GET` | `/prov_status` | Whether a ThingsBoard token is saved |
| `POST` | `/provision` | Trigger HTTP provisioning to ThingsBoard |
| `POST` | `/set_token` | Manually save a ThingsBoard token |
| `GET` | `/get_thresh` | Get current alert thresholds |
| `GET` | `/set_thresh` | Set alert thresholds (`temp`, `hum`, `tvoc`, `eco2` as query params) |

### `/sensors` Response
```json
{
  "temp": 22.5,
  "hum": 55.0,
  "aqi": 2,
  "aqi_label": "Good",
  "tvoc": 120,
  "eco2": 650,
  "aqi_status": "Normal",
  "light_on": true
}
```

### `/set_wifi` Request
```
POST /set_wifi
Content-Type: application/x-www-form-urlencoded

ssid=MyNetwork&pass=mypassword&secure=true
```

Pass `secure=false` for open networks — the password field will be cleared server-side.

### Captive Portal

The following URLs redirect to `/` to trigger the OS captive portal popup on iOS and Android:
`/hotspot-detect.html`, `/generate_204`, `/gen_204`, `/success.txt`, and all unknown URLs via `onNotFound`.

---

## ThingsBoard Setup Guide

### 1. Create a Device Profile

In ThingsBoard Cloud → **Profiles → Device Profiles → Add Profile**:
- Name: `PROTO_BF_DEVICES`
- Transport: MQTT
- Provisioning: **Allow creating new devices**

### 2. Configure Provisioning Keys

Under the device profile provisioning settings, copy the generated key and secret into `secrets.h`:

```cpp
#define TB_PROVISION_KEY    "your_provision_key"
#define TB_PROVISION_SECRET "your_provision_secret"
```

### 3. `secrets.h` Setup

Create `include/secrets.h` — this file is gitignored, never commit it:

```cpp
#pragma once

#define HOME_SSID           "YourWiFiSSID"
#define HOME_PASSWORD       "YourWiFiPassword"
#define AP_PASSWORD         "YourAPPassword"
#define TB_PROVISION_KEY    "your_provision_key"
#define TB_PROVISION_SECRET "your_provision_secret"
```

### 4. First Boot Provisioning

On first boot with WiFi connected, the device automatically calls `POST https://thingsboard.cloud/api/v1/provision` with the device name (`ESP32-{MAC}`), provision key and secret. On success the returned access token is saved to NVS and used for all future MQTT connections — no manual steps required.

To re-provision, erase the NVS `provision` namespace or call `provisioningClearToken()`.

### 5. Dashboard Setup

To show all units on a single dashboard without manual device assignment:
- Add an **Entities widget**
- Set datasource to **Device type alias** → `PROTO_BF_DEVICES`

> Group assignment via REST API requires ThingsBoard PE. On the free Cloud plan, Device type aliases are the correct approach.

### 6. Alerts

Thresholds are pushed as device attributes on every MQTT connect. Use them in ThingsBoard alarm rules to trigger notifications when `alert_temp` or `alert_hum` telemetry keys become `true`.

---

## Build, Flash & Test

### Requirements

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Python 3.x
- USB data cable (not charge-only — this is a common cause of upload failures)

### Build & Flash

```bash
# Build only
pio run -e lolin_c3_mini

# Build and flash
pio run -e lolin_c3_mini --target upload

# Serial monitor
pio device monitor --baud 115200
```

> If upload fails with a serial exception, hold the **BOOT** button on the ESP32-C3 while clicking upload, release when `Connecting....` appears in the terminal.

### Clean Build

If you suspect PlatformIO is uploading a cached binary:

```bash
pio run --target clean
pio run -e lolin_c3_mini --target upload
```

### Running Tests

```bash
# All native tests (no hardware required)
pio test -e native

# Single suite
pio test -e native --filter "native/test_threshold_logic"

# Verbose
pio test -e native -vvv
```

See `test/README.md` for full test suite documentation.

### Multi-Unit Debug Setup

When running multiple units simultaneously, assign each a different AP channel to avoid 2.4GHz interference:

| Unit | Channel |
|---|---|
| Unit 1 | 1 |
| Unit 2 | 6 |
| Unit 3 | 11 |

```cpp
// main.cpp — change per unit
WiFi.softAP(apSsid.c_str(), AP_PASSWORD, 11); // channels 1, 6, or 11
```

### Device Discovery

`discover.html` (project root) is a standalone browser tool that scans your local subnet and lists all online units. Open it on a machine connected to the same WiFi, enter your subnet (e.g. `192.168.1`) and hit **Scan Network**. Each card shows live sensor data and links directly to that unit's web UI.

---

## Known Issues & Notes

- `WiFi.disconnect(false)` must be used — passing `true` shuts off the radio and prevents connection
- ENS160 requires ~3 minutes warm-up and ~1 hour on first ever power-on for accurate readings
- The 300-byte MQTT telemetry buffer must be updated if new fields are added to the payload
- `secrets.h` is excluded from version control — each developer must create their own copy from the template above