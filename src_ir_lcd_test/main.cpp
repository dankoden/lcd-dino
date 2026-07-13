#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <IRremote.hpp>

constexpr uint8_t IR_PIN = 4;
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;

LiquidCrystal_I2C lcd(0x27, LCD_COLUMNS, LCD_ROWS);

uint16_t lastCommand = 0xFFFF;
bool lastWasRepeat = false;

uint16_t normalizeIrCommand(const IRData& data) {
  if (data.command != 0) {
    return data.command;
  }

  return (data.decodedRawData >> 16) & 0xFF;
}

void formatBinary8(uint8_t value, char* output) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    *output = (value & (1 << bit)) ? '1' : '0';
    output++;
  }

  *output = '\0';
}

void showWaitingScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("IR LCD TEST"));
  lcd.setCursor(0, 1);
  lcd.print(F("PRESS REMOTE"));
}

void showCommand(uint16_t command, bool isRepeat) {
  const uint8_t command8 = command & 0xFF;
  char binary[9];
  formatBinary8(command8, binary);

  char line[17];

  lcd.setCursor(0, 0);
  snprintf(line, sizeof(line), "HEX CMD: 0x%02X", command8);
  lcd.print(line);
  for (uint8_t i = strlen(line); i < LCD_COLUMNS; i++) {
    lcd.print(' ');
  }

  lcd.setCursor(0, 1);
  snprintf(line, sizeof(line), "BIN:%s%c", binary, isRepeat ? 'R' : ' ');
  lcd.print(line);
  for (uint8_t i = strlen(line); i < LCD_COLUMNS; i++) {
    lcd.print(' ');
  }
}

void printSerialDebug(const IRData& data, uint16_t command, bool isRepeat) {
  Serial.print(F("protocol="));
  Serial.print(static_cast<uint8_t>(data.protocol));
  Serial.print(F(" address=0x"));
  Serial.print(data.address, HEX);
  Serial.print(F(" command=0x"));
  Serial.print(command, HEX);
  Serial.print(F(" raw=0x"));
  Serial.print(data.decodedRawData, HEX);
  Serial.print(F(" flags=0x"));
  Serial.print(data.flags, HEX);
  Serial.print(F(" repeat="));
  Serial.println(isRepeat ? F("yes") : F("no"));
}

void setup() {
  Serial.begin(9600);

  Wire.begin();
  Wire.setClock(400000);

  lcd.init();
  lcd.backlight();
  showWaitingScreen();

  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);

  Serial.println(F("IR LCD test ready. Press remote buttons."));
}

void loop() {
  if (!IrReceiver.decode()) {
    return;
  }

  const IRData& data = IrReceiver.decodedIRData;
  const bool isRepeat = (data.flags & IRDATA_FLAGS_IS_REPEAT) != 0;
  const uint16_t command = normalizeIrCommand(data);

  if (command != 0 && (!isRepeat || command != lastCommand || !lastWasRepeat)) {
    showCommand(command, isRepeat);
    printSerialDebug(data, command, isRepeat);
    lastCommand = command;
    lastWasRepeat = isRepeat;
  }

  IrReceiver.resume();
}

