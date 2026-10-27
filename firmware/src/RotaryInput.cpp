#include "RotaryInput.h"

RotaryInput::RotaryInput(uint8_t clockPin, uint8_t dataPin,
                         uint8_t switchPin)
    : clockPin_(clockPin), dataPin_(dataPin), switchPin_(switchPin) {}

void RotaryInput::begin() {
  pinMode(clockPin_, INPUT_PULLUP);
  pinMode(dataPin_, INPUT_PULLUP);
  pinMode(switchPin_, INPUT_PULLUP);
  previousAB_ = (digitalRead(clockPin_) << 1) | digitalRead(dataPin_);
}

Rotation RotaryInput::readRotation() {
  static const int8_t TRANSITIONS[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  const uint8_t currentAB =
    (digitalRead(clockPin_) << 1) | digitalRead(dataPin_);
  const uint8_t index = (previousAB_ << 2) | currentAB;
  steps_ += TRANSITIONS[index];
  previousAB_ = currentAB;

  if (steps_ >= 4) {
    steps_ = 0;
    return Rotation::Clockwise;
  }
  if (steps_ <= -4) {
    steps_ = 0;
    return Rotation::CounterClockwise;
  }
  return Rotation::None;
}

ButtonAction RotaryInput::readButton() {
  const bool raw = digitalRead(switchPin_);

  if (raw != buttonRaw_) {
    buttonRaw_ = raw;
    buttonChangedAt_ = millis();
  }
  if (millis() - buttonChangedAt_ < 35) {
    return ButtonAction::None;
  }

  if (raw != buttonStable_) {
    buttonStable_ = raw;
    if (buttonStable_ == LOW) {
      buttonPressedAt_ = millis();
      longPressHandled_ = false;
    } else if (!longPressHandled_) {
      return ButtonAction::ShortPress;
    }
  }

  if (buttonStable_ == LOW && !longPressHandled_ &&
      millis() - buttonPressedAt_ >= 800) {
    longPressHandled_ = true;
    return ButtonAction::LongPress;
  }

  return ButtonAction::None;
}
