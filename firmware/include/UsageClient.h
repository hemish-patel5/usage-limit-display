#pragma once

#include "UsageData.h"

class UsageClient {
 public:
  explicit UsageClient(const char* serverUrl);

  bool fetch();
  const UsageData& data() const;

 private:
  const char* serverUrl_;
  UsageData data_;
};
