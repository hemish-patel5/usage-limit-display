
#include <Arduino.h>
#include <LiquidCrystal.h>

// LCD pins: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(0, 1, 3, 4, 5, 6);

// KY-040 encoder pins
const int ENC_CLK = 7;
const int ENC_DT  = 10;
const int ENC_SW  = 20;

int counter = 0;

// Rotary encoder state
uint8_t previousState = 0;
int8_t encoderSteps = 0;

// Button debounce
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastButtonChange = 0;

void updateDisplay() {
  lcd.setCursor(0, 0);
  lcd.print("Hello World!    ");

  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print("Counter: ");
  lcd.print(counter);
}

void setup() {
  Serial.begin(115200);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  delay(1000);

  lcd.begin(16, 2);
  lcd.clear();

  previousState =
      (digitalRead(ENC_CLK) << 1) |
       digitalRead(ENC_DT);

  updateDisplay();

  Serial.println("LCD + KY-040 ready");
}

void loop() {
  // Quadrature encoder transition table
  // Valid transitions contribute +1 or -1.
  static const int8_t transitions[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  uint8_t currentState =
      (digitalRead(ENC_CLK) << 1) |
       digitalRead(ENC_DT);

  if (currentState != previousState) {
    uint8_t index =
        (previousState << 2) | currentState;

    encoderSteps += transitions[index];
    previousState = currentState;

    // Most KY-040 encoders produce four transitions
    // per physical detent.
    if (encoderSteps >= 4) {
      counter++;
      encoderSteps = 0;
      updateDisplay();

      Serial.print("Counter: ");
      Serial.println(counter);
    }
    else if (encoderSteps <= -4) {
      counter--;
      encoderSteps = 0;
      updateDisplay();

      Serial.print("Counter: ");
      Serial.println(counter);
    }
  }

  // Read encoder push button
  bool buttonReading = digitalRead(ENC_SW);

  if (buttonReading != lastButtonReading) {
    lastButtonChange = millis();
  }

  if (millis() - lastButtonChange > 30) {
    if (buttonReading != stableButtonState) {
      stableButtonState = buttonReading;

      if (stableButtonState == LOW) {
        counter = 0;
        encoderSteps = 0;
        updateDisplay();

        Serial.println("Counter reset!");
      }
    }
  }

  lastButtonReading = buttonReading;
}
