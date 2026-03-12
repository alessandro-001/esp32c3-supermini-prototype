#pragma once

//! ── Web Server (Access Point) ────────────────────────────────────────────────
void webServerInit();       // Start WiFi AP and HTTP server
void webServerHandle();     // Must be called in loop() to handle client requests
