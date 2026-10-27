#pragma once

#include <Arduino.h>

namespace AppConfig {

constexpr char SERVER_URL[] = "http://192.168.1.49:8765";
constexpr char TIME_ZONE[] = "NZST-12NZDT,M9.5.0,M4.1.0/3";

constexpr unsigned long POLL_INTERVAL_MS = 30000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long DISPLAY_INTERVAL_MS = 250;

// 1602A LCD in 4-bit mode: RS, EN, D4, D5, D6, D7.
constexpr uint8_t LCD_RS = 0;
constexpr uint8_t LCD_ENABLE = 1;
constexpr uint8_t LCD_D4 = 3;
constexpr uint8_t LCD_D5 = 4;
constexpr uint8_t LCD_D6 = 5;
constexpr uint8_t LCD_D7 = 6;

// KY-040 rotary encoder: CLK, DT, SW.
constexpr uint8_t ENCODER_CLK = 7;
constexpr uint8_t ENCODER_DT = 10;
constexpr uint8_t ENCODER_SWITCH = 20;

}  // namespace AppConfig
