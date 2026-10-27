#pragma once

#include <Arduino.h>
#include <LiquidCrystal.h>

#include "UsageData.h"

enum class DashboardScreen : uint8_t {
  DateTime = 0,
  FiveHour = 1,
  Weekly = 2,
};

class DashboardDisplay {
 public:
  DashboardDisplay(uint8_t rs, uint8_t enable, uint8_t d4,
                   uint8_t d5, uint8_t d6, uint8_t d7);

  void begin();
  void showStartup();
  void showConnecting();
  void render(DashboardScreen screen, const UsageData& usage);

 private:
  LiquidCrystal lcd_;
  String previousLine1_;
  String previousLine2_;

  void displayLines(String line1, String line2);
  void showDateTime();
  void showFiveHour(const UsageData& usage);
  void showWeekly(const UsageData& usage);

  static String fit16(String value);
  static bool clockIsSynced();
  static unsigned long secondsUntil(time_t target);
  static String fiveHourResetText(time_t resetAt);
  static String weeklyResetText(time_t resetAt);
};
