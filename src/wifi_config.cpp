/*
  Reference-only Wi-Fi fix file.

  This is NOT wired into the Docker/API project.
  You can delete this file after copying the relevant changes into the ESP32
  firmware wifi_config.cpp file.

  Main fixes:
  1. Do not disconnect/re-begin Wi-Fi if already connected.
  2. Force normal station mode for runtime connection.
  3. Disable Wi-Fi sleep using both WiFi.setSleep(false) and esp_wifi_set_ps().
  4. Enable auto reconnect.
  5. Avoid persisting Wi-Fi settings repeatedly to flash.
*/

#include "wifi_config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "web_server.h"
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

const String& wifiConfigSsid() {
  return gSsid;
}

bool wifiConfigConnect(uint32_t timeoutMs) {
    if (gSsid.isEmpty()) {
        Serial.println("[WiFi] No credentials saved — skipping connect");
        return false;
    }

    // CHANGE: if Wi-Fi is already connected, do not force disconnect/reconnect.
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    // OLD CODE:
    // WiFi.disconnect(false);
    // delay(100);
    // WiFi.begin(gSsid.c_str(), gPass.c_str());

    // CHANGE: station mode for normal runtime operation.
    // If you intentionally need AP+STA while running, replace WIFI_STA with WIFI_AP_STA.
    WiFi.mode(WIFI_STA);

    // CHANGE: avoid flash writes from WiFi stack and improve reconnect behavior.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    // CHANGE: disable Wi-Fi sleep using both Arduino and ESP-IDF APIs.
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    WiFi.begin(gSsid.c_str(), gPass.c_str());

    Serial.printf("[WiFi] Connecting to %s\n", gSsid.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);

        Serial.printf("[WiFi] Connected! IP: %s RSSI: %d\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        logPush("[WiFi] Connected! IP: " + WiFi.localIP().toString());
        return true;
    }

    Serial.printf("[WiFi] Connection failed, status: %d\n", WiFi.status());
    logPush("[WiFi] Failed (status: " + String(WiFi.status()) + ")");
    return false;
}

bool wifiConfigHasCredentials() {
  return wifiConfigSsid().length() > 0;
}

void wifiConfigClear() {
    prefs.begin("netcfg", false);
    prefs.clear();
    prefs.end();
    gSsid = "";
    gPass = "";
    Serial.println("[WiFi] Credentials cleared");
}

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
