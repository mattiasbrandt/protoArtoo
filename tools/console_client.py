#!/usr/bin/env python3
# =============================================================================
# tools/console_client.py  (formerly tools/serial_monitor.py)
#
# The Console Client (CONTEXT.md) for protoArtoo ESP32 bench verification: the
# boot-log capture every epic uses, the interactive serial terminal an operator
# sits at, and the scripted mode a bench day and its agents drive -- on either
# Console Adapter (docs/console-protocol.md), one host program, no shim.
#
# Opens the port without becoming its controlling terminal and without touching
# DTR/RTS on POSIX systems. Output goes to stdout; status/errors go to stderr.
# Exit code 0 on success; see "Exit codes" below for the rest.
#
# The default POSIX backend is one of the attach methods measured at 0/5 resets
# (docs/troubleshooting.md, "Serial monitor caveat"). The --pyserial backend is
# NOT safe: it drives DTR and RTS low in two separate ioctls after the open, which
# resets the board every time and can leave it in the ROM download stub. It is
# refused outright for --interactive and scripted serial use, and remains only
# for the read-only capture mode's comparison use.
#
# --interactive extends the same proven-safe open (open_posix_port(), still no
# DTR/RTS touch, still O_NOCTTY) with a write path and a raw-mode local terminal,
# for the bidirectional Controller Console (docs/console-protocol.md section 8).
# It is not the only supported client -- `pio device monitor` (this repo's
# artoo_esp32 env already ships `monitor_raw = yes`) and `picocom` were both
# measured 0/5 resets in the same attach matrix and remain fully supported; see
# docs/troubleshooting.md "Serial monitor caveat" / "Console interactive
# session" for the operator instructions and the safe-flag list for each.
#
# --script (or the same directives given directly on the command line) drives
# either Console Adapter -- --port <dev> for serial, --http <base-url> for the
# browser adapter's POST /api/console -- from a flat file of one directive per
# line, so a bench row is a tracked, replayable transcript instead of a hand
# session. See "Scripted mode" below for the directive grammar and exit codes.
#
# Usage:
#   # Capture for 10 s (default), print to stdout:
#   python3 tools/console_client.py
#
#   # Capture for 30 s on a specific port:
#   python3 tools/console_client.py --port /dev/ttyUSB1 --duration 30
#
#   # Exit as soon as a known string appears (preferred for agent verification):
#   python3 tools/console_client.py --until "init complete" --timeout 20
#
#   # Stream continuously (human monitoring — Ctrl+C to exit):
#   python3 tools/console_client.py --stream
#
#   # Interactive Controller Console session (bidirectional, raw mode,
#   # Ctrl-C to exit -- see docs/troubleshooting.md before first use):
#   python3 tools/console_client.py --interactive
#
#   # Scripted, serial adapter -- a tracked row file, replayable by a human or an agent:
#   python3 tools/console_client.py --port /dev/ttyACM0 --script tools/bench_rows/firebeetle2.txt
#
#   # Scripted, browser adapter -- same directive grammar, POST /api/console instead:
#   python3 tools/console_client.py --http http://artoo.local --send system.status.health
# =============================================================================

import argparse
import os
import select
import signal
import sys
import termios
import time
import tty

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


def open_posix_port(port: str, baud: int, writable: bool = False) -> int:
    """Open `port` without becoming its controlling terminal, without touching
    DTR/RTS. `writable=True` opens O_RDWR instead of O_RDONLY for --interactive;
    every other termios setting (below) is identical for both, so the write path
    reuses the exact open already measured 0/5 resets rather than a new one."""
    access = os.O_RDWR if writable else os.O_RDONLY
    fd = os.open(port, access | os.O_NOCTTY | os.O_NONBLOCK)
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


def _read_or_none(fd: int, size: int) -> bytes | None:
    """os.read that swallows only a spurious non-blocking wakeup (`None`).

    A true end-of-file/closed-peer still returns `b""`, distinct from `None`,
    so a caller can tell "nothing happened this pass, loop again" apart from
    "the peer is gone, stop". `BlockingIOError` is a subclass of `OSError`,
    so it has to be caught here explicitly: a bare `except OSError` around
    the call site would misreport this harmless retry as a fatal I/O error."""
    try:
        return os.read(fd, size)
    except BlockingIOError:
        return None


