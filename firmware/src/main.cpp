
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "wifi_secrets.h"

const char* SERVER = "http://192.168.1.49:8765";

constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
constexpr unsigned long WIFI_RETRY_MS = 10000;

unsigned long lastWifiAttempt = 0;

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "idle";
    case WL_NO_SSID_AVAIL: return "SSID not found";
    case WL_SCAN_COMPLETED: return "scan completed";
    case WL_CONNECTED: return "connected";
    case WL_CONNECT_FAILED: return "connection failed (check authentication)";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "unknown";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("WiFi disconnect reason code: ");
    Serial.println(info.wifi_sta_disconnected.reason);
  }
}

bool connectWiFi() {
  lastWifiAttempt = millis();

  // Clear any stale connection left in the WiFi driver before trying again.
  WiFi.disconnect(false, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi connection failed: ");
    Serial.println(wifiStatusName(WiFi.status()));
    return false;
  }

  Serial.println("WiFi connected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  return true;
}

void fetchEndpoint(const char* path) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    return;
  }

  HTTPClient http;

  String url = String(SERVER) + path;

  Serial.print("GET ");
  Serial.println(url);

  http.begin(url);
  http.setTimeout(5000);

  int status = http.GET();

  Serial.print("HTTP status: ");
  Serial.println(status);

  if (status == 200) {
    Serial.println(http.getString());
  } else {
    Serial.println("Request failed");
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  WiFi.onEvent(onWiFiEvent);
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
      connectWiFi();
    }
    delay(100);
    return;
  }

  fetchEndpoint("/usage");
  fetchEndpoint("/image-limit");

  Serial.println("----------------");

  delay(10000);
}
