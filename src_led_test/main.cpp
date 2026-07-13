#include <Arduino.h>

constexpr uint8_t RED_LED_PIN = 8;
constexpr uint8_t GREEN_LED_PIN = 9;

void setup() {
  Serial.begin(9600);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(GREEN_LED_PIN, HIGH);

  Serial.println(F("LED test: D8 HIGH, D9 HIGH"));
}

void loop() {
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(GREEN_LED_PIN, HIGH);
}

