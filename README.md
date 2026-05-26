# IES-WI-C3A x BossFarm Smart Monitor — Firmware Implementation


![ESP32-C3](https://img.shields.io/badge/ESP32--C3-Lolin_C3_Mini-blue?style=flat-square)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-orange?style=flat-square)
![SCD40](https://img.shields.io/badge/SCD40-CO2,_Temperature,_Humidity_Sensor-00a6d6?style=flat-square)
![LDR](https://img.shields.io/badge/LDR-Light_Detection-yellow?style=flat-square)
![NeoPixel](https://img.shields.io/badge/NeoPixel-WS2812B-purple?style=flat-square)
![Local MQTT](https://img.shields.io/badge/Local_MQTT-Mosquitto-green?style=flat-square)
![Firmware](https://img.shields.io/badge/Firmware-v1.0.0-lightgrey?style=flat-square)
 
---
ESP32-C3 firmware for environmental monitoring with local MQTT publishing, persistent NVS storage, and dynamic sensor type configuration.

---

## Architecture Overview

### Module Structure

```
sensors.h ← Single source of truth for all sensor state
├── scd40.cpp/h                 (SCD40 CO2 driver, raw I2C)
├── ldr.cpp/h                   (LDR light detection)
└── extern declarations         (sensorTemp, sensorHum, sensorCO2, ldrLightOn, alert flags)

main.cpp                        (setup/loop, sensor reads, LED, MQTT publish, WiFi fallback)
├── scd40Read()                 (5s interval via polling)
├── ldrRead()                   (5s interval via polling)
├── NeoPixel ring               (20ms update, hue from temp)
└── MQTT publish                (5s telemetry, 60s attributes)

local_mqtt.cpp/h                (MQTT connection to Raspberry Pi broker)
├── localMqttPublish()          (telemetry: temp, hum, CO2, light)
├── localMqttPublishConfig()    (attributes: thresholds, firmware, MAC)
├── Sensor type dispatch        (ENV/SOIL/MIN topic routing)
└── NVS broker storage

web_server.cpp/h                (HTTP server, captive portal, single-page UI)
├── handleRoot()                (HTML UI)
├── handleSensors()             (live JSON)
├── handleSetWifi()             (WiFi provisioning)
├── handleSetThresh()           (threshold persistence)
├── handleSetSensorType()       (type 1/2/3 selection)
└── Log buffer                  (circular 40-line buffer)

wifi_config.cpp/h               (WiFi credential storage & connection)
├── wifiConfigSave()            (NVS netcfg namespace)
├── wifiConfigConnect()         (STA connection with timeout)
└── wifiApStart()               (AP mode for configuration)

factory_reset.cpp/h             (GPIO4 button, 5s hold detection)
└── factoryResetExecute()       (clears all NVS namespaces)

provisioning.cpp/h              (ThingsBoard provisioning — DISABLED by default)
└── (Optional: HTTP provisioning flow, HTTP-only, not used in main flow)
```

### Global State (sensors.h)

All sensor state is declared as `extern` in `sensors.h` and defined in their respective driver files:

```cpp
// From scd40.cpp
extern float    sensorTemp;    // Temperature (°C)
extern float    sensorHum;     // Humidity (%)
extern uint16_t sensorCO2;     // CO2 (ppm)
extern bool     sensorOK;      // Sensor initialized
extern bool     alertTemp;     // Temp threshold exceeded
extern bool     alertHum;      // Humidity threshold exceeded
extern bool     alertCO2;      // CO2 threshold exceeded

// From ldr.cpp
extern bool ldrLightOn;        // Light detected
extern bool ldrOK;             // Sensor initialized
```

This ensures **no module directly includes sensor drivers** — everything goes through `sensors.h`.

---

## Sensor Drivers

### SCD40 (scd40.cpp)

**Raw I2C implementation** — no external library. Implements Sensirion protocol with CRC-8 validation.

**Timing:**
- I2C clock: 100 kHz
- Periodic measurement: 5-second interval
- First valid reading: ~5.2 seconds after boot
- Poll interval: 1 second (checks data-ready flag)
- Minimum read interval: 5.2 seconds (enforced)

**CRC-8 Validation:**
- Polynomial: `0x31`
- Every 16-bit word has an 8-bit CRC byte
- All reads and writes validated before accepting

**Command Set:**
```cpp
0x21B1  START_PERIODIC_MEASUREMENT
0xEC05  READ_MEASUREMENT           (3 words: CO2, temp, humidity)
0x3F86  STOP_PERIODIC_MEASUREMENT
0xE4B8  GET_DATA_READY_STATUS
0x3682  GET_SERIAL_NUMBER
0x202F  GET_SENSOR_VARIANT
```

**Data Conversion:**
```cpp
CO2_ppm = word0
Temperature_C = -45 + 175 * word1 / 65535
Humidity_% = 100 * word2 / 65535
```

**Sanity Checks:**
- Temperature: -40°C to 120°C (raw validation in readMeasurementFrame)
- Humidity: 0% to 100%
- CO2: 0 to 40,000 ppm (rejects if >40k)
- Any CRC failure: frame discarded, retry on next poll

**Debug Output:**
```
[SCD40] DataReady raw=0x0800 ready=YES
[SCD40 RAW] 02 84 4F  5C 4A AB  85 54 69   (3 words + 3 CRCs)
[SCD40] CRC CO2=OK TEMP=OK HUM=OK
[SCD40] Decoded rawCO2=644 rawT=23626 rawH=34132 -> CO2=644 T=22.78 H=53.29
[SCD40] T:22.8°C H:53.3% CO2:644 ppm (Good)
```

### LDR (ldr.cpp)

**Voltage divider ADC input** on GPIO0.

**Timing:**
- Reads every 5 seconds (synced with sensor interval)
- 5 samples taken with 5ms delay between reads
- Minimum valid reading requires 3+ valid ADC values (filters spikes)

**Logic:**
- ADC value > threshold → light ON
- ADC value ≤ threshold → light OFF
- Threshold adjustable in `config.h` (default: 50)

**Filtering:**
- Silently holds previous reading if all 5 samples are near max (4095) — strapping spike
- Requires 3+ valid samples to update state

---

## Main Loop Flow (main.cpp)

### setup()

1. Serial init (115200 baud)
2. NeoPixel init + clear
3. Factory reset button init
4. SCD40 init (I2C, periodic measurement start)
5. LDR init (ADC setup)
6. WiFi init:
   - Mode: WIFI_AP_STA
   - AP always enabled (unique SSID from MAC)
   - Load saved WiFi credentials
   - If commissioned: attempt STA connection (15s timeout)
   - If not commissioned: AP visible, no STA attempt
7. Web server init (HTTP on port 80)
8. Load thresholds from NVS

### loop()

**Execution order (runs every ~1ms):**

1. **Web server handle** (non-blocking)
2. **Local MQTT handle** (non-blocking, 5s reconnect)
3. **Provisioning handle** (if ThingsBoard enabled)
4. **Factory reset check** (GPIO4 hold detection)
5. **Delay 1ms** (prevent watchdog, allow background tasks)

**Every 5 seconds (sensor interval):**
- Clear LED ring
- LDR read
- Show updated LED (hue from temperature)

**Every 20ms (LED update):**
- Calculate target hue from sensorTemp
- Smooth transition (±20 LSBs per update)
- Apply gamma correction
- Show on ring

**Every 5 seconds (MQTT publish):**
- Local MQTT: publish telemetry
- If commissioned: update alertTemp, alertHum, alertCO2
- If first connection: publish attributes once

**Every 30 seconds (health log):**
- Log free heap, uptime, WiFi status, MQTT status, RSSI

**WiFi fallback logic:**
- If WiFi disconnected for >30s: re-enable AP
- Retry STA every 120s if commissioned and credentials exist
- If reconnected: disable AP, enable mDNS

---

## Local MQTT (local_mqtt.cpp)

### Initialization

```cpp
localMqttInit()
├── Load sensor type from NVS (device/sensor_type)
├── Load broker IP/port from NVS (broker/host, broker/port)
├── Set MQTT server (broker IP, port 1883)
├── Attempt connection
└── Log connection status
```

### Connection

- Non-blocking: attempts every 5 seconds if disconnected
- Client ID: `ESP32C3-{MAC_NO_COLON}`
- No username/password (local broker)
- Keep-alive: 30s
- Socket timeout: 5s
- Buffer size: 1024 bytes

### Publishing

**Telemetry (every 5 seconds):**

Topic: `sensors/{DEVICE_ID}/{MEASUREMENT_TYPE}`

Device IDs by type:
- Type 1: `ENV_{LAST_4_MAC_CHARS}` (e.g., `ENV_61D4`)
- Type 2: `SOIL_{LAST_4_MAC_CHARS}`
- Type 3: `MIN_{LAST_4_MAC_CHARS}`

Measurement types:
- Type 1: `telemetry`
- Type 2: `soil`
- Type 3: `mineral`

Example payload (Type 1, ~900 bytes):
```json
{
  "device_id": "ENV_61D4",
  "timestamp": "2024-05-25T14:30:45Z",
  "reading": {
    "sensor_type": 1,
    "sensor_type_label": "environment",
    "firmware": "1.0.0",
    "rssi": -65,
    "temperature": 22.50,
    "humidity": 55.00,
    "co2": 650,
    "eco2": 650,
    "co2_label": "Good",
    "co2_valid": true,
    "alert_temp": false,
    "alert_temp_num": 0,
    "alert_hum": false,
    "alert_hum_num": 0,
    "alert_co2": false,
    "alert_co2_num": 0,
    "light_on": true,
    "light_on_num": 1
  }
}
```

**Attributes (every 60 seconds, forced after reconnect):**

Topic: `sensors/{DEVICE_ID}/attributes`

```json
{
  "device_id": "ENV_61D4",
  "timestamp": "2024-05-25T14:30:45Z",
  "reading": {
    "mac": "A4CF12AA61D4",
    "ip": "192.168.0.42",
    "rssi": -65,
    "firmware": "1.0.0",
    "sensor_type": 1,
    "sensor_type_label": "environment",
    "highTempThreshold": 30.00,
    "lowTempThreshold": 5.00,
    "highHumThreshold": 80.00,
    "lowHumThreshold": 20.00,
    "highEco2Threshold": 1000.00
  }
}
```

### Broker Configuration

**Load from NVS (broker namespace):**
```cpp
gBrokerHost = localMqttGetBrokerIP();   // defaults to LOCAL_MQTT_SERVER from config.h
gBrokerPort = localMqttGetBrokerPort(); // defaults to 1883
```

**Set dynamically via web API:**
```cpp
POST /set_broker?ip=192.168.0.50&port=1883
```

Persists to NVS for next boot.

---

## Web Server (web_server.cpp)

### Captive Portal

Auto-redirects unknown URLs to `/` via HTTP 302:
- `/hotspot-detect.html`
- `/library/test/success.html`
- `/success.txt`
- `/generate_204`
- `/gen_204`
- All other unknown URLs

Triggers iOS/Android captive portal popup.

### Single-Page HTML UI (const HTML_PAGE[])

**Modern dark theme** with:
- Device info card (MAC, IP, firmware, mDNS)
- Live sensor data (temp, humidity, CO2, light)
- WiFi connection UI (scan, select, connect)
- Alert thresholds (editable, persistent)
- Device log terminal (circular buffer display)
- Factory reset button
- Sensor type selector (pre-commissioning only)

**JavaScript features:**
- Auto-refresh sensors every 3s
- Auto-refresh device info every 5s
- Auto-refresh logs every 1s
- Scan networks, select SSID
- Save thresholds with validation
- Clear log buffer

### Endpoints

**GET /sensors**
```json
{
  "temp": 22.5,
  "hum": 55.0,
  "co2": 650,
  "co2_label": "Good",
  "alert_temp": false,
  "alert_hum": false,
  "alert_co2": false,
  "scd40_ok": true,
  "light_on": true,
  "ldr_ok": true
}
```

**POST /set_wifi** (form-urlencoded)
```
ssid=MyNetwork&pass=password123&secure=true
```
Returns:
```json
{"ok": true, "ip": "192.168.0.100", "mdns": "bossfarm-61d4.local"}
```

**GET /device_info**
```json
{
  "device_id": "A4CF12AA61D4",
  "device_name": "ESP32-A4CF12AA61D4",
  "mdns": "bossfarm-61d4.local",
  "ip": "192.168.0.100",
  "rssi": -65,
  "firmware": "1.0.0",
  "commissioned": true
}
```

**GET /get_thresh, POST /set_thresh**
- `temp`: max temperature threshold (°C)
- `temp_low`: min temperature threshold
- `hum`: max humidity threshold (%)
- `hum_low`: min humidity threshold
- `co2`: max CO2 threshold (ppm)

**GET /get_sensor_type, POST /set_sensor_type**
- GET returns: `{"sensor_type": 1, "label": "environment"}`
- POST param: `type=1|2|3`

**GET /scan**
Returns JSON array of WiFi networks:
```json
[
  {"ssid": "MyNetwork", "rssi": -45, "secure": true},
  {"ssid": "OpenNet", "rssi": -65, "secure": false}
]
```

**GET /logs**
Circular log buffer (last 40 lines):
```json
{
  "seq": 12345,
  "lines": [
    "[Boot] WiFi OK — IP: 192.168.0.100",
    "[SCD40] T:22.8°C H:53.3% CO2:644 ppm (Good)",
    "[LocalMQTT] connected weedsync.local:1883"
  ]
}
```

**POST /factory_reset**
Clears all NVS namespaces: `provision`, `netcfg`, `thresholds`, `device`, `broker`

---

## NVS Persistence (Preferences)

### Namespaces

**`netcfg`** — WiFi configuration
```cpp
prefs.putString("ssid", ssid);
prefs.putString("pass", pass);
```

**`thresholds`** — Alert thresholds
```cpp
prefs.putFloat("temp", 30.0f);
prefs.putFloat("temp_low", 5.0f);
prefs.putFloat("hum", 80.0f);
prefs.putFloat("hum_low", 20.0f);
prefs.putFloat("co2", 1000.0f);
```

**`device`** — Device configuration
```cpp
prefs.putBool("commissioned", true);
prefs.putUChar("sensor_type", 1);
```

**`broker`** — Local MQTT broker settings
```cpp
prefs.putString("host", "192.168.0.50");
prefs.putUShort("port", 1883);
```

**`provision`** — ThingsBoard token (if enabled)
```cpp
prefs.putString("token", "abc123...");
prefs.putString("device_name", "ESP32-A4CF12AA61D4");
```

### Loading on Boot

```cpp
wifiConfigBegin()       // Load from netcfg
loadThresholdsFromNVS() // Load from thresholds
loadSensorType()        // Load from device
loadBroker()            // Load from broker
```

---

## Commissioning Model

**Not commissioned (first boot):**
1. AP enabled, WiFi not connected
2. Web UI visible at `192.168.4.1`
3. No MQTT publishing
4. Sensor type selector shown (1=Env, 2=Soil, 3=Mineral)

**Commissioning:**
1. Select WiFi, enter password
2. Connect to network
3. Device marked commissioned: `device/commissioned = true`
4. AP stays on, WiFi connected
5. MQTT starts publishing

**After commissioning:**
- Sensor type selector hidden
- Broker and threshold UI available
- AP re-enables if WiFi drops for 30s
- Reconnects every 120s if disconnected

---

## LED Ring (NeoPixel)

### Hue Mapping

Temperature → Hue conversion:
```cpp
float norm = constrain((sensorTemp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN), 0.0f, 1.0f);
uint16_t newHue = (uint16_t)((1.0f - norm) * 43690);
// 15°C (TEMP_MIN) → 43690 (blue)
// 35°C (TEMP_MAX) → 0 (red)
```

### LED Update (20ms interval)

- Smooth hue transition: ±20 LSBs per update
- Saturation: 255 (full color)
- Brightness: 200 (gamma-corrected)
- Ring update applied every 20ms

### States

**Before sensor init:**
- Rainbow breathe (error state, sensor not OK)

**After sensor init:**
- Temperature-mapped hue
- Smooth transitions

---

## Factory Reset

**Button:** GPIO4, active LOW

**Logic:**
1. Press detected → start timer
2. Hold >5 seconds → execute reset
3. Release before 5s → cancel

**Reset clears:**
- `provision` namespace (ThingsBoard token)
- `netcfg` namespace (WiFi credentials)
- `thresholds` namespace (alert settings)
- `device` namespace (commissioning flag, sensor type)
- `broker` namespace (MQTT broker IP/port)

**Result:** Device reboots into unconfigured state, AP visible again.

---

## Configuration (config.h)

```cpp
// Access Point
#define AP_SSID             "ESP32C3_Hotspot"

// Hardware Pins
#define NEOPIXEL_PIN        3
#define I2C_SDA             7
#define I2C_SCL             6
#define LDR_PIN             0
#define FACTORY_RESET_PIN   4

// Timing
#define SENSOR_INTERVAL     5000   // 5 seconds
#define LED_INTERVAL        20     // 20ms

// Temperature Range (for LED hue)
#define TEMP_MIN            15.0f
#define TEMP_MAX            35.0f

// Thresholds
#define LDR_THRESHOLD       50

// Local MQTT (default)
#define LOCAL_MQTT_SERVER   "weedsync.local"
#define LOCAL_MQTT_PORT     1883

// mDNS
#define MDNS_PREFIX         "bossfarm"

// Device
#define FIRMWARE_VERSION    "1.0.0"
#define SENSOR_TYPE_DEFAULT 1  // 1=Environment, 2=Soil, 3=Mineral
```

---

## Sensor Types

### Type 1: Environment
- **Topic:** `sensors/ENV_{MAC}/telemetry`
- **Fields:** temperature, humidity, CO2, light
- **Use case:** General environmental monitoring

### Type 2: Soil
- **Topic:** `sensors/SOIL_{MAC}/soil`
- **Fields:** EC (electrical conductivity), RH (relative humidity)
- **Use case:** Soil monitoring (extensible for additional sensors)

### Type 3: Mineral
- **Topic:** `sensors/MIN_{MAC}/mineral`
- **Fields:** EC, N, P, K (nitrogen, phosphorus, potassium)
- **Use case:** Mineral content tracking (extensible for soil nutrient sensors)

**Current implementation:** Types 2 & 3 have placeholder payloads. Extend by adding actual sensor drivers and populating the fields in `localMqttPublish()`.

---

## Alert Thresholds

**Calculation (in main loop):**
```cpp
alertTemp = (sensorTemp > threshTemp) || (sensorTemp < threshTempLow);
alertHum  = (sensorHum  > threshHum)  || (sensorHum  < threshHumLow);
alertCO2  = (sensorCO2  > threshCO2);
```

**Thresholds trigger on >strictly greater than, not equal.**

**Defaults (config.h via web UI):**
- `threshTemp`: 30.0°C
- `threshTempLow`: 5.0°C
- `threshHum`: 80.0%
- `threshHumLow`: 20.0%
- `threshCO2`: 1000.0 ppm

**Persistence:**
- Loaded from NVS on boot
- Saved via `POST /set_thresh`
- Published to MQTT attributes every 60s

---

## Feature Flags

**ThingsBoard (disabled by default):**
```cpp
static const bool thingsBoardEnabled = false;
```

When disabled:
- `provisioning.cpp` still compiled
- MQTT provisioning loop doesn't run
- `mqttPublish()` and `mqttPublishAttributes()` are no-ops
- No cloud connectivity

To enable: Set `thingsBoardEnabled = true` in `main.cpp` and populate `secrets.h` with provisioning credentials.

---

## Logging

**Circular buffer (40 lines):**
- New messages append to `_logBuf[_logHead % 40]`
- `_logHead` increments each log
- Served via `GET /logs` endpoint
- Displayed in web UI terminal

**Log sources:**
- `[Boot]` — startup messages
- `[WiFi]` — WiFi connection events
- `[LocalMQTT]` — MQTT connection and publish status
- `[SCD40]` — sensor data, errors
- `[LDR]` — light detection status
- `[Thresh]` — threshold updates
- `[Sys]` — health metrics (heap, uptime, WiFi, MQTT, RSSI)

---

## Testing

**Native unit tests** (no hardware required):
```bash
pio test -e native
```

Covers:
- Sensor validation (temp, humidity, CO2 ranges)
- Threshold logic (alert calculations)
- WiFi config validation
- MQTT payload building
- Provisioning JSON parsing
- AQI label mapping

See `test/native/` for implementation.

---

## Known Limitations & Future Work

- **Soil & Mineral types:** Placeholder payloads only — implement actual sensor drivers
- **ThingsBoard:** Disabled by default, not actively maintained
- **Buffer sizes:** MQTT payload 1024 bytes, log 40 lines — increase if needed
- **WiFi security:** No WPA3 (ESP32-C3 limitation)
- **I2C:** Single bus, single device (SCD40 only)
- **Power:** Always-on AP draws ~100mA extra vs STA-only mode

---

## Build & Deployment

```bash
# Build
pio run -e lolin_c3_mini

# Flash
pio run -e lolin_c3_mini --target upload

# Monitor
pio device monitor --baud 115200

# Clean build
pio run --target clean && pio run -e lolin_c3_mini --target upload
```

**Environment:** PlatformIO with Arduino framework, ESP32-C3 Lolin mini board.