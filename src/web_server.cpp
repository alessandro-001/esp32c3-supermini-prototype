#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "config.h"
#include "secrets.h"
#include "sensors.h"        // covers SHTC3, ENS160 and LDR — no need for direct sensor includes
#include "wifi_config.h"
#include "provisioning.h"
#include "mqtt.h"
#include "local_mqtt.h"


WebServer server(80); // HTTP server on port 80

//* Wi-Fi Configuration Implementation + Web Server Endpoints + UI

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BOSS FARM — Device Setup</title>
  <style>
    :root {
      --bg:      #0a0c0f;
      --surface: #111418;
      --border:  #1e2530;
      --accent:  #00e5ff;
      --green:   #00e676;
      --yellow:  #ffd740;
      --red:     #ff1744;
      --muted:   #4a5568;
      --text:    #e2e8f0;
      --mono:    'Courier New', 'Lucida Console', monospace;
      --sans:    'Trebuchet MS', 'Segoe UI', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body { width: 100%; min-height: 100%; }
    body {
      font-family: var(--sans);
      background: var(--bg);
      color: var(--text);
      max-width: 520px;
      width: 100vw;
      margin: 0 auto;
      padding: 20px 16px 40px;
      position: relative;
    }
    body::before {
      content: '';
      position: fixed;
      inset: 0;
      background-image:
        linear-gradient(rgba(0,229,255,0.03) 1px, transparent 1px),
        linear-gradient(90deg, rgba(0,229,255,0.03) 1px, transparent 1px);
      background-size: 40px 40px;
      pointer-events: none;
      z-index: 0;
    }
    .page { position: relative; z-index: 1; }

    /* ── header ── */
    .page-header {
      border-bottom: 1px solid var(--border);
      padding-bottom: 16px;
      margin-bottom: 24px;
    }
    .page-header .label {
      font-family: var(--mono);
      font-size: 0.65rem;
      color: var(--accent);
      letter-spacing: 0.2em;
      text-transform: uppercase;
      margin-bottom: 4px;
    }
    .page-header h1 {
      font-size: 1.5rem;
      font-weight: 700;
      letter-spacing: -0.02em;
    }
    .page-header h1 span { color: var(--accent); }

    /* ── cards ── */
    .card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 18px;
      margin-bottom: 16px;
      transition: border-color 0.2s;
    }
    .card-header {
      display: flex;
      align-items: center;
      gap: 10px;
      margin-bottom: 14px;
    }
    .card-icon { font-size: 1.3rem; line-height: 1; }
    .card-title {
      font-size: 0.8rem;
      font-weight: 700;
      letter-spacing: 0.1em;
      text-transform: uppercase;
      color: var(--muted);
      font-family: var(--mono);
    }

    /* ── rows inside cards ── */
    .row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 9px 0;
      border-bottom: 1px solid var(--border);
      font-size: 0.9rem;
    }
    .row:last-child { border-bottom: none; }
    .row-label { color: var(--muted); font-size: 0.85rem; }
    .val {
      font-family: var(--mono);
      font-size: 1rem;
      font-weight: 600;
      color: var(--accent);
      word-break: break-all;
    }

    /* ── sensor grid (2-col inside sensor card) ── */
    .sensor-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }
    .sensor-tile {
      background: var(--bg);
      border: 1px solid var(--border);
      border-radius: 7px;
      padding: 10px 12px;
    }
    .sensor-tile.full { grid-column: 1 / -1; }
    .sensor-tile-label {
      font-family: var(--mono);
      font-size: 0.62rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.08em;
      margin-bottom: 3px;
    }
    .sensor-tile-val {
      font-family: var(--mono);
      font-size: 1rem;
      font-weight: 600;
      color: var(--text);
    }
    .sensor-tile-val.light-on  { color: var(--yellow); }
    .sensor-tile-val.light-off { color: var(--muted); }
    .sensor-tile-val.aqi-1 { color: var(--green); }
    .sensor-tile-val.aqi-2 { color: #a8e063; }
    .sensor-tile-val.aqi-3 { color: var(--yellow); }
    .sensor-tile-val.aqi-4 { color: #ff9100; }
    .sensor-tile-val.aqi-5 { color: var(--red); }

    /* ── buttons ── */
    button {
      width: 100%;
      padding: 12px;
      margin-top: 12px;
      border: none;
      border-radius: 7px;
      font-family: var(--sans);
      font-size: 0.95rem;
      font-weight: 700;
      cursor: pointer;
      letter-spacing: 0.02em;
      transition: opacity 0.15s, transform 0.1s;
    }
    button:active { transform: scale(0.98); }
    button:disabled { opacity: 0.4; cursor: not-allowed; }
    button.btn-primary  { background: var(--accent); color: #000; }
    button.btn-wifi     { background: #1a3a6e; color: var(--accent); border: 1px solid var(--accent); }
    button.btn-save     { background: rgba(0,230,118,0.12); color: var(--green); border: 1px solid rgba(0,230,118,0.3); }

    /* ── inputs ── */
    label {
      display: block;
      font-family: var(--mono);
      font-size: 0.68rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.1em;
      margin: 12px 0 5px;
    }
    input {
      width: 100%;
      padding: 10px 12px;
      background: var(--bg);
      border: 1px solid var(--border);
      border-radius: 7px;
      color: var(--text);
      font-family: var(--mono);
      font-size: 0.95rem;
      outline: none;
      transition: border-color 0.2s;
    }
    input:focus { border-color: var(--accent); }

    /* ── network list ── */
    .net {
      padding: 10px 12px;
      margin: 5px 0;
      background: var(--bg);
      border: 1px solid var(--border);
      border-radius: 7px;
      cursor: pointer;
      font-family: var(--mono);
      font-size: 0.85rem;
      word-break: break-all;
      transition: border-color 0.15s;
    }
    .net:hover { border-color: var(--accent); color: var(--accent); }
    .hidden { display: none !important; }

    /* ── threshold grid ── */
    .thresh-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }

    /* ── status badge ── */
    .badge {
      font-family: var(--mono);
      font-size: 0.7rem;
      padding: 3px 10px;
      border-radius: 20px;
      border: 1px solid var(--border);
    }
    .badge.active  { background: rgba(0,230,118,0.1); color: var(--green); border-color: rgba(0,230,118,0.3); }
    .badge.inactive { background: rgba(255,23,68,0.1); color: var(--red); border-color: rgba(255,23,68,0.3); }

    /* ── toast ── */
    #popup-msg {
      position: fixed;
      top: -60px;
      left: 0;
      width: 100%;
      z-index: 9999;
      text-align: center;
      padding: 15px 0;
      font-family: var(--mono);
      font-size: 0.9rem;
      font-weight: 600;
      transition: top 0.4s cubic-bezier(.4,2,.6,1);
      border-bottom: 2px solid var(--accent);
      background: var(--surface);
      color: var(--text);
    }
    #popup-msg.success { background: #0d2b1a; color: var(--green); border-bottom-color: var(--green); }
    #popup-msg.error   { background: #2b0d0d; color: var(--red);   border-bottom-color: var(--red);   }
    #popup-msg.info    { background: #0d1a2b; color: var(--accent); border-bottom-color: var(--accent); }

    @media (max-width: 420px) {
      .sensor-grid, .thresh-grid { grid-template-columns: 1fr; }
      .sensor-tile.full { grid-column: 1; }
    }
  </style>
</head>
<body>
<div id="popup-msg"></div>
<div class="page">

  <!-- Header -->
  <div class="page-header">
    <div class="label">// device setup</div>
    <h1>BOSS FARM <span>MONITOR</span></h1>
  </div>

  <!-- Device Info -->
  <div class="card" id="step1">
    <div class="card-header">
      <span class="card-icon">🔌</span>
      <span class="card-title">Device Information</span>
    </div>
    <div class="row">
      <span class="row-label">Device ID</span>
      <span class="val" id="device-id">–</span>
    </div>
    <div class="row">
      <span class="row-label">Device Name</span>
      <span class="val" id="device-name">–</span>
    </div>
    <div class="row">
      <span class="row-label">Cloud Status</span>
      <span id="prov-status"><span class="badge">Checking...</span></span>
    </div>
    <div id="register-row">
      <button class="btn-primary" id="register-btn" onclick="registerDevice()">Register Device</button>
    </div>
    <div id="prov-msg"></div>
  </div>

  <!-- Live Sensor Data — all sensors in one card -->
  <div class="card" id="step4">
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
        <div class="sensor-tile-label">🌬 AQI</div>
        <div class="sensor-tile-val" id="aqi">– <span id="aqi-label" style="font-size:0.75rem;color:var(--muted);font-weight:400;"></span></div>
      </div>
      <div class="sensor-tile">
        <div class="sensor-tile-label">💨 TVOC</div>
        <div class="sensor-tile-val" id="tvoc">– <span style="font-size:0.7rem;color:var(--muted);">ppb</span></div>
      </div>
      <div class="sensor-tile">
        <div class="sensor-tile-label">💨 eCO2</div>
        <div class="sensor-tile-val" id="eco2">– <span style="font-size:0.7rem;color:var(--muted);">ppm</span></div>
      </div>
      <div class="sensor-tile full">
        <div class="sensor-tile-label">📊 Air Status</div>
        <div class="sensor-tile-val" id="aqi-status" style="color:var(--muted);">–</div>
      </div>
    </div>
  </div>

  <!-- WiFi Connection -->
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
      <button class="btn-wifi" onclick="scanWifi()">🔍 Scan Networks</button>
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

  <!-- Alert Thresholds -->
  <div class="card" id="step-thresh">
    <div class="card-header">
      <span class="card-icon">⚠️</span>
      <span class="card-title">Alert Thresholds</span>
    </div>
    <div class="thresh-grid">
      <div>
        <label>🌡 Max Temp (°C)</label>
        <input type="number" id="thresh-temp" step="0.5" min="-40" max="125" placeholder="30">
      </div>
      <div>
        <label>💧 Max Humidity (%)</label>
        <input type="number" id="thresh-hum" step="1" min="0" max="100" placeholder="80">
      </div>
      <div>
        <label>🌬 Max TVOC (ppb)</label>
        <input type="number" id="thresh-tvoc" step="50" min="0" max="65000" placeholder="500">
      </div>
      <div>
        <label>💨 Max eCO2 (ppm)</label>
        <input type="number" id="thresh-eco2" step="50" min="400" max="65000" placeholder="1000">
      </div>
    </div>
    <button class="btn-save" onclick="saveThresholds()">💾 Save Thresholds</button>
    <div id="thresh-msg"></div>
  </div>

</div><!-- /page -->

<script>
let selectedSecure = false; // track if selected network is secure (shows password field)

function loadThresholds() {
  fetch('/get_thresh')
    .then(r => r.json())
    .then(th => {
      document.getElementById('thresh-temp').value = th.temp;
      document.getElementById('thresh-hum').value  = th.hum;
      document.getElementById('thresh-tvoc').value = th.tvoc;
      document.getElementById('thresh-eco2').value = th.eco2;
    });
}

function checkProvStatus() {
  fetch('/prov_status')
    .then(r => r.json())
    .then(res => {
      const statusEl = document.getElementById('prov-status');
      const registerRow = document.getElementById('register-row');
      if (res.provisioned) {
        statusEl.innerHTML = '<span class="badge active">Active ✅</span>';
        registerRow.style.display = 'none';
      } else {
        statusEl.innerHTML = '<span class="badge inactive">Not registered ❌</span>';
        registerRow.style.display = '';
      }
    })
    .catch(() => {
      document.getElementById('prov-status').textContent = 'Unknown';
    });
}

function saveThresholds() {
  const temp = parseFloat(document.getElementById("thresh-temp").value);
  const hum  = parseFloat(document.getElementById("thresh-hum").value);
  const tvoc = parseFloat(document.getElementById("thresh-tvoc").value);
  const eco2 = parseFloat(document.getElementById("thresh-eco2").value);
  if (isNaN(temp) || isNaN(hum) || isNaN(tvoc) || isNaN(eco2)) {
    showMsg("Enter valid values for all thresholds", "error");
    return;
  }
  showMsg("Saving thresholds...", "info");
  fetch("/set_thresh?temp=" + temp + "&hum=" + hum + "&tvoc=" + tvoc + "&eco2=" + eco2)
    .then(r => r.text())
    .then(() => showMsg("✓ Thresholds saved!", "success"))
    .catch(() => showMsg("Failed to save", "error"));
}

function registerDevice() {
  const btn = document.getElementById('register-btn');
  btn.disabled = true;
  showMsg('Registering device...', 'info');
  fetch('/provision', { method: 'POST' })
    .then(r => r.json())
    .then(res => {
      if (res.status === 'ok') {
        showMsg('Device registered! Connecting to ThingsBoard...', 'success');
        checkProvStatus();
      } else {
        showMsg(res.message || 'Provisioning failed', 'error');
        btn.disabled = false;
      }
      updateStatus();
    })
    .catch(() => {
      showMsg('Provisioning failed', 'error');
      btn.disabled = false;
    });
}

function scanWifi() {
  const nets = document.getElementById('networks');
  nets.innerHTML = 'Scanning...';
  fetch('/scan').then(r => r.json()).then(list => {
    nets.innerHTML = list.length ? list.map(n => `<div class='net' onclick='selectNet("${n.ssid.replace(/'/g, "\\'").replace(/"/g, '\\"')}",${n.secure})'>${n.secure ? "🔒" : "🔓"} ${n.ssid} (${n.rssi} dBm)</div>`).join('') : 'No networks found';
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
    body: 'ssid=' + encodeURIComponent(ssid) +
          '&pass=' + encodeURIComponent(pass) +
          '&secure=' + (selectedSecure ? 'true' : 'false')
  }).then(r => r.text()).then(t => {
    showMsg(t, t.includes('Connected') ? 'success' : 'error');
    if (t.includes('Connected')) {
      document.getElementById('wifi-form').classList.add('hidden');
      document.getElementById('networks').innerHTML = '';
    }
    updateStatus();
  }).catch(() => showMsg('Connection failed', 'error'));
}

function updateStatus() {
  fetch('/wifi').then(r => r.json()).then(w => {
    document.getElementById('curr-wifi').textContent = w.ssid || '–';
    document.getElementById('conn-status').textContent = w.connected ? '✓ Connected' : '✗ Not connected';
    document.getElementById('conn-status').style.color = w.connected ? '#00e676' : '#ff1744';
  }).catch(() => {});
  fetch('/device_info').then(r => r.json()).then(info => {
    document.getElementById('device-id').textContent = info.device_id || '–';
    document.getElementById('device-name').textContent = info.device_name || '–';
  }).catch(() => {});
}

function aqiColor(aqi) {
  const colors = { 1:'#2ecc71', 2:'#a8e063', 3:'#f39c12', 4:'#e67e22', 5:'#e74c3c' };
  return colors[aqi] || '#aaa';
}

function pollSensors() {
  fetch('/sensors').then(r => r.json()).then(d => {
    // ── SHTC3 ────────────────────────────────────────────────────────────────
    const tempEl = document.getElementById('temp');
    const humEl  = document.getElementById('hum');
    if (d.shtc3_ok) {
      tempEl.textContent = d.temp.toFixed(1) + ' °C';
      tempEl.style.color = '';
      humEl.textContent  = d.hum.toFixed(1) + ' %';
      humEl.style.color  = '';
    } else {
      tempEl.textContent = 'N/A'; tempEl.style.color = 'var(--muted)';
      humEl.textContent  = 'N/A'; humEl.style.color  = 'var(--muted)';
    }
    // ── LDR ──────────────────────────────────────────────────────────────────
    const lightEl = document.getElementById('light');
    if (d.ldr_ok) {
      lightEl.textContent = d.light_on ? 'ON' : 'OFF';
      lightEl.className = 'sensor-tile-val ' + (d.light_on ? 'light-on' : 'light-off');
      lightEl.style.color = '';
    } else {
      lightEl.textContent = 'N/A';
      lightEl.className = 'sensor-tile-val';
      lightEl.style.color = 'var(--muted)';
    }
    // ── ENS160 ───────────────────────────────────────────────────────────────
    if (d.ens160_ok && d.aqi >= 1) {
      const aqiEl = document.getElementById('aqi');
      aqiEl.innerHTML = d.aqi + ' <span id="aqi-label" style="font-size:0.75rem;color:var(--muted);font-weight:400;">' + (d.aqi_label || '') + '</span>';
      aqiEl.className = 'sensor-tile-val aqi-' + d.aqi;
      document.getElementById('tvoc').innerHTML = d.tvoc + ' <span style="font-size:0.7rem;color:var(--muted);">ppb</span>';
      document.getElementById('eco2').innerHTML = d.eco2 + ' <span style="font-size:0.7rem;color:var(--muted);">ppm</span>';
      const statusEl = document.getElementById('aqi-status');
      statusEl.textContent = d.aqi_status || '–';
      statusEl.style.color = d.aqi_status === 'Normal' ? '#00e676' : 'var(--muted)';
    } else {
      ['aqi', 'tvoc', 'eco2', 'aqi-status'].forEach(id => {
        const el = document.getElementById(id);
        el.textContent = 'N/A';
        el.className = 'sensor-tile-val';
        el.style.color = 'var(--muted)';
      });
    }
  });
}

function showMsg(txt, type) {
  const el = document.getElementById('popup-msg');
  el.textContent = txt;
  el.className = type || '';
  el.style.top = '0';
  clearTimeout(el._hideTimer);
  el._hideTimer = setTimeout(() => {
    el.style.top = '-60px';
    el.className = '';
  }, 5000);
}

updateStatus();
loadThresholds();
checkProvStatus();
pollSensors();
setInterval(pollSensors, 3000);
setInterval(updateStatus, 5000);
</script>
</body>
</html>
)rawliteral";

// ── Device Discovery Page ─────────────────────────────────────────────────────
// Served at /discover — avoids file:// CORS issues by running from a real HTTP origin
const char DISCOVER_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BOSS FARM — Device Discovery</title>
  <style>
    :root {
      --bg:      #0a0c0f;
      --surface: #111418;
      --border:  #1e2530;
      --accent:  #00e5ff;
      --green:   #00e676;
      --yellow:  #ffd740;
      --red:     #ff1744;
      --muted:   #4a5568;
      --text:    #e2e8f0;
      --mono:    'Courier New', 'Lucida Console', monospace;
      --sans:    'Trebuchet MS', 'Segoe UI', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: var(--bg); color: var(--text); font-family: var(--sans); min-height: 100vh; overflow-x: hidden; }
    body::before {
      content: ''; position: fixed; inset: 0;
      background-image: linear-gradient(rgba(0,229,255,0.03) 1px,transparent 1px), linear-gradient(90deg,rgba(0,229,255,0.03) 1px,transparent 1px);
      background-size: 40px 40px; pointer-events: none; z-index: 0;
    }
    .wrap { position: relative; z-index: 1; max-width: 960px; margin: 0 auto; padding: 40px 20px; }
    header { display: flex; align-items: flex-end; justify-content: space-between; border-bottom: 1px solid var(--border); padding-bottom: 24px; margin-bottom: 36px; flex-wrap: wrap; gap: 16px; }
    .logo-block .label { font-family: var(--mono); font-size: 0.7rem; color: var(--accent); letter-spacing: 0.2em; text-transform: uppercase; margin-bottom: 6px; }
    .logo-block h1 { font-size: 2rem; font-weight: 700; letter-spacing: -0.02em; line-height: 1; }
    .logo-block h1 span { color: var(--accent); }
    .status-pill { font-family: var(--mono); font-size: 0.75rem; padding: 6px 14px; border-radius: 20px; border: 1px solid var(--border); color: var(--muted); display: flex; align-items: center; gap: 8px; }
    .status-pill .dot { width: 7px; height: 7px; border-radius: 50%; background: var(--muted); transition: background 0.3s; }
    .status-pill.scanning .dot { background: var(--yellow); animation: pulse 0.8s infinite; }
    .status-pill.done .dot { background: var(--green); }
    @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.3} }
    .controls { display: flex; gap: 12px; margin-bottom: 32px; flex-wrap: wrap; align-items: flex-end; }
    .field { display: flex; flex-direction: column; gap: 6px; flex: 1; min-width: 180px; }
    .field label { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); letter-spacing: 0.1em; text-transform: uppercase; }
    .field input { background: var(--surface); border: 1px solid var(--border); border-radius: 6px; padding: 10px 14px; font-family: var(--mono); font-size: 0.95rem; color: var(--text); outline: none; transition: border-color 0.2s; width: 100%; }
    .field input:focus { border-color: var(--accent); }
    .btn { padding: 10px 28px; border-radius: 6px; border: none; font-family: var(--sans); font-size: 0.95rem; font-weight: 600; cursor: pointer; letter-spacing: 0.02em; transition: opacity 0.2s, transform 0.1s; white-space: nowrap; height: 42px; }
    .btn:active { transform: scale(0.97); }
    .btn:disabled { opacity: 0.4; cursor: not-allowed; }
    .btn-primary { background: var(--accent); color: #000; }
    .btn-ghost { background: transparent; border: 1px solid var(--border); color: var(--muted); }
    .progress-wrap { height: 2px; background: var(--border); border-radius: 2px; margin-bottom: 32px; overflow: hidden; display: none; }
    .progress-wrap.active { display: block; }
    .progress-bar { height: 100%; background: var(--accent); width: 0%; transition: width 0.3s; border-radius: 2px; }
    .stats { display: flex; gap: 24px; margin-bottom: 28px; font-family: var(--mono); font-size: 0.78rem; color: var(--muted); }
    .stats span { color: var(--text); }
    .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 16px; }
    .card { background: var(--surface); border: 1px solid var(--border); border-radius: 10px; padding: 20px; cursor: pointer; transition: border-color 0.2s, transform 0.15s, box-shadow 0.2s; text-decoration: none; color: inherit; display: block; animation: fadeIn 0.3s ease both; }
    .card:hover { border-color: var(--accent); transform: translateY(-2px); box-shadow: 0 8px 32px rgba(0,229,255,0.08); }
    @keyframes fadeIn { from{opacity:0;transform:translateY(8px)} to{opacity:1;transform:translateY(0)} }
    .card-header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 16px; }
    .device-name { font-weight: 600; font-size: 1rem; line-height: 1.3; }
    .device-id { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); margin-top: 3px; }
    .online-badge { font-family: var(--mono); font-size: 0.65rem; padding: 3px 8px; border-radius: 10px; background: rgba(0,230,118,0.12); color: var(--green); border: 1px solid rgba(0,230,118,0.25); white-space: nowrap; }
    .card-ip { font-family: var(--mono); font-size: 0.78rem; color: var(--accent); margin-bottom: 16px; }
    .sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-bottom: 16px; }
    .sensor-item { background: var(--bg); border: 1px solid var(--border); border-radius: 6px; padding: 8px 10px; }
    .sensor-item.full-width { grid-column: 1 / -1; }
    .sensor-label { font-size: 0.65rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 2px; }
    .sensor-val { font-family: var(--mono); font-size: 0.9rem; font-weight: 600; color: var(--text); }
    .sensor-val.light-on { color: var(--yellow); }
    .sensor-val.light-off { color: var(--muted); }
    .sensor-val.aqi-1 { color: var(--green); }
    .sensor-val.aqi-2 { color: #a8e063; }
    .sensor-val.aqi-3 { color: var(--yellow); }
    .sensor-val.aqi-4 { color: #ff9100; }
    .sensor-val.aqi-5 { color: var(--red); }
    .card-footer { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); display: flex; justify-content: space-between; border-top: 1px solid var(--border); padding-top: 12px; margin-top: 4px; }
    .empty { grid-column: 1 / -1; text-align: center; padding: 60px 20px; color: var(--muted); }
    .empty .icon { font-size: 3rem; margin-bottom: 16px; opacity: 0.4; }
    .empty p { font-size: 0.9rem; line-height: 1.7; }
    @media (max-width: 500px) { .logo-block h1 { font-size: 1.5rem; } .controls { flex-direction: column; } .btn { width: 100%; } }
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
    <div class="field" style="max-width:100px;">
      <label>From</label>
      <input type="number" id="range-from" value="1" min="1" max="254">
    </div>
    <div class="field" style="max-width:100px;">
      <label>To</label>
      <input type="number" id="range-to" value="254" min="1" max="254">
    </div>
    <div class="field" style="max-width:100px;">
      <label>Timeout (ms)</label>
      <input type="number" id="timeout" value="800" min="200" max="3000" step="100">
    </div>
    <button class="btn btn-primary" id="scan-btn" onclick="startScan()">Scan Network</button>
    <button class="btn btn-ghost" id="stop-btn" onclick="stopScan()" disabled>Stop</button>
  </div>
  <div class="progress-wrap" id="progress-wrap">
    <div class="progress-bar" id="progress-bar"></div>
  </div>
  <div class="stats" id="stats" style="display:none">
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
<script>
let scanning=false,stopFlag=false,foundCount=0,scanStart=0,statsTimer=null;
function setStatus(text,state){
  const pill=document.getElementById('status-pill'),txt=document.getElementById('status-text');
  pill.className='status-pill '+(state||'');txt.textContent=text;
}
function aqiClass(aqi){if(!aqi||aqi<1||aqi>5)return '';return 'aqi-'+aqi;}
function formatSensorVal(d){
  const items=[];
  if(d.temp!==undefined&&d.temp!==null)items.push({label:'🌡 Temp',val:d.temp.toFixed(1)+' °C',cls:''});
  if(d.hum!==undefined&&d.hum!==null)items.push({label:'💧 Humidity',val:d.hum.toFixed(1)+' %',cls:''});
  if(d.aqi!==undefined&&d.aqi>=1&&d.aqi<=5)items.push({label:'🌬 AQI',val:d.aqi+' — '+(d.aqi_label||''),cls:aqiClass(d.aqi)});
  if(d.tvoc!==undefined&&d.tvoc>=0)items.push({label:'💨 TVOC',val:d.tvoc+' ppb',cls:''});
  if(d.eco2!==undefined&&d.eco2>=400)items.push({label:'💨 eCO2',val:d.eco2+' ppm',cls:''});
  if(d.light_on!==undefined)items.push({label:'💡 Light',val:d.light_on?'ON':'OFF',cls:d.light_on?'light-on':'light-off'});
  if(d.aqi_status&&d.aqi_status!=='Initialising'&&d.aqi_status!=='Error')items.push({label:'📊 Air Status',val:d.aqi_status,cls:''});
  return items;
}
function renderCard(ip,info,sensors){
  const sensorItems=formatSensorVal(sensors);
  const sensorsHtml=sensorItems.length
    ?sensorItems.map((s,i)=>{
      const fw=s.label.includes('Air Status')||(sensorItems.length%2!==0&&i===sensorItems.length-1);
      return '<div class="sensor-item'+(fw?' full-width':'')+'"><div class="sensor-label">'+s.label+'</div><div class="sensor-val '+s.cls+'">'+s.val+'</div></div>';
    }).join('')
    :'<div class="sensor-item full-width"><div class="sensor-label">Sensors</div><div class="sensor-val" style="color:var(--muted)">No data</div></div>';
  const card=document.createElement('a');
  card.className='card';card.href='http://'+ip;card.target='_blank';
  card.innerHTML='<div class="card-header"><div><div class="device-name">'+(info.device_name||'ESP32 Device')+'</div><div class="device-id">'+(info.device_id||'–')+'</div></div><span class="online-badge">● ONLINE</span></div><div class="card-ip">'+ip+'</div><div class="sensors">'+sensorsHtml+'</div><div class="card-footer"><span>fw '+(sensors.firmware||'–')+'</span><span style="color:var(--accent)">Open UI →</span></div>';
  return card;
}
async function fetchWithTimeout(url,ms){
  const ctrl=new AbortController(),tid=setTimeout(()=>ctrl.abort(),ms);
  try{const r=await fetch(url,{signal:ctrl.signal});clearTimeout(tid);return r;}
  catch{clearTimeout(tid);return null;}
}
async function probeIp(ip,timeoutMs){
  const infoRes=await fetchWithTimeout('http://'+ip+'/device_info',timeoutMs);
  if(!infoRes||!infoRes.ok)return null;
  let info={};
  try{info=await infoRes.json();}catch{return null;}
  if(!info.device_name||!info.device_name.startsWith('ESP32'))return null;
  let sensors={};
  const sensRes=await fetchWithTimeout('http://'+ip+'/sensors',timeoutMs);
  if(sensRes&&sensRes.ok){try{sensors=await sensRes.json();}catch{}}
  return{ip,info,sensors};
}
async function startScan(){
  if(scanning)return;
  const subnet=document.getElementById('subnet').value.trim();
  const from=parseInt(document.getElementById('range-from').value);
  const to=parseInt(document.getElementById('range-to').value);
  const timeout=parseInt(document.getElementById('timeout').value);
  if(!subnet||isNaN(from)||isNaN(to)||from>to){alert('Please check your subnet and range values.');return;}
  scanning=true;stopFlag=false;foundCount=0;scanStart=Date.now();
  const total=to-from+1;let scanned=0;
  document.getElementById('scan-btn').disabled=true;
  document.getElementById('stop-btn').disabled=false;
  document.getElementById('progress-wrap').classList.add('active');
  document.getElementById('stats').style.display='flex';
  document.getElementById('stat-total').textContent=total;
  document.getElementById('stat-scanned').textContent=0;
  document.getElementById('stat-found').textContent=0;
  document.getElementById('grid').innerHTML='';
  setStatus('Scanning...','scanning');
  statsTimer=setInterval(()=>{document.getElementById('stat-elapsed').textContent=((Date.now()-scanStart)/1000).toFixed(1)+'s';},200);
  const ips=[];for(let i=from;i<=to;i++)ips.push(subnet+'.'+i);
  const BATCH=30;
  for(let b=0;b<ips.length&&!stopFlag;b+=BATCH){
    const batch=ips.slice(b,b+BATCH);
    const results=await Promise.all(batch.map(ip=>probeIp(ip,timeout)));
    for(const result of results){
      scanned++;
      if(result){foundCount++;document.getElementById('stat-found').textContent=foundCount;document.getElementById('grid').appendChild(renderCard(result.ip,result.info,result.sensors));}
    }
    document.getElementById('stat-scanned').textContent=scanned;
    document.getElementById('progress-bar').style.width=((scanned/total)*100).toFixed(1)+'%';
  }
  clearInterval(statsTimer);scanning=false;
  document.getElementById('scan-btn').disabled=false;
  document.getElementById('stop-btn').disabled=true;
  document.getElementById('progress-bar').style.width='100%';
  if(foundCount===0){
    document.getElementById('grid').innerHTML='<div class="empty"><div class="icon">🔍</div><p>No ESP32 Smart Monitor units found on <strong>'+subnet+'.'+from+'–'+to+'</strong>.<br>Make sure all units are connected to the same WiFi and try adjusting the subnet or timeout.</p></div>';
    setStatus('No devices found','');
  }else{setStatus('Found '+foundCount+' device'+(foundCount>1?'s':''),'done');}
}
function stopScan(){
  stopFlag=true;clearInterval(statsTimer);setStatus('Stopped','');
  document.getElementById('scan-btn').disabled=false;
  document.getElementById('stop-btn').disabled=true;
  scanning=false;
}
</script>
</body>
</html>
)rawliteral";

