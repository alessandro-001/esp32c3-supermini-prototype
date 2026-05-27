#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "config.h"
#include "secrets.h"
#include "sensors.h"
#include "wifi_config.h"
#include "provisioning.h"
#include "mqtt.h"
#include "local_mqtt.h"
#include <math.h>

// ── Log buffer ────────────────────────────────────────────────────────────────
#define LOG_BUF_SIZE 40
static String _logBuf[LOG_BUF_SIZE];
static uint32_t _logHead = 0;

void logPush(const String& line) {
    _logBuf[_logHead % LOG_BUF_SIZE] = line;
    _logHead++;
}

extern bool registerDevice();
extern bool deviceIsCommissioned();
extern void setCommissionedPublic();

WebServer server(80);

//* Wi-Fi Configuration Implementation + Web Server Endpoints + UI

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BOSS FARM — Device Setup</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.js"></script>
  <style>
    :root {
      --bg:      #f9f7f3;
      --surface: #ffffff;
      --border:  #e8e4dc;
      --accent:  #2d5d3f;
      --green:   #2d5d3f;
      --yellow:  #d4a137;
      --red:     #c94c4c;
      --muted:   #8b8680;
      --text:    #3d3a36;
      --mono:    'Courier New', 'Lucida Console', monospace;
      --sans:    'Trebuchet MS', 'Segoe UI', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body { width: 100%; min-height: 100%; }
    body {
      font-family: var(--sans);
      background: var(--bg);
      color: var(--text);
      width: 100vw;
      margin: 0 auto;
      padding: 24px 32px 40px;
      position: relative;
    }
    body::before {
      content: '';
      position: fixed; inset: 0;
      background-image:
        linear-gradient(rgba(45,93,63,0.02) 1px, transparent 1px),
        linear-gradient(90deg, rgba(45,93,63,0.02) 1px, transparent 1px);
      background-size: 40px 40px;
      pointer-events: none; z-index: 0;
    }
    .page { position: relative; z-index: 1; max-width: 1500px; margin: 0 auto; }
    .page-header { border-bottom: 2px solid var(--accent); padding-bottom: 24px; margin-bottom: 32px; }
    .page-header .label { font-family: var(--mono); font-size: 0.65rem; color: var(--accent); letter-spacing: 0.2em; text-transform: uppercase; margin-bottom: 8px; }
    .page-header h1 { font-size: 2.2rem; font-weight: 700; letter-spacing: -0.02em; color: var(--text); }
    .page-header h1 span { color: var(--accent); }
    
    /* Layout Grid */
    .row-layout { display: grid; gap: 20px; margin-bottom: 20px; }
    .row-2col { grid-template-columns: 1fr 1fr; }
    .row-1col { grid-template-columns: 1fr; }
    
    .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 24px; transition: all 0.2s; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }
    .card:hover { border-color: var(--accent); box-shadow: 0 2px 8px rgba(45,93,63,0.08); }
    .card-header { display: flex; align-items: center; gap: 12px; margin-bottom: 18px; padding-bottom: 12px; border-bottom: 1px solid var(--border); }
    .card-icon { font-size: 1.4rem; line-height: 1; }
    .card-title { font-size: 0.85rem; font-weight: 700; letter-spacing: 0.1em; text-transform: uppercase; color: var(--accent); font-family: var(--mono); }
    .row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid var(--border); font-size: 0.9rem; }
    .row:last-child { border-bottom: none; }
    .row-label { color: var(--muted); font-size: 0.85rem; font-weight: 500; }
    .val { font-family: var(--mono); font-size: 1rem; font-weight: 600; color: var(--accent); word-break: break-all; }
    .val.small { font-size: 0.82rem; }
    
    /* Sensor Grid */
    .sensor-grid { display: grid; grid-template-columns: repeat(5, 1fr); gap: 12px; margin-bottom: 20px; }
    .sensor-tile { background: var(--bg); border: 1px solid var(--border); border-radius: 10px; padding: 16px 12px; transition: all 0.2s; text-align: center; }
    .sensor-tile:hover { border-color: var(--accent); background: rgba(45,93,63,0.03); }
    .sensor-tile-label { font-family: var(--mono); font-size: 0.62rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 8px; font-weight: 600; }
    .sensor-tile-val { font-family: var(--mono); font-size: 1.25rem; font-weight: 700; color: var(--accent); }
    .sensor-tile-val.light-on  { color: var(--yellow); }
    .sensor-tile-val.light-off { color: var(--muted); }
    .sensor-tile-val.aqi-1 { color: var(--green); }
    .sensor-tile-val.aqi-2 { color: #5da86b; }
    .sensor-tile-val.aqi-3 { color: var(--yellow); }
    .sensor-tile-val.aqi-4 { color: #d4824d; }
    .sensor-tile-val.aqi-5 { color: var(--red); }
    
    /* Chart Container */
    .chart-container { position: relative; height: 300px; margin: 20px 0; padding: 0 12px; }
    
    /* Threshold Grid - Single Row */
    .thresh-grid { display: grid; grid-template-columns: repeat(5, 1fr); gap: 16px; }
    .thresh-item { display: flex; flex-direction: column; }
    .thresh-item label { margin: 0 0 8px 0; }
    .thresh-item input { width: 100%; }
    .thresh-item.full { grid-column: 1 / -1; }
    
    /* Button Styles */
    button { padding: 12px 16px; margin-top: 12px; border: none; border-radius: 8px; font-family: var(--sans); font-size: 0.95rem; font-weight: 700; cursor: pointer; letter-spacing: 0.02em; transition: all 0.15s; }
    button:active { transform: scale(0.98); }
    button:disabled { opacity: 0.4; cursor: not-allowed; }
    button.btn-primary { background: var(--accent); color: #fff; box-shadow: 0 2px 6px rgba(45,93,63,0.2); width: 100%; }
    button.btn-primary:hover { background: #1f4a2d; box-shadow: 0 4px 12px rgba(45,93,63,0.3); }
    button.btn-wifi    { background: transparent; color: var(--accent); border: 1.5px solid var(--accent); width: 100%; }
    button.btn-wifi:hover { background: rgba(45,93,63,0.1); }
    button.btn-save    { background: rgba(45,93,63,0.1); color: var(--green); border: 1.5px solid var(--green); width: auto; }
    button.btn-save:hover { background: rgba(45,93,63,0.15); }
    button.btn-reset   { background: rgba(201,76,76,0.1);  color: var(--red);   border: 1.5px solid var(--red); width: auto; }
    button.btn-reset:hover { background: rgba(201,76,76,0.15); }
    
    label { display: block; font-family: var(--mono); font-size: 0.68rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.1em; margin: 0 0 6px 0; font-weight: 600; }
    input { width: 100%; padding: 10px 12px; background: var(--bg); border: 1.5px solid var(--border); border-radius: 8px; color: var(--text); font-family: var(--mono); font-size: 0.95rem; outline: none; transition: all 0.2s; }
    input:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(45,93,63,0.1); }
    
    .net { padding: 10px 12px; margin: 5px 0; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; cursor: pointer; font-family: var(--mono); font-size: 0.85rem; word-break: break-all; transition: all 0.15s; color: var(--text); }
    .net:hover { border-color: var(--accent); color: var(--accent); background: rgba(45,93,63,0.03); }
    .hidden { display: none !important; }
    
    .badge { font-family: var(--mono); font-size: 0.7rem; padding: 4px 12px; border-radius: 20px; border: 1px solid var(--border); }
    .badge.online  { background: rgba(45,93,63,0.15);  color: var(--green);  border-color: var(--green); }
    .badge.offline { background: rgba(201,76,76,0.15);  color: var(--red);    border-color: var(--red); }
    .badge.waiting { background: rgba(212,161,55,0.15); color: var(--yellow); border-color: var(--yellow); }
    
    #popup-msg { position: fixed; top: -60px; left: 0; width: 100%; z-index: 9999; text-align: center; padding: 15px 0; font-family: var(--mono); font-size: 0.9rem; font-weight: 600; transition: top 0.4s cubic-bezier(.4,2,.6,1); border-bottom: 2px solid var(--accent); background: var(--surface); color: var(--text); box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
    #popup-msg.success { background: rgba(45,93,63,0.95); color: #fff; border-bottom-color: var(--green); }
    #popup-msg.error   { background: rgba(201,76,76,0.95); color: #fff;   border-bottom-color: var(--red); }
    #popup-msg.info    { background: rgba(45,93,63,0.95); color: #fff; border-bottom-color: var(--accent); }
    
    /* Responsive */
    @media (max-width: 1200px) {
      .row-2col { grid-template-columns: 1fr; }
      .sensor-grid { grid-template-columns: repeat(4, 1fr); }
      .thresh-grid { grid-template-columns: repeat(3, 1fr); }
    }
    @media (max-width: 768px) {
      body { padding: 20px 20px 40px; }
      .page-header h1 { font-size: 1.8rem; }
      .sensor-grid { grid-template-columns: repeat(3, 1fr); }
      .thresh-grid { grid-template-columns: repeat(2, 1fr); }
      .card { padding: 18px; }
    }
    @media (max-width: 640px) {
      body { padding: 16px 12px 40px; }
      .page-header h1 { font-size: 1.5rem; }
      .page-header { padding-bottom: 16px; margin-bottom: 24px; }
      .sensor-grid { grid-template-columns: repeat(2, 1fr); }
      .thresh-grid { grid-template-columns: 1fr; }
      .row-2col { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
<div id="popup-msg"></div>
<div class="page">

  <div class="page-header">
    <div class="label">// device setup</div>
    <h1>BOSS FARM <span>MONITOR</span></h1>
  </div>

  <!-- Device Info & WiFi Connection (Side by Side) -->
  <div class="row-layout row-2col">
    <!-- Device Info Card -->
    <div class="card" id="step1">
      <div class="card-header">
        <span class="card-icon">🔌</span>
        <span class="card-title">Device Information</span>
      </div>
      <div class="row">
        <span class="row-label">Device ID</span>
        <span class="val small" id="device-id">–</span>
      </div>
      <div class="row">
        <span class="row-label">Firmware</span>
        <span class="val small" id="device-firmware">–</span>
      </div>
      <div class="row">
        <span class="row-label">IP Address</span>
        <span class="val" id="device-ip">–</span>
      </div>
      <div class="row">
        <span class="row-label">Hostname</span>
        <span class="val small" id="device-mdns">–</span>
      </div>
      <div class="row">
        <span class="row-label">Signal</span>
        <span class="val" id="device-rssi">–</span>
      </div>
      <div id="sensor-type-commissioned" class="row hidden">
        <span class="row-label">Sensor Type</span>
        <span class="val" id="sensor-type-badge">–</span>
      </div>
      <div id="sensor-type-selector" class="row hidden" style="margin-top:8px;flex-direction:column;align-items:flex-start;">
        <label style="margin-bottom:8px;">Select sensor type for this unit</label>
        <div style="display:flex;gap:8px;width:100%;margin-bottom:8px;">
          <button class="btn-wifi" style="flex:1;margin-top:0;padding:10px;" onclick="setSensorType(1)">Environment</button>
          <button class="btn-wifi" style="flex:1;margin-top:0;padding:10px;" onclick="setSensorType(2)">Soil</button>
          <button class="btn-wifi" style="flex:1;margin-top:0;padding:10px;" onclick="setSensorType(3)">Mineral</button>
        </div>
        <div id="sensor-type-selected" style="font-family:var(--mono);font-size:0.8rem;color:var(--muted);">None selected</div>
      </div>
      <div class="row">
        <span class="row-label">Network Status</span>
        <span id="device-status"><span class="badge waiting">Checking...</span></span>
      </div>
    </div>

    <!-- WiFi Connection Card -->
    <div class="card" id="step2">
      <div class="card-header">
        <span class="card-icon">📶</span>
        <span class="card-title">WiFi Connection</span>
      </div>
      <div class="row">
        <span class="row-label">Network</span>
        <span class="val" id="curr-wifi">–</span>
      </div>
      <div class="row">
        <span class="row-label">Status</span>
        <span id="conn-status" style="font-family:var(--mono);font-size:0.85rem;">–</span>
      </div>
      <div id="wifi-config">
        <button class="btn-wifi" style="margin-top:12px;" onclick="scanWifi()">🔍 Scan Networks</button>
        <div id="networks"></div>
        <div id="wifi-form" class="hidden">
          <label>Network</label>
          <input type="text" id="wifi-ssid" readonly>
          <label>Password</label>
          <input type="password" id="wifi-pass" placeholder="Enter password">
          <button class="btn-primary" onclick="saveWifi()">💾 Connect</button>
        </div>
      </div>
    </div>
  </div>

  <!-- Live Sensor Data with Chart -->
  <div class="card row-1col" id="step4">
    <div class="card-header">
      <span class="card-icon">📡</span>
      <span class="card-title">Live Sensor Data</span>
    </div>
    <div class="sensor-grid">
      <div class="sensor-tile">
        <div class="sensor-tile-label">🌡 Temperature</div>
        <div class="sensor-tile-val" id="temp">–</div>
      </div>
      <div class="sensor-tile">
        <div class="sensor-tile-label">💧 Humidity</div>
        <div class="sensor-tile-val" id="hum">–</div>
      </div>
      <div class="sensor-tile">
        <div class="sensor-tile-label">💡 Light</div>
        <div class="sensor-tile-val" id="light">–</div>
      </div>
      <div class="sensor-tile">
        <div class="sensor-tile-label">💨 CO2</div>
        <div class="sensor-tile-val" id="co2">– <span style="font-size:0.7rem;color:var(--muted);">ppm</span></div>
      </div>
      <div class="sensor-tile">
        <div class="sensor-tile-label">📊 Air Quality</div>
        <div class="sensor-tile-val" id="co2-label" style="color:var(--muted);">–</div>
      </div>
    </div>
    <div class="chart-container">
      <canvas id="sensorChart"></canvas>
    </div>
  </div>

  <!-- Alert Thresholds (Single Row) -->
  <div class="card row-1col" id="step-thresh">
    <div class="card-header">
      <span class="card-icon">⚠️</span>
      <span class="card-title">Alert Thresholds</span>
    </div>
    <div class="thresh-grid">
      <div class="thresh-item">
        <label>🌡 Min Temp (°C)</label>
        <input type="number" id="thresh-temp-low" step="0.5" min="-40" max="125" placeholder="5">
      </div>
      <div class="thresh-item">
        <label>🌡 Max Temp (°C)</label>
        <input type="number" id="thresh-temp" step="0.5" min="-40" max="125" placeholder="30">
      </div>
      <div class="thresh-item">
        <label>💧 Min Humidity (%)</label>
        <input type="number" id="thresh-hum-low" step="1" min="0" max="100" placeholder="20">
      </div>
      <div class="thresh-item">
        <label>💧 Max Humidity (%)</label>
        <input type="number" id="thresh-hum" step="1" min="0" max="100" placeholder="80">
      </div>
      <div class="thresh-item">
        <label>💨 Max CO2 (ppm)</label>
        <input type="number" id="thresh-co2" step="50" min="400" max="40000" placeholder="1000">
      </div>
    </div>
    <button class="btn-save" onclick="saveThresholds()">💾 Save Thresholds</button>
  </div>

  <!-- Device Log Terminal -->
  <div class="card row-1col">
    <div class="card-header">
      <span class="card-icon">🖥</span>
      <span class="card-title">Device Log</span>
    </div>
    <div id="terminal" style="background:#f5f5f1;border:1px solid rgba(45,93,63,0.2);border-radius:8px;padding:14px;height:240px;overflow-y:auto;font-family:var(--mono);font-size:0.72rem;color:#2d5d3f;line-height:1.8;text-shadow:0 0 2px rgba(45,93,63,0.1);box-shadow:inset 0 0 10px rgba(45,93,63,0.03);"></div>
    <button class="btn-wifi" style="margin-top:8px;font-size:0.8rem;padding:8px 12px;width:auto;" onclick="document.getElementById('terminal').innerHTML='';lastLogSeq=-1;">🗑 Clear</button>
  </div>

  <!-- Factory Reset -->
  <div class="card row-1col" id="step-reset">
    <div class="card-header">
      <span class="card-icon">⚠️</span>
      <span class="card-title">Factory Reset</span>
    </div>
    <div class="row">
      <span class="row-label" style="color:var(--muted);font-size:0.8rem;">
        Clears all settings and WiFi credentials. Device will reboot and AP will reappear.
      </span>
    </div>
    <button class="btn-reset" style="width:auto;" onclick="factoryReset()">🗑 Factory Reset</button>
  </div>

</div><!-- /page -->

<!-- ============================================================================
     SCRIPT SECTION
     
     Location guide for your C++ backend:
     
     1. ALL DEVICE CONTROL LOGIC (lines below) runs client-side
     2. API endpoints being called: /device_info, /wifi, /sensors, /logs, 
        /get_thresh, /set_thresh, /scan, /set_wifi, /factory_reset, 
        /get_sensor_type, /set_sensor_type
     3. CHART FUNCTIONS (initChart, updateChart): Called automatically when 
        sensor data updates. Called in pollSensors() at ~line 400
     4. Add to your C++ the following endpoints if not already present:
        - GET /device_info → JSON with device_id, firmware, mdns, ip, rssi, commissioned
        - GET /sensors → JSON with temp, hum, ldr_ok, light_on, scd40_ok, co2, co2_label
        - GET /wifi → JSON with ssid, connected
        - GET /logs → JSON with seq, lines[]
        - GET /get_thresh → JSON with temp, temp_low, hum, hum_low, co2
        - POST /set_thresh → accepts temp, temp_low, hum, hum_low, co2 params
        - GET /scan → JSON with array of {ssid, rssi, secure}
        - POST /set_wifi → accepts ssid, pass, secure; returns {ok, ip, mdns, msg}
        - POST /factory_reset → triggers reset
        - GET /get_sensor_type → JSON with sensor_type (1,2,3)
        - POST /set_sensor_type → accepts type param
     
     ============================================================================ -->

<script>
// ═══════════════════════════════════════════════════════════════════════════
// CHART INITIALIZATION & UPDATE (for sensor data visualization)
// ═══════════════════════════════════════════════════════════════════════════

let sensorChart = null;
let chartData = {
  temp: [],
  humidity: [],
  co2: [],
  timestamps: []
};

function initChart() {
  const ctx = document.getElementById('sensorChart');
  if (!ctx) return;
  
  sensorChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: chartData.timestamps,
      datasets: [
        {
          label: 'Temperature (°C)',
          data: chartData.temp,
          borderColor: '#2d5d3f',
          backgroundColor: 'rgba(45,93,63,0.1)',
          tension: 0.3,
          fill: false,
          borderWidth: 2,
          pointRadius: 4,
          pointBackgroundColor: '#2d5d3f'
        },
        {
          label: 'Humidity (%)',
          data: chartData.humidity,
          borderColor: '#d4a137',
          backgroundColor: 'rgba(212,161,55,0.1)',
          tension: 0.3,
          fill: false,
          borderWidth: 2,
          pointRadius: 4,
          pointBackgroundColor: '#d4a137'
        },
        {
          label: 'CO₂ (ppm/10)',
          data: chartData.co2,
          borderColor: '#c94c4c',
          backgroundColor: 'rgba(201,76,76,0.1)',
          tension: 0.3,
          fill: false,
          borderWidth: 2,
          pointRadius: 4,
          pointBackgroundColor: '#c94c4c'
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          display: true,
          labels: {
            color: '#3d3a36',
            font: { family: "'Courier New', monospace", size: 12 },
            usePointStyle: true,
            padding: 15
          }
        },
        filler: { propagate: true }
      },
      scales: {
        y: {
          beginAtZero: false,
          ticks: { color: '#8b8680', font: { size: 11 } },
          grid: { color: 'rgba(232,228,220,0.3)' }
        },
        x: {
          ticks: { color: '#8b8680', font: { size: 11 } },
          grid: { color: 'rgba(232,228,220,0.3)' }
        }
      }
    }
  });
}

function updateChart(temp, humidity, co2) {
  if (!sensorChart) {
    initChart();
    return;
  }
  
  const now = new Date().toLocaleTimeString();
  chartData.timestamps.push(now);
  chartData.temp.push(typeof temp === 'number' ? temp : null);
  chartData.humidity.push(typeof humidity === 'number' ? humidity : null);
  chartData.co2.push(typeof co2 === 'number' ? co2/10 : null);
  
  // Keep only last 30 data points
  if (chartData.timestamps.length > 30) {
    chartData.timestamps.shift();
    chartData.temp.shift();
    chartData.humidity.shift();
    chartData.co2.shift();
  }
  
  sensorChart.update('none');
}

// ═══════════════════════════════════════════════════════════════════════════
// DEVICE CONTROL FUNCTIONS (existing logic - do not modify)
// ═══════════════════════════════════════════════════════════════════════════

let selectedSecure = false;

function loadThresholds() {
  fetch('/get_thresh').then(r => r.json()).then(th => {
    document.getElementById('thresh-temp').value     = th.temp;
    document.getElementById('thresh-temp-low').value = th.temp_low;
    document.getElementById('thresh-hum').value      = th.hum;
    document.getElementById('thresh-hum-low').value  = th.hum_low;
    document.getElementById('thresh-co2').value      = th.co2;
  });
}

function saveThresholds() {
  const temp    = parseFloat(document.getElementById('thresh-temp').value);
  const tempLow = parseFloat(document.getElementById('thresh-temp-low').value);
  const hum     = parseFloat(document.getElementById('thresh-hum').value);
  const humLow  = parseFloat(document.getElementById('thresh-hum-low').value);
  const co2     = parseFloat(document.getElementById('thresh-co2').value);
  if ([temp, tempLow, hum, humLow, co2].some(isNaN)) { showMsg('Enter valid values for all thresholds', 'error'); return; }
  if (tempLow >= temp) { showMsg('Min Temp must be lower than Max Temp', 'error'); return; }
  if (humLow  >= hum)  { showMsg('Min Humidity must be lower than Max Humidity', 'error'); return; }
  showMsg('Saving thresholds...', 'info');
  fetch('/set_thresh?temp=' + temp + '&temp_low=' + tempLow + '&hum=' + hum + '&hum_low=' + humLow + '&co2=' + co2)
    .then(r => r.text())
    .then(() => showMsg('✓ Thresholds saved!', 'success'))
    .catch(() => showMsg('Failed to save', 'error'));
}

function scanWifi() {
  const nets = document.getElementById('networks');
  nets.innerHTML = 'Scanning...';
  fetch('/scan').then(r => r.json()).then(list => {
    nets.innerHTML = list.length
      ? list.map(n => `<div class='net' onclick='selectNet("${n.ssid.replace(/'/g,"\\'").replace(/"/g,'\\"')}",${n.secure})'>${n.secure?"🔒":"🔓"} ${n.ssid} (${n.rssi} dBm)</div>`).join('')
      : 'No networks found';
  }).catch(() => nets.innerHTML = 'Scan failed');
}

function selectNet(ssid, secure) {
  selectedSecure = secure;
  document.getElementById('wifi-ssid').value = ssid;
  document.getElementById('wifi-pass').value = '';
  document.getElementById('wifi-form').classList.remove('hidden');
  if (secure) document.getElementById('wifi-pass').focus();
}

function saveWifi() {
  const ssid = document.getElementById('wifi-ssid').value.trim();
  const pass = document.getElementById('wifi-pass').value;
  if (!ssid) { showMsg('SSID required', 'error'); return; }
  showMsg('Connecting...', 'info');
  fetch('/set_wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass) + '&secure=' + (selectedSecure ? 'true' : 'false')
  })
  .then(r => r.json())
  .then(res => {
    if (res.ok) showProvisionedOverlay(res.ip, res.mdns);
    else showMsg(res.msg || 'Connection failed', 'error');
  })
  .catch(() => showMsg('Connection failed', 'error'));
}

function showProvisionedOverlay(ip, mdns) {
  const overlay = document.createElement('div');
  overlay.id = 'prov-overlay';
  overlay.style.cssText = 'position:fixed;inset:0;z-index:99999;background:rgba(249,247,243,0.97);display:flex;flex-direction:column;align-items:center;justify-content:center;padding:32px;text-align:center;';
  overlay.innerHTML = `
    <div style="font-size:3rem;margin-bottom:16px;">✅</div>
    <div style="font-family:var(--mono);font-size:0.7rem;color:var(--accent);letter-spacing:0.2em;text-transform:uppercase;margin-bottom:8px;">Device Provisioned</div>
    <div style="font-size:1.3rem;font-weight:700;margin-bottom:24px;">Successfully joined network</div>
    <div style="background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:20px;width:100%;max-width:320px;margin-bottom:24px;">
      <div style="display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid var(--border);">
        <span style="color:var(--muted);font-size:0.85rem;">IP Address</span>
        <span style="font-family:var(--mono);color:var(--accent);">${ip}</span>
      </div>
      <div style="display:flex;justify-content:space-between;align-items:center;padding:8px 0;">
        <span style="color:var(--muted);font-size:0.85rem;">Local hostname</span>
        <span style="font-family:var(--mono);color:var(--accent);font-size:0.85rem;">${mdns}</span>
      </div>
    </div>
    <a href="http://${ip}" target="_blank" style="display:block;width:100%;max-width:320px;background:var(--accent);color:#fff;font-weight:700;font-size:1rem;padding:14px;border-radius:8px;text-decoration:none;margin-bottom:12px;">Open Device UI →</a>
    <a href="http://${mdns}" target="_blank" style="display:block;width:100%;max-width:320px;background:transparent;color:var(--accent);font-weight:600;font-size:0.9rem;padding:12px;border-radius:8px;text-decoration:none;border:1px solid var(--accent);margin-bottom:20px;">Open via ${mdns} →</a>
    <p style="color:var(--muted);font-size:0.78rem;font-family:var(--mono);line-height:1.6;">Reconnect your phone to your WiFi network<br>to access the device UI.</p>
  `;
  document.body.appendChild(overlay);
}

function updateStatus() {
  fetch('/wifi').then(r => r.json()).then(w => {
    document.getElementById('curr-wifi').textContent = w.ssid || '–';
    document.getElementById('conn-status').textContent = w.connected ? '✓ Connected' : '✗ Not connected';
    document.getElementById('conn-status').style.color = w.connected ? '#2d5d3f' : '#c94c4c';
  }).catch(() => {});

  fetch('/device_info').then(r => r.json()).then(info => {
    document.getElementById('device-id').textContent       = info.device_id  || '–';
    document.getElementById('device-firmware').textContent = info.firmware    || '–';
    document.getElementById('device-mdns').textContent     = info.mdns        || '–';

    const ip = info.ip && info.ip !== '0.0.0.0' ? info.ip : null;
    const ipEl = document.getElementById('device-ip');
    ipEl.textContent = ip || '–';
    ipEl.style.color = ip ? 'var(--accent)' : 'var(--muted)';

    const rssi = info.rssi || 0;
    const rssiEl = document.getElementById('device-rssi');
    rssiEl.textContent = rssi ? rssi + ' dBm' : '–';
    rssiEl.style.color = rssi >= -60 ? '#2d5d3f' : rssi >= -75 ? '#d4a137' : '#c94c4c';

    const statusEl = document.getElementById('device-status');
    if (info.commissioned && ip) {
      statusEl.innerHTML = '<span class="badge online">● Online</span>';
    } else if (!info.commissioned) {
      statusEl.innerHTML = '<span class="badge waiting">Not commissioned</span>';
    } else {
      statusEl.innerHTML = '<span class="badge offline">● Offline</span>';
    }

    loadSensorType();
    if (info.commissioned) {
      document.getElementById('sensor-type-commissioned').classList.remove('hidden');
      document.getElementById('sensor-type-selector').classList.add('hidden');
    } else {
      document.getElementById('sensor-type-commissioned').classList.add('hidden');
      document.getElementById('sensor-type-selector').classList.remove('hidden');
    }
  }).catch(() => {});
}

function pollSensors() {
  fetch('/sensors').then(r => r.json()).then(d => {
    const tempEl      = document.getElementById('temp');
    const humEl       = document.getElementById('hum');
    const lightEl     = document.getElementById('light');
    const co2El       = document.getElementById('co2');
    const co2LabelEl  = document.getElementById('co2-label');

    // Temperature
    if (d.temp != null) {
      tempEl.textContent = d.temp.toFixed(1);
      tempEl.style.color = '';
    } else {
      tempEl.textContent = '–';
      tempEl.style.color = 'var(--muted)';
    }

    // Humidity
    if (d.hum != null) {
      humEl.textContent = d.hum.toFixed(1);
      humEl.style.color = '';
    } else {
      humEl.textContent = '–';
      humEl.style.color = 'var(--muted)';
    }

    // Light / LDR
    if (d.ldr_ok) {
      lightEl.textContent = d.light_on ? 'ON' : 'OFF';
      lightEl.className = 'sensor-tile-val ' + (d.light_on ? 'light-on' : 'light-off');
      lightEl.style.color = '';
    } else {
      lightEl.textContent = 'N/A';
      lightEl.className = 'sensor-tile-val';
      lightEl.style.color = 'var(--muted)';
    }

    // CO2 / SCD40
    if (d.scd40_ok) {
      if (d.co2 != null && d.co2 >= 400) {
        co2El.innerHTML = d.co2 + ' <span style="font-size:0.7rem;color:var(--muted);">ppm</span>';
        co2LabelEl.textContent = d.co2_label || '–';
        co2LabelEl.style.color = d.co2 < 1000 ? '#2d5d3f' : d.co2 < 1500 ? '#d4a137' : '#c94c4c';
        co2El.style.color = '';
      } else if (d.co2 === 0 || d.co2 == null) {
        co2El.textContent = 'Warming up';
        co2El.style.color = 'var(--muted)';
        co2LabelEl.textContent = '';
        co2LabelEl.style.color = 'var(--muted)';
      } else {
        co2El.textContent = 'N/A';
        co2El.style.color = 'var(--muted)';
        co2LabelEl.textContent = '';
        co2LabelEl.style.color = 'var(--muted)';
      }
    } else {
      co2El.textContent = 'N/A';
      co2El.style.color = 'var(--muted)';
      co2LabelEl.textContent = '';
      co2LabelEl.style.color = 'var(--muted)';
    }

    // Update chart with latest sensor values
    if (d.temp != null || d.hum != null || d.co2 != null) {
      updateChart(d.temp, d.hum, d.co2);
    }
  }).catch(err => { console.error('pollSensors error', err); });
}

function showMsg(txt, type) {
  const el = document.getElementById('popup-msg');
  el.textContent = txt; el.className = type || ''; el.style.top = '0';
  clearTimeout(el._hideTimer);
  el._hideTimer = setTimeout(() => { el.style.top = '-60px'; el.className = ''; }, 5000);
}

function factoryReset() {
  if (!confirm('Reset this device? All settings and WiFi credentials will be cleared. The AP hotspot will reappear.')) return;
  showMsg('Resetting...', 'info');
  fetch('/factory_reset', { method: 'POST' })
    .then(() => showMsg('Device reset — reconnect to the AP hotspot', 'success'))
    .catch(() => showMsg('Reset sent — reconnect to the AP hotspot', 'success'));
}

let lastLogSeq = -1;
function pollLogs() {
  fetch('/logs').then(r => r.json()).then(data => {
    if (data.seq === lastLogSeq) return;
    lastLogSeq = data.seq;
    const t = document.getElementById('terminal');
    t.innerHTML = data.lines.map(l => '<div>' + l.replace(/</g,'&lt;').replace(/>/g,'&gt;') + '</div>').join('');
    t.scrollTop = t.scrollHeight;
  }).catch(() => {});
}

const SENSOR_LABELS = { 1: 'Environment', 2: 'Soil', 3: 'Mineral' };

function loadSensorType() {
  fetch('/get_sensor_type').then(r => r.json()).then(d => {
    const label = SENSOR_LABELS[d.sensor_type] || '–';
    document.getElementById('sensor-type-badge').textContent = label;
  }).catch(() => {});
}

function setSensorType(type) {
  fetch('/set_sensor_type', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'type=' + type
  })
  .then(r => r.json())
  .then(res => {
    if (res.status === 'ok') {
      document.getElementById('sensor-type-selected').textContent =
        '✓ Set to ' + (SENSOR_LABELS[type] || type);
      document.getElementById('sensor-type-selected').style.color = 'var(--green)';
      showMsg('✓ Sensor type saved', 'success');
    }
  })
  .catch(() => showMsg('Failed to set sensor type', 'error'));
}

