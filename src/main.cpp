#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#define RAW_BUFFER_LENGTH 260
#include <IRremote.hpp>
#include "ir_codes.h"

constexpr uint8_t IR_PIN = 4;
constexpr uint8_t RED_LED_PIN = 8;
constexpr uint8_t GREEN_LED_PIN = 9;
constexpr uint8_t BUZZER_PIN = 13;

// Typical 21-key NEC remote:
// PLAY/PAUSE = 0x43, OK/5 = 0x1C. Some remotes label 0x1C as "5".
constexpr uint16_t CMD_PLAY_PAUSE = 0x43;
constexpr uint16_t CMD_JUMP_OK_OR_5 = 0x1C;
constexpr uint16_t CMD_JUMP_UP = 0x46;
constexpr uint16_t CMD_JUMP_VOL_UP = 0x15;

LiquidCrystal_I2C lcd(0x27, 16, 2);

constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;

byte dinoRunFrame1[8] = {
  B00000,
  B00110,
  B01111,
  B01110,
  B11110,
  B01100,
  B01010,
  B10010
};

byte dinoRunFrame2[8] = {
  B00000,
  B00110,
  B01111,
  B01110,
  B11110,
  B01100,
  B00110,
  B01001
};

byte dinoJumpFrame[8] = {
  B00000,
  B00110,
  B01111,
  B01110,
  B11110,
  B01100,
  B01100,
  B00000
};

byte cactusSmall[8] = {
  B00000,
  B00000,
  B00100,
  B10101,
  B10101,
  B11101,
  B00100,
  B00100
};

byte cactusLarge[8] = {
  B00100,
  B10100,
  B10101,
  B10101,
  B11111,
  B00100,
  B00100,
  B00100
};

byte groundSymbol[8] = {
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B10101,
  B11111
};

byte playSymbol[8] = {
  B10000,
  B11000,
  B11100,
  B11110,
  B11100,
  B11000,
  B10000,
  B00000
};

byte skullSymbol[8] = {
  B01110,
  B11111,
  B10101,
  B11111,
  B01110,
  B01110,
  B01010,
  B00000
};

constexpr uint8_t CHAR_DINO_RUN_1 = 0;
constexpr uint8_t CHAR_DINO_RUN_2 = 1;
constexpr uint8_t CHAR_DINO_JUMP = 2;
constexpr uint8_t CHAR_CACTUS_SMALL = 3;
constexpr uint8_t CHAR_CACTUS_LARGE = 4;
constexpr uint8_t CHAR_GROUND = 5;
constexpr uint8_t CHAR_PLAY = 6;
constexpr uint8_t CHAR_SKULL = 7;

enum class GameState : uint8_t {
  Waiting,
  Running,
  Paused,
  GameOver
};

GameState gameState = GameState::Waiting;

constexpr int8_t DINO_COLUMN = 1;
constexpr uint8_t GROUND_ROW = 1;
constexpr uint8_t JUMP_ROW = 0;

int8_t cactusColumn = 15;
uint8_t cactusType = CHAR_CACTUS_SMALL;
bool jumping = false;
uint16_t score = 0;

unsigned long movementInterval = 310;
constexpr unsigned long MIN_MOVEMENT_INTERVAL = 115;
constexpr unsigned long JUMP_DURATION = 700;
constexpr unsigned long RUN_ANIMATION_INTERVAL = 120;
constexpr unsigned long PAUSE_BLINK_INTERVAL = 400;
constexpr unsigned long GAME_OVER_RED_LED_DURATION = 5000;
constexpr unsigned long IR_DEBOUNCE_MS = 160;
constexpr bool IR_DEBUG = false;
constexpr bool AUDIO_ENABLED = false;
constexpr bool JUMP_AUDIO_ENABLED = true;
constexpr bool GAME_OVER_AUDIO_ENABLED = true;

unsigned long lastMovementTime = 0;
unsigned long jumpStartTime = 0;
unsigned long lastAnimationTime = 0;
unsigned long lastPauseBlinkTime = 0;
unsigned long lastAcceptedIrTime = 0;
unsigned long gameOverStartTime = 0;
uint16_t lastAcceptedIrCommand = 0;
uint32_t lastAcceptedIrRaw = 0;