// ── Global threshold variables (single source of truth) ──────────────────────
float threshTemp = 30.0f;
float threshHum  = 80.0f;
float threshTvoc = 500.0f;
float threshEco2 = 1000.0f;

extern float sensorTemp;
extern float sensorHum;
extern bool  alertTemp;
extern bool  alertHum;

// ── NVS namespace (consistent across load/save) ───────────────────────────────
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

// ── Threshold persistence ─────────────────────────────────────────────────────

static void loadThresholdsFromNVS() {
    Preferences prefs;
    prefs.begin(THRESH_NVS_NS, true);
    threshTemp = prefs.getFloat("temp", 30.0f);
    threshHum  = prefs.getFloat("hum",  80.0f);
    threshTvoc = prefs.getFloat("tvoc", 500.0f);
    threshEco2 = prefs.getFloat("eco2", 1000.0f);
    prefs.end();
    Serial.printf("[Thresh] Loaded — Temp: %.1f, Hum: %.1f, TVOC: %.0f, eCO2: %.0f\n",
                  threshTemp, threshHum, threshTvoc, threshEco2);
}

static void saveThresholdsToNVS(float temp, float hum, float tvoc, float eco2) {
    Preferences prefs;
    prefs.begin(THRESH_NVS_NS, false);
    prefs.putFloat("temp", temp);
    prefs.putFloat("hum",  hum);
    prefs.putFloat("tvoc", tvoc);
    prefs.putFloat("eco2", eco2);
    prefs.end();
}
/// Returns a stable device ID for telemetry topics, based on MAC address
static String getTelemetryDeviceId() {
    // Must match the device_id currently published by local_mqtt.cpp
    return "IESWIC3A_" + WiFi.macAddress().substring(12);
}