def _write_all(fd: int, data: bytes) -> None:
    """Write every byte of `data`, retrying on non-blocking backpressure.

    open_posix_port()'s writable fd is O_NONBLOCK (same open as the read-only
    backend), so a full kernel write buffer raises BlockingIOError rather than
    blocking -- that is backpressure, not a failure, and is retried via
    select() rather than surfaced as a write error."""
    view = memoryview(data)
    while view:
        try:
            n = os.write(fd, view)
        except BlockingIOError:
            select.select([], [fd], [], 0.1)
            continue
        view = view[n:]


def run_interactive(serial_fd: int, stdin_fd: int, stdout_fd: int) -> int:
    """Full-duplex copy between stdin_fd and serial_fd until Ctrl-C (0x03).

    Ctrl-C is the LOCAL client's own exit key here, matching the convention
    already documented for every other supported Console terminal
    (docs/console.md "Attach a serial terminal": "Ctrl-C detaches -- that is
    your terminal program's own default key, not something the firmware
    does; the firmware never closes a terminal on its own"; the firmware's
    ready banner says the same thing to the operator, and `pio device
    monitor`'s own `--exit-char` default is 3, i.e. Ctrl-C). Ctrl-C is
    therefore consumed here, not forwarded -- inventing a second, unforwarded
    local exit key that no other supported client uses would be a new
    convention for the operator to learn, not a safety requirement; nothing
    in docs/console-protocol.md has the firmware acting on an inbound Ctrl-C
    byte.

    Takes plain fds, not sys.stdin/stdout, and touches no termios itself --
    that keeps it testable against a pty pair with no real terminal involved,
    and keeps raw-mode enter/restore (a correctness property, not cosmetic)
    entirely inside interactive_fd()'s try/finally.
    """
    while True:
        r, _, _ = select.select([stdin_fd, serial_fd], [], [])

        if serial_fd in r:
            try:
                chunk = _read_or_none(serial_fd, 256)
            except OSError as e:
                print(f"\r\nERROR: serial port read failed: {e}", file=sys.stderr)
                return 1
            if chunk is None:
                pass  # spurious non-blocking wakeup; nothing to forward yet
            elif not chunk:
                print("\r\nERROR: serial port closed", file=sys.stderr)
                return 1
            else:
                _write_all(stdout_fd, chunk)

        if stdin_fd in r:
            data = os.read(stdin_fd, 256)
            if not data:
                return 0  # stdin EOF (a closed pipe; a real raw-mode terminal has no EOF key)
            if b"\x03" in data:
                idx = data.index(b"\x03")
                if idx:
                    try:
                        _write_all(serial_fd, data[:idx])
                    except OSError as e:
                        print(f"\r\nERROR: serial port write failed: {e}", file=sys.stderr)
                        return 1
                return 0
            try:
                _write_all(serial_fd, data)
            except OSError as e:
                print(f"\r\nERROR: serial port write failed: {e}", file=sys.stderr)
                return 1


def interactive_fd(fd: int) -> int:
    """Attach the current terminal's stdin/stdout to `fd` full-duplex.

    Raw mode is restored on every exit path -- normal return, an exception
    from the loop, or SIGTERM/SIGHUP/SIGINT -- via try/finally. A tool that
    leaves the operator's terminal in raw mode after a crash costs them their
    shell, which is worse than the tool not existing (228 pin trap 2): this
    teardown is part of correctness, not optional cleanup, and that is also
    why SIGTERM/SIGHUP are converted to a KeyboardInterrupt here rather than
    left at their default disposition, which would terminate the process
    without ever reaching the `finally` block.

    If stdin is not a real terminal (piped input, as in the test suite),
    termios is left untouched: tty.setraw()/tcgetattr() require a real tty
    and would raise on a pipe, and there is nothing to save or restore.
    """
    stdin_fd = sys.stdin.fileno()
    stdout_fd = sys.stdout.fileno()
    is_tty = os.isatty(stdin_fd)
    saved_termios = tty.setraw(stdin_fd) if is_tty else None

    def _to_keyboard_interrupt(signum, _frame):
        raise KeyboardInterrupt(f"signal {signum}")

    old_handlers = {
        sig: signal.signal(sig, _to_keyboard_interrupt)
        for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT)
    }
    try:
        print("[monitor] interactive: Ctrl-C to exit\r", file=sys.stderr)
        try:
            return run_interactive(fd, stdin_fd, stdout_fd)
        except KeyboardInterrupt:
            return 0
    finally:
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)
        if is_tty:
            termios.tcsetattr(stdin_fd, termios.TCSAFLUSH, saved_termios)