bool alternateRunFrame = false;
bool pauseLedState = false;
bool gameOverReplayLedShown = false;
bool systemAwake = false;
uint8_t lastIrPinLevel = HIGH;
uint16_t irPinEdgeCount = 0;
unsigned long lastIrActivityReportTime = 0;

constexpr uint16_t GAME_OVER_FREQ[4] = {800, 600, 400, 220};
constexpr uint16_t GAME_OVER_TONE_DURATION[4] = {130, 150, 190, 350};
constexpr uint16_t GAME_OVER_GAP[4] = {145, 165, 205, 0};

struct GameOverMelodyState {
  uint8_t step = 0;
  unsigned long stepStart = 0;
  bool active = false;
};

GameOverMelodyState gameOverMelody;

struct BuzzerState {
  bool active = false;
  bool level = false;
  uint16_t halfPeriodMicros = 0;
  unsigned long nextToggleMicros = 0;
  unsigned long stopAtMillis = 0;
};

struct JumpSoundState {
  bool active = false;
  uint8_t step = 0;
  unsigned long stepStart = 0;
};

BuzzerState buzzerState;
JumpSoundState jumpSound;

uint8_t screenBuffer[LCD_ROWS][LCD_COLUMNS];
uint8_t previousScreenBuffer[LCD_ROWS][LCD_COLUMNS];
constexpr uint8_t UNKNOWN_CELL = 255;
constexpr int8_t UNKNOWN_POSITION = -127;
constexpr uint16_t UNKNOWN_SCORE = 0xFFFF;

const __FlashStringHelper* gameStateName() {
  switch (gameState) {
    case GameState::Waiting:
      return F("Waiting");
    case GameState::Running:
      return F("Running");
    case GameState::Paused:
      return F("Paused");
    case GameState::GameOver:
      return F("GameOver");
  }

  return F("Unknown");
}

void printGameAction(const __FlashStringHelper* action) {
  if (!IR_DEBUG) {
    return;
  }

  Serial.print(F("GAME action="));
  Serial.print(action);
  Serial.print(F(" state="));
  Serial.println(gameStateName());
}

void clearScreenBuffer() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    for (uint8_t column = 0; column < LCD_COLUMNS; column++) {
      screenBuffer[row][column] = ' ';
    }
  }
}

void invalidatePreviousBuffer() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    for (uint8_t column = 0; column < LCD_COLUMNS; column++) {
      previousScreenBuffer[row][column] = UNKNOWN_CELL;
    }
  }
}

void putText(uint8_t column, uint8_t row, const char* text) {
  if (row >= LCD_ROWS) {
    return;
  }

  while (*text != '\0' && column < LCD_COLUMNS) {
    screenBuffer[row][column] = static_cast<uint8_t>(*text);
    column++;
    text++;
  }
}

void putNumber(uint8_t column, uint8_t row, uint16_t value) {
  char numberBuffer[6];
  snprintf(numberBuffer, sizeof(numberBuffer), "%u", value);
  putText(column, row, numberBuffer);
}

void putCustomCharacter(uint8_t column, uint8_t row, uint8_t character) {
  if (column >= LCD_COLUMNS || row >= LCD_ROWS) {
    return;
  }

  screenBuffer[row][column] = character;
}

void writeLcdCell(uint8_t column, uint8_t row, uint8_t value) {
  lcd.setCursor(column, row);

  if (value <= 7) {
    lcd.write(value);
  } else {
    lcd.write(static_cast<char>(value));
  }

  previousScreenBuffer[row][column] = value;
}

void updateLcdFromBuffer() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    for (uint8_t column = 0; column < LCD_COLUMNS; column++) {
      const uint8_t newValue = screenBuffer[row][column];

      if (newValue == previousScreenBuffer[row][column]) {
        continue;
      }

      writeLcdCell(column, row, newValue);
    }
  }
}

void setWaitingLeds() {
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);
}

void setPowerIdleOutputs() {
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

void setRunningLeds() {
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, LOW);
}

void setGameOverAlertLeds() {
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);
}

void setGameOverReplayLeds() {
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, LOW);
}

void stopBuzzer() {
  buzzerState.active = false;
  buzzerState.level = false;
  digitalWrite(BUZZER_PIN, LOW);
}