// ── HTTP Handlers ─────────────────────────────────────────────────────────────

// ── Captive Portal — makes iPhone/Android connect without internet ────────────
static void handleCaptivePortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

static void handleRoot() {
    server.send(200, "text/html", HTML_PAGE);
}

static void handleWifiGet() {
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
        server.send(400, "text/plain", "Missing ssid/pass");
        return;
    }
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    // Don't validate password length for open networks
    bool isOpen = !server.hasArg("secure") || server.arg("secure") == "false";
    if (isOpen) pass = "";

    if (!wifiConfigSave(ssid, pass)) {
        server.send(400, "text/plain", "Invalid password (min 8 chars)");
        return;
    }
    bool ok = wifiConfigConnect(10000);
    server.send(200, "text/plain", ok ? "Connected to " + ssid : "Connection failed");
}

// ── CORS helper — required for /discover page to probe other units ────────────
static void addCorsHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET");
}

static void handleDeviceInfo() {
    addCorsHeaders(); // allows discover page to probe this unit
    char buf[128];
    String devMac  = getDeviceMacString();
    String devName = getDeviceName();
    snprintf(buf, sizeof(buf), "{\"device_id\":\"%s\",\"device_name\":\"%s\"}",
             devMac.c_str(), devName.c_str());
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
        server.send(200, "application/json",
                    "{\"status\":\"error\",\"message\":\"WiFi not connected\"}");
        return;
    }
    String token = provisioningRequest();
    if (!token.isEmpty()) {
        mqttSetToken(token);
        server.send(200, "application/json",
                    "{\"status\":\"ok\",\"message\":\"Provisioning successful\"}");
    } else {
        server.send(200, "application/json",
                    "{\"status\":\"error\",\"message\":\"Provisioning failed\"}");
    }
}

