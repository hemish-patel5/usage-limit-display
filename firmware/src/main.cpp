
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

const char* WIFI_SSID = "Hemish";
const char* WIFI_PASSWORD = "Lalofrom9sari";

const char* SERVER = "http://192.168.1.49:8765";

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

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 20000
  ) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection failed");
  }
}

void loop() {
  fetchEndpoint("/usage");
  fetchEndpoint("/image-limit");

  Serial.println("----------------");

  delay(10000);
}