void startBuzzer(uint16_t frequency, uint16_t durationMs) {
  if (frequency == 0 || durationMs == 0) {
    stopBuzzer();
    return;
  }

  buzzerState.active = true;
  buzzerState.level = false;
  buzzerState.halfPeriodMicros = 500000UL / frequency;
  buzzerState.nextToggleMicros = micros();
  buzzerState.stopAtMillis = millis() + durationMs;
  digitalWrite(BUZZER_PIN, LOW);
}

void updateBuzzer() {
  if (!buzzerState.active) {
    return;
  }

  if (static_cast<long>(millis() - buzzerState.stopAtMillis) >= 0) {
    stopBuzzer();
    return;
  }

  const unsigned long nowMicros = micros();
  if (static_cast<long>(nowMicros - buzzerState.nextToggleMicros) >= 0) {
    buzzerState.level = !buzzerState.level;
    digitalWrite(BUZZER_PIN, buzzerState.level ? HIGH : LOW);
    buzzerState.nextToggleMicros += buzzerState.halfPeriodMicros;
  }
}

void playStartSound() {
  if (!AUDIO_ENABLED) {
    return;
  }
  startBuzzer(900, 70);
}

void playJumpSound() {
  if (!JUMP_AUDIO_ENABLED) {
    return;
  }

  jumpSound.active = true;
  jumpSound.step = 0;
  jumpSound.stepStart = millis();
  startBuzzer(1500, 35);
}

void playScoreSound() {
  if (!AUDIO_ENABLED) {
    return;
  }
  startBuzzer(1800, 70);
}

void playPauseSound() {
  if (!AUDIO_ENABLED) {
    return;
  }
  startBuzzer(700, 70);
}

void playResumeSound() {
  if (!AUDIO_ENABLED) {
    return;
  }
  startBuzzer(1100, 70);
}

void updateJumpSound() {
  if (!jumpSound.active) {
    return;
  }

  if (jumpSound.step == 0 && millis() - jumpSound.stepStart >= 42) {
    jumpSound.step = 1;
    jumpSound.stepStart = millis();
    startBuzzer(2300, 45);
    return;
  }

  if (jumpSound.step == 1 && millis() - jumpSound.stepStart >= 55) {
    jumpSound.active = false;
  }
}

void startGameOverSound() {
  if (!GAME_OVER_AUDIO_ENABLED) {
    gameOverMelody.active = false;
    stopBuzzer();
    return;
  }

  gameOverMelody.step = 0;
  gameOverMelody.stepStart = millis();
  gameOverMelody.active = true;
  startBuzzer(GAME_OVER_FREQ[0], GAME_OVER_TONE_DURATION[0]);
}

void updateGameOverSound() {
  if (!gameOverMelody.active) {
    return;
  }

  const unsigned long stepTotal =
    GAME_OVER_TONE_DURATION[gameOverMelody.step] +
    GAME_OVER_GAP[gameOverMelody.step];

  if (millis() - gameOverMelody.stepStart < stepTotal) {
    return;
  }

  gameOverMelody.step++;

  if (gameOverMelody.step >= 4) {
    gameOverMelody.active = false;
    stopBuzzer();
    return;
  }

  gameOverMelody.stepStart = millis();
  startBuzzer(GAME_OVER_FREQ[gameOverMelody.step], GAME_OVER_TONE_DURATION[gameOverMelody.step]);
}

void updateGameOverLeds() {
  if (gameState != GameState::GameOver || gameOverReplayLedShown) {
    return;
  }

  if (millis() - gameOverStartTime >= GAME_OVER_RED_LED_DURATION) {
    gameOverReplayLedShown = true;
    setGameOverReplayLeds();
  }
}

void drawWaitingScreen() {
  clearScreenBuffer();
  putCustomCharacter(0, 0, CHAR_PLAY);
  putText(2, 0, "DINO GAME");
  putText(0, 1, "PLAY TO START");
  updateLcdFromBuffer();
}

void drawPausedScreen() {
  clearScreenBuffer();
  putText(3, 0, "PAUSED");
  putText(0, 1, "PLAY TO RESUME");
  updateLcdFromBuffer();
}

void drawGameOverScreen() {
  clearScreenBuffer();
  putCustomCharacter(0, 0, CHAR_SKULL);
  putText(2, 0, "GAME OVER");

  if (score < 10) {
    putNumber(15, 0, score);
  } else if (score < 100) {
    putNumber(14, 0, score);
  } else {
    putNumber(13, 0, score);
  }

  putText(1, 1, "PLAY=RESTART");
  updateLcdFromBuffer();
}

