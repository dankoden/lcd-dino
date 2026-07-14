#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#define RAW_BUFFER_LENGTH 260
#include <IRremote.hpp>

constexpr uint8_t IR_PIN = 4;
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;

LiquidCrystal_I2C lcd(0x27, LCD_COLUMNS, LCD_ROWS);

uint16_t lastCommand = 0xFFFF;
uint32_t lastRaw = 0;
bool lastWasRepeat = false;
char captureLabel[8] = "";
bool captureArmed = false;
uint8_t lastIrPinLevel = HIGH;
uint16_t irPinEdgeCount = 0;
unsigned long lastIrActivityReportTime = 0;

uint32_t computeRawFingerprint() {
  uint32_t hash = 2166136261UL;
  const uint16_t rawlen = IrReceiver.irparams.rawlen;

  hash ^= rawlen & 0xFF;
  hash *= 16777619UL;
  hash ^= rawlen >> 8;
  hash *= 16777619UL;

  for (uint16_t index = 1; index < rawlen; index++) {
    const uint8_t normalizedTick = (IrReceiver.irparams.rawbuf[index] + 2) / 4;
    hash ^= normalizedTick;
    hash *= 16777619UL;
    hash ^= index & 1;
    hash *= 16777619UL;
  }

  return hash;
}

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

void showCapturePrompt(const char* label) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("LEARN "));
  lcd.print(label);
  lcd.setCursor(0, 1);
  lcd.print(F("PRESS BUTTON"));
}

void showIntermissionScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("WAIT 10 SEC"));
  lcd.setCursor(0, 1);
  lcd.print(F("NEXT: JUMP"));
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
  Serial.print(F(" rawlen="));
  Serial.print(IrReceiver.irparams.rawlen);
  Serial.print(F(" fp=0x"));
  Serial.print(computeRawFingerprint(), HEX);
  Serial.print(F(" flags=0x"));
  Serial.print(data.flags, HEX);
  Serial.print(F(" repeat="));
  Serial.println(isRepeat ? F("yes") : F("no"));
}

void printLearnLine(const char* label, const IRData& data, uint16_t command, bool isRepeat) {
  Serial.print(F("LEARN label="));
  Serial.print(label);
  Serial.print(F(" protocol="));
  Serial.print(static_cast<uint8_t>(data.protocol));
  Serial.print(F(" address=0x"));
  Serial.print(data.address, HEX);
  Serial.print(F(" command=0x"));
  Serial.print(command, HEX);
  Serial.print(F(" raw=0x"));
  Serial.print(data.decodedRawData, HEX);
  Serial.print(F(" rawlen="));
  Serial.print(IrReceiver.irparams.rawlen);
  Serial.print(F(" fp=0x"));
  Serial.print(computeRawFingerprint(), HEX);
  Serial.print(F(" flags=0x"));
  Serial.print(data.flags, HEX);
  Serial.print(F(" repeat="));
  Serial.println(isRepeat ? F("yes") : F("no"));
}

void pollSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  const char command = Serial.read();

  if (command == 'O') {
    strcpy(captureLabel, "POWER");
    captureArmed = true;
    showCapturePrompt(captureLabel);
    Serial.println(F("READY label=POWER"));
    return;
  }

  if (command == 'P') {
    strcpy(captureLabel, "PLAY");
    captureArmed = true;
    showCapturePrompt(captureLabel);
    Serial.println(F("READY label=PLAY"));
    return;
  }

  if (command == 'J') {
    strcpy(captureLabel, "JUMP");
    captureArmed = true;
    showCapturePrompt(captureLabel);
    Serial.println(F("READY label=JUMP"));
    return;
  }

  if (command == 'I') {
    showWaitingScreen();
    Serial.println(F("IR LCD test ready. Send O to learn POWER, P to learn PLAY, J to learn JUMP."));
    return;
  }

  if (command == 'W') {
    captureArmed = false;
    showIntermissionScreen();
    Serial.println(F("READY wait-next-jump"));
  }
}

void updateIrActivityDebug() {
  const uint8_t level = digitalRead(IR_PIN);

  if (level != lastIrPinLevel) {
    lastIrPinLevel = level;
    irPinEdgeCount++;
  }

  const unsigned long now = millis();
  if (irPinEdgeCount > 0 && now - lastIrActivityReportTime >= 300) {
    Serial.print(F("IR_ACTIVITY edges="));
    Serial.print(irPinEdgeCount);
    Serial.print(F(" level="));
    Serial.println(level == HIGH ? F("HIGH") : F("LOW"));
    irPinEdgeCount = 0;
    lastIrActivityReportTime = now;
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(IR_PIN, INPUT);

  Wire.begin();
  Wire.setClock(400000);

  lcd.init();
  lcd.backlight();
  showWaitingScreen();

  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);

  Serial.println(F("IR LCD test ready. Send O to learn POWER, P to learn PLAY, J to learn JUMP."));
}

void loop() {
  pollSerialCommands();
  updateIrActivityDebug();

  if (!IrReceiver.decode()) {
    return;
  }

  const IRData& data = IrReceiver.decodedIRData;
  const bool isRepeat = (data.flags & IRDATA_FLAGS_IS_REPEAT) != 0;
  const uint16_t command = normalizeIrCommand(data);

  const bool hasPayload = command != 0 || data.decodedRawData != 0;
  const bool isDuplicate =
    isRepeat &&
    command == lastCommand &&
    data.decodedRawData == lastRaw &&
    lastWasRepeat;

  if (hasPayload && !isDuplicate) {
    showCommand(command, isRepeat);
    printSerialDebug(data, command, isRepeat);

    if (captureArmed && !isRepeat) {
      printLearnLine(captureLabel, data, command, isRepeat);
      captureArmed = false;
    }

    lastCommand = command;
    lastRaw = data.decodedRawData;
    lastWasRepeat = isRepeat;
  }

  IrReceiver.resume();
}
