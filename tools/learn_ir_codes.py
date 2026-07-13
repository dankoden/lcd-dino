#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
    raise


ROOT = Path(__file__).resolve().parents[1]
HEADER_PATH = ROOT / "include" / "ir_codes.h"

LEARN_RE = re.compile(
    r"LEARN label=(?P<label>\w+)\s+"
    r"protocol=(?P<protocol>\d+)\s+"
    r"address=0x(?P<address>[0-9A-Fa-f]+)\s+"
    r"command=0x(?P<command>[0-9A-Fa-f]+)\s+"
    r"raw=0x(?P<raw>[0-9A-Fa-f]+)\s+"
    r"rawlen=(?P<rawlen>\d+)\s+"
    r"fp=0x(?P<fingerprint>[0-9A-Fa-f]+)\s+"
    r"flags=0x(?P<flags>[0-9A-Fa-f]+)\s+"
    r"repeat=(?P<repeat>yes|no)"
)

def run_pio(args):
    subprocess.run(["pio", *args], cwd=ROOT, check=True)


def parse_learn_line(line):
    match = LEARN_RE.search(line)
    if not match:
        return None

    return {
        "label": match.group("label"),
        "protocol": int(match.group("protocol"), 10),
        "address": int(match.group("address"), 16),
        "command": int(match.group("command"), 16),
        "raw": int(match.group("raw"), 16),
        "rawlen": int(match.group("rawlen"), 10),
        "fingerprint": int(match.group("fingerprint"), 16),
        "flags": int(match.group("flags"), 16),
        "repeat": match.group("repeat") == "yes",
    }


def wait_for_learn(ser, label, timeout):
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        raw_line = ser.readline()
        if not raw_line:
            continue

        line = raw_line.decode("utf-8", "replace").strip()
        if line:
            print(line)

        learned = parse_learn_line(line)
        if learned and learned["label"] == label and not learned["repeat"]:
            return learned

    raise TimeoutError(f"Timed out waiting for {label} IR code")


def capture_button(ser, label, command, timeout):
    print()
    print(f"Press the remote button for {label}.")
    ser.write(command.encode("ascii"))
    ser.flush()
    return wait_for_learn(ser, label, timeout)


def code_key(code):
    if code.get("fingerprint"):
        return ("fp", code["rawlen"], code["fingerprint"])
    return ("raw", code["protocol"], code["address"], code["command"], code["raw"])


def unique_codes(samples):
    unique = []
    seen = set()

    for sample in samples:
        key = code_key(sample)
        if key in seen:
            continue

        seen.add(key)
        unique.append(sample)

    return unique


def remove_ambiguous_codes(play_codes, jump_codes):
    play_keys = {code_key(code) for code in play_codes}
    jump_keys = {code_key(code) for code in jump_codes}
    ambiguous_keys = play_keys & jump_keys

    if not ambiguous_keys:
        return play_codes, jump_codes

    print()
    print("Ambiguous codes found in both PLAY and JUMP; these will be ignored:")
    for key in sorted(ambiguous_keys):
        print(f"  {key}")

    filtered_play = [code for code in play_codes if code_key(code) not in ambiguous_keys]
    filtered_jump = [code for code in jump_codes if code_key(code) not in ambiguous_keys]

    if not filtered_play:
        raise RuntimeError("All PLAY codes were ambiguous. Choose a different PLAY button.")

    if not filtered_jump:
        raise RuntimeError("All JUMP codes were ambiguous. Choose a different JUMP button.")

    return filtered_play, filtered_jump


def capture_button_samples(ser, label, command, sample_count, timeout):
    samples = []

    for index in range(sample_count):
        print()
        print(f"{label}: sample {index + 1}/{sample_count}")
        samples.append(capture_button(ser, label, command, timeout))

    unique = unique_codes(samples)
    print()
    print(f"{label}: captured {len(samples)} samples, {len(unique)} unique codes")
    for index, code in enumerate(unique, start=1):
        print(
            f"  {index}: protocol={code['protocol']} "
            f"address=0x{code['address']:X} command=0x{code['command']:X} "
            f"raw=0x{code['raw']:X} rawlen={code['rawlen']} fp=0x{code['fingerprint']:X}"
        )

    return unique