void drawGameScreen() {
  clearScreenBuffer();

  for (uint8_t column = 0; column < LCD_COLUMNS; column++) {
    putCustomCharacter(column, GROUND_ROW, CHAR_GROUND);
  }

  const uint8_t dinoRow = jumping ? JUMP_ROW : GROUND_ROW;
  const uint8_t dinoCharacter =
    jumping ? CHAR_DINO_JUMP : (alternateRunFrame ? CHAR_DINO_RUN_2 : CHAR_DINO_RUN_1);

  putCustomCharacter(DINO_COLUMN, dinoRow, dinoCharacter);

  if (cactusColumn >= 0 && cactusColumn < LCD_COLUMNS) {
    putCustomCharacter(static_cast<uint8_t>(cactusColumn), GROUND_ROW, cactusType);
  }

  putText(11, 0, "S:");

  if (score < 10) {
    putNumber(15, 0, score);
  } else if (score < 100) {
    putNumber(14, 0, score);
  } else {
    putNumber(13, 0, score);
  }

  updateLcdFromBuffer();
}

uint8_t dinoRow() {
  return jumping ? JUMP_ROW : GROUND_ROW;
}

uint8_t dinoCharacter() {
  return jumping ? CHAR_DINO_JUMP : (alternateRunFrame ? CHAR_DINO_RUN_2 : CHAR_DINO_RUN_1);
}

uint8_t scoreCellAt(uint8_t column) {
  if (column == 11) {
    return 'S';
  }

  if (column == 12) {
    return ':';
  }

  char digits[4] = {' ', ' ', ' ', '\0'};
  const uint16_t shownScore = score > 999 ? 999 : score;
  snprintf(digits, sizeof(digits), "%3u", shownScore);

  if (column >= 13 && column <= 15) {
    return static_cast<uint8_t>(digits[column - 13]);
  }

  return ' ';
}

uint8_t gameCellAt(uint8_t column, uint8_t row) {
  if (column == DINO_COLUMN && row == dinoRow()) {
    return dinoCharacter();
  }

  if (row == GROUND_ROW) {
    if (cactusColumn >= 0 &&
        cactusColumn < LCD_COLUMNS &&
        column == static_cast<uint8_t>(cactusColumn)) {
      return cactusType;
    }

    return CHAR_GROUND;
  }

  if (row == 0 && column >= 11) {
    return scoreCellAt(column);
  }

  return ' ';
}

void renderGameCellIfChanged(uint8_t column, uint8_t row) {
  if (column >= LCD_COLUMNS || row >= LCD_ROWS) {
    return;
  }

  const uint8_t value = gameCellAt(column, row);
  if (previousScreenBuffer[row][column] != value) {
    writeLcdCell(column, row, value);
  }
}

void renderGameDirtyCells(int8_t oldDinoRow, int8_t oldCactusColumn, uint16_t oldScore) {
  if (oldDinoRow >= 0) {
    renderGameCellIfChanged(DINO_COLUMN, static_cast<uint8_t>(oldDinoRow));
  }

  renderGameCellIfChanged(DINO_COLUMN, dinoRow());

  if (oldCactusColumn >= 0 && oldCactusColumn < LCD_COLUMNS) {
    renderGameCellIfChanged(static_cast<uint8_t>(oldCactusColumn), GROUND_ROW);
  }

  if (cactusColumn >= 0 && cactusColumn < LCD_COLUMNS) {
    renderGameCellIfChanged(static_cast<uint8_t>(cactusColumn), GROUND_ROW);
  }

  if (oldScore != score) {
    for (uint8_t column = 11; column < LCD_COLUMNS; column++) {
      renderGameCellIfChanged(column, 0);
    }
  }
}

void generateNewCactus() {
  cactusColumn = 15 + random(0, 4);
  cactusType = random(0, 2) == 0 ? CHAR_CACTUS_SMALL : CHAR_CACTUS_LARGE;
}

void resetIrDebounce() {
  lastAcceptedIrTime = 0;
  lastAcceptedIrCommand = 0;
  lastAcceptedIrRaw = 0;
}

