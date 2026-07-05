#!/usr/bin/env python3
"""Bridge RIGHT_NODE serial state logs to a Linux virtual keyboard.

The ESP32 right node remains the real-time consolidator. This script runs on
Linux, reads the ESP2 serial output, parses the consolidated keyboard state
line, and emits key events through uinput.
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import sys
from dataclasses import dataclass
from typing import Dict, Iterable, Optional

import serial
from serial import SerialException


STATE_RE = re.compile(
    r"Keyboard state:\s+"
    r"A=(?P<A>[01])\s+"
    r"B=(?P<B>[01])\s+"
    r"UP=(?P<UP>[01])\s+"
    r"DOWN=(?P<DOWN>[01])\s+"
    r"LEFT=(?P<LEFT>[01])\s+"
    r"RIGHT=(?P<RIGHT>[01])"
)

KEY_ORDER = ("A", "B", "UP", "DOWN", "LEFT", "RIGHT")


@dataclass(frozen=True)
class KeyChange:
    name: str
    is_pressed: bool


def parse_keyboard_state(line: str) -> Optional[Dict[str, bool]]:
    match = STATE_RE.search(line)
    if match is None:
        return None

    return {name: match.group(name) == "1" for name in KEY_ORDER}


def diff_states(
    previous: Dict[str, bool],
    current: Dict[str, bool],
) -> Iterable[KeyChange]:
    for name in KEY_ORDER:
        if previous[name] != current[name]:
            yield KeyChange(name=name, is_pressed=current[name])


class DryRunKeyboard:
    def emit(self, change: KeyChange) -> None:
        action = "pressed" if change.is_pressed else "released"
        print(f"[dry-run] {change.name} {action}", flush=True)

    def release_all(self, state: Dict[str, bool]) -> None:
        for name, is_pressed in state.items():
            if is_pressed:
                print(f"[dry-run] {name} released", flush=True)


class UInputKeyboard:
    def __init__(self) -> None:
        from evdev import UInput, ecodes

        self._ecodes = ecodes
        self._key_map = {
            "A": ecodes.KEY_A,
            "B": ecodes.KEY_B,
            "UP": ecodes.KEY_UP,
            "DOWN": ecodes.KEY_DOWN,
            "LEFT": ecodes.KEY_LEFT,
            "RIGHT": ecodes.KEY_RIGHT,
        }
        capabilities = {ecodes.EV_KEY: list(self._key_map.values())}
        self._device = UInput(capabilities, name="espnow-split-keyboard")

    def emit(self, change: KeyChange) -> None:
        value = 1 if change.is_pressed else 0

        self._device.write(
            self._ecodes.EV_KEY,
            self._key_map[change.name],
            value,
        )
        self._device.syn()

    def release_all(self, state: Dict[str, bool]) -> None:
        for name, is_pressed in state.items():
            if is_pressed:
                self._device.write(
                    self._ecodes.EV_KEY,
                    self._key_map[name],
                    0,
                )
        self._device.syn()


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Read ESP2 RIGHT_NODE serial logs and emit Linux key events.",
    )
    parser.add_argument(
        "-p",
        "--port",
        default="/dev/ttyUSB1",
        help="Serial port connected to ESP2. Default: /dev/ttyUSB1",
    )
    parser.add_argument(
        "-b",
        "--baudrate",
        default=115200,
        type=int,
        help="Serial baudrate. Default: 115200",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Parse and print key changes without creating a uinput keyboard.",
    )
    parser.add_argument(
        "--echo-serial",
        action="store_true",
        help="Also print every serial line received from ESP2.",
    )
    return parser


def open_esp_serial(port: str, baudrate: int) -> serial.Serial:
    esp_serial = serial.Serial()

    esp_serial.port = port
    esp_serial.baudrate = baudrate
    esp_serial.timeout = 1
    esp_serial.write_timeout = 1
    esp_serial.exclusive = True

    # Avoid toggling auto-reset control lines while opening the monitor port.
    esp_serial.dtr = False
    esp_serial.rts = False

    esp_serial.open()
    esp_serial.reset_input_buffer()

    return esp_serial


def print_serial_error(port: str, err: SerialException) -> None:
    print(
        "\nSerial bridge stopped: "
        f"could not keep reading {port}.\n"
        f"pyserial error: {err}\n\n"
        "Check these first:\n"
        "  1. Close make esp2-monitor or any other serial monitor.\n"
        "  2. Confirm ESP2 is still connected on the same port.\n"
        f"  3. Check who is using the port: lsof {port}\n"
        "  4. Run dry mode again after ESP2 has finished rebooting.",
        file=sys.stderr,
        flush=True,
    )


def print_uinput_error(err: Exception) -> None:
    print(
        "\nCould not create the Linux virtual keyboard at /dev/uinput.\n"
        f"Error: {err}\n\n"
        "Try:\n"
        "  sudo modprobe uinput\n"
        "  sudo make py\n\n"
        "If you do not want to use sudo every time, create a udev rule later "
        "to grant your user write access to /dev/uinput.",
        file=sys.stderr,
        flush=True,
    )


def main() -> int:
    args = build_arg_parser().parse_args()
    current_state = {name: False for name in KEY_ORDER}
    keep_running = True

    def stop(_signum: int, _frame: object) -> None:
        nonlocal keep_running
        keep_running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    try:
        keyboard = DryRunKeyboard() if args.dry_run else UInputKeyboard()
    except Exception as err:
        if os.path.exists("/dev/uinput"):
            print_uinput_error(err)
            return 3

        print(
            "\n/dev/uinput does not exist. Load the Linux uinput module first:\n"
            "  sudo modprobe uinput\n"
            "  sudo make py",
            file=sys.stderr,
            flush=True,
        )
        return 3

    print(
        f"Opening {args.port} at {args.baudrate} baud. "
        "Press Ctrl+C to stop.",
        flush=True,
    )

    exit_code = 0

    try:
        with open_esp_serial(args.port, args.baudrate) as esp_serial:
            while keep_running:
                raw_line = esp_serial.readline()
                if not raw_line:
                    continue

                line = raw_line.decode("utf-8", errors="replace").strip()
                if args.echo_serial:
                    print(line, flush=True)

                next_state = parse_keyboard_state(line)
                if next_state is None:
                    continue

                for change in diff_states(current_state, next_state):
                    keyboard.emit(change)

                current_state = next_state
    except SerialException as err:
        print_serial_error(args.port, err)
        exit_code = 2
    finally:
        keyboard.release_all(current_state)

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
