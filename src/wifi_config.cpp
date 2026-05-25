#include "wifi_config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "web_server.h"
#include "secrets.h"

//! ── Wi-Fi Configuration ───────────────────────────────────────────────────────

static String gSsid;
static String gPass;

// Escape special characters in JSON strings (e.g. SSID) to prevent malformed JSON.
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

// Scan for Wi-Fi networks and return a JSON array of SSID, RSSI, and security type.
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

// Initialize Wi-Fi configuration by loading saved credentials from NVS.
void wifiConfigBegin(const char* defaultSsid, const char* defaultPass) {
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

// Save Wi-Fi credentials to NVS. Returns true on success, false on failure (e.g. invalid SSID or password).
bool wifiConfigSave(const String& ssid, const String& pass) {
    if (ssid.length() == 0) return false;
    if (pass.length() > 0 && pass.length() < 8) {
        Serial.println("[WiFi] Password too short (min 8 chars for WPA)");
        return false;
    }

    gSsid = ssid;
    gPass = pass;

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

    WiFi.mode(WIFI_AP_STA);

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

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

// Check if Wi-Fi credentials are saved in NVS.
bool wifiConfigHasCredentials() {
    return gSsid.length() > 0;
}

// Clear saved Wi-Fi credentials from NVS and reset in-memory variables.
void wifiConfigClear() {
    Preferences prefs;
    prefs.begin("netcfg", false);
    prefs.clear();
    prefs.end();
    gSsid = "";
    gPass = "";
    Serial.println("[WiFi] Credentials cleared");
}

// Start Wi-Fi Access Point for provisioning. This is a fallback in case the AP fails to start in main.cpp.
void wifiApStart() {
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