// Initialize on page load
window.addEventListener('load', function() {
  updateStatus();
  loadSensorType();
  loadThresholds();
  pollSensors();
  pollLogs();
  
  // Set up polling intervals
  setInterval(pollSensors, 3000);
  setInterval(updateStatus, 5000);
  setInterval(pollLogs, 1000);
});
</script>
</body>
</html>
)rawliteral";

// ── Device Discovery Page ─────────────────────────────────────────────────────
const char DISCOVER_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BOSS FARM — Device Discovery</title>
  <style>
    :root {
      --bg:      #f9f7f3;
      --surface: #ffffff;
      --border:  #e8e4dc;
      --accent:  #2d5d3f;
      --green:   #2d5d3f;
      --yellow:  #d4a137;
      --red:     #c94c4c;
      --muted:   #8b8680;
      --text:    #3d3a36;
      --mono:    'Courier New', 'Lucida Console', monospace;
      --sans:    'Trebuchet MS', 'Segoe UI', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: var(--bg); color: var(--text); font-family: var(--sans); min-height: 100vh; overflow-x: hidden; }
    body::before {
      content: '';
      position: fixed; inset: 0;
      background-image:
        linear-gradient(rgba(45,93,63,0.02) 1px, transparent 1px),
        linear-gradient(90deg, rgba(45,93,63,0.02) 1px, transparent 1px);
      background-size: 40px 40px;
      pointer-events: none; z-index: 0;
    }
    .wrap { position: relative; z-index: 1; max-width: 1200px; margin: 0 auto; padding: 40px 32px; }
    header { display: flex; align-items: flex-end; justify-content: space-between; border-bottom: 2px solid var(--accent); padding-bottom: 24px; margin-bottom: 36px; flex-wrap: wrap; gap: 16px; }
    .logo-block .label { font-family: var(--mono); font-size: 0.7rem; color: var(--accent); letter-spacing: 0.2em; text-transform: uppercase; margin-bottom: 6px; }
    .logo-block h1 { font-size: 2.2rem; font-weight: 700; letter-spacing: -0.02em; line-height: 1; color: var(--text); }
    .logo-block h1 span { color: var(--accent); }
    .status-pill { font-family: var(--mono); font-size: 0.75rem; padding: 8px 16px; border-radius: 20px; border: 1px solid var(--border); color: var(--muted); display: flex; align-items: center; gap: 8px; background: var(--surface); }
    .status-pill .dot { width: 7px; height: 7px; border-radius: 50%; background: var(--muted); transition: background 0.3s; }
    .status-pill.scanning .dot { background: var(--yellow); animation: pulse 0.8s infinite; }
    .status-pill.done .dot { background: var(--green); }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
    
    .controls { display: flex; gap: 12px; margin-bottom: 32px; flex-wrap: wrap; align-items: flex-end; }
    .field { display: flex; flex-direction: column; gap: 6px; flex: 1; min-width: 180px; }
    .field label { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); letter-spacing: 0.1em; text-transform: uppercase; font-weight: 600; }
    .field input { background: var(--surface); border: 1.5px solid var(--border); border-radius: 8px; padding: 10px 14px; font-family: var(--mono); font-size: 0.95rem; color: var(--text); outline: none; transition: all 0.2s; }
    .field input:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(45,93,63,0.1); }
    
    .btn { padding: 10px 28px; border-radius: 8px; border: none; font-family: var(--sans); font-size: 0.95rem; font-weight: 700; cursor: pointer; letter-spacing: 0.02em; transition: all 0.15s; white-space: nowrap; height: 42px; }
    .btn:active { transform: scale(0.97); }
    .btn:disabled { opacity: 0.4; cursor: not-allowed; }
    .btn-primary { background: var(--accent); color: #fff; box-shadow: 0 2px 6px rgba(45,93,63,0.2); }
    .btn-primary:hover { background: #1f4a2d; box-shadow: 0 4px 12px rgba(45,93,63,0.3); }
    .btn-ghost { background: transparent; border: 1.5px solid var(--border); color: var(--muted); }
    .btn-ghost:hover { border-color: var(--accent); color: var(--accent); }
    
    .progress-wrap { height: 2px; background: var(--border); border-radius: 2px; margin-bottom: 32px; overflow: hidden; display: none; }
    .progress-wrap.active { display: block; }
    .progress-bar { height: 100%; background: var(--accent); width: 0%; transition: width 0.3s; border-radius: 2px; }
    
    .stats { display: flex; gap: 24px; margin-bottom: 28px; font-family: var(--mono); font-size: 0.78rem; color: var(--muted); }
    .stats span { color: var(--text); font-weight: 600; }
    
    .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 16px; }
    .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 20px; cursor: pointer; transition: all 0.2s; text-decoration: none; color: inherit; display: block; animation: fadeIn 0.3s ease both; }
    .card:hover { border-color: var(--accent); transform: translateY(-2px); box-shadow: 0 2px 8px rgba(45,93,63,0.08); }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(8px); } to { opacity: 1; transform: translateY(0); } }
    
    .card-header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 16px; }
    .device-name { font-weight: 600; font-size: 1.1rem; line-height: 1.3; color: var(--text); }
    .device-id { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); margin-top: 4px; }
    .online-badge { font-family: var(--mono); font-size: 0.65rem; padding: 4px 10px; border-radius: 20px; background: rgba(45,93,63,0.15); color: var(--green); border: 1px solid var(--green); white-space: nowrap; }
    
    .card-ip { font-family: var(--mono); font-size: 0.85rem; color: var(--accent); margin-bottom: 16px; font-weight: 600; }
    
    .sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-bottom: 16px; }
    .sensor-item { background: var(--bg); border: 1px solid var(--border); border-radius: 8px; padding: 10px 12px; }
    .sensor-item.full-width { grid-column: 1 / -1; }
    .sensor-label { font-size: 0.65rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 4px; font-weight: 600; }
    .sensor-val { font-family: var(--mono); font-size: 0.95rem; font-weight: 600; color: var(--accent); }
    .sensor-val.light-on { color: var(--yellow); }
    .sensor-val.light-off { color: var(--muted); }
    .sensor-val.aqi-1 { color: var(--green); }
    .sensor-val.aqi-2 { color: #5da86b; }
    .sensor-val.aqi-3 { color: var(--yellow); }
    .sensor-val.aqi-4 { color: #d4824d; }
    .sensor-val.aqi-5 { color: var(--red); }
    
    .card-footer { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); display: flex; justify-content: space-between; border-top: 1px solid var(--border); padding-top: 12px; margin-top: 4px; }
    .card-footer span:last-child { color: var(--accent); font-weight: 600; }
    
    .empty { grid-column: 1 / -1; text-align: center; padding: 60px 20px; color: var(--muted); }
    .empty .icon { font-size: 3rem; margin-bottom: 16px; opacity: 0.5; }
    .empty p { font-size: 0.95rem; line-height: 1.7; }
    
    @media (max-width: 768px) {
      .wrap { padding: 32px 20px; }
      .logo-block h1 { font-size: 1.8rem; }
      .controls { flex-direction: column; }
      .grid { grid-template-columns: 1fr; }
    }
    @media (max-width: 500px) {
      .wrap { padding: 20px 16px; }
      .logo-block h1 { font-size: 1.5rem; }
      .controls { flex-direction: column; }
      .btn { width: 100%; }
      header { flex-direction: column; align-items: flex-start; }
      .status-pill { align-self: flex-start; }
    }
  </style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="logo-block">
      <div class="label">// device discovery</div>
      <h1>BOSS FARM <span>MONITOR</span></h1>
    </div>
    <div class="status-pill" id="status-pill">
      <span class="dot"></span>
      <span id="status-text">Ready</span>
    </div>
  </header>

  <div class="controls">
    <div class="field">
      <label>Subnet prefix</label>
      <input type="text" id="subnet" value="192.168.0" placeholder="e.g. 192.168.0">
    </div>
    <div class="field" style="max-width: 100px;">
      <label>From</label>
      <input type="number" id="range-from" value="1" min="1" max="254">
    </div>
    <div class="field" style="max-width: 100px;">
      <label>To</label>
      <input type="number" id="range-to" value="254" min="1" max="254">
    </div>
    <div class="field" style="max-width: 120px;">
      <label>Timeout (ms)</label>
      <input type="number" id="timeout" value="800" min="200" max="3000" step="100">
    </div>
    <button class="btn btn-primary" id="scan-btn" onclick="startScan()">Scan Network</button>
    <button class="btn btn-ghost" id="stop-btn" onclick="stopScan()" disabled>Stop</button>
  </div>

  <div class="progress-wrap" id="progress-wrap">
    <div class="progress-bar" id="progress-bar"></div>
  </div>

  <div class="stats" id="stats" style="display: none;">
    Scanned <span id="stat-scanned">0</span> / <span id="stat-total">0</span> &nbsp;·&nbsp;
    Found <span id="stat-found">0</span> device(s) &nbsp;·&nbsp;
    <span id="stat-elapsed">0s</span>
  </div>

  <div class="grid" id="grid">
    <div class="empty">
      <div class="icon">📡</div>
      <p>Enter your subnet and hit <strong>Scan Network</strong>.<br>All ESP32 Smart Monitor units on the same WiFi will appear here.</p>
    </div>
  </div>
</div>

<!-- ============================================================================
     DEVICE DISCOVERY SCRIPT (DO NOT MODIFY LOGIC)
     
     This script handles network scanning and device discovery.
     All functions below are client-side and do not require backend changes.
     ============================================================================ -->

<script>
let scanning = false, stopFlag = false, foundCount = 0, scanStart = 0, statsTimer = null;

function setStatus(text, state) {
  const pill = document.getElementById('status-pill');
  const txt = document.getElementById('status-text');
  pill.className = 'status-pill ' + (state || '');
  txt.textContent = text;
}

function aqiClass(aqi) {
  if (!aqi || aqi < 1 || aqi > 5) return '';
  return 'aqi-' + aqi;
}

function formatSensorVal(d) {
  const items = [];
  if (d.temp !== undefined && d.temp !== null)
    items.push({ label: '🌡 Temp', val: d.temp.toFixed(1) + ' °C', cls: '' });
  if (d.hum !== undefined && d.hum !== null)
    items.push({ label: '💧 Humidity', val: d.hum.toFixed(1) + ' %', cls: '' });
  if (d.aqi !== undefined && d.aqi >= 1 && d.aqi <= 5)
    items.push({ label: '🌬 AQI', val: d.aqi + ' — ' + (d.aqi_label || ''), cls: aqiClass(d.aqi) });
  if (d.tvoc !== undefined && d.tvoc >= 0)
    items.push({ label: '💨 TVOC', val: d.tvoc + ' ppb', cls: '' });
  if (d.eco2 !== undefined && d.eco2 >= 400)
    items.push({ label: '💨 eCO2', val: d.eco2 + ' ppm', cls: '' });
  if (d.light_on !== undefined)
    items.push({ label: '💡 Light', val: d.light_on ? 'ON' : 'OFF', cls: d.light_on ? 'light-on' : 'light-off' });
  if (d.aqi_status && d.aqi_status !== 'Initialising' && d.aqi_status !== 'Error')
    items.push({ label: '📊 Air Status', val: d.aqi_status, cls: '' });
  return items;
}

function renderCard(ip, info, sensors) {
  const sensorItems = formatSensorVal(sensors);
  const sensorsHtml = sensorItems.length
    ? sensorItems.map((s, i) => {
        const fw = s.label.includes('Air Status') || (sensorItems.length % 2 !== 0 && i === sensorItems.length - 1);
        return '<div class="sensor-item' + (fw ? ' full-width' : '') + '"><div class="sensor-label">' + s.label + '</div><div class="sensor-val ' + s.cls + '">' + s.val + '</div></div>';
      }).join('')
    : '<div class="sensor-item full-width"><div class="sensor-label">Sensors</div><div class="sensor-val" style="color:var(--muted)">No data</div></div>';

  const card = document.createElement('a');
  card.className = 'card';
  card.href = 'http://' + ip;
  card.target = '_blank';
  card.innerHTML = `
    <div class="card-header">
      <div>
        <div class="device-name">${info.device_name || 'ESP32 Device'}</div>
        <div class="device-id">${info.device_id || '–'}</div>
      </div>
      <span class="online-badge">● ONLINE</span>
    </div>
    <div class="card-ip">${ip}</div>
    <div class="sensors">${sensorsHtml}</div>
    <div class="card-footer">
      <span>fw ${sensors.firmware || '–'}</span>
      <span>Open UI →</span>
    </div>
  `;
  return card;
}

async function fetchWithTimeout(url, ms) {
  const ctrl = new AbortController();
  const tid = setTimeout(() => ctrl.abort(), ms);
  try {
    const r = await fetch(url, { signal: ctrl.signal });
    clearTimeout(tid);
    return r;
  } catch {
    clearTimeout(tid);
    return null;
  }
}

async function probeIp(ip, timeoutMs) {
  const infoRes = await fetchWithTimeout('http://' + ip + '/device_info', timeoutMs);
  if (!infoRes || !infoRes.ok) return null;
  
  let info = {};
  try {
    info = await infoRes.json();
  } catch {
    return null;
  }
  
  if (!info.device_name || !info.device_name.startsWith('ESP32')) return null;
  
  let sensors = {};
  const sensRes = await fetchWithTimeout('http://' + ip + '/sensors', timeoutMs);
  if (sensRes && sensRes.ok) {
    try {
      sensors = await sensRes.json();
    } catch {}
  }
  
  return { ip, info, sensors };
}

async function startScan() {
  if (scanning) return;
  
  const subnet = document.getElementById('subnet').value.trim();
  const from = parseInt(document.getElementById('range-from').value);
  const to = parseInt(document.getElementById('range-to').value);
  const timeout = parseInt(document.getElementById('timeout').value);
  
  if (!subnet || isNaN(from) || isNaN(to) || from > to) {
    alert('Please check your subnet and range values.');
    return;
  }
  
  scanning = true;
  stopFlag = false;
  foundCount = 0;
  scanStart = Date.now();
  
  const total = to - from + 1;
  let scanned = 0;
  
  document.getElementById('scan-btn').disabled = true;
  document.getElementById('stop-btn').disabled = false;
  document.getElementById('progress-wrap').classList.add('active');
  document.getElementById('stats').style.display = 'flex';
  document.getElementById('stat-total').textContent = total;
  document.getElementById('stat-scanned').textContent = 0;
  document.getElementById('stat-found').textContent = 0;
  document.getElementById('grid').innerHTML = '';
  
  setStatus('Scanning...', 'scanning');
  
  statsTimer = setInterval(() => {
    document.getElementById('stat-elapsed').textContent = ((Date.now() - scanStart) / 1000).toFixed(1) + 's';
  }, 200);
  
  const ips = [];
  for (let i = from; i <= to; i++) {
    ips.push(subnet + '.' + i);
  }
  
  const BATCH = 30;
  for (let b = 0; b < ips.length && !stopFlag; b += BATCH) {
    const batch = ips.slice(b, b + BATCH);
    const results = await Promise.all(batch.map(ip => probeIp(ip, timeout)));
    
    for (const result of results) {
      scanned++;
      if (result) {
        foundCount++;
        document.getElementById('stat-found').textContent = foundCount;
        document.getElementById('grid').appendChild(renderCard(result.ip, result.info, result.sensors));
      }
    }
    
    document.getElementById('stat-scanned').textContent = scanned;
    document.getElementById('progress-bar').style.width = ((scanned / total) * 100).toFixed(1) + '%';
  }
  
  clearInterval(statsTimer);
  scanning = false;
  document.getElementById('scan-btn').disabled = false;
  document.getElementById('stop-btn').disabled = true;
  document.getElementById('progress-bar').style.width = '100%';
  
  if (foundCount === 0) {
    document.getElementById('grid').innerHTML = `
      <div class="empty">
        <div class="icon">🔍</div>
        <p>No ESP32 Smart Monitor units found on <strong>${subnet}.${from}–${to}</strong>.<br>Make sure all units are on the same WiFi and try adjusting the subnet or timeout.</p>
      </div>
    `;
    setStatus('No devices found', '');
  } else {
    setStatus('Found ' + foundCount + ' device' + (foundCount > 1 ? 's' : ''), 'done');
  }
}

function stopScan() {
  stopFlag = true;
  clearInterval(statsTimer);
  setStatus('Stopped', '');
  document.getElementById('scan-btn').disabled = false;
  document.getElementById('stop-btn').disabled = true;
  scanning = false;
}
</script>
</body>
</html>
)rawliteral";

