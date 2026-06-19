# RS485 Sensor Integration Guide — IES-WI-C6A x BossFarm

New files (copy as-is into the repo):

| File | Destination |
|---|---|
| `include/rs485_sensor.h` | `include/` |
| `src/sensors/rs485_sensor.cpp` | `src/sensors/` |

Driver mapping (binds to the EXISTING sensor type — no new NVS keys for selection):

| Sensor Type | Driver | Serial | Modbus |
|---|---|---|---|
| 1 Environment | none (RS485 idle) | — | — |
| 2 Soil | Halisense Soil 7-in-1 | 4800,N,8,1 | addr 1, FC 0x03, regs 0x0000–0x0006 |
| 3 Mineral | CWT Water pH/EC | 9600,N,8,1 | addr 1, FC 0x03, regs 0x0000–0x0002 |

Pins (from schematic): TX=GPIO16 (TXD0 pad → MAX3485 DI), RX=GPIO17 (RXD0 pad ← RO),
DE/RE=GPIO14 (RS485_FC). Console is on USB-CDC (`ARDUINO_USB_CDC_ON_BOOT=1`) so the
UART0 pads are free; the driver uses UART1 routed via GPIO matrix.

> ⚠️ VERIFY ONE THING ON HARDWARE: schematic pin 19 of the ESP32-C6-MINI-1 is GPIO14
> per the Espressif datasheet. If the first boot log shows RS485 timeouts AND the FC
> net measures no toggling on GPIO14, change only `RS485_DE_PIN` in config.h.

---

## 1. `include/config.h` — ADD at the end

```cpp
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
```

---

## 2. `src/main.cpp`

**2a.** Add include near the other includes:
```cpp
#include "rs485_sensor.h"
```

**2b.** In `setup()`, right after `ldrInit();`:
```cpp
    rs485SensorInit();   // loads sensor type from NVS, starts UART only for type 2/3
```

**2c.** In `loop()`, right after `scd40Read();`:
```cpp
    rs485SensorRead();   // self-throttled to 5s; no-op for environment type
```

