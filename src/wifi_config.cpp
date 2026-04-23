#include "wifi_config.h"
#include <Preferences.h>
#include <WiFi.h>
#include "config.h"
#include "web_server.h"
#include "wifi_config.h"
#include "secrets.h"


//! ── Wi-Fi Configuration ───────────────────────────────────────────────────────

static Preferences prefs;
static String gSsid;
static String gPass;

/// Escape special JSON characters to prevent XSS
static String escapeJson(const String& input) {
  String output;
  output.reserve(input.length() + 10);
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input[i];
    switch (c) {
      case '"':  output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n";  break;
      case '\r': output += "\\r";  break;
      case '\t': output += "\\t";  break;
      case '<':  output += "\\u003c"; break;
      case '>':  output += "\\u003e"; break;
      default:   output += c;
    }
  }
  return output;
}

/// Scan available Wi-Fi networks, returns JSON array
String wifiScanNetworks() {
  Serial.println("[WiFi] Scanning networks...");

  int n = WiFi.scanNetworks();
  String json = "[";

  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"";
    json += escapeJson(WiFi.SSID(i));
    json += "\",\"rssi\":";
    json += WiFi.RSSI(i);
    json += ",\"secure\":";
    json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
    json += "}";
  }

  json += "]";
  WiFi.scanDelete();

  Serial.printf("[WiFi] Found %d networks\n", n);
  return json;
}

/// Load saved credentials from NVS (local storage), fallback to defaults
void wifiConfigBegin(const char* defaultSsid, const char* defaultPass) {
    prefs.begin("netcfg", false);

#ifdef DEV_MODE
    gSsid = prefs.getString("ssid", defaultSsid);
    gPass = prefs.getString("pass", defaultPass);
#else
    gSsid = prefs.getString("ssid", "");
    gPass = prefs.getString("pass", "");
#endif

    Serial.printf("[WiFi] Loaded SSID: %s\n", gSsid.isEmpty() ? "(none)" : gSsid.c_str());
}

/// Save new credentials to NVS (local storage)
bool wifiConfigSave(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  if (pass.length() > 0 && pass.length() < 8) {
    Serial.println("[WiFi] Password too short (min 8 chars for WPA)");
    return false;
  }

  gSsid = ssid;
  gPass = pass;
  prefs.putString("ssid", gSsid);
  prefs.putString("pass", gPass);
  Serial.printf("[WiFi] Saved SSID: %s\n", gSsid.c_str());
  return true;
}

/// Get current saved SSID
const String& wifiConfigSsid() {
  return gSsid;
}

/// Connect to saved Wi-Fi, returns true on success
bool wifiConfigConnect(uint32_t timeoutMs) {
  //if (gSsid.isEmpty()) return false;
  if (gSsid.isEmpty()) {
        Serial.println("[WiFi] No credentials saved — skipping connect");
        return false;
    }

  WiFi.disconnect(true);  // full disconnect
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(gSsid.c_str(), gPass.c_str());

  Serial.printf("[WiFi] Connecting to %s", gSsid.c_str());
  Serial.printf("[WiFi] Trying SSID: %s, PASS: %s\n", gSsid.c_str(), gPass.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.printf("[WiFi] Connection failed, status: %d\n", WiFi.status());
  return false;
}

/// Check if WiFi credentials exist in NVS
bool wifiConfigHasCredentials() {
  return wifiConfigSsid().length() > 0;
}

/// Clear saved WiFi credentials from NVS (called by factory reset)
void wifiConfigClear() {
    prefs.begin("netcfg", false);
    prefs.clear();
    prefs.end();
    gSsid = "";
    gPass = "";
    Serial.println("[WiFi] Credentials cleared");
}

/// Start Access Point mode for configuration
void wifiApStart() {
  WiFi.mode(WIFI_AP);
  delay(200);

  bool ok = WiFi.softAP(AP_SSID, NULL, 6);
  delay(1000);

  if (!ok) {
    Serial.println("\u2717 WiFi softAP() FAILED");
    return;
  }

  Serial.printf("\u2713 AP started \u2014 SSID: %s  IP: %s\n",
    AP_SSID, WiFi.softAPIP().toString().c_str());
}