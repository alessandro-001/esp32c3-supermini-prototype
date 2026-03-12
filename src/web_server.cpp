
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

WebServer server(80);

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
    .step-header { display:flex; align-items:center; gap:10px; margin-bottom:12px; flex-wrap:wrap; }
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
      .row, .step-header { flex-direction: column; align-items: flex-start; gap: 0.5em; }
      button, input { font-size: 1em; }
    }
  </style>
</head>
<body>
  <h1>Web AP: ESP32 Device Setup</h1>

  <!-- Step 1: Device Info (placeholder) -->
  <div class="step" id="step1">
    <div class="step-header">
      <div class="step-num">1</div>
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
      <span id="prov-status">–</span>
    </div>
  </div>

  <!-- Step 2: WiFi -->
  <div class="step" id="step2">
    <div class="step-header">
      <div class="step-num">2</div>
      <div class="step-title">WiFi Connection</div>
    </div>
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

  <!-- Step 4: Live Data (sensor readings) -->
  <div class="step" id="step4">
    <div class="step-header">
      <div class="step-num">3</div>
      <div class="step-title">Live Sensor Data</div>
    </div>
    <div class="row">
      <span>🌡 Temperature</span>
      <span><span class="val" id="temp">–</span> °C</span>
    </div>
    <div class="row">
      <span>💧 Humidity</span>
      <span><span class="val" id="hum">–</span> %</span>
    </div>
  </div>

  <div id="msg"></div>

<script>
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
}
function pollSensors() {
  fetch('/sensors').then(r => r.json()).then(d => {
    document.getElementById('temp').textContent = d.temp !== undefined ? d.temp.toFixed(1) : '–';
    document.getElementById('hum').textContent = d.hum !== undefined ? d.hum.toFixed(1) : '–';
  });
}
function showMsg(txt, type) {
  const el = document.getElementById('msg');
  el.textContent = txt;
  el.className = type || '';
  if (type === 'success') setTimeout(() => { el.textContent = ''; el.className = ''; }, 5000);
}
updateStatus();
pollSensors();
setInterval(pollSensors, 3000);
setInterval(updateStatus, 5000);
</script>
</body>
</html>
)rawliteral";

// --- State for QR provisioning ---
static String qrDeviceKey = "";
static String qrDeviceSecret = "";
static String qrDeviceName = "";
// Generate a unique device name from MAC address
String getDeviceName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[32];
  snprintf(name, sizeof(name), "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(name);
}

static void handleRoot() { server.send(200, "text/html", HTML_PAGE); }



static String escapeJson(const String& input) {
  String output;
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"') output += "\\\"";
    else if (c == '\\') output += "\\\\";
    else output += c;
  }
  return output;
}

static void handleWifiGet() {
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"connected\":%s}",
           escapeJson(wifiConfigSsid()).c_str(),
           WiFi.status() == WL_CONNECTED ? "true" : "false");
  server.send(200, "application/json", buf);
}

static void handleScan() { server.send(200, "application/json", wifiScanNetworks()); }

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
  String devName = getDeviceName();
  snprintf(buf, sizeof(buf), "{\"device_id\":\"%s\",\"device_name\":\"%s\",\"provisioned\":%s}",
           devName.c_str(), devName.c_str(),
           WiFi.status() == WL_CONNECTED ? "true" : "false");
  server.send(200, "application/json", buf);
}

static void handleSetQrData() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }
  const char* dk = doc["dk"];
  const char* ds = doc["ds"];
  const char* n = doc["n"];
  if (dk) qrDeviceKey = dk;
  if (ds) qrDeviceSecret = ds;
  if (n) qrDeviceName = n;
  Serial.printf("[QR] ✓ %s\n", qrDeviceName.c_str());
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleProvision() {
  // Stub: always return error for now
  server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"Provisioning not implemented\"}");
}

static void handleSetToken() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }
  String token = server.arg("token");
  if (token.length() < 5) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Token too short\"}");
    return;
  }
  Preferences prefs;
  prefs.begin("provision", false);
  prefs.putString("token", token);
  prefs.end();
  Serial.printf("[Web] ✅ Token saved: %.10s...\n", token.c_str());
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}


// Minimal /sensors endpoint for temperature and humidity
static void handleSensors() {
  char buf[64];
  extern float sensorTemp, sensorHum;
  snprintf(buf, sizeof(buf), "{\"temp\":%.1f,\"hum\":%.1f}", sensorTemp, sensorHum);
  server.send(200, "application/json", buf);
}

void webServerInit() {
  server.on("/", handleRoot);
  server.on("/sensors", handleSensors);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/set_wifi", HTTP_POST, handleSetWifi);
  server.on("/provision", HTTP_POST, handleProvision);
  server.on("/device_info", HTTP_GET, handleDeviceInfo);
  server.on("/set_token", HTTP_POST, handleSetToken);
  server.on("/set_qr_data", HTTP_POST, handleSetQrData);
  server.begin();
  Serial.println("[Web] Server started");
}

void webServerHandle() { server.handleClient(); }