void powerOnSystem() {
  systemAwake = true;
  gameState = GameState::Waiting;
  jumping = false;
  jumpSound.active = false;
  gameOverMelody.active = false;
  stopBuzzer();
  resetIrDebounce();

  lcd.backlight();
  lcd.clear();
  invalidatePreviousBuffer();
  setWaitingLeds();
  drawWaitingScreen();
  printGameAction(F("powerOn"));
}

void powerOffSystem() {
  systemAwake = false;
  gameState = GameState::Waiting;
  jumping = false;
  jumpSound.active = false;
  gameOverMelody.active = false;
  stopBuzzer();
  resetIrDebounce();

  clearScreenBuffer();
  lcd.clear();
  lcd.noBacklight();
  invalidatePreviousBuffer();
  setPowerIdleOutputs();
  printGameAction(F("powerOff"));
}

void startNewGame() {
  score = 0;
  jumping = false;
  alternateRunFrame = false;
  movementInterval = 310;

  generateNewCactus();

  const unsigned long now = millis();
  lastMovementTime = now;
  lastAnimationTime = now;
  jumpStartTime = 0;
  gameOverStartTime = 0;
  gameOverReplayLedShown = false;
  resetIrDebounce();

  gameOverMelody.active = false;
  jumpSound.active = false;
  stopBuzzer();

  gameState = GameState::Running;
  printGameAction(F("startNewGame"));
  setRunningLeds();
  playStartSound();
  drawGameScreen();
}

void startJump() {
  if (gameState != GameState::Running || jumping) {
    printGameAction(F("jumpBlocked"));
    return;
  }

  const int8_t oldDinoRow = dinoRow();
  jumping = true;
  jumpStartTime = millis();
  lastAnimationTime = jumpStartTime;
  printGameAction(F("jump"));
  playJumpSound();
  renderGameDirtyCells(oldDinoRow, cactusColumn, score);
}

bool cactusTouchesDino() {
  return !jumping && cactusColumn == DINO_COLUMN;
}

void finishGame() {
  gameState = GameState::GameOver;
  jumping = false;
  jumpSound.active = false;
  gameOverStartTime = millis();
  gameOverReplayLedShown = false;
  resetIrDebounce();
  printGameAction(F("gameOver"));
  setGameOverAlertLeds();
  startGameOverSound();
  drawGameOverScreen();
}

void increaseDifficulty() {
  if (score % 2 == 0 && movementInterval > MIN_MOVEMENT_INTERVAL) {
    if (movementInterval >= 140) {
      movementInterval -= 12;
    } else {
      movementInterval = MIN_MOVEMENT_INTERVAL;
    }
  }
}

void updateRunningGame() {
  if (gameState != GameState::Running) {
    return;
  }

  const unsigned long now = millis();
  bool needsRedraw = false;
  const int8_t oldDinoRow = dinoRow();
  const int8_t oldCactusColumn = cactusColumn;
  const uint16_t oldScore = score;

  if (jumping && now - jumpStartTime >= JUMP_DURATION) {
    jumping = false;
    needsRedraw = true;
  }

  if (!jumping && now - lastAnimationTime >= RUN_ANIMATION_INTERVAL) {
    lastAnimationTime = now;
    alternateRunFrame = !alternateRunFrame;
    needsRedraw = true;
  }

  if (now - lastMovementTime >= movementInterval) {
    lastMovementTime = now;
    cactusColumn--;
    needsRedraw = true;

    if (cactusTouchesDino()) {
      finishGame();
      return;
    }

    if (cactusColumn < 0) {
      score++;
      playScoreSound();
      increaseDifficulty();
      generateNewCactus();
    }
  }

  if (needsRedraw) {
    renderGameDirtyCells(oldDinoRow, oldCactusColumn, oldScore);
  }
}

void pauseGame() {
  if (gameState != GameState::Running) {
    return;
  }

  gameState = GameState::Paused;
  pauseLedState = true;
  lastPauseBlinkTime = millis();

  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, LOW);
  playPauseSound();
  drawPausedScreen();
}

void resumeGame() {
  if (gameState != GameState::Paused) {
    return;
  }

  gameState = GameState::Running;

  const unsigned long now = millis();
  lastMovementTime = now;
  lastAnimationTime = now;

  if (jumping) {
    jumpStartTime = now;
  }

  setRunningLeds();
  playResumeSound();
  drawGameScreen();
}

