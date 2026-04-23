#pragma once

#include <Arduino.h>

//! ── Network Scan ─────────────────────────────────────────────────────────────
String       wifiScanNetworks();                                                // Scan and return available networks as JSON

//! ── WiFi Configuration ───────────────────────────────────────────────────────
void         wifiConfigBegin(const char* defaultSsid, const char* defaultPass); // Load saved credentials from NVS, fallback to defaults
bool         wifiConfigSave(const String& ssid, const String& pass);            // Save new credentials to NVS
const String& wifiConfigSsid();                                                 // Get current saved SSID
bool         wifiConfigConnect(uint32_t timeoutMs);                             // Connect to saved WiFi (STA mode)
bool         wifiConfigHasCredentials();  
void         wifiConfigClear();                                                 // Clear saved WiFi credentials

//! ── Access Point ─────────────────────────────────────────────────────────────
void         wifiApStart();                                                     // Start AP mode for configuration

