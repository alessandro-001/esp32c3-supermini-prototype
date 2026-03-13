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

WebServer server(80); // HTTP server on port 80

//* Wi-Fi Configuration Implementation + Web Server Endpoints + UI

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Setup</title>
  <style>
    * { box-sizing: border-box; }
    html, body { width: 100%; min-height: 100%; margin: 0; padding: 0; }
    body { font-family: Arial, sans-serif; background:#111; color:#eee; max-width: 480px; width: 100vw; margin:0 auto; padding:16px; }
    h1 { color:#00d2ff; font-size:1.4rem; margin-bottom:20px; text-align:center; }
    .row { display:flex; flex-wrap:wrap; justify-content:space-between; align-items:center; padding:10px 0; border-bottom:1px solid #222; }
    .val { font-size:1.1rem; font-weight:bold; color:#00d2ff; word-break:break-all; }
    label { display:block; margin:10px 0 4px; font-size:0.9rem; color:#aaa; }
    input { width:100%; max-width:100%; padding:10px; background:#222; border:1px solid #444; border-radius:8px; color:#eee; font-size:1rem; }
    input:focus { border-color:#00d2ff; outline:none; }
    button { width:100%; max-width:100%; padding:14px; margin-top:12px; background:#2ecc71; color:#000; border:none; border-radius:8px; font-size:1rem; font-weight:bold; cursor:pointer; }
    button.wifi { background:#3498db; color:#fff; }
    button:disabled { background:#555; color:#999; cursor:not-allowed; }
    #msg { margin-top:16px; text-align:center; font-size:0.95rem; min-height:24px; padding:10px; border-radius:6px; }
    .success { background:#1a3d1a; color:#2ecc71; }
    .error { background:#3d1a1a; color:#e74c3c; }
    .info { background:#1a2a3d; color:#3498db; }
    .net { padding:12px; margin:6px 0; background:#222; border-radius:8px; cursor:pointer; word-break:break-all; }
    .net:hover { background:#333; }
    .hidden { display:none !important; }
    .step { background:#1a1a2e; border:1px solid #333; border-radius:10px; padding:16px; margin:16px 0; width:100%; box-sizing:border-box; }
    .step-header { display:flex; align-items:center; gap:10px; margin-bottom:12px; flex-direction:row; flex-wrap:nowrap; }
    .step-num { background:#00d2ff; color:#000; width:28px; height:28px; border-radius:50%; display:flex; align-items:center; justify-content:center; font-weight:bold; font-size:0.9rem; flex-shrink:0; }
    .step-title { font-size:1.1rem; font-weight:bold; }
    .step.completed { border-color:#2ecc71; }
    .step.completed .step-num { background:#2ecc71; }
    .status-box { background:#1a2a1a; border:1px solid #2a4a2a; border-radius:8px; padding:12px; margin-bottom:16px; }
    .status-box.warning { background:#2a2a1a; border-color:#4a4a2a; }
    .hint { font-size:0.8rem; color:#666; margin-top:6px; }
    @media (max-width: 600px) {
      body { padding: 4vw; font-size: 1em; }
      .step { padding: 4vw; margin: 4vw 0; }
      h1 { font-size: 1.1rem; }
      .row { flex-direction: column; align-items: flex-start; gap: 0.5em; }
      .step-header { flex-direction: row; align-items: center; gap: 10px; }
      button, input { font-size: 1em; }
    }
    #popup-msg {
      background: #222;
      color: #fff;
      border-bottom: 2px solid #00d2ff;
      box-shadow: 0 2px 8px rgba(0,0,0,0.15);
    }
    #popup-msg.success { background: #1a3d1a; color: #2ecc71; border-bottom: 2px solid #2ecc71; }
    #popup-msg.error { background: #3d1a1a; color: #e74c3c; border-bottom: 2px solid #e74c3c; }
    #popup-msg.info { background: #1a2a3d; color: #3498db; border-bottom: 2px solid #3498db; }
  </style>

</head>
<body>
<div id="popup-msg" style="position:fixed;top:-60px;left:0;width:100%;z-index:9999;text-align:center;transition:top 0.4s cubic-bezier(.4,2,.6,1);padding:16px 0;font-size:1.1rem;font-weight:bold;"></div>
  <!-- AP Title -->
  <h1>Web AP: ESP32 Device Setup</h1>
  
  <!-- Device Info -->
  <div class="step" id="step1">
  <div class="step-header">
  <span style="font-size:28px;">🔌</span>
  <div class="step-title">Device Information</div>
  </div>
  <div class="row">
  <span>Device ID</span>
  <span class="val" id="device-id">–</span>
  </div>
  <div class="row">
  <span>Device Name</span>
  <span class="val" id="device-name">–</span>
  </div>
  <div class="row">
    <span>Status</span>
    <span id="prov-status">Checking...</span>
  </div>
  <div class="row" id="register-row">
    <button id="register-btn" onclick="registerDevice()">Register Device</button>
  </div>
  <div id="prov-msg"></div>
  </div>
  
  <!-- Live Data (sensor readings) -->
  <div class="step" id="step4">
    <div class="step-header"><span style="font-size:28px;">📡</span><div class="step-title">Live Sensor Data</div></div>
    <div class="row">
      <span>🌡 Temperature</span>
      <span><span class="val" id="temp">–</span> °C</span>
    </div>
    <div class="row">
      <span>💧 Humidity</span>
      <span><span class="val" id="hum">–</span> %</span>
    </div>
  </div>

  <!-- WiFi Connection -->
  <div class="step" id="step2">
    <div class="step-header"><span style="font-size:28px;">📶</span><div class="step-title">WiFi Connection</div></div>
    <div class="row">
      <span>Network</span>
      <span id="curr-wifi">–</span>
    </div>
    <div class="row">
      <span>Status</span>
      <span id="conn-status">–</span>
    </div>
    <div id="wifi-config">
      <button class="wifi" onclick="scanWifi()">🔍 Scan Networks</button>
      <div id="networks"></div>
      <div id="wifi-form" class="hidden">
        <label>Network</label>
        <input type="text" id="wifi-ssid" readonly>
        <label>Password</label>
        <input type="password" id="wifi-pass" placeholder="Enter password">
        <button onclick="saveWifi()">💾 Connect</button>
      </div>
    </div>
  </div>

  <!-- Thresholds -->
  <div class="step" id="step-thresh">
    <div class="step-header">
      <span style="font-size:28px;">⚠️</span>
      <div class="step-title">Alert Thresholds</div>
    </div>
    <div class="input-row">
      <div>
        <label>Max Temp (°C)</label>
        <input type="number" id="thresh-temp" step="0.5" min="-40" max="125" placeholder="30">
      </div>
      <div>
        <label>Max Humidity (%)</label>
        <input type="number" id="thresh-hum" step="1" min="0" max="100" placeholder="80">
      </div>
    </div>
    <button class="thresh" onclick="saveThresholds()">💾 Save Thresholds</button>
    <div id="thresh-msg"></div>
  </div>

<script>
function loadThresholds() {
  fetch('/get_thresh')
    .then(r => r.json())
    .then(th => {
      document.getElementById('thresh-temp').value = th.temp;
      document.getElementById('thresh-hum').value = th.hum;
    });
}

function checkProvStatus() {
  fetch('/prov_status')
    .then(r => r.json())
    .then(res => {
      const statusEl = document.getElementById('prov-status');
      const registerRow = document.getElementById('register-row');
      if (res.provisioned) {
        statusEl.innerHTML = '<span style="color:#2ecc71;font-weight:bold;">Active ✅</span>';
        registerRow.style.display = 'none'; // hide button if already registered
      } else {
        statusEl.innerHTML = '<span style="color:#e74c3c;font-weight:bold;">Not registered ❌</span>';
        registerRow.style.display = '';
      }
    })
    .catch(() => {
      document.getElementById('prov-status').textContent = 'Unknown';
    });
}

function saveThresholds() {
  const temp = parseFloat(document.getElementById("thresh-temp").value);
  const hum = parseFloat(document.getElementById("thresh-hum").value);
  if (isNaN(temp) || isNaN(hum)) {
    showMsg("Enter valid threshold values", "error");
    return;
  }
  showMsg("Saving thresholds...", "info");
  fetch("/set_thresh?temp=" + temp + "&hum=" + hum)
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
        checkProvStatus(); // refresh status → shows Active ✅ and hides button
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
    body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass)
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
    document.getElementById('conn-status').style.color = w.connected ? '#2ecc71' : '#e74c3c';
  }).catch(() => {});
  fetch('/device_info').then(r => r.json()).then(info => {
    document.getElementById('device-id').textContent = info.device_id || '–';
    document.getElementById('device-name').textContent = info.device_name || '–';
  }).catch(() => {});
}

function pollSensors() {
  fetch('/sensors').then(r => r.json()).then(d => {
    document.getElementById('temp').textContent = d.temp !== undefined ? d.temp.toFixed(1) : '–';
    document.getElementById('hum').textContent = d.hum !== undefined ? d.hum.toFixed(1) : '–';
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

// ── Global threshold variables (single source of truth) ──────────────────────
float threshTemp = 30.0f;
float threshHum  = 80.0f;

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
    // Use all 6 MAC bytes to match ThingsBoard device name format: ESP32-8856A674FCE0
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
    prefs.begin(THRESH_NVS_NS, true); // read-only
    threshTemp = prefs.getFloat("temp", 30.0f);
    threshHum  = prefs.getFloat("hum",  80.0f);
    prefs.end();
    Serial.printf("[Thresh] Loaded — Temp: %.1f, Hum: %.1f\n", threshTemp, threshHum);
}

static void saveThresholdsToNVS(float temp, float hum) {
    Preferences prefs;
    prefs.begin(THRESH_NVS_NS, false); // read-write
    prefs.putFloat("temp", temp);
    prefs.putFloat("hum",  hum);
    prefs.end();
}

// ── HTTP Handlers ─────────────────────────────────────────────────────────────

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
    if (!wifiConfigSave(ssid, pass)) {
        server.send(400, "text/plain", "Invalid password");
        return;
    }
    bool ok = wifiConfigConnect(10000);
    server.send(200, "text/plain", ok ? "Connected to " + ssid : "Connection failed");
}

static void handleDeviceInfo() {
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
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"temp\":%.1f,\"hum\":%.1f}", sensorTemp, sensorHum);
    server.send(200, "application/json", buf);
}

static void handleSetThresh() {
    if (server.hasArg("temp")) threshTemp = server.arg("temp").toFloat();
    if (server.hasArg("hum"))  threshHum  = server.arg("hum").toFloat();

    // FIX: use consistent NVS namespace via helper
    saveThresholdsToNVS(threshTemp, threshHum);

    Serial.printf("[Thresh] Saved — Temp: %.1f, Hum: %.1f\n", threshTemp, threshHum);

    // Update alert flags immediately
    alertTemp = (sensorTemp > threshTemp);
    alertHum  = (sensorHum  > threshHum);

    // FIX: only publish if MQTT is ready
    if (mqttIsConnected()) {
        mqttPublishAttributes();
    }

    server.send(200, "text/plain", "OK");
}

static void handleGetThresh() {
    char buf[64];
    // FIX: use single threshTemp/threshHum variables (no more thresholdTemp/thresholdHum)
    snprintf(buf, sizeof(buf), "{\"temp\":%.2f,\"hum\":%.2f}", threshTemp, threshHum);
    server.send(200, "application/json", buf);
}

// ── Server Init ───────────────────────────────────────────────────────────────

void webServerInit() {
    server.on("/",            handleRoot);
    server.on("/sensors",     handleSensors);
    server.on("/wifi",        HTTP_GET,  handleWifiGet);
    server.on("/scan",        HTTP_GET,  handleScan);
    server.on("/set_wifi",    HTTP_POST, handleSetWifi);
    server.on("/prov_status",  HTTP_GET,  handleProvStatus);
    server.on("/provision",    HTTP_POST, handleProvision);
    server.on("/device_info", HTTP_GET,  handleDeviceInfo);
    server.on("/set_token",   HTTP_POST, handleSetToken);
    server.on("/set_thresh",             handleSetThresh);
    server.on("/get_thresh",  HTTP_GET,  handleGetThresh);

    loadThresholdsFromNVS(); // Load saved thresholds on boot
    server.begin();
    Serial.println("[Web] Server started");
}

void webServerHandle() {
    server.handleClient();
}