(SCD40/LDR stay initialised for all types — they are on-board. The RS485 UART is
only initialised when type 2 or 3 is selected, per the "don't initialise unselected
drivers" requirement.)

---

## 3. `src/local_mqtt.cpp` — replace placeholder payloads

**3a.** Add include near the top with the others:
```cpp
#include "rs485_sensor.h"
```

**3b.** In `localMqttPublish()`, REPLACE the entire `else if (gSensorType == 2)` block's
placeholder section. Delete:
```cpp
    //* Replace these placeholder values with your real soil sensor readings.
    float soilEc = 0.0f;
    float soilRh = 0.0f;
```
and replace BOTH snprintf reading bodies of the soil branch with the full field set
(template below shows the with-timestamp variant; mirror it without `timestamp` for
the second one, exactly like the existing pattern):

```cpp
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
            "\"sensor_ok\":%s,"
            "\"moisture\":%.1f,"
            "\"temperature\":%.1f,"
            "\"ec\":%.0f,"
            "\"ph\":%.1f,"
            "\"n\":%u,"
            "\"p\":%u,"
            "\"k\":%u,"
            "\"alert_moist\":%s,"
            "\"alert_ec\":%s,"
            "\"alert_ph\":%s"
          "}"
        "}",
        deviceId.c_str(),
        timestamp.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        boolText(soilOK),
        soilMoist, soilTemp, soilEc, soilPh,
        soilN, soilP, soilK,
        boolText(alertSoilMoist),
        boolText(alertSoilEc),
        boolText(alertSoilPh)
      );
```

**3c.** Same for the mineral (`else`) branch — delete the four placeholder floats and use:

```cpp
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
            "\"sensor_ok\":%s,"
            "\"ph\":%.2f,"
            "\"ec\":%.0f,"
            "\"temperature\":%.1f,"
            "\"alert_ph\":%s,"
            "\"alert_ec\":%s"
          "}"
        "}",
        deviceId.c_str(),
        timestamp.c_str(),
        gSensorType,
        sensorTypeLabel(gSensorType),
        FIRMWARE_VERSION,
        WiFi.RSSI(),
        boolText(waterOK),
        waterPh, waterEc, waterTemp,
        boolText(alertWaterPh),
        boolText(alertWaterEc)
      );
```

(Existing topics `sensors/SOIL_xxxx/soil` and `sensors/MIN_xxxx/mineral` are unchanged
— the InfluxDB bridge keeps working; it just gets real fields now.)

---

## 4. `src/web_server.cpp`

**4a.** Add include with the others:
```cpp
#include "rs485_sensor.h"
```

**4b.** Below the existing "Global threshold variables" block, ADD:
```cpp
// ── RS485 sensor thresholds (referenced by rs485_sensor.cpp) ─────────────────
float threshWaterPhLow    = 5.5f;
float threshWaterPhHigh   = 7.5f;
float threshWaterEcHigh   = 3000.0f;
float threshSoilMoistLow  = 20.0f;
float threshSoilMoistHigh = 80.0f;
float threshSoilEcHigh    = 2000.0f;
float threshSoilPhLow     = 5.5f;
float threshSoilPhHigh    = 7.5f;

static const char* RS485_THRESH_NVS_NS = "rs485thresh";

static void loadRs485ThreshFromNVS() {
    Preferences prefs;
    prefs.begin(RS485_THRESH_NVS_NS, true);
    threshWaterPhLow    = prefs.getFloat("wph_low",     5.5f);
    threshWaterPhHigh   = prefs.getFloat("wph_high",    7.5f);
    threshWaterEcHigh   = prefs.getFloat("wec_high", 3000.0f);
    threshSoilMoistLow  = prefs.getFloat("smoist_low",  20.0f);
    threshSoilMoistHigh = prefs.getFloat("smoist_high", 80.0f);
    threshSoilEcHigh    = prefs.getFloat("sec_high", 2000.0f);
    threshSoilPhLow     = prefs.getFloat("sph_low",     5.5f);
    threshSoilPhHigh    = prefs.getFloat("sph_high",    7.5f);
    prefs.end();
}

static void saveRs485ThreshToNVS() {
    Preferences prefs;
    prefs.begin(RS485_THRESH_NVS_NS, false);
    prefs.putFloat("wph_low",     threshWaterPhLow);
    prefs.putFloat("wph_high",    threshWaterPhHigh);
    prefs.putFloat("wec_high",    threshWaterEcHigh);
    prefs.putFloat("smoist_low",  threshSoilMoistLow);
    prefs.putFloat("smoist_high", threshSoilMoistHigh);
    prefs.putFloat("sec_high",    threshSoilEcHigh);
    prefs.putFloat("sph_low",     threshSoilPhLow);
    prefs.putFloat("sph_high",    threshSoilPhHigh);
    prefs.end();
}
```

**4c.** In `handleSensors()`: change `char buf[512];` to `char buf[1024];` and extend the
JSON. Easiest is to append before the closing brace — replace the final part of the
format string and arguments so it ends like this:

```cpp
    snprintf(buf, sizeof(buf),
        "{"
        "\"temp\":%s,"
        "\"hum\":%s,"
        "\"co2\":%s,"
        "\"co2_label\":\"%s\","
        "\"alert_temp\":%s,\"alert_temp_num\":%d,"
        "\"alert_hum\":%s,\"alert_hum_num\":%d,"
        "\"alert_co2\":%s,\"alert_co2_num\":%d,"
        "\"scd40_ok\":%s,"
        "\"light_on\":%s,\"light_on_num\":%d,"
        "\"ldr_ok\":%s,"
        "\"sensor_type\":%u,"
        "\"rs485_status\":\"%s\","
        "\"water_ok\":%s,\"water_ph\":%.2f,\"water_ec\":%.0f,\"water_temp\":%.1f,"
        "\"alert_water_ph\":%s,\"alert_water_ec\":%s,"
        "\"soil_ok\":%s,\"soil_moist\":%.1f,\"soil_temp\":%.1f,\"soil_ec\":%.0f,"
        "\"soil_ph\":%.1f,\"soil_n\":%u,\"soil_p\":%u,\"soil_k\":%u,"
        "\"alert_soil_moist\":%s,\"alert_soil_ec\":%s,\"alert_soil_ph\":%s"
        "}",
        tempBuf, humBuf, co2Buf, co2Label(sensorCO2),
        alertTemp  ? "true" : "false", alertTemp  ? 1 : 0,
        alertHum   ? "true" : "false", alertHum   ? 1 : 0,
        alertCO2   ? "true" : "false", alertCO2   ? 1 : 0,
        sensorOK   ? "true" : "false",
        ldrLightOn ? "true" : "false", ldrLightOnNum(),
        ldrOK      ? "true" : "false",
        rs485ActiveType(),
        rs485StatusLabel(),
        waterOK ? "true" : "false", waterPh, waterEc, waterTemp,
        alertWaterPh ? "true" : "false", alertWaterEc ? "true" : "false",
        soilOK ? "true" : "false", soilMoist, soilTemp, soilEc,
        soilPh, soilN, soilP, soilK,
        alertSoilMoist ? "true" : "false",
        alertSoilEc    ? "true" : "false",
        alertSoilPh    ? "true" : "false"
    );
```

**4d.** ADD two handlers (near `handleGetThresh`/`handleSetThresh`):

```cpp
static void handleGetRs485Thresh() {
    addCorsHeaders();
    char buf[320];
    snprintf(buf, sizeof(buf),
        "{\"wph_low\":%.2f,\"wph_high\":%.2f,\"wec_high\":%.0f,"
        "\"smoist_low\":%.1f,\"smoist_high\":%.1f,\"sec_high\":%.0f,"
        "\"sph_low\":%.2f,\"sph_high\":%.2f}",
        threshWaterPhLow, threshWaterPhHigh, threshWaterEcHigh,
        threshSoilMoistLow, threshSoilMoistHigh, threshSoilEcHigh,
        threshSoilPhLow, threshSoilPhHigh);
    server.send(200, "application/json", buf);
}

static void handleSetRs485Thresh() {
    if (server.hasArg("wph_low"))     threshWaterPhLow    = server.arg("wph_low").toFloat();
    if (server.hasArg("wph_high"))    threshWaterPhHigh   = server.arg("wph_high").toFloat();
    if (server.hasArg("wec_high"))    threshWaterEcHigh   = server.arg("wec_high").toFloat();
    if (server.hasArg("smoist_low"))  threshSoilMoistLow  = server.arg("smoist_low").toFloat();
    if (server.hasArg("smoist_high")) threshSoilMoistHigh = server.arg("smoist_high").toFloat();
    if (server.hasArg("sec_high"))    threshSoilEcHigh    = server.arg("sec_high").toFloat();
    if (server.hasArg("sph_low"))     threshSoilPhLow     = server.arg("sph_low").toFloat();
    if (server.hasArg("sph_high"))    threshSoilPhHigh    = server.arg("sph_high").toFloat();

    saveRs485ThreshToNVS();

    // Recompute alert flags against current readings immediately
    alertWaterPh   = waterOK && ((waterPh < threshWaterPhLow) || (waterPh > threshWaterPhHigh));
    alertWaterEc   = waterOK && (waterEc > threshWaterEcHigh);
    alertSoilMoist = soilOK  && ((soilMoist < threshSoilMoistLow) || (soilMoist > threshSoilMoistHigh));
    alertSoilEc    = soilOK  && (soilEc > threshSoilEcHigh);
    alertSoilPh    = soilOK  && ((soilPh < threshSoilPhLow) || (soilPh > threshSoilPhHigh));

    Serial.println("[Thresh] RS485 thresholds saved");
    server.send(200, "text/plain", "OK");
}
```

**4e.** In `handleSetSensorType()`, after `localMqttSetSensorType(t);` ADD:
```cpp
    rs485ApplySensorType(t);   // live-switch the RS485 driver & baud rate
```

**4f.** In `webServerInit()`, register routes and load thresholds (next to the existing ones):
```cpp
    server.on("/get_rs485_thresh", HTTP_GET,  handleGetRs485Thresh);
    server.on("/set_rs485_thresh",            handleSetRs485Thresh);
```
and after `loadThresholdsFromNVS();`:
```cpp
    loadRs485ThreshFromNVS();
```

**4g. HTML — two distinct sections.** In `HTML_PAGE`, the existing environment card has
`id="step4"` and thresholds `id="step-thresh"`. INSERT the following two cards
immediately AFTER the closing `</div>` of `step-thresh` (before the Device Log card).
Both start hidden; JS shows exactly one section based on sensor type:

```html
  <!-- ══════════ SOIL 7-IN-1 SECTION (sensor type 2) ══════════ -->
  <div class="card row-1col hidden" id="card-soil">
    <div class="card-header">
      <span class="card-icon">🌱</span>
      <span class="card-title">Soil 7-in-1 Sensor (RS485)</span>
      <span class="badge waiting" id="soil-status" style="margin-left:auto;">–</span>
    </div>
    <div class="sensor-grid" style="grid-template-columns:repeat(7,1fr);">
      <div class="sensor-tile"><div class="sensor-tile-label">💧 Moisture</div>
        <div class="sensor-tile-val" id="soil-moist">– <span style="font-size:0.7rem;color:var(--muted);">%</span></div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">🌡 Soil Temp</div>
        <div class="sensor-tile-val" id="soil-temp">– <span style="font-size:0.7rem;color:var(--muted);">°C</span></div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">⚡ Soil EC</div>
        <div class="sensor-tile-val" id="soil-ec">– <span style="font-size:0.7rem;color:var(--muted);">uS/cm</span></div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">🧪 Soil pH</div>
        <div class="sensor-tile-val" id="soil-ph">–</div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">N</div>
        <div class="sensor-tile-val" id="soil-n">– <span style="font-size:0.7rem;color:var(--muted);">mg/kg</span></div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">P</div>
        <div class="sensor-tile-val" id="soil-p">– <span style="font-size:0.7rem;color:var(--muted);">mg/kg</span></div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">K</div>
        <div class="sensor-tile-val" id="soil-k">– <span style="font-size:0.7rem;color:var(--muted);">mg/kg</span></div></div>
    </div>
    <div class="chart-container"><canvas id="soilChart"></canvas></div>
    <div class="card-header" style="margin-top:8px;">
      <span class="card-icon">⚠️</span><span class="card-title">Soil Alert Thresholds</span>
    </div>
    <div class="thresh-grid">
      <div class="thresh-item"><label>💧 Min Moisture (%)</label>
        <input type="number" id="th-smoist-low" step="1" min="0" max="100"></div>
      <div class="thresh-item"><label>💧 Max Moisture (%)</label>
        <input type="number" id="th-smoist-high" step="1" min="0" max="100"></div>
      <div class="thresh-item"><label>⚡ Max EC (uS/cm)</label>
        <input type="number" id="th-sec-high" step="50" min="0" max="20000"></div>
      <div class="thresh-item"><label>🧪 Min pH</label>
        <input type="number" id="th-sph-low" step="0.1" min="0" max="14"></div>
      <div class="thresh-item"><label>🧪 Max pH</label>
        <input type="number" id="th-sph-high" step="0.1" min="0" max="14"></div>
    </div>
    <button class="btn-save" onclick="saveSoilThresh()">💾 Save Soil Thresholds</button>
  </div>

  <!-- ══════════ WATER pH/EC SECTION (sensor type 3 / mineral) ══════════ -->
  <div class="card row-1col hidden" id="card-water">
    <div class="card-header">
      <span class="card-icon">💦</span>
      <span class="card-title">Water pH + EC Sensor (RS485)</span>
      <span class="badge waiting" id="water-status" style="margin-left:auto;">–</span>
    </div>
    <div class="sensor-grid" style="grid-template-columns:repeat(3,1fr);">
      <div class="sensor-tile"><div class="sensor-tile-label">🧪 Water pH</div>
        <div class="sensor-tile-val" id="water-ph">–</div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">⚡ Water EC</div>
        <div class="sensor-tile-val" id="water-ec">– <span style="font-size:0.7rem;color:var(--muted);">uS/cm</span></div></div>
      <div class="sensor-tile"><div class="sensor-tile-label">🌡 Water Temp</div>
        <div class="sensor-tile-val" id="water-temp">– <span style="font-size:0.7rem;color:var(--muted);">°C</span></div></div>
    </div>
    <div class="chart-container"><canvas id="waterChart"></canvas></div>
    <div class="card-header" style="margin-top:8px;">
      <span class="card-icon">⚠️</span><span class="card-title">Water Alert Thresholds</span>
    </div>
    <div class="thresh-grid" style="grid-template-columns:repeat(3,1fr);">
      <div class="thresh-item"><label>🧪 Min pH</label>
        <input type="number" id="th-wph-low" step="0.1" min="0" max="14"></div>
      <div class="thresh-item"><label>🧪 Max pH</label>
        <input type="number" id="th-wph-high" step="0.1" min="0" max="14"></div>
      <div class="thresh-item"><label>⚡ Max EC (uS/cm)</label>
        <input type="number" id="th-wec-high" step="50" min="0" max="20000"></div>
    </div>
    <button class="btn-save" onclick="saveWaterThresh()">💾 Save Water Thresholds</button>
  </div>
```

**4h. JavaScript.** Inside the existing `<script>` block:

(i) ADD these functions (anywhere top-level, e.g. after `updateChart`):

```js
// ── RS485 charts ───────────────────────────────────────────────────────────
function makeLineChart(canvasId, datasets) {
  return new Chart(document.getElementById(canvasId), {
    type: 'line',
    data: { labels: [], datasets: datasets.map(d => ({
      label: d.label, data: [], borderColor: d.color,
      tension: 0.3, fill: false, borderWidth: 2, pointRadius: 3,
      pointBackgroundColor: d.color })) },
    options: { responsive: true, maintainAspectRatio: false,
      plugins: { legend: { display: true, labels: { color: '#3d3a36',
        font: { family: "'Courier New', monospace", size: 12 },
        usePointStyle: true, padding: 15 } } },
      scales: { y: { ticks: { color: '#8b8680', font: { size: 11 } },
                     grid: { color: 'rgba(232,228,220,0.3)' } },
                x: { ticks: { color: '#8b8680', font: { size: 11 } },
                     grid: { color: 'rgba(232,228,220,0.3)' } } } }
  });
}
function pushChart(chart, values) {
  chart.data.labels.push(new Date().toLocaleTimeString());
  values.forEach((v, i) => chart.data.datasets[i].data.push(v));
  if (chart.data.labels.length > 30) {
    chart.data.labels.shift();
    chart.data.datasets.forEach(ds => ds.data.shift());
  }
  chart.update('none');
}

let soilChart = null, waterChart = null, currentSensorType = 1;

function setTile(id, val, digits, alert) {
  const el = document.getElementById(id);
  if (val == null) { el.firstChild.textContent = '– '; el.style.color = 'var(--muted)'; return; }
  el.firstChild.textContent = Number(val).toFixed(digits) + ' ';
  el.style.color = alert ? 'var(--red)' : '';
}
function setStatusBadge(id, ok) {
  document.getElementById(id).outerHTML =
    '<span class="badge ' + (ok ? 'online' : 'offline') + '" id="' + id +
    '" style="margin-left:auto;">' + (ok ? '● Sensor OK' : '● No response') + '</span>';
}

function updateRs485Ui(d) {
  if (currentSensorType === 2) {
    setStatusBadge('soil-status', d.soil_ok);
    setTile('soil-moist', d.soil_ok ? d.soil_moist : null, 1, d.alert_soil_moist);
    setTile('soil-temp',  d.soil_ok ? d.soil_temp  : null, 1, false);
    setTile('soil-ec',    d.soil_ok ? d.soil_ec    : null, 0, d.alert_soil_ec);
    setTile('soil-ph',    d.soil_ok ? d.soil_ph    : null, 1, d.alert_soil_ph);
    setTile('soil-n',     d.soil_ok ? d.soil_n     : null, 0, false);
    setTile('soil-p',     d.soil_ok ? d.soil_p     : null, 0, false);
    setTile('soil-k',     d.soil_ok ? d.soil_k     : null, 0, false);
    if (d.soil_ok) {
      if (!soilChart) soilChart = makeLineChart('soilChart', [
        { label: 'Moisture (%)',  color: '#2d5d3f' },
        { label: 'Temp (°C)',     color: '#d4a137' },
        { label: 'EC (uS/cm ÷100)', color: '#c94c4c' },
        { label: 'pH',            color: '#4a7fb5' }]);
      pushChart(soilChart, [d.soil_moist, d.soil_temp, d.soil_ec / 100, d.soil_ph]);
    }
  } else if (currentSensorType === 3) {
    setStatusBadge('water-status', d.water_ok);
    setTile('water-ph',   d.water_ok ? d.water_ph   : null, 2, d.alert_water_ph);
    setTile('water-ec',   d.water_ok ? d.water_ec   : null, 0, d.alert_water_ec);
    setTile('water-temp', d.water_ok ? d.water_temp : null, 1, false);
    if (d.water_ok) {
      if (!waterChart) waterChart = makeLineChart('waterChart', [
        { label: 'pH',            color: '#4a7fb5' },
        { label: 'EC (uS/cm ÷100)', color: '#c94c4c' },
        { label: 'Temp (°C)',     color: '#d4a137' }]);
      pushChart(waterChart, [d.water_ph, d.water_ec / 100, d.water_temp]);
    }
  }
}

function applySensorTypeVisibility(t) {
  currentSensorType = t;
  document.getElementById('step4').classList.toggle('hidden', t !== 1);
  document.getElementById('step-thresh').classList.toggle('hidden', t !== 1);
  document.getElementById('card-soil').classList.toggle('hidden', t !== 2);
  document.getElementById('card-water').classList.toggle('hidden', t !== 3);
}

// ── RS485 thresholds ──────────────────────────────────────────────────────
function loadRs485Thresh() {
  fetch('/get_rs485_thresh').then(r => r.json()).then(th => {
    document.getElementById('th-wph-low').value     = th.wph_low;
    document.getElementById('th-wph-high').value    = th.wph_high;
    document.getElementById('th-wec-high').value    = th.wec_high;
    document.getElementById('th-smoist-low').value  = th.smoist_low;
    document.getElementById('th-smoist-high').value = th.smoist_high;
    document.getElementById('th-sec-high').value    = th.sec_high;
    document.getElementById('th-sph-low').value     = th.sph_low;
    document.getElementById('th-sph-high').value    = th.sph_high;
  }).catch(() => {});
}
function saveSoilThresh() {
  const lo = parseFloat(document.getElementById('th-smoist-low').value);
  const hi = parseFloat(document.getElementById('th-smoist-high').value);
  const ec = parseFloat(document.getElementById('th-sec-high').value);
  const pl = parseFloat(document.getElementById('th-sph-low').value);
  const ph = parseFloat(document.getElementById('th-sph-high').value);
  if ([lo, hi, ec, pl, ph].some(isNaN)) { showMsg('Enter valid values', 'error'); return; }
  if (lo >= hi) { showMsg('Min moisture must be lower than max', 'error'); return; }
  if (pl >= ph) { showMsg('Min pH must be lower than max', 'error'); return; }
  fetch('/set_rs485_thresh?smoist_low=' + lo + '&smoist_high=' + hi +
        '&sec_high=' + ec + '&sph_low=' + pl + '&sph_high=' + ph)
    .then(() => showMsg('✓ Soil thresholds saved!', 'success'))
    .catch(() => showMsg('Failed to save', 'error'));
}
function saveWaterThresh() {
  const pl = parseFloat(document.getElementById('th-wph-low').value);
  const ph = parseFloat(document.getElementById('th-wph-high').value);
  const ec = parseFloat(document.getElementById('th-wec-high').value);
  if ([pl, ph, ec].some(isNaN)) { showMsg('Enter valid values', 'error'); return; }
  if (pl >= ph) { showMsg('Min pH must be lower than max', 'error'); return; }
  fetch('/set_rs485_thresh?wph_low=' + pl + '&wph_high=' + ph + '&wec_high=' + ec)
    .then(() => showMsg('✓ Water thresholds saved!', 'success'))
    .catch(() => showMsg('Failed to save', 'error'));
}
```

(ii) In `loadSensorType()`, ADD inside the `.then(d => { ... })`:
```js
    applySensorTypeVisibility(d.sensor_type);
```

(iii) At the END of `pollSensors()`'s `.then(d => { ... })`, ADD:
```js
    if (d.sensor_type) currentSensorType = d.sensor_type;
    updateRs485Ui(d);
```

(iv) In `setSensorType(type)`'s success branch, ADD:
```js
      applySensorTypeVisibility(type);
```

(v) In the `window.addEventListener('load', ...)` block, ADD:
```js
  loadRs485Thresh();
```

---

## 5. Build

```bash
pio run -e esp32c6
```
No new lib_deps — the driver uses only HardwareSerial + Preferences (already in core).
