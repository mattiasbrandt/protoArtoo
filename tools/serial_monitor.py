#!/usr/bin/env python3
# =============================================================================
# tools/serial_monitor.py
#
# Serial monitor for protoArtoo ESP32 bench verification.
#
# Holds DTR and RTS low before opening the port so the ESP32 is NOT reset
# on connect. Output goes to stdout; status/errors go to stderr.
# Exit code 0 on success, 1 on failure.
#
# Usage:
#   # Capture for 10 s (default), print to stdout:
#   python3 tools/serial_monitor.py
#
#   # Capture for 30 s on a specific port:
#   python3 tools/serial_monitor.py --port /dev/ttyUSB1 --duration 30
#
#   # Exit as soon as a known string appears (preferred for agent verification):
#   python3 tools/serial_monitor.py --until "init complete" --timeout 20
#
#   # Stream continuously (human monitoring — Ctrl+C to exit):
#   python3 tools/serial_monitor.py --stream
# =============================================================================

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed — run: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def open_port(port: str, baud: int) -> serial.Serial:
    """
    Open the serial port with DTR and RTS held low so the ESP32 auto-reset
    circuit is not triggered on connect.
    """
    s = serial.Serial()
    s.port     = port
    s.baudrate = baud
    s.bytesize = serial.EIGHTBITS
    s.parity   = serial.PARITY_NONE
    s.stopbits = serial.STOPBITS_ONE
    s.timeout  = 0.1  # non-blocking read with short timeout

    # Set control lines BEFORE open() so they do not pulse on connect.
    # DTR low + RTS low = ESP32 stays in normal run mode.
    s.dtr = False
    s.rts = False

    s.open()
    return s


def read_lines(s: serial.Serial, deadline: float, until: str | None) -> int:
    """
    Read lines from s until deadline or until 'until' string is found.
    Prints each line to stdout. Returns 0 if 'until' was found (or not used),
    1 if timed out before finding 'until'.
    """
    found = False
    while time.time() < deadline:
        raw = s.readline()
        if raw:
            line = raw.decode("utf-8", errors="replace").rstrip()
            print(line, flush=True)
            if until and until in line:
                found = True
                break

    if until and not found:
        print(f"TIMEOUT: '{until}' not seen within time limit", file=sys.stderr)
        return 1
    return 0


def stream_forever(s: serial.Serial) -> None:
    """Stream lines until Ctrl+C."""
    try:
        while True:
            raw = s.readline()
            if raw:
                print(raw.decode("utf-8", errors="replace").rstrip(), flush=True)
    except KeyboardInterrupt:
        print("\n[monitor exited]", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="protoArtoo serial monitor — does not reset the ESP32 on connect"
    )
    parser.add_argument(
        "--port", default="/dev/ttyUSB0",
        help="Serial port (default: /dev/ttyUSB0)"
    )
    parser.add_argument(
        "--baud", type=int, default=115200,
        help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--duration", type=float, default=10.0,
        help="Seconds to capture in timed mode (default: 10)"
    )
    parser.add_argument(
        "--until", type=str, default=None,
        help="Exit successfully as soon as this string appears in output"
    )
    parser.add_argument(
        "--timeout", type=float, default=None,
        help="Max seconds to wait for --until string (default: same as --duration)"
    )
    parser.add_argument(
        "--stream", action="store_true",
        help="Stream continuously until Ctrl+C (ignores --duration and --until)"
    )
    args = parser.parse_args()

    try:
        s = open_port(args.port, args.baud)
    except serial.SerialException as e:
        print(f"ERROR: could not open {args.port}: {e}", file=sys.stderr)
        return 1

    print(f"[monitor] {args.port} @ {args.baud} baud (DTR/RTS low — no reset)", file=sys.stderr)

    try:
        if args.stream:
            stream_forever(s)
            return 0

        wait = args.timeout if args.timeout is not None else args.duration
        deadline = time.time() + wait
        return read_lines(s, deadline, args.until)

    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())