static void handleSetToken() {
    if (!server.hasArg("token")) {
        server.send(400, "application/json", "{\"status\":\"error\"}");
        return;
    }
    String token = server.arg("token");
    if (token.length() < 5) {
        server.send(400, "application/json",
                    "{\"status\":\"error\",\"message\":\"Token too short\"}");
        return;
    }
    Preferences prefs;
    prefs.begin("provision", false);
    prefs.putString("token", token);
    prefs.end();
    Serial.printf("[Web] ✅ Token saved: %.10s...\n", token.c_str());
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleSensors() {
    addCorsHeaders(); // allows discover page to fetch sensor data from this unit
    char buf[420];  // includes alert and light boolean/numeric mirrors
    snprintf(buf, sizeof(buf),
        "{"
        "\"temp\":%.1f,"
        "\"hum\":%.1f,"
        "\"alert_temp\":%s,"
        "\"alert_temp_num\":%d,"
        "\"alert_hum\":%s,"
        "\"alert_hum_num\":%d,"
        "\"shtc3_ok\":%s,"
        "\"aqi\":%d,"
        "\"aqi_label\":\"%s\","
        "\"tvoc\":%d,"
        "\"eco2\":%d,"
        "\"aqi_status\":\"%s\","
        "\"ens160_ok\":%s,"
        "\"light_on\":%s,"
        "\"light_on_num\":%d,"
        "\"ldr_ok\":%s"
        "}",
        sensorTemp,
        sensorHum,
        alertTemp ? "true" : "false",
        alertTemp ? 1 : 0,
        alertHum  ? "true" : "false",
        alertHum  ? 1 : 0,
        sensorOK   ? "true" : "false",
        ens160AQI,
        ens160AQILabel(ens160AQI),
        ens160TVOC,
        ens160eCO2,
        ens160Status.c_str(),
        ens160OK   ? "true" : "false",
        ldrLightOn ? "true" : "false",
        ldrLightOnNum(),
        ldrOK      ? "true" : "false"
    );
    server.send(200, "application/json", buf);
}

static void handleSetThresh() {
    if (server.hasArg("temp")) threshTemp = server.arg("temp").toFloat();
    if (server.hasArg("hum"))  threshHum  = server.arg("hum").toFloat();
    if (server.hasArg("tvoc")) threshTvoc = server.arg("tvoc").toFloat();
    if (server.hasArg("eco2")) threshEco2 = server.arg("eco2").toFloat();

    saveThresholdsToNVS(threshTemp, threshHum, threshTvoc, threshEco2);

    Serial.printf("[Thresh] Saved locally — Temp: %.1f, Hum: %.1f, TVOC: %.0f, eCO2: %.0f\n",
                  threshTemp, threshHum, threshTvoc, threshEco2);

    alertTemp = (sensorTemp > threshTemp);
    alertHum  = (sensorHum  > threshHum);

    if (localMqttIsConnected()) {
      // Low/aqi defaults are kept here until dedicated UI fields are introduced.
      localMqttPublishConfig(threshTemp, 10.0f,
                   threshHum,  25.0f,
                   3,
                   threshEco2,
                   threshTvoc);
    } else {
      Serial.println("[LocalMQTT] Not connected, config topic not published");
    }

    if (WiFi.isConnected()) {
        String deviceId = getTelemetryDeviceId();
        String apiUrl = "http://192.168.0.16:8000/api/thresholds/" + deviceId;

        StaticJsonDocument<256> doc;
        doc["device_id"] = deviceId;

        JsonObject thresh = doc.createNestedObject("thresholds");
        thresh["temp_high"] = threshTemp;
        thresh["hum_high"]  = threshHum;
        thresh["tvoc_high"] = threshTvoc;
        thresh["co2_high"]  = threshEco2;   // API expects co2_high, not eco2_high

        String payload;
        serializeJson(doc, payload);

        HTTPClient http;
        http.begin(apiUrl);
        http.addHeader("Content-Type", "application/json");

        int httpCode = http.POST(payload);
        String response = http.getString();

        if (httpCode >= 200 && httpCode < 300) {
            Serial.printf("[Thresh] API update OK (HTTP %d): %s\n", httpCode, response.c_str());
        } else {
            Serial.printf("[Thresh] API update FAILED (HTTP %d): %s\n", httpCode, response.c_str());
        }

        http.end();
    } else {
        Serial.println("[Thresh] WiFi not connected, thresholds not sent to API");
    }

    if (mqttIsConnected()) {
        mqttPublishAttributes();
    }

    server.send(200, "text/plain", "OK");
}

static void handleGetThresh() {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"temp\":%.2f,\"hum\":%.2f,\"tvoc\":%.0f,\"eco2\":%.0f}",
             threshTemp, threshHum, threshTvoc, threshEco2);
    server.send(200, "application/json", buf);
}

