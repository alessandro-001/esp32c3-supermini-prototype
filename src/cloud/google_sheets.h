#pragma once
#include <Arduino.h>

void googleSheetsInit();
bool googleSheetsSend(String jsonPayload);
bool isWiFiConnected();