void updatePausedState() {
  if (gameState != GameState::Paused) {
    return;
  }

  const unsigned long now = millis();

  if (now - lastPauseBlinkTime >= PAUSE_BLINK_INTERVAL) {
    lastPauseBlinkTime = now;
    pauseLedState = !pauseLedState;
    digitalWrite(GREEN_LED_PIN, pauseLedState ? HIGH : LOW);
  }
}

void handlePlayPauseButton() {
  switch (gameState) {
    case GameState::Waiting:
      startNewGame();
      break;
    case GameState::Running:
      pauseGame();
      break;
    case GameState::Paused:
      resumeGame();
      break;
    case GameState::GameOver:
      startNewGame();
      break;
  }
}

bool learnedCodeMatches(const LearnedIrCode& expected, const IRData& data, uint16_t command) {
  const uint8_t protocol = static_cast<uint8_t>(data.protocol);
  const bool protocolMatches = expected.protocol == 0 || expected.protocol == protocol;
  const bool addressMatches = expected.address == data.address;

  if (expected.command != 0) {
    return data.command != 0 &&
           command == expected.command &&
           protocolMatches &&
           addressMatches;
  }

  return expected.raw != 0 &&
         data.decodedRawData == expected.raw &&
         protocolMatches &&
         addressMatches;
}

bool isLearnedPlayCommand(const IRData& data, uint16_t command) {
  for (uint8_t i = 0; i < IR_LEARNED_PLAY_COUNT; i++) {
    LearnedIrCode learnedCode;
    memcpy_P(&learnedCode, &IR_LEARNED_PLAY_CODES[i], sizeof(learnedCode));

    if (learnedCodeMatches(learnedCode, data, command)) {
      return true;
    }
  }

  return false;
}

bool isLearnedPowerCommand(const IRData& data, uint16_t command) {
  for (uint8_t i = 0; i < IR_LEARNED_POWER_COUNT; i++) {
    LearnedIrCode learnedCode;
    memcpy_P(&learnedCode, &IR_LEARNED_POWER_CODES[i], sizeof(learnedCode));

    if (learnedCodeMatches(learnedCode, data, command)) {
      return true;
    }
  }

  return false;
}

bool isLearnedJumpCommand(const IRData& data, uint16_t command) {
  for (uint8_t i = 0; i < IR_LEARNED_JUMP_COUNT; i++) {
    LearnedIrCode learnedCode;
    memcpy_P(&learnedCode, &IR_LEARNED_JUMP_CODES[i], sizeof(learnedCode));

    if (learnedCodeMatches(learnedCode, data, command)) {
      return true;
    }
  }

  return false;
}

bool isStandardJumpCommand(const IRData& data, uint16_t command) {
  if (IR_LEARNED_JUMP_COUNT > 0) {
    return false;
  }

  if (data.command == 0) {
    return false;
  }

  return command == CMD_JUMP_OK_OR_5 ||
         command == CMD_JUMP_UP ||
         command == CMD_JUMP_VOL_UP;
}

bool isPowerCommand(const IRData& data, uint16_t command) {
  if (IR_LEARNED_POWER_COUNT > 0) {
    return isLearnedPowerCommand(data, command);
  }

  return false;
}

bool isPlayCommand(const IRData& data, uint16_t command) {
  if (IR_LEARNED_PLAY_COUNT > 0) {
    return isLearnedPlayCommand(data, command);
  }

  return data.command != 0 && command == CMD_PLAY_PAUSE;
}

bool isJumpCommand(const IRData& data, uint16_t command) {
  if (IR_LEARNED_JUMP_COUNT > 0) {
    return isLearnedJumpCommand(data, command);
  }

  return isStandardJumpCommand(data, command);
}

void handlePowerButton() {
  if (systemAwake) {
    powerOffSystem();
  } else {
    powerOnSystem();
  }
}

void handleRemoteCommand(const IRData& data, uint16_t command) {
  if (isPowerCommand(data, command)) {
    handlePowerButton();
    return;
  }

  if (!systemAwake) {
    return;
  }

  if (isPlayCommand(data, command)) {
    handlePlayPauseButton();
    return;
  }

  if (isJumpCommand(data, command)) {
    startJump();
  }
}

uint16_t normalizeIrCommand(const IRData& data) {
  if (data.command != 0) {
    return data.command;
  }

  // For NEC-style packets IRremote's raw value commonly stores command in byte 2.
  const uint16_t rawCommand = (data.decodedRawData >> 16) & 0xFF;
  return rawCommand;
}

