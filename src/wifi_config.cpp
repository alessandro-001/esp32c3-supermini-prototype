#include "wifi_config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "web_server.h"
#include "secrets.h"

//! ── Wi-Fi Configuration ───────────────────────────────────────────────────────

// NOTE: prefs is NOT kept as a module-level open handle — each function
// opens and closes its own handle to avoid NVS namespace conflicts.
// (Bug 5 fix: wifiConfigBegin() was leaving prefs open permanently.)

static String gSsid;
static String gPass;

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

    // Scan in non-blocking mode preserving current WiFi state
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
    // BUG 5 FIX: open, read, then CLOSE the handle.
    // Original code left prefs open permanently, causing NVS conflicts in
    // wifiConfigSave() and on any subsequent Preferences use from another module.
    Preferences prefs;
    prefs.begin("netcfg", true); // read-only

#ifdef DEV_MODE
    gSsid = prefs.getString("ssid", defaultSsid);
    gPass = prefs.getString("pass", defaultPass);
#else
    gSsid = prefs.getString("ssid", "");
    gPass = prefs.getString("pass", "");
#endif

    prefs.end(); // ← close — was missing in the original

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

    // BUG 5 FIX: open a fresh handle for writing (old code relied on the
    // module-level prefs being left open by wifiConfigBegin, which is fragile).
    Preferences prefs;
    prefs.begin("netcfg", false);
    prefs.putString("ssid", gSsid);
    prefs.putString("pass", gPass);
    prefs.end();

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

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Already connected");
        return true;
    }

    // BUG 2 FIX: must use WIFI_AP_STA, not WIFI_STA.
    // Original code used WIFI_STA which silently tears down the AP that
    // main.cpp carefully started in WIFI_AP_STA mode, preventing the web UI
    // from being accessible during and after the STA connection attempt.
    WiFi.mode(WIFI_AP_STA);

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // BUG 3 FIX: give the driver time to settle after mode change before
    // calling begin(). The ESP32-C3 WiFi stack on espressif32@6.9.0 is
    // sensitive to mode changes immediately followed by begin().
    delay(100);

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

        Serial.printf("[WiFi] Connected! IP: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        logPush("[WiFi] Connected! IP: " + WiFi.localIP().toString());
        return true;
    }

    Serial.printf("[WiFi] Connection failed, status=%d\n", WiFi.status());
    logPush("[WiFi] Failed (status: " + String(WiFi.status()) + ")");
    return false;
}

bool wifiConfigHasCredentials() {
    return gSsid.length() > 0;
}

void wifiConfigClear() {
    Preferences prefs;
    prefs.begin("netcfg", false);
    prefs.clear();
    prefs.end();
    gSsid = "";
    gPass = "";
    Serial.println("[WiFi] Credentials cleared");
}

void wifiApStart() {
    // This function is a fallback — in normal operation the AP is started
    // directly in main.cpp via WiFi.softAP() in WIFI_AP_STA mode.
    WiFi.mode(WIFI_AP_STA);
    delay(200);

    bool ok = WiFi.softAP(AP_SSID, NULL, 6);
    delay(500);

    if (!ok) {
        Serial.println("[WiFi] softAP() FAILED");
        return;
    }

    Serial.printf("[WiFi] AP started — SSID: %s  IP: %s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
}