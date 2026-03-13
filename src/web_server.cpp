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
  <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined" />
</head>
<body>
<div id="popup-msg" style="position:fixed;top:-60px;left:0;width:100%;z-index:9999;text-align:center;transition:top 0.4s cubic-bezier(.4,2,.6,1);padding:16px 0;font-size:1.1rem;font-weight:bold;"></div>
  <!-- AP Title -->
  <h1>Web AP: ESP32 Device Setup</h1>
  
  <!-- Device Info -->
  <div class="step" id="step1">
  <div class="step-header">
  <span class="material-symbols-outlined" style="font-size:28px;color:#00d2ff;">memory</span>
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
  <div class="row">
    <button id="register-btn" onclick="registerDevice()">Register Device</button>
  </div>
  <div id="prov-msg"></div>
  </div>
  
  <!-- Live Data (sensor readings) -->
  <div class="step" id="step4">
    <div class="step-header"><span class="material-symbols-outlined" style="font-size:28px;color:#00d2ff;">sensors</span><div class="step-title">Live Sensor Data</div></div>
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
    <div class="step-header"><span class="material-symbols-outlined" style="font-size:28px;color:#00d2ff;">wifi</span><div class="step-title">WiFi Connection</div></div>
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
      <div class="step-num">⚙</div>
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

//? JavaScript for handling UI interactions, AJAX calls to ESP32 endpoints, and dynamic updates
function loadThresholds() {
  fetch('/get_thresh')
    .then(r => r.json())
    .then(th => {
      document.getElementById('thresh-temp').value = th.temp;
      document.getElementById('thresh-hum').value = th.hum;
    });
}

//? Save thresholds to ESP32 via POST request
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
//? Handle device registration (provisioning) when user clicks "Register Device" button
function registerDevice() {
  const btn = document.getElementById('register-btn');
  btn.disabled = true;
  showMsg('Registering device...', 'info');
  fetch('/provision', { method: 'POST' })
    .then(r => r.json())
    .then(res => {
      if (res.status === 'ok') {
        showMsg('Device registered! Restarting MQTT...', 'success');
        document.getElementById('prov-status').textContent = 'Provisioned';
        // Optionally, trigger MQTT reconnect by reloading page or calling endpoint
      } else {
        showMsg(res.message || 'Provisioning failed', 'error');
        document.getElementById('prov-status').textContent = 'Not provisioned';
      }
      btn.disabled = false;
      updateStatus();
    })
    .catch(() => {
      showMsg('Provisioning failed', 'error');
      btn.disabled = false;
    });
}
  
//? Scan for Wi-Fi networks and display results
function scanWifi() {
  const nets = document.getElementById('networks');
  nets.innerHTML = 'Scanning...';
  fetch('/scan').then(r => r.json()).then(list => {
    nets.innerHTML = list.length ? list.map(n => `<div class='net' onclick='selectNet("${n.ssid.replace(/'/g, "\\'").replace(/"/g, '\\"')}",${n.secure})'>${n.secure ? "🔒" : "🔓"} ${n.ssid} (${n.rssi} dBm)</div>`).join('') : 'No networks found';
  }).catch(() => nets.innerHTML = 'Scan failed');
}

//? When user selects a Wi-Fi network, show the form to enter password (if needed)
function selectNet(ssid, secure) {
  document.getElementById('wifi-ssid').value = ssid;
  document.getElementById('wifi-pass').value = '';
  document.getElementById('wifi-form').classList.remove('hidden');
  if (secure) document.getElementById('wifi-pass').focus();
}

//? Save Wi-Fi credentials and attempt connection
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

//? Periodically update Wi-Fi connection status and device info
function updateStatus() {
  fetch('/wifi').then(r => r.json()).then(w => {
    document.getElementById('curr-wifi').textContent = w.ssid || '–';
    document.getElementById('conn-status').textContent = w.connected ? '✓ Connected' : '✗ Not connected';
    document.getElementById('conn-status').style.color = w.connected ? '#2ecc71' : '#e74c3c';
  }).catch(() => {});
  // Update device info
  fetch('/device_info').then(r => r.json()).then(info => {
    document.getElementById('device-id').textContent = info.device_id || '–';
    document.getElementById('device-name').textContent = info.device_name || '–';
  }).catch(() => {});
}

