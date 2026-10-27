#pragma once

#include <time.h>

struct UsageWindow {
  float usedPercent = 0.0f;
  time_t resetAt = 0;
};

enum class ImageLimitStatus {
  Unknown,
  LimitReached,
  ResetTimePassed,
  Available,
};

struct ImageLimitData {
  ImageLimitStatus status = ImageLimitStatus::Unknown;
  unsigned long remainingAtFetch = 0;
  unsigned long fetchedAtMillis = 0;
  bool serverAvailable = false;
};

struct PerformanceData {
  float cpuPercent = 0.0f;
  float ramPercent = 0.0f;
  bool available = false;
};

struct UsageData {
  PerformanceData performance;
  UsageWindow fiveHour;
  UsageWindow weekly;
  ImageLimitData images;
  bool usageAvailable = false;
};