// ── Global threshold variables ────────────────────────────────────────────────
float threshTemp    = 30.0f;
float threshTempLow =  5.0f;
float threshHum     = 80.0f;
float threshHumLow  = 20.0f;
float threshCO2     = 1000.0f;

extern float sensorTemp;
extern float sensorHum;
extern bool  alertTemp;
extern bool  alertHum;

static const char* THRESH_NVS_NS = "thresholds";

// ── Helpers ───────────────────────────────────────────────────────────────────

static String escapeJson(const String& input) {
    String output;
    for (unsigned int i = 0; i < input.length(); i++) {
        char c = input[i];
        if      (c == '"')  output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else if (c == '<')  output += "\\u003c";
        else if (c == '>')  output += "\\u003e";
        else                output += c;
    }
    return output;
}

static String getDeviceName() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char name[32];
    snprintf(name, sizeof(name), "ESP32-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(name);
}

static String getDeviceMacString() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static void loadThresholdsFromNVS() {
    Preferences prefs;
    prefs.begin(THRESH_NVS_NS, true);
    threshTemp    = prefs.getFloat("temp",      30.0f);
    threshTempLow = prefs.getFloat("temp_low",   5.0f);
    threshHum     = prefs.getFloat("hum",       80.0f);
    threshHumLow  = prefs.getFloat("hum_low",   20.0f);
    threshCO2     = prefs.getFloat("eco2",    1000.0f);
    prefs.end();
    Serial.printf("[Thresh] Loaded — TempH:%.1f TempL:%.1f HumH:%.1f HumL:%.1f CO2:%.0f\n",
                  threshTemp, threshTempLow, threshHum, threshHumLow, threshCO2);
}

