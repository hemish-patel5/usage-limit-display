#include "DashboardDisplay.h"

#include <math.h>
#include <time.h>

DashboardDisplay::DashboardDisplay(uint8_t rs, uint8_t enable, uint8_t d4,
                                   uint8_t d5, uint8_t d6, uint8_t d7)
    : lcd_(rs, enable, d4, d5, d6, d7) {}

void DashboardDisplay::begin() {
  lcd_.begin(16, 2);
}

void DashboardDisplay::showStartup() {
  displayLines("USAGE TRACKER", "STARTING...");
}

void DashboardDisplay::showConnecting() {
  displayLines("USAGE TRACKER", "CONNECTING WIFI");
}

void DashboardDisplay::render(DashboardScreen screen, const UsageData& usage) {
  switch (screen) {
    case DashboardScreen::DateTime:
      showDateTime();
      break;
    case DashboardScreen::FiveHour:
      showFiveHour(usage);
      break;
    case DashboardScreen::Weekly:
      showWeekly(usage);
      break;
  }
}

String DashboardDisplay::fit16(String value) {
  if (value.length() > 16) {
    value.remove(16);
  }
  while (value.length() < 16) {
    value += ' ';
  }
  return value;
}

void DashboardDisplay::displayLines(String line1, String line2) {
  line1 = fit16(line1);
  line2 = fit16(line2);

  if (line1 != previousLine1_) {
    lcd_.setCursor(0, 0);
    lcd_.print(line1);
    previousLine1_ = line1;
  }
  if (line2 != previousLine2_) {
    lcd_.setCursor(0, 1);
    lcd_.print(line2);
    previousLine2_ = line2;
  }
}

bool DashboardDisplay::clockIsSynced() {
  return time(nullptr) > 1700000000;
}

unsigned long DashboardDisplay::secondsUntil(time_t target) {
  const time_t now = time(nullptr);
  if (!clockIsSynced() || target <= now) {
    return 0;
  }
  return static_cast<unsigned long>(target - now);
}

String DashboardDisplay::fiveHourResetText(time_t resetAt) {
  if (!clockIsSynced()) {
    return "Reset: syncing";
  }

  const unsigned long remaining = secondsUntil(resetAt);
  const unsigned long hours = remaining / 3600;
  const unsigned long minutes = (remaining % 3600) / 60;
  return "Reset: " + String(hours) + "h " + String(minutes) + "m";
}

String DashboardDisplay::weeklyResetText(time_t resetAt) {
  if (!clockIsSynced()) {
    return "Reset: syncing";
  }

  const unsigned long remaining = secondsUntil(resetAt);
  const unsigned long days = remaining / 86400;
  const unsigned long hours = (remaining % 86400) / 3600;
  return "Reset: " + String(days) + "d " + String(hours) + "h";
}

void DashboardDisplay::showDateTime() {
  if (!clockIsSynced()) {
    displayLines("Syncing time...", "Please wait");
    return;
  }

  const time_t now = time(nullptr);
  struct tm localTime;
  localtime_r(&now, &localTime);

  int hour = localTime.tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }

  char timeLine[17];
  snprintf(timeLine, sizeof(timeLine), "%d:%02d %s", hour,
           localTime.tm_min, localTime.tm_hour >= 12 ? "PM" : "AM");

  static const char* DAYS[] = {
    "Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"
  };
  char dateLine[17];
  snprintf(dateLine, sizeof(dateLine), "%s %02d-%02d-%04d",
           DAYS[localTime.tm_wday], localTime.tm_mday,
           localTime.tm_mon + 1, localTime.tm_year + 1900);

  displayLines(timeLine, dateLine);
}

void DashboardDisplay::showFiveHour(const UsageData& usage) {
  if (!usage.available) {
    displayLines("5H Left: --%", "No usage data");
    return;
  }

  const int used = constrain(
    static_cast<int>(roundf(usage.fiveHour.usedPercent)), 0, 100);
  displayLines("5H Left: " + String(100 - used) + "%",
               fiveHourResetText(usage.fiveHour.resetAt));
}

void DashboardDisplay::showWeekly(const UsageData& usage) {
  if (!usage.available) {
    displayLines("Weekly: --%", "No usage data");
    return;
  }

  const int used = constrain(
    static_cast<int>(roundf(usage.weekly.usedPercent)), 0, 100);
  displayLines("Weekly: " + String(used) + "%",
               weeklyResetText(usage.weekly.resetAt));
}