def wait_between_buttons(ser, delay_seconds):
    if delay_seconds <= 0:
        return

    ser.write(b"W")
    ser.flush()

    print()
    print(f"Waiting {delay_seconds} seconds before JUMP capture. LCD should show the next step.")

    for remaining in range(delay_seconds, 0, -1):
        print(f"  next step in {remaining}s")
        time.sleep(1)


def format_code_array(name, codes):
    rows = []
    for code in codes:
        rows.append(
            f"  {{{code['protocol']}, 0x{code['address']:04X}, "
            f"0x{code['command']:04X}, 0x{code['raw']:08X}UL, "
            f"{code['rawlen']}, 0x{code['fingerprint']:08X}UL}},"
        )

    body = "\n".join(rows)
    return f"""constexpr uint8_t IR_LEARNED_{name}_COUNT = {len(codes)};
const LearnedIrCode IR_LEARNED_{name}_CODES[] PROGMEM = {{
{body}
}};
"""


def format_header(play_codes, jump_codes):
    return f"""#pragma once

#include <Arduino.h>
#include <avr/pgmspace.h>

// Generated by tools/learn_ir_codes.py.
// Re-run the learner when using another remote.

struct LearnedIrCode {{
  uint8_t protocol;
  uint16_t address;
  uint16_t command;
  uint32_t raw;
  uint16_t rawlen;
  uint32_t fingerprint;
}};

{format_code_array("PLAY", play_codes)}
{format_code_array("JUMP", jump_codes)}
"""


def write_header(play_codes, jump_codes):
    HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.write_text(format_header(play_codes, jump_codes), encoding="utf-8")
    print()
    print(f"Wrote {HEADER_PATH}")
    print(f"PLAY unique codes: {len(play_codes)}")
    print(f"JUMP unique codes: {len(jump_codes)}")


def main():
    parser = argparse.ArgumentParser(description="Learn PLAY and JUMP IR codes over Serial and generate include/ir_codes.h.")
    parser.add_argument("--port", default="/dev/cu.usbserial-110", help="Serial port for Arduino")
    parser.add_argument("--baud", type=int, default=9600, help="Serial baud rate")
    parser.add_argument("--timeout", type=float, default=30.0, help="Seconds to wait for each button")
    parser.add_argument("--upload-test", action="store_true", help="Upload ir_lcd_test before learning")
    parser.add_argument("--build-game", action="store_true", help="Build the main game after writing codes")
    parser.add_argument("--upload-game", action="store_true", help="Upload the main game after writing codes")
    parser.add_argument("--samples", type=int, default=30, help="Samples to capture for each button")
    parser.add_argument("--between-delay", type=int, default=10, help="Seconds to wait between PLAY and JUMP capture")
    args = parser.parse_args()

    if args.samples < 1:
        parser.error("--samples must be at least 1")

    if args.upload_test:
        run_pio(["run", "-e", "ir_lcd_test", "-t", "upload", "--upload-port", args.port])
        time.sleep(1.5)

    with serial.Serial(args.port, args.baud, timeout=0.25) as ser:
        time.sleep(2.0)
        ser.reset_input_buffer()
        ser.write(b"I")
        ser.flush()

        play_codes = capture_button_samples(ser, "PLAY", "P", args.samples, args.timeout)
        wait_between_buttons(ser, args.between_delay)
        jump_codes = capture_button_samples(ser, "JUMP", "J", args.samples, args.timeout)

    play_codes, jump_codes = remove_ambiguous_codes(play_codes, jump_codes)

    write_header(play_codes, jump_codes)

    if args.build_game or args.upload_game:
        run_pio(["run", "-e", "uno"])

    if args.upload_game:
        run_pio(["run", "-e", "uno", "-t", "upload", "--upload-port", args.port])


if __name__ == "__main__":
    main()