static void saveThresholdsToNVS(float temp, float tempLow, float hum, float humLow, float co2) {
    Preferences prefs;
    prefs.begin(THRESH_NVS_NS, false);
    prefs.putFloat("temp",     temp);
    prefs.putFloat("temp_low", tempLow);
    prefs.putFloat("hum",      hum);
    prefs.putFloat("hum_low",  humLow);
    prefs.putFloat("co2",      co2);
    prefs.end();
}

static String getTelemetryDeviceId() {
    uint8_t type = localMqttGetSensorType();
    const char* prefix = "ENV_";
    if (type == 2) prefix = "SOIL_";
    else if (type == 3) prefix = "MIN_";

    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String suffix = mac.length() >= 4 ? mac.substring(mac.length() - 4) : mac;
    return String(prefix) + suffix;
}

// ── HTTP Handlers ─────────────────────────────────────────────────────────────

static void handleCaptivePortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

static void addCorsHeaders();

static void handleRoot() {
  addCorsHeaders();
  server.send(200, "text/html", HTML_PAGE);
}

static void handleWifiGet() {
  addCorsHeaders();
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"connected\":%s}",
       escapeJson(wifiConfigSsid()).c_str(),
       WiFi.status() == WL_CONNECTED ? "true" : "false");
  server.send(200, "application/json", buf);
}

