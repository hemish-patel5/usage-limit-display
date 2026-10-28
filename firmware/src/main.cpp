#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "AppConfig.h"
#include "DashboardDisplay.h"
#include "RotaryInput.h"
#include "UsageClient.h"
#include "wifi_secrets.h"

DashboardDisplay display(
  AppConfig::LCD_RS, AppConfig::LCD_ENABLE,
  AppConfig::LCD_D4, AppConfig::LCD_D5,
  AppConfig::LCD_D6, AppConfig::LCD_D7);

RotaryInput rotary(
  AppConfig::ENCODER_CLK,
  AppConfig::ENCODER_DT,
  AppConfig::ENCODER_SWITCH);

UsageClient usageClient(AppConfig::SERVER_URL);

DashboardScreen currentScreen = DashboardScreen::DateTime;
unsigned long lastPoll = 0;
unsigned long lastPerformancePoll = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastInteraction = 0;
bool wifiWasConnected = false;
bool timeSyncReported = false;

void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("WiFi disconnected, reason: %u\n",
                  info.wifi_sta_disconnected.reason);
  }
}

void renderDisplay() {
  display.render(currentScreen, usageClient.data());
}

void changeScreen(Rotation rotation) {
  if (rotation == Rotation::None) {
    return;
  }

  constexpr int SCREEN_COUNT = 5;
  int screen = static_cast<int>(currentScreen);
  screen += rotation == Rotation::Clockwise ? 1 : -1;
  screen = (screen + SCREEN_COUNT) % SCREEN_COUNT;
  currentScreen = static_cast<DashboardScreen>(screen);
  lastInteraction = millis();
  renderDisplay();
}

void handleButton(ButtonAction action) {
  if (action == ButtonAction::ShortPress) {
    lastInteraction = millis();
    usageClient.fetch();
    lastPoll = millis();
    renderDisplay();
  } else if (action == ButtonAction::LongPress) {
    currentScreen = DashboardScreen::DateTime;
    lastInteraction = millis();
    renderDisplay();
  }
}

void startWiFi() {
  lastWifiAttempt = millis();
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void manageWiFi() {
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.print("WiFi connected, IP: ");
      Serial.println(WiFi.localIP());

      configTzTime(AppConfig::TIME_ZONE,
                   "pool.ntp.org", "time.google.com");
      usageClient.fetch();
      lastPoll = millis();
      lastPerformancePoll = millis();
      renderDisplay();
    }
    return;
  }

  if (wifiWasConnected) {
    Serial.println("WiFi disconnected");
    wifiWasConnected = false;
  }

  if (millis() - lastWifiAttempt >=
      AppConfig::WIFI_RETRY_INTERVAL_MS) {
    startWiFi();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  display.begin();
  display.showStartup();
  rotary.begin();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(handleWiFiEvent);

  display.showConnecting();
  lastInteraction = millis();
  startWiFi();
}

void loop() {
  changeScreen(rotary.readRotation());
  handleButton(rotary.readButton());
  manageWiFi();

  if (!timeSyncReported && time(nullptr) > 1700000000) {
    timeSyncReported = true;
    Serial.println("System clock synchronized");
  }

  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastPoll >= AppConfig::POLL_INTERVAL_MS) {
    usageClient.fetch();
    lastPoll = millis();
  }

  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastPerformancePoll >=
        AppConfig::PERFORMANCE_POLL_INTERVAL_MS) {
    usageClient.fetchPerformance();
    lastPerformancePoll = millis();
  }

  if (millis() - lastDisplayUpdate >=
      AppConfig::DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = millis();

    if (millis() - lastInteraction >=
        AppConfig::AUTO_HOME_INTERVAL_MS) {
      currentScreen = DashboardScreen::DateTime;
    }
    renderDisplay();
  }

  delay(2);
}
