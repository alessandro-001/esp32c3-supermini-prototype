#pragma once

//! ── Web Server (Access Point) ────────────────────────────────────────────────
void webServerInit();       // Start WiFi AP and HTTP server
void webServerHandle();     // Must be called in loop() to handle client requests
void logPush(const String& line);  // Push a line to the in-memory log buffer