static void handleScan() {
    server.send(200, "application/json", wifiScanNetworks());
}

static void handleSetWifi() {
    if (!server.hasArg("ssid") || !server.hasArg("pass")) {
        server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing ssid/pass\"}");
        return;
    }

    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    // Open network => ignore password
    bool isOpen = !server.hasArg("secure") || server.arg("secure") == "false";
    if (isOpen) pass = "";

    if (!wifiConfigSave(ssid, pass)) {
        server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid password (min 8 chars)\"}");
        return;
    }

    bool ok = wifiConfigConnect(10000);
    if (!ok) {
        server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Connection failed — check password\"}");
        return;
    }

    // Success path
    setCommissionedPublic();
    localMqttInit();  // start local mqtt now that WiFi is up

    String ip       = WiFi.localIP().toString();
    String mac      = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    String hostname = String(MDNS_PREFIX) + "-" + mac.substring(8);

    String json = "{\"ok\":true,\"ip\":\"" + ip + "\",\"mdns\":\"" + hostname + ".local\"}";
    server.send(200, "application/json", json);

    // IMPORTANT: disconnect AP AFTER response is sent
    delay(200);
    WiFi.softAPdisconnect(true);
    Serial.println("[AP] Hotspot hidden after WiFi connect via web UI");
}

