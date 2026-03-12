#include <Arduino.h>
#include <WebServer.h>
#include "config.h"
#include "web_server.h"

static WebServer server(80);

void webServerInit() {
  // Routes will be added here later
  server.begin();
  Serial.println("✓ HTTP server started");
}

void webServerHandle() {
  server.handleClient();
}
