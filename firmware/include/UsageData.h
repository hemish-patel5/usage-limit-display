#pragma once

#include <time.h>

struct UsageWindow {
  float usedPercent = 0.0f;
  time_t resetAt = 0;
};

struct UsageData {
  UsageWindow fiveHour;
  UsageWindow weekly;
  bool available = false;
};