static void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Accept");
}

static void handleDeviceInfo() {
    addCorsHeaders();
    String mac     = getDeviceMacString();
    String devName = getDeviceName();
    String mac2    = WiFi.macAddress();
    mac2.replace(":", "");
    mac2.toLowerCase();
    String mdns = String(MDNS_PREFIX) + "-" + mac2.substring(8) + ".local";
    char buf[320];
    snprintf(buf, sizeof(buf),
        "{\"device_id\":\"%s\",\"device_name\":\"%s\",\"mdns\":\"%s\","
        "\"ip\":\"%s\",\"rssi\":%d,\"firmware\":\"%s\",\"commissioned\":%s}",
        mac.c_str(), devName.c_str(), mdns.c_str(),
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(), FIRMWARE_VERSION,
        deviceIsCommissioned() ? "true" : "false"
    );
    server.send(200, "application/json", buf);
}

static void handleProvStatus() {
    bool provisioned = provisioningHasToken();
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"provisioned\":%s}", provisioned ? "true" : "false");
    server.send(200, "application/json", buf);
}

static void handleProvision() {
    if (!WiFi.isConnected()) {
        server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"WiFi not connected\"}");
        return;
    }
    String token = provisioningRequest();
    if (!token.isEmpty()) {
        mqttSetToken(token);
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Provisioning successful\"}");
    } else {
        server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"Provisioning failed\"}");
    }
}

