#include "WifiService.h"

#include <WiFi.h>

#include "AppConfig.h"

bool WifiService::begin(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
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

