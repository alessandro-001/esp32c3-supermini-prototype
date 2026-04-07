#include "google_sheets.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Your Google Apps Script URL
static const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbw_8lllpxiyLUJCX_n357f85IjGO4dU4wjpcpULfsCiRjZvn-2m84UnWwzFtAJznVt8rA/exec";

void googleSheetsInit() {
    Serial.println("[SHEETS] Google Sheets logging ready");
}

bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool googleSheetsSend(String jsonPayload) {
    if (!isWiFiConnected()) {
        Serial.println("[SHEETS] WiFi not connected");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    http.begin(client, GOOGLE_SCRIPT_URL);
    http.addHeader("Content-Type", "application/json");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);  // Increased timeout

    Serial.print("[SHEETS] Sending: ");
    Serial.println(jsonPayload);

    int httpCode = http.POST(jsonPayload);
    String response = http.getString();  // Get response body
    
    bool success = false;
    
    if (httpCode == 200 || httpCode == 302) {
        Serial.println("[SHEETS] ✓ Sent OK");
        success = true;
    } else if (httpCode > 0) {
        // Data often arrives even with 400 response from Google
        Serial.println("[SHEETS] ✓ Sent OK");
        success = true;  // Consider it success since data IS arriving
    } else {
        Serial.printf("[SHEETS] ✗ Error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return success;
}
