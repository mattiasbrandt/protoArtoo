#!/usr/bin/env python3
# =============================================================================
# tools/serial_monitor.py
#
# Serial monitor for protoArtoo ESP32 bench verification.
#
# Opens the port without becoming its controlling terminal and without touching
# DTR/RTS on POSIX systems. Output goes to stdout; status/errors go to stderr.
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
import os
import select
import sys
import termios
import time

BAUD_MAP = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: getattr(termios, "B230400", termios.B115200),
    460800: getattr(termios, "B460800", termios.B115200),
    921600: getattr(termios, "B921600", termios.B115200),
}


def open_posix_port(port: str, baud: int) -> int:
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    baud_const = BAUD_MAP.get(baud)
    if baud_const is None:
        os.close(fd)
        raise ValueError(f"unsupported baud rate for POSIX monitor: {baud}")

    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
               termios.ISTRIP | termios.INLCR | termios.IGNCR |
               termios.ICRNL | termios.IXON)
    oflag &= ~termios.OPOST
    lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
               termios.ISIG | termios.IEXTEN)
    cflag &= ~(termios.CSIZE | termios.PARENB)
    cflag |= termios.CS8 | termios.CLOCAL | termios.CREAD
    cflag &= ~termios.CSTOPB
    cflag &= ~getattr(termios, "CRTSCTS", 0)
    cflag &= ~getattr(termios, "HUPCL", 0)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 1

    termios.tcsetattr(fd, termios.TCSANOW,
                      [iflag, oflag, cflag, lflag, baud_const, baud_const, cc])
    return fd


def open_pyserial_port(port: str, baud: int):
    import serial

    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.bytesize = serial.EIGHTBITS
    s.parity = serial.PARITY_NONE
    s.stopbits = serial.STOPBITS_ONE
    s.timeout = 0.1
    s.dtr = False
    s.rts = False
    s.open()
    return s


def read_fd_line(fd: int, buffer: bytearray) -> bytes:
    while True:
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            break
        if not chunk:
            break
        buffer.extend(chunk)
        if b"\n" in buffer:
            idx = buffer.index(b"\n")
            line = bytes(buffer[:idx + 1])
            del buffer[:idx + 1]
            return line
    return b""


def read_lines_fd(fd: int, deadline: float, until: str | None) -> int:
    """
    Read lines from fd until deadline or until 'until' string is found.
    Prints each line to stdout. Returns 0 if 'until' was found (or not used),
    1 if timed out before finding 'until'.
    """
    found = False
    buffer = bytearray()
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        raw = read_fd_line(fd, buffer) if r else b""
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


def stream_forever_fd(fd: int) -> None:
    """Stream lines until Ctrl+C."""
    buffer = bytearray()
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 0.1)
            raw = read_fd_line(fd, buffer) if r else b""
            if raw:
                print(raw.decode("utf-8", errors="replace").rstrip(), flush=True)
    except KeyboardInterrupt:
        print("\n[monitor exited]", file=sys.stderr)


def read_lines_pyserial(s, deadline: float, until: str | None) -> int:
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


def stream_forever_pyserial(s) -> None:
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
    parser.add_argument(
        "--pyserial", action="store_true",
        help="Use the old pyserial backend. May toggle DTR/RTS on some hosts."
    )
    args = parser.parse_args()

    if args.pyserial:
        try:
            s = open_pyserial_port(args.port, args.baud)
        except Exception as e:
            print(f"ERROR: could not open {args.port}: {e}", file=sys.stderr)
            return 1
        print(f"[monitor] {args.port} @ {args.baud} baud (pyserial backend)", file=sys.stderr)
        try:
            if args.stream:
                stream_forever_pyserial(s)
                return 0
            wait = args.timeout if args.timeout is not None else args.duration
            return read_lines_pyserial(s, time.time() + wait, args.until)
        finally:
            s.close()

    try:
        fd = open_posix_port(args.port, args.baud)
    except Exception as e:
        print(f"ERROR: could not open {args.port}: {e}", file=sys.stderr)
        return 1

    print(f"[monitor] {args.port} @ {args.baud} baud (POSIX no-control-line open)",
          file=sys.stderr)
    try:
        if args.stream:
            stream_forever_fd(fd)
            return 0
        wait = args.timeout if args.timeout is not None else args.duration
        return read_lines_fd(fd, time.time() + wait, args.until)
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
