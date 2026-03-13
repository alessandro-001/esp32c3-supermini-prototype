#pragma once
#include <Arduino.h>


//! ── Device Provisioning ───────────────────────────────────────────────────────

// Provisioning states (for MQTT provisioning state machine)
enum ProvisioningState {
    PROV_STATE_IDLE,
    PROV_STATE_CONNECTING,
    PROV_STATE_REQUESTING,
    PROV_STATE_WAITING_RESPONSE,
    PROV_STATE_SUCCESS,
    PROV_STATE_FAILED
};

// Run on boot: loads token from NVS or triggers provisioning flow
// Returns the token to be used for MQTT (from NVS or freshly provisioned)
String provisioningInit();

// Send HTTP provisioning request to ThingsBoard, save token to NVS
// Returns token on success, empty string on failure
// REQUIRED by webserver.cpp handleProvision()
String provisioningRequest();

// Execute MQTT provisioning flow (call when WiFi is connected and no token exists)
// Returns true if provisioning started successfully
bool provisioningStartMqtt();

// Handle provisioning state machine (call in loop)
void provisioningHandle();

// Check if device needs provisioning
bool provisioningNeeded();

// Returns true if a token is already saved in NVS
bool provisioningHasToken();

// Clear saved token from NVS (for reset/re-provisioning)
void provisioningClearToken();

// Get the device unique ID (MAC address, no colons)
String provisioningDeviceId();

// Get current provisioning state
ProvisioningState provisioningGetState();

// Callback type for provisioning complete
typedef void (*ProvisioningCallback)(bool success, const String& token);

// Set callback for provisioning completion (called on success or failure)
void provisioningSetCallback(ProvisioningCallback cb);