def read_fd_line(fd: int, buffer: bytearray) -> bytes:
    """Pop and return one already-terminated line from `buffer`, reading more
    only if none is available yet.

    #264: the buffer is checked FIRST, before touching the fd. The previous
    order checked only right after an os.read() call, so a line already
    fully buffered from an earlier call -- left behind because that call's
    single os.read() happened to return a chunk containing more than one
    "\\n" -- was never looked at again until fresh bytes happened to arrive
    on the fd and trigger another append+check. A burst delivered in one
    read (short lines, well under the 256-byte request) could sit in
    `buffer` complete and unread indefinitely.
    """
    while True:
        if b"\n" in buffer:
            idx = buffer.index(b"\n")
            line = bytes(buffer[:idx + 1])
            del buffer[:idx + 1]
            return line
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            break
        if not chunk:
            break
        buffer.extend(chunk)
    return b""


def read_lines_fd(fd: int, deadline: float, until: str | None) -> int:
    """
    Read lines from fd until deadline or until 'until' string is found.
    Prints each line to stdout. Returns 0 if 'until' was found (or not used),
    1 if timed out before finding 'until'.

    #264: every complete line already sitting in `buffer` is drained and
    printed before returning to select() -- one select() wake-up used to
    emit at most one line even when a burst had already arrived complete,
    so the rest of that burst waited for another wake-up that only came if
    MORE bytes later arrived on the fd. A burst that had already fully
    landed could then sit past the deadline unprinted, and --until could
    miss a string that was already in the buffer, unread. A trailing
    fragment with no terminator is flushed at the deadline too, instead of
    being discarded silently.
    """
    found = False
    buffer = bytearray()
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        while True:
            raw = read_fd_line(fd, buffer)
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace").rstrip()
            print(line, flush=True)
            if until and until in line:
                found = True
                break
        if found:
            break

    if not found and buffer:
        tail = bytes(buffer).decode("utf-8", errors="replace").rstrip()
        if tail:
            print(tail, flush=True)
            if until and until in tail:
                found = True

    if until and not found:
        print(f"TIMEOUT: '{until}' not seen within time limit", file=sys.stderr)
        return 1
    return 0


def stream_forever_fd(fd: int) -> None:
    """Stream lines until Ctrl+C.

    #264: drains every complete line already buffered before returning to
    select(), for the same reason read_lines_fd() does -- see its docstring.
    """
    buffer = bytearray()
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 0.1)
            if not r:
                continue
            while True:
                raw = read_fd_line(fd, buffer)
                if not raw:
                    break
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
        description=(
            "protoArtoo serial monitor. The default POSIX backend does not reset the "
            "ESP32 on connect (measured 0/5 unseated); --pyserial does, every time."
        )
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
        help=(
            "Use the old pyserial backend. WARNING: this RESETS the board on open "
            "(measured 7/7 unseated) and can strand it in the ROM download stub, "
            "off the network. Comparison use only -- see docs/troubleshooting.md."
        )
    )
    parser.add_argument(
        "--interactive", action="store_true",
        help=(
            "Bidirectional Controller Console session: raw-mode local terminal, "
            "safe POSIX backend only (no --pyserial), Ctrl-C to exit. Read "
            "docs/troubleshooting.md before first use."
        )
    )
    args = parser.parse_args()

    if args.interactive:
        if args.pyserial:
            print(
                "ERROR: --interactive does not support --pyserial: that backend "
                "is not safe (measured 7/7 resets; see docs/troubleshooting.md).",
                file=sys.stderr,
            )
            return 1
        if args.stream or args.until:
            print(
                "ERROR: --interactive cannot be combined with --stream or --until.",
                file=sys.stderr,
            )
            return 1
        try:
            fd = open_posix_port(args.port, args.baud, writable=True)
        except Exception as e:
            print(f"ERROR: could not open {args.port}: {e}", file=sys.stderr)
            return 1
        print(
            f"[monitor] {args.port} @ {args.baud} baud "
            "(POSIX no-control-line open, interactive)",
            file=sys.stderr,
        )
        try:
            return interactive_fd(fd)
        finally:
            os.close(fd)

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
