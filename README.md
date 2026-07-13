# LCD Dino

LCD Dino is a tiny Chrome-Dino-style game for Arduino Uno, a 16x2 I2C LCD, an IR remote, two LEDs, and a buzzer.

The project is built with PlatformIO and keeps the game loop non-blocking so IR replay, jump input, LCD rendering, LEDs, and buzzer feedback can run together.

## Features

- Arduino Uno / ATmega328P
- 16x2 I2C LCD with custom dino, cactus, ground, play, and game-over characters
- IR remote control
- `PLAY/PAUSE` starts, pauses, resumes, and restarts after Game Over
- `5` / `OK` jumps
- Dirty-cell LCD rendering during gameplay to reduce flicker
- Red/green LED state indicators
- Jump chirp and Game Over melody
- Buzzer driver implemented without Arduino `tone()` to avoid IR timer conflicts
- Separate LED-only test firmware for wiring checks
- Separate IR+LCD test firmware for reading remote button codes

## Hardware

| Part | Arduino Uno pin |
| --- | --- |
| IR receiver signal | D4 |
| Red LED | D8 |
| Green LED | D9 |
| Buzzer | D13 |
| I2C LCD SDA | SDA / A4 |
| I2C LCD SCL | SCL / A5 |

The default LCD address is `0x27`. If the LCD backlight works but no text appears, try changing this line in `src/main.cpp`:

```cpp
LiquidCrystal_I2C lcd(0x27, 16, 2);
```

to:

```cpp
LiquidCrystal_I2C lcd(0x3F, 16, 2);
```

## Controls

| Remote button | IR command | Action |
| --- | --- | --- |
| `PLAY/PAUSE` | `0x43` | Start, pause, resume, restart after Game Over |
| `5` / `OK` | `0x1C` | Jump while the game is running |
| `CH` | `0x46` | Jump fallback |
| `VOL+` | `0x15` | Jump fallback |

Press `PLAY/PAUSE` first to start the game. Pressing `5` while the start screen is shown is ignored because the game is still waiting.

## LEDs

| State | Red LED D8 | Green LED D9 |
| --- | --- | --- |
| Waiting for `PLAY` | On | On |
| Running | Off | On |
| Game Over alert | On for 5 seconds | Off |
| Waiting for replay | Off | On |

## Buzzer

The buzzer is driven manually with `micros()` instead of Arduino `tone()`. This keeps IR input stable on Arduino Uno.

- Jump: short two-tone chirp
- Game Over: descending non-blocking melody
- Start/pause/score sounds are disabled by default

Relevant switches in `src/main.cpp`:

```cpp
constexpr bool AUDIO_ENABLED = false;
constexpr bool JUMP_AUDIO_ENABLED = true;
constexpr bool GAME_OVER_AUDIO_ENABLED = true;
```

## LCD Performance

During gameplay, the code does not redraw the full 16x2 display on every tick. It updates only the cells that changed:

- old/new dino position
- old/new cactus position
- score cells when the score changes

This dirty-cell renderer reduces I2C traffic and makes the animation feel much smoother on HD44780 I2C backpacks.

## Build

Install PlatformIO Core, then build:

```sh
pio run -e uno
```

Upload to the board:

```sh
pio run -e uno -t upload
```

If PlatformIO does not auto-detect the board, pass the port explicitly:

```sh
pio run -e uno -t upload --upload-port /dev/cu.usbserial-110
```

## LED Wiring Test

There is a minimal test firmware that only turns on both LEDs:

```sh
pio run -e led_test -t upload
```

Expected behavior:

- D8 red LED is on
- D9 green LED is on

If the LEDs do not light in this test, fix wiring before debugging the game logic.

## IR LCD Test

There is also a remote-code test firmware that reads the IR receiver and shows the last button command on the LCD:

```sh
pio run -e ir_lcd_test -t upload
```

LCD output:

- row 1: command in hexadecimal, for example `HEX CMD: 0x1C`
- row 2: command in binary, for example `BIN:00011100`

The test also prints extended decode data to Serial Monitor:

```sh
pio device monitor --baud 9600
```

Serial output includes protocol, address, normalized command, raw decoded data, flags, and repeat state.

## Learning Another Remote

For remotes that do not behave like a simple NEC keypad, use the learner workflow. This is useful for remotes that produce different `raw` packets or do not expose stable button commands. Air-conditioner remotes often send a changing state packet for the same physical button, so the learner captures multiple samples.

1. Upload the IR LCD test firmware:

   ```sh
   pio run -e ir_lcd_test -t upload --upload-port /dev/cu.usbserial-110
   ```

2. Run the learner:

   ```sh
   python3 tools/learn_ir_codes.py --port /dev/cu.usbserial-110 --samples 30 --build-game
   ```

3. Follow the prompts:

   - press the same `PLAY` / start / pause / replay button 30 times
   - wait 10 seconds while the LCD shows the transition prompt
   - press the same `JUMP` button 30 times

The learner writes unique packets to `include/ir_codes.h`. For long or stateful remotes, the game matches the learned raw-buffer fingerprint instead of trusting one short command byte. Duplicate packets inside one action are merged. Packets that appear in both `PLAY` and `JUMP` are treated as ambiguous and ignored, because they cannot safely represent two different actions.

After this learner flow, upload the main game again. Old learned rows without `rawlen` and `fingerprint` are intentionally not trusted for stateful remotes because they can make unrelated buttons look like `PLAY` or `JUMP`.

To learn and immediately upload the game:

```sh
python3 tools/learn_ir_codes.py --port /dev/cu.usbserial-110 --samples 30 --between-delay 10 --upload-test --upload-game
```

If `pyserial` is missing:

```sh
python3 -m pip install pyserial
```

## Project Layout

```text
.
├── platformio.ini
├── src/
│   └── main.cpp
├── src_ir_lcd_test/
│   └── main.cpp
├── src_led_test/
│   └── main.cpp
├── include/
│   └── ir_codes.h
└── tools/
    └── learn_ir_codes.py
```

## Notes

- The project currently targets Arduino Uno.
- IR feedback LED is disabled because Uno `LED_BUILTIN` is D13, and D13 is used by the buzzer.
- The game keeps sound and LED effects non-blocking so replay after Game Over remains responsive.