void printIrDebug(const IRData& data, uint16_t command, bool accepted) {
  if (!IR_DEBUG) {
    return;
  }

  Serial.print(F("IR protocol="));
  Serial.print(static_cast<uint8_t>(data.protocol));
  Serial.print(F(" addr=0x"));
  Serial.print(data.address, HEX);
  Serial.print(F(" cmd=0x"));
  Serial.print(data.command, HEX);
  Serial.print(F(" normalized=0x"));
  Serial.print(command, HEX);
  Serial.print(F(" raw=0x"));
  Serial.print(data.decodedRawData, HEX);
  Serial.print(F(" flags=0x"));
  Serial.print(data.flags, HEX);
  Serial.print(F(" accepted="));
  Serial.println(accepted ? F("yes") : F("no"));
}

bool shouldAcceptIrCommand(const IRData& data, uint16_t command, bool isRepeat) {
  if (command == 0 && data.decodedRawData == 0) {
    return false;
  }

  const unsigned long now = millis();

  // Holding PLAY/POWER should not rapidly toggle state, but the first PLAY after
  // Game Over must be accepted. finishGame() clears this debounce state.
  if (isRepeat && (isPlayCommand(data, command) || isPowerCommand(data, command))) {
    return false;
  }

  if (command == lastAcceptedIrCommand &&
      data.decodedRawData == lastAcceptedIrRaw &&
      now - lastAcceptedIrTime < IR_DEBOUNCE_MS) {
    return false;
  }

  lastAcceptedIrCommand = command;
  lastAcceptedIrRaw = data.decodedRawData;
  lastAcceptedIrTime = now;
  return true;
}

void readRemote() {
  if (!IrReceiver.decode()) {
    return;
  }

  const IRData& data = IrReceiver.decodedIRData;
  const uint16_t command = normalizeIrCommand(data);
  const bool isRepeat = (data.flags & IRDATA_FLAGS_IS_REPEAT) != 0;
  const bool accepted = shouldAcceptIrCommand(data, command, isRepeat);

  printIrDebug(data, command, accepted);

  if (accepted) {
    handleRemoteCommand(data, command);
  }

  IrReceiver.resume();
}

void updateIrSignalDebug() {
  if (!IR_DEBUG) {
    return;
  }

  const uint8_t level = digitalRead(IR_PIN);
  if (level != lastIrPinLevel) {
    lastIrPinLevel = level;
    irPinEdgeCount++;
  }

  const unsigned long now = millis();
  if (irPinEdgeCount > 0 && now - lastIrActivityReportTime >= 500) {
    Serial.print(F("IR pin activity edges="));
    Serial.print(irPinEdgeCount);
    Serial.print(F(" level="));
    Serial.println(level == HIGH ? F("HIGH") : F("LOW"));
    irPinEdgeCount = 0;
    lastIrActivityReportTime = now;
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(IR_PIN, INPUT);

  setPowerIdleOutputs();

  Wire.begin();
  Wire.setClock(400000);

  lcd.init();
  lcd.createChar(CHAR_DINO_RUN_1, dinoRunFrame1);
  lcd.createChar(CHAR_DINO_RUN_2, dinoRunFrame2);
  lcd.createChar(CHAR_DINO_JUMP, dinoJumpFrame);
  lcd.createChar(CHAR_CACTUS_SMALL, cactusSmall);
  lcd.createChar(CHAR_CACTUS_LARGE, cactusLarge);
  lcd.createChar(CHAR_GROUND, groundSymbol);
  lcd.createChar(CHAR_PLAY, playSymbol);
  lcd.createChar(CHAR_SKULL, skullSymbol);

  lcd.clear();
  lcd.noBacklight();
  invalidatePreviousBuffer();
  // D13 is used by the buzzer, so do not let IRremote use LED_BUILTIN feedback.
  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);

  randomSeed(analogRead(A0) ^ analogRead(A1) ^ micros());
  Serial.println(F("LCD Dino ready. Press POWER to wake."));
}

void loop() {
  readRemote();
  updateIrSignalDebug();
  updateRunningGame();
  updatePausedState();
  updateGameOverLeds();
  updateJumpSound();
  updateGameOverSound();
  updateBuzzer();
}