//? Fetch live sensor data every few seconds and update the UI
function pollSensors() {
  fetch('/sensors').then(r => r.json()).then(d => {
    document.getElementById('temp').textContent = d.temp !== undefined ? d.temp.toFixed(1) : '–';
    document.getElementById('hum').textContent = d.hum !== undefined ? d.hum.toFixed(1) : '–';
  });
}

//? Utility function to show temporary messages to the user
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

updateStatus();                   // Initial status update
loadThresholds();                 // Load saved thresholds into form
pollSensors();                    // Start polling sensor data
setInterval(pollSensors, 3000);   // Update sensor data every 3 seconds
setInterval(updateStatus, 5000);  // Update connection status every 5 seconds
</script>
</body>
</html>
)rawliteral";

// Global variables to hold QR provisioning data (when needed)
static String qrDeviceKey = "";
static String qrDeviceSecret = "";
static String qrDeviceName = "";
float threshTemp = 30.0;
float threshHum = 80.0;
extern float sensorTemp;
extern float sensorHum;
extern bool alertTemp;
extern bool alertHum;

// Generate a unique device name from MAC address
String getDeviceName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[32];
  snprintf(name, sizeof(name), "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(name);
}

// Get MAC address as string (e.g., "A1:B2:C3:D4:E5:F6")
String getDeviceMacString() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// Handle root URL - serve the HTML page
static void handleRoot() { server.send(200, "text/html", HTML_PAGE); }


// Escape special characters in JSON strings
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

// Get current Wi-Fi status and SSID
static void handleWifiGet() {
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"connected\":%s}",
           escapeJson(wifiConfigSsid()).c_str(),
           WiFi.status() == WL_CONNECTED ? "true" : "false");
  server.send(200, "application/json", buf);
}

// Scan for Wi-Fi networks and return JSON list
static void handleScan() { server.send(200, "application/json", wifiScanNetworks()); }

// Handle Wi-Fi configuration POST request
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

// Get device information (ID and name) as JSON
static void handleDeviceInfo() {
  char buf[128];
  String devMac = getDeviceMacString();
  String devName = getDeviceName();
  snprintf(buf, sizeof(buf), "{\"device_id\":\"%s\",\"device_name\":\"%s\"}",
           devMac.c_str(), devName.c_str());
  server.send(200, "application/json", buf);
}

// Handle QR provisioning data sent from the mobile app
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

// Handle provisioning request: send device info to ThingsBoard and get token
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

// Handle token update from web interface (for manual token entry)
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

// --- Thresholds persistence ---
static float thresholdTemp = 30.0f;
static float thresholdHum = 80.0f;

static void loadThresholdsFromNVS() {
  Preferences prefs;
  prefs.begin("thresh", true);
  thresholdTemp = prefs.getFloat("temp", 30.0f);
  thresholdHum = prefs.getFloat("hum", 80.0f);
  prefs.end();
}

static void saveThresholdsToNVS(float temp, float hum) {
  Preferences prefs;
  prefs.begin("thresh", false);
  prefs.putFloat("temp", temp);
  prefs.putFloat("hum", hum);
  prefs.end();
}

static void handleSetThresh() {
  Serial.println("[DEBUG] handleSetThresh called"); // Debug print

  if (server.hasArg("temp")) threshTemp = server.arg("temp").toFloat();
  if (server.hasArg("hum"))  threshHum  = server.arg("hum").toFloat();

  Preferences prefs;
  prefs.begin("thresholds", false);
  prefs.putFloat("temp", threshTemp);
  prefs.putFloat("hum", threshHum);
  prefs.end();

  Serial.printf("[Thresh] Saved - Temp: %.1f, Hum: %.1f\n", threshTemp, threshHum);

  // Update alerts immediately
  alertTemp = (sensorTemp > threshTemp);
  alertHum  = (sensorHum > threshHum);

  // Publish new thresholds to ThingsBoard (if you have this function)
  mqttPublishAttributes();

  server.send(200, "text/plain", "OK");
}

static void handleGetThresh() {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"temp\":%.2f,\"hum\":%.2f}", thresholdTemp, thresholdHum);
  server.send(200, "application/json", buf);
}

//* Initialize web server and define HTTP endpoints
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
  server.on("/set_qr_data", HTTP_POST, handleSetQrData);
  server.on("/set_thresh", handleSetThresh);
  server.on("/get_thresh", HTTP_GET, handleGetThresh);
  loadThresholdsFromNVS();
  server.begin();
  Serial.println("[Web] Server started");
}

void webServerHandle() { server.handleClient(); }