static void handleSetToken() {
    if (!server.hasArg("token")) { server.send(400, "application/json", "{\"status\":\"error\"}"); return; }
    String token = server.arg("token");
    if (token.length() < 5) { server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Token too short\"}"); return; }
    Preferences prefs;
    prefs.begin("provision", false);
    prefs.putString("token", token);
    prefs.end();
    Serial.printf("[Web] Token saved: %.10s...\n", token.c_str());
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleSensors() {
    addCorsHeaders();

    char buf[512];
    char tempBuf[32];
    char humBuf[32];
    char co2Buf[16];

    // guard against NaN/Inf so JSON stays valid
    if (isnan(sensorTemp) || isinf(sensorTemp)) strncpy(tempBuf, "null", sizeof(tempBuf));
    else snprintf(tempBuf, sizeof(tempBuf), "%.1f", sensorTemp);

    if (isnan(sensorHum) || isinf(sensorHum)) strncpy(humBuf, "null", sizeof(humBuf));
    else snprintf(humBuf, sizeof(humBuf), "%.1f", sensorHum);

    snprintf(co2Buf, sizeof(co2Buf), "%u", sensorCO2);

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
        "\"ldr_ok\":%s"
        "}",
        tempBuf,
        humBuf,
        co2Buf,
        co2Label(sensorCO2),
        alertTemp  ? "true" : "false", alertTemp  ? 1 : 0,
        alertHum   ? "true" : "false", alertHum   ? 1 : 0,
        alertCO2   ? "true" : "false", alertCO2   ? 1 : 0,
        sensorOK   ? "true" : "false",
        ldrLightOn ? "true" : "false", ldrLightOnNum(),
        ldrOK      ? "true" : "false"
    );
    server.send(200, "application/json", buf);
}

