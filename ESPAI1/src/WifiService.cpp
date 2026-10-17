#include "WifiService.h"

#include <WiFi.h>

#include "AppConfig.h"

static const char* kHostname = "espai1";

bool WifiService::begin(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(kHostname);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

bool WifiService::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiService::ip() const {
  return WiFi.localIP().toString();
}

const char* WifiService::hostname() const {
  return kHostname;
}
