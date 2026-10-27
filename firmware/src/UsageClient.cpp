#include "UsageClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace {

bool getJSON(const char* serverUrl, const char* path, JsonDocument& document) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  const String url = String(serverUrl) + path;

  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  http.setReuse(false);
  http.useHTTP10(true);

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Connection", "close");
  const int status = http.GET();
  Serial.printf("GET %s -> HTTP %d\n", path, status);

  bool success = false;
  if (status == HTTP_CODE_OK) {
    const DeserializationError error =
      deserializeJson(document, http.getString());

    if (error) {
      Serial.print("JSON error: ");
      Serial.println(error.c_str());
    } else {
      success = true;
    }
  } else {
    Serial.println(http.errorToString(status));
  }

  http.end();
  client.stop();
  return success;
}

}  // namespace

UsageClient::UsageClient(const char* serverUrl) : serverUrl_(serverUrl) {}

bool UsageClient::fetch() {
  const bool usageOK = fetchUsage();
  const bool imagesOK = fetchImageLimit();
  return usageOK && imagesOK;
}

bool UsageClient::fetchUsage() {
  StaticJsonDocument<1024> document;

  if (!getJSON(serverUrl_, "/usage", document) ||
      document["status"] != "ok" ||
      document["five_hour"].isNull() ||
      document["weekly"].isNull()) {
    data_.usageAvailable = false;
    return false;
  }

  data_.fiveHour.usedPercent =
    document["five_hour"]["used_percent"].as<float>();
  data_.fiveHour.resetAt =
    document["five_hour"]["resets_at"].as<long long>();
  data_.weekly.usedPercent =
    document["weekly"]["used_percent"].as<float>();
  data_.weekly.resetAt =
    document["weekly"]["resets_at"].as<long long>();
  data_.usageAvailable = true;

  Serial.printf("5-hour used: %.1f%%, weekly used: %.1f%%\n",
                data_.fiveHour.usedPercent, data_.weekly.usedPercent);
  return true;
}

bool UsageClient::fetchImageLimit() {
  StaticJsonDocument<768> document;

  if (!getJSON(serverUrl_, "/image-limit", document)) {
    data_.images.serverAvailable = false;
    return false;
  }

  const char* status = document["status"] | "unknown";
  ImageLimitStatus parsedStatus;

  if (strcmp(status, "limit_reached") == 0) {
    if (!document["reset_in_seconds"].is<unsigned long>()) {
      data_.images.serverAvailable = false;
      return false;
    }
    parsedStatus = ImageLimitStatus::LimitReached;
  } else if (strcmp(status, "reset_time_passed") == 0) {
    parsedStatus = ImageLimitStatus::ResetTimePassed;
  } else if (strcmp(status, "available") == 0) {
    parsedStatus = ImageLimitStatus::Available;
  } else if (strcmp(status, "unknown") == 0) {
    parsedStatus = ImageLimitStatus::Unknown;
  } else {
    data_.images.serverAvailable = false;
    return false;
  }

  data_.images.status = parsedStatus;
  data_.images.remainingAtFetch =
    document["reset_in_seconds"] | 0UL;
  data_.images.fetchedAtMillis = millis();
  data_.images.serverAvailable = true;

  Serial.printf("Image limit: %s, reset in %lu seconds\n",
                status, data_.images.remainingAtFetch);
  return true;
}

const UsageData& UsageClient::data() const {
  return data_;
}