static void handleSetThresh() {
    bool fromApi = server.hasArg("from_api") && server.arg("from_api") == "1";

    if (server.hasArg("temp"))     threshTemp    = server.arg("temp").toFloat();
    if (server.hasArg("temp_low")) threshTempLow = server.arg("temp_low").toFloat();
    if (server.hasArg("hum"))      threshHum     = server.arg("hum").toFloat();
    if (server.hasArg("hum_low"))  threshHumLow  = server.arg("hum_low").toFloat();
    if (server.hasArg("co2"))      threshCO2     = server.arg("co2").toFloat();  // lowercase co2

    saveThresholdsToNVS(threshTemp, threshTempLow, threshHum, threshHumLow, threshCO2);
    Serial.printf("[Thresh] Saved — TempH:%.1f TempL:%.1f HumH:%.1f HumL:%.1f CO2:%.0f\n",
                  threshTemp, threshTempLow, threshHum, threshHumLow, threshCO2);

    alertTemp = (sensorTemp > threshTemp) || (sensorTemp < threshTempLow);
    alertHum  = (sensorHum  > threshHum)  || (sensorHum  < threshHumLow);
    alertCO2  = (sensorCO2  > threshCO2);

    if (localMqttIsConnected()) {
        localMqttPublishConfig(threshTemp, threshTempLow, threshHum, threshHumLow, threshCO2);
    }

    if (!fromApi && WiFi.isConnected()) {
        String deviceId = getTelemetryDeviceId();
        String apiUrl   = "http://192.168.0.16:8000/api/thresholds/" + deviceId;

        StaticJsonDocument<256> doc;
        doc["device_id"] = deviceId;
        JsonObject thresh = doc.createNestedObject("thresholds");
        thresh["temp_high"] = threshTemp;
        thresh["temp_low"]  = threshTempLow;
        thresh["hum_high"]  = threshHum;
        thresh["hum_low"]   = threshHumLow;
        thresh["co2_high"]  = threshCO2;

        String payload;
        serializeJson(doc, payload);

        HTTPClient http;
        http.begin(apiUrl);
        http.addHeader("Content-Type", "application/json");
        int httpCode = http.POST(payload);
        Serial.printf("[Thresh] API update HTTP %d\n", httpCode);
        http.end();
    }

    if (mqttIsConnected()) mqttPublishAttributes();
    server.send(200, "text/plain", "OK");
}

static void handleGetThresh() {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"temp\":%.2f,\"temp_low\":%.2f,\"hum\":%.2f,\"hum_low\":%.2f,\"co2\":%.0f}",
             threshTemp, threshTempLow, threshHum, threshHumLow, threshCO2);
    server.send(200, "application/json", buf);
}

static void handleFactoryReset() {
    Serial.println("[Reset] Web factory reset — clearing all NVS...");
    const char* namespaces[] = { "device", "netcfg", "thresholds", "provision", nullptr };
    for (int i = 0; namespaces[i] != nullptr; i++) {
        Preferences p;
        p.begin(namespaces[i], false);
        p.clear();
        p.end();
        Serial.printf("[Reset] Cleared: %s\n", namespaces[i]);
    }
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

static void handleDiscover() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", DISCOVER_PAGE);
}

static void handleLogs() {
    addCorsHeaders();
    uint32_t count = _logHead < LOG_BUF_SIZE ? _logHead : LOG_BUF_SIZE;
    uint32_t start = _logHead >= LOG_BUF_SIZE ? _logHead % LOG_BUF_SIZE : 0;
    String json = "{\"seq\":" + String(_logHead) + ",\"lines\":[";
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) json += ",";
        String line = _logBuf[(start + i) % LOG_BUF_SIZE];
        line.replace("\\", "\\\\");
        line.replace("\"", "\\\"");
        json += "\"" + line + "\"";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

static void handleRegister() {
    bool wifiOk = registerDevice();
    if (wifiOk) {
        String ip       = WiFi.localIP().toString();
        String mac      = WiFi.macAddress();
        mac.replace(":", "");
        mac.toLowerCase();
        String hostname = String(MDNS_PREFIX) + "-" + mac.substring(8);
        String json     = "{\"wifi\":true,\"broker\":" +
                          String(localMqttIsConnected() ? "true" : "false") +
                          ",\"ip\":\"" + ip + "\",\"mdns\":\"" + hostname + ".local\"}";
        server.send(200, "application/json", json);
    } else {
        server.send(200, "application/json", "{\"wifi\":false,\"broker\":false,\"ip\":\"\",\"mdns\":\"\"}");
    }
}

static void handleSetBroker() {
    if (!server.hasArg("ip")) { server.send(400, "application/json", "{\"status\":\"error\"}"); return; }
    String ip     = server.arg("ip");
    uint16_t port = server.hasArg("port") ? (uint16_t)server.arg("port").toInt() : 1883;
    localMqttSetBroker(ip, port);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleBrokerStatus() {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"connected\":%s,\"ip\":\"%s\",\"port\":%d}",
             localMqttIsConnected()        ? "true" : "false",
             localMqttGetBrokerIP().c_str(),
             localMqttGetBrokerPort());
    server.send(200, "application/json", buf);
}

// Sensor type for UI display purposes (1=environment, 2=soil, 3=mineral)
static void handleGetSensorType() {
    uint8_t t = localMqttGetSensorType();
    char buf[80];
    const char* labels[] = { "", "environment", "soil", "mineral" };
    snprintf(buf, sizeof(buf),
             "{\"sensor_type\":%d,\"label\":\"%s\"}",
             t, (t >= 1 && t <= 3) ? labels[t] : "environment");
    server.send(200, "application/json", buf);
}

static void handleSetSensorType() {
    if (!server.hasArg("type")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing type\"}");
        return;
    }
    uint8_t t = (uint8_t)server.arg("type").toInt();
    if (t < 1 || t > 3) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"type must be 1, 2 or 3\"}");
        return;
    }
    localMqttSetSensorType(t);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ── Server Init — ALL routes registered before server.begin() ─────────────────
void webServerInit() {
    server.on("/",                          handleRoot);
    server.on("/discover",    HTTP_GET,     handleDiscover);

    // Captive portal
    server.on("/hotspot-detect.html",       handleCaptivePortal);
    server.on("/library/test/success.html", handleCaptivePortal);
    server.on("/success.txt",               handleCaptivePortal);
    server.on("/generate_204",              handleCaptivePortal);
    server.on("/gen_204",                   handleCaptivePortal);
    server.onNotFound(handleCaptivePortal);

    // API
    server.on("/sensors",       handleSensors);
    server.on("/wifi",          HTTP_GET,  handleWifiGet);
    server.on("/scan",          HTTP_GET,  handleScan);
    server.on("/set_wifi",      HTTP_POST, handleSetWifi);
    server.on("/prov_status",   HTTP_GET,  handleProvStatus);
    server.on("/provision",     HTTP_POST, handleProvision);
    server.on("/device_info",   HTTP_GET,  handleDeviceInfo);
    server.on("/set_token",     HTTP_POST, handleSetToken);
    server.on("/set_thresh",               handleSetThresh);
    server.on("/get_thresh",    HTTP_GET,  handleGetThresh);
    server.on("/factory_reset", HTTP_POST, handleFactoryReset);
    server.on("/register",      HTTP_POST, handleRegister);
    server.on("/set_broker",    HTTP_POST, handleSetBroker);
    server.on("/broker_status", HTTP_GET,  handleBrokerStatus);
    server.on("/get_sensor_type", HTTP_GET,  handleGetSensorType);
    server.on("/set_sensor_type", HTTP_POST, handleSetSensorType);
    server.on("/logs",          HTTP_GET,  handleLogs);

    loadThresholdsFromNVS();
    server.begin();
    Serial.println("[Web] Server started");
}

void webServerHandle() {
    server.handleClient();
}