// ── Server Init ───────────────────────────────────────────────────────────────

static void handleDiscover() {
    // Served from the ESP itself — real HTTP origin, no CORS issues
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", DISCOVER_PAGE);
}

void webServerInit() {
    server.on("/",                          handleRoot);
    server.on("/discover",    HTTP_GET,     handleDiscover);

    // iOS captive portal detection URLs
    server.on("/hotspot-detect.html",       handleCaptivePortal);
    server.on("/library/test/success.html", handleCaptivePortal);
    server.on("/success.txt",               handleCaptivePortal);
    // Android captive portal detection URLs
    server.on("/generate_204",              handleCaptivePortal);
    server.on("/gen_204",                   handleCaptivePortal);
    server.onNotFound(handleCaptivePortal); // catch all unknown URLs → redirect

    server.on("/sensors",      handleSensors);
    server.on("/wifi",         HTTP_GET,  handleWifiGet);
    server.on("/scan",         HTTP_GET,  handleScan);
    server.on("/set_wifi",     HTTP_POST, handleSetWifi);
    server.on("/prov_status",  HTTP_GET,  handleProvStatus);
    server.on("/provision",    HTTP_POST, handleProvision);
    server.on("/device_info",  HTTP_GET,  handleDeviceInfo);
    server.on("/set_token",    HTTP_POST, handleSetToken);
    server.on("/set_thresh",              handleSetThresh);
    server.on("/get_thresh",   HTTP_GET,  handleGetThresh);

    loadThresholdsFromNVS();
    server.begin();
    Serial.println("[Web] Server started");
}

void webServerHandle() {
    server.handleClient();
}