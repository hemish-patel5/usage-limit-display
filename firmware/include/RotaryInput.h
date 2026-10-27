#pragma once

#include <Arduino.h>

enum class Rotation : int8_t {
  None = 0,
  Clockwise = 1,
  CounterClockwise = -1,
};

enum class ButtonAction : uint8_t {
  None,
  ShortPress,
  LongPress,
};

class RotaryInput {
 public:
  RotaryInput(uint8_t clockPin, uint8_t dataPin, uint8_t switchPin);

  void begin();
  Rotation readRotation();
  ButtonAction readButton();

 private:
  uint8_t clockPin_;
  uint8_t dataPin_;
  uint8_t switchPin_;
  uint8_t previousAB_ = 0;
  int steps_ = 0;

  bool buttonRaw_ = HIGH;
  bool buttonStable_ = HIGH;
  bool longPressHandled_ = false;
  unsigned long buttonChangedAt_ = 0;
  unsigned long buttonPressedAt_ = 0;
};
