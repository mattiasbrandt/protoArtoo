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
# Exit code 0 on success in every mode; scripted mode's other codes (a request
# that never closed, a sink-confirmed drop, an adapter-confirmed cap) are the
# EXIT_* constants in the "Scripted mode" section below.
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
import json
import os
import re
import select
import signal
import socket
import subprocess
import sys
import termios
import time
import tty
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone

# tools/resolve_upload_port.py is reused, never edited (#264 pin): its
# discover() is the enumeration this file's board-identity header needs.
# Inserted explicitly rather than relying on sys.path[0] (only set to this
# script's own directory when run as `python3 tools/console_client.py` --
# a test loading this module by file path via importlib does not get that
# for free).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import resolve_upload_port  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

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

    # #264: flush immediately after termios, in every mode that opens through
    # this function (listen, interactive, scripted) -- discards whatever
    # arrived under the port's PREVIOUS line settings (a stale partial
    # escape byte, a fragment from before this process existed) so the first
    # bytes this process decodes are the first bytes it actually applied
    # raw-mode settings for. TCIFLUSH is the input-side-only flush: nothing
    # queued for transmit is touched.
    termios.tcflush(fd, termios.TCIFLUSH)
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


def run_interactive(serial_fd: int, stdin_fd: int, stdout_fd: int,
                    color: bool = False) -> int:
    """Full-duplex copy between stdin_fd and serial_fd until Ctrl-C (0x03).

    `color` colours Console Record lines on their way to the screen, through
    InteractiveRecordColorizer -- the same two-state rule scripted mode uses,
    applied to a byte stream that also carries embedded-cli's editor redraw.
    That class's docstring is the contract; the two properties that matter
    here are that every byte the firmware sent still reaches the screen in
    order (only SGR is added, and only at a line boundary), and that the
    stdin -> serial direction below is not touched by any of it. `color`
    false is a byte-exact pass-through, unchanged from before it existed.

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
    colorizer = InteractiveRecordColorizer(color)

    def release_held() -> None:
        """Never leave held bytes on the floor. Called on every quiet moment
        inside the loop, and once more in the `finally` below so that a
        return, an I/O error or the KeyboardInterrupt raised by
        interactive_fd()'s signal handlers all end with the screen holding
        every byte the device sent. flush() empties the buffer, so calling it
        twice writes nothing twice."""
        pending = colorizer.flush()
        if pending:
            _write_all(stdout_fd, pending)

    try:
        while True:
            # None (block) unless a candidate record line is being held, in which
            # case the hold has to be able to expire on a silent port.
            r, _, _ = select.select([stdin_fd, serial_fd], [], [],
                                    colorizer.select_timeout())
            if not r:
                release_held()
                continue

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
                    _write_all(stdout_fd, colorizer.feed(chunk))

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
    finally:
        release_held()


def interactive_fd(fd: int, color: bool = False) -> int:
    """Attach the current terminal's stdin/stdout to `fd` full-duplex.

    `color` is the caller's own --color/--no-color/isatty() decision, passed
    straight through to run_interactive(); this function only owns the
    terminal's raw mode and its restoration.

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
            return run_interactive(fd, stdin_fd, stdout_fd, color=color)
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


# =============================================================================
# Scripted mode (#264)
#
# One directive per line (from --script <file>) or the same directives given
# directly on the command line, driving either Console Adapter: --port <dev>
# for serial, --http <base-url> for the browser adapter's POST /api/console.
# Grammar and verdicts: docs/console-protocol.md; the send/reassembly
# algorithm below is read from tasks/console_bench.py's send_command() (the
# gitignored stopgap driver named in the #264 coordinator pin) -- its read
# loop was sound per ADR 0036, so it is the reference this reimplements
# against the shared ConsoleRecord model both transports render through.
# =============================================================================

# Exit codes (docs/console-protocol.md's verdict rule: status=err is data and
# never changes these). Cross-directive severity is the MAX seen across the
# whole run -- not explicitly ordered by the ticket for the case where more
# than one applies in one run, but 4 (a server-confirmed answer cap) is a
# more specific diagnosis than 3 (a server-confirmed drop), which is more
# specific than 2 (we simply never heard a close) -- so "highest number wins"
# tracks "most specific finding wins".
EXIT_OK = 0
EXIT_TOOL_FAILURE = 1
EXIT_TIMEOUT = 2
EXIT_LOSS = 3
EXIT_ADAPTER_CAPPED = 4

DEFAULT_SEND_TIMEOUT = 8.0   # tasks/console_bench.py's --timeout default
DEFAULT_LISTEN_SECONDS = 2.0  # tasks/console_bench.py's raw/key drain floor was 1.5s; rounded up
DEFAULT_SETTLE_SECONDS = 0.3  # acceptance criterion's stated default


# =============================================================================
# Provenance header and colour (#264)
# =============================================================================

def git_head_and_dirty(repo_root: str) -> tuple[str, bool] | None:
    """(short sha, dirty) for `repo_root`, or None if git metadata is not
    available at all (no git on PATH, not a git checkout) -- the header's
    caller falls back to an explicit `REPO: UNKNOWN` line rather than
    guessing."""
    try:
        sha_proc = subprocess.run(
            ["git", "rev-parse", "--short=8", "HEAD"],
            cwd=repo_root, capture_output=True, text=True, timeout=5,
        )
        if sha_proc.returncode != 0:
            return None
        status_proc = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=repo_root, capture_output=True, text=True, timeout=5,
        )
        return sha_proc.stdout.strip(), bool(status_proc.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        return None


def lookup_by_id(port: str) -> str | None:
    """The by-id identity resolve_upload_port.discover() has for `port`, or
    None if the port has no stable name (or is not currently enumerated --
    a race with the port itself, not this function's problem to solve)."""
    real = os.path.realpath(port)
    for device, stable in resolve_upload_port.discover():
        if device == port or os.path.realpath(device) == real:
            return stable
    return None


def fetch_json_status(base_url: str, timeout: float = 5.0) -> dict | None:
    """GET <base_url>/api/status, parsed. None on any failure to reach it or
    parse it -- reported to stderr (never silently swallowed), since image
    identity is mandatory and a caller falls back to --image or the
    explicit UNKNOWN line, never to a guess."""
    url = base_url.rstrip("/") + "/api/status"
    try:
        with urllib.request.urlopen(urllib.request.Request(url), timeout=timeout) as resp:
            body = resp.read()
        return json.loads(body)
    except (urllib.error.URLError, socket.timeout, TimeoutError, ValueError) as e:
        print(f"[console] WARNING: could not fetch {url} for image identity: {e}",
              file=sys.stderr)
        return None


def resolve_color(args) -> bool:
    """--color / --no-color / default-to-isatty, decided in one place for
    every mode that colours (scripted and interactive alike).

    The default is stdout's own tty-ness, never stdin's: interactive mode
    needs a terminal on stdin for raw mode either way, but its output can
    still be redirected to a transcript, and "colour never reaches a
    redirected transcript" is the rule that has to hold there too."""
    if args.no_color:
        return False
    if args.color:
        return True
    return sys.stdout.isatty()


def build_provenance_header(args) -> list[str]:
    """Port/HTTP, by-id identity, baud, host UTC time, repo HEAD+dirty,
    board, and image identity -- the last one mandatory (acceptance
    criterion): from the HTTP status when reachable, else --image, else an
    explicit UNKNOWN line rather than leaving host HEAD as the only
    version on the page (the #233 misreading this exists to catch)."""
    lines: list[str] = []

    if args.http:
        lines.append(f"HTTP: {args.http}")
    else:
        port_line = f"PORT: {args.port}"
        by_id = lookup_by_id(args.port)
        if by_id:
            port_line += f" ({by_id})"
        lines.append(port_line)
        lines.append(f"BAUD: {args.baud}")

    lines.append(f"HOST-TIME: {datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}")

    head_dirty = git_head_and_dirty(REPO_ROOT)
    if head_dirty is None:
        lines.append("REPO: UNKNOWN")
    else:
        sha, dirty = head_dirty
        lines.append(f"REPO: {sha}{' (dirty)' if dirty else ''}")

    lines.append(f"BOARD: {args.board} (asserted)" if args.board else "BOARD: (not asserted)")

    status = None
    if args.http:
        status = fetch_json_status(args.http)
    elif args.status:
        status = fetch_json_status(args.status)
    if status is not None:
        fw = status.get("firmwareVersion", "UNKNOWN")
        fs = status.get("fsVersion", "UNKNOWN")
        lines.append(f"IMAGE: firmwareVersion={fw} fsVersion={fs}")
    elif args.image:
        lines.append(f"IMAGE: {args.image}")
    else:
        lines.append("IMAGE: UNKNOWN (not evidence)")

    return lines


# The wire text a Console Record line begins with (docs/console-protocol.md
# 3: "one per line, prefixed `< `", and the first pair is always the Request
# ID) and the token that makes it an error one. Both are printable ASCII, so
# the byte forms below are exact -- section 7 keeps the record envelope
# ASCII-only.
RECORD_LINE_PREFIX = "< id="
RECORD_ERR_TOKEN = "status=err"

SGR_RECORD = "\x1b[36m"
SGR_RECORD_ERR = "\x1b[31m"
SGR_RESET = "\x1b[0m"


def record_color_code(line: str) -> str | None:
    """The SGR colour `line` gets as a Console Record, or None if it is not
    one.

    Two states, matching the browser's two CSS classes (data/app.js's
    log-line-command / log-line-command-error): an ordinary Console Record
    line and an error one (`status=err`). Only a line that IS a
    rendered/wire Console Record is touched -- log lines, banners, and this
    tool's own verdict markers ([TIMEOUT], [LOSS], the `--- send ... ---`
    marker, ...) are left alone, and none of them start with the record
    prefix this checks for.

    Both callers -- scripted mode's line-at-a-time colorize_record_line()
    and interactive mode's byte-stream InteractiveRecordColorizer -- decide
    here, so there is one rule and not two that can drift.
    """
    if not line.startswith(RECORD_LINE_PREFIX):
        return None
    return SGR_RECORD_ERR if RECORD_ERR_TOKEN in line else SGR_RECORD


def colorize_record_line(line: str, enabled: bool) -> str:
    """Scripted mode's colouring: one already-split, already-stripped line in,
    the same line (coloured or not) out.

    `enabled` is the caller's own --color/--no-color/isatty() decision, not
    decided here, so ANSI can never reach a redirected transcript by
    accident: the check is what stays true regardless of terminal, only
    the caller's threading of `enabled` decides whether it ever fires.
    """
    if not enabled:
        return line
    code = record_color_code(line)
    return line if code is None else f"{code}{line}{SGR_RESET}"


class InteractiveRecordColorizer:
    """Colours Console Record lines inside interactive mode's device->screen
    byte stream (#267), without ever altering a byte the firmware sent.

    Why this cannot just reuse colorize_record_line(): run_interactive() is a
    byte pump, not a line reader. The same direction carries embedded-cli's
    editor redraw -- the input-line clear (`\r`, spaces, `\r`), the prompt
    and buffered-command reprint, cursor save/restore and left/right moves
    (lib/embedded-cli/src/embedded_cli.c's clearCurrentLine, moveCursor,
    printLiveAutocompletion) -- and those sequences are what Tab completion,
    history Up/Down and mid-line Backspace are made of. Splitting that stream
    into lines and reprinting them would corrupt the redraw; holding it until
    a newline would stall the echo the operator is typing against.

    The contract this keeps instead, and what the tests assert:

    1. **Byte transparency modulo SGR.** Strip every SGR sequence this class
       emits from its output and you get its input back, byte for byte, in
       order. Nothing is dropped, reordered, re-encoded or re-wrapped, so no
       redraw sequence can be corrupted -- the terminal receives the
       firmware's own bytes.
    2. **SGR is inserted only at a line boundary**, immediately before a
       record's first byte and immediately after its last, never inside an
       escape sequence: the only injection point is a position that directly
       follows a `\r` or `\n` (or the start of the session), which is
       exactly where an escape sequence cannot be in progress. SGR does not
       move the cursor or occupy a column, so embedded-cli's own model of the
       input line stays true.
    3. **Bounded hold.** At most one candidate record line is held back
       (HOLD_LIMIT bytes), and only until its terminator arrives, the limit
       is hit, or the caller flushes on a quiet stream (HOLD_TIMEOUT_S).
       Whatever is held is released verbatim, uncoloured, rather than lost.
    4. **Disabled means untouched.** With `enabled` false, feed() returns its
       input unchanged and nothing is ever held -- the pump behaves exactly
       as it did before this class existed, which is what keeps ANSI out of a
       redirected transcript.

    Input (keystrokes -> firmware) never passes through here at all.

    A record that arrives while the operator is mid-entry is NOT coloured: it
    is written by the framed emitter without clearing the input line
    (include/console_serial_output.h), so it does not start at column 0 and
    this class does not pretend it does. The answer to a submitted command --
    the case an operator actually watches -- always follows embedded-cli's
    own `\r\n` echo of the Enter key, so it is at a line start.
    """

    # A record line on the wire is at most PA_LOG_SERIAL_LINE_MAX - 1 bytes
    # plus CR LF = 257 (include/console_serial_output.h). 512 is that with
    # room to spare: the limit exists to bound the hold if something that is
    # not a record ever starts with the record prefix, not to clip records.
    HOLD_LIMIT = 512

    # How long the caller waits for the rest of a held candidate before
    # giving up and releasing it uncoloured. A record is one Serial.write()
    # (ADR 0036), so a split mid-record is host-side fragmentation and
    # resolves within a USB frame / a UART character time; 50 ms is far above
    # both and still imperceptible.
    HOLD_TIMEOUT_S = 0.05

    _PREFIX = RECORD_LINE_PREFIX.encode("ascii")
    _TERMINATORS = (b"\r", b"\n")

    def __init__(self, enabled: bool):
        self.enabled = enabled
        self._held = bytearray()
        self._in_record = False
        self._at_line_start = True

    def select_timeout(self) -> float | None:
        """The timeout the caller's select() should use: None (block, as
        before) unless bytes are being held, in which case the hold must be
        able to expire even if the device never sends another byte."""
        return self.HOLD_TIMEOUT_S if self._held else None

    def feed(self, chunk: bytes) -> bytes:
        """The bytes to write to the screen for this chunk -- possibly fewer
        than came in (a candidate record still accumulating), possibly more
        (a completed record, plus its SGR)."""
        if not self.enabled:
            return chunk

        out = bytearray()
        for i in range(len(chunk)):
            b = chunk[i:i + 1]

            if self._held:
                self._held += b
                if self._in_record:
                    if b in self._TERMINATORS:
                        out += self._emit_record()
                    elif len(self._held) >= self.HOLD_LIMIT:
                        out += self.flush()
                elif self._PREFIX.startswith(bytes(self._held)):
                    if len(self._held) == len(self._PREFIX):
                        self._in_record = True
                else:
                    out += self.flush()
                continue

            if self._at_line_start and b == self._PREFIX[:1]:
                self._held += b
                continue

            out += b
            self._at_line_start = b in self._TERMINATORS

        return bytes(out)

    def flush(self) -> bytes:
        """Release whatever is held, verbatim and uncoloured. Called by the
        caller when the stream goes quiet, and internally whenever a
        candidate turns out not to be a record."""
        data = bytes(self._held)
        self._held.clear()
        self._in_record = False
        if data:
            self._at_line_start = data[-1:] in self._TERMINATORS
        return data

    def _emit_record(self):
        data = bytes(self._held)
        self._held.clear()
        self._in_record = False
        self._at_line_start = True

        text, terminator = data[:-1], data[-1:]
        # Decoded for the two-state DECISION only; what goes to the screen is
        # `text` itself, so a value carrying UTF-8 (protocol section 7) is
        # never re-encoded on its way through.
        code = record_color_code(text.decode("utf-8", "replace"))
        if code is None:
            return data  # unreachable: the prefix already matched
        return code.encode("ascii") + text + SGR_RESET.encode("ascii") + terminator


class ScriptUsageError(Exception):
    """A directive is malformed, or valid but not usable on the active
    transport (raw/key/listen are serial-only) or the active terminal
    (pause with no controlling tty). Fatal: the run stops rather than
    skipping the bad line, per the ticket's "fails loudly" requirement."""


class AdapterCapped(Exception):
    """Raised by a transport's send_line()/send_raw() when the ADAPTER
    itself reports it could not carry the full answer (HTTP 500 "response
    too large for this adapter", or a 200 envelope with "truncated":true).
    Kept distinct from "did not close in time": both look like a stalled
    request from the caller's side, but one is the server explicitly
    refusing size, and the ticket requires the transcript never blame a
    timeout for a cap."""

    def __init__(self, message: str):
        super().__init__(message)
        self.message = message


class ConsoleClientToolFailure(Exception):
    """The transport itself could not be used at all -- HTTP unreachable,
    a malformed response the adapter should never send. Exit 1, same
    bucket as an open() failure: this is not a per-request outcome, it
    means the run cannot meaningfully continue."""


# Key names accepted by the `key <name[,name...]>` directive, mapped to the
# exact bytes embedded-cli's line editor recognises. Read from
# lib/embedded-cli/src/embedded_cli.c, not guessed at from a generic VT100
# reference: onControlInput()/isControlChar() (Enter, Backspace, Tab) and
# onEscapedInput() (arrows via ESC[<A-D>, Home via ESC[H, End via ESC[F,
# Delete via ESC[3~ -- Home/End's alternate ESC[1~/ESC[7~ and ESC[4~/ESC[8~
# forms exist in the library too, but a real terminal's arrow/Home/End keys
# emit the primary forms below, which is what a Console Client is standing
# in for here). Backspace is 0x7F: isControlChar() treats '\b' and 0x7F as
# the same key, and 0x7F (DEL) is what a raw-mode terminal's physical
# Backspace key sends on this project's supported terminals.
KEY_BYTES = {
    "tab": b"\t",
    "enter": b"\r",
    "backspace": b"\x7f",
    "delete": b"\x1b[3~",
    "up": b"\x1b[A",
    "down": b"\x1b[B",
    "right": b"\x1b[C",
    "left": b"\x1b[D",
    "home": b"\x1b[H",
    "end": b"\x1b[F",
}


def resolve_key_bytes(names: str) -> bytes:
    """`names` is a comma-separated list, e.g. `up,up,enter`."""
    out = bytearray()
    for name in names.split(","):
        name = name.strip()
        try:
            out.extend(KEY_BYTES[name])
        except KeyError:
            raise ScriptUsageError(
                f"unknown key name {name!r}; known keys: {', '.join(sorted(KEY_BYTES))}"
            ) from None
    return bytes(out)


def unescape_raw(text: str) -> bytes:
    """Decode `raw <escaped bytes>`'s backslash escapes (\\t, \\r, \\xNN, ...).

    Lifted from tasks/console_bench.py's own `unescape()` (the reference
    driver named in the #264 pin): encode to UTF-8 first so a literal
    multi-byte character in the directive still round-trips, decode with
    Python's own escape grammar, then re-encode latin-1 to get the exact
    byte values back out (unicode_escape's decode step yields a str whose
    code points ARE the intended byte values for anything in the escape
    grammar).
    """
    return text.encode("utf-8").decode("unicode_escape").encode("latin-1")


def _split_kv_pairs(rest: str) -> dict[str, str]:
    """Split a Console Record's trailing `key=value ...` text into a dict.

    Hand-rolled rather than shlex: docs/console-protocol.md 1.2 and
    consoleQuoteValue() (src/console/console_record.cpp) define a narrower
    quoting grammar than shlex's -- a value is either bare (no spaces) or
    double-quoted with ONLY `"` and `\\` escaped inside -- and the firmware
    is the one producing these lines, so they are always well-formed against
    that exact grammar.
    """
    pairs: dict[str, str] = {}
    i, n = 0, len(rest)
    while i < n:
        while i < n and rest[i] == " ":
            i += 1
        if i >= n:
            break
        eq = rest.find("=", i)
        if eq == -1:
            break
        key = rest[i:eq]
        i = eq + 1
        if i < n and rest[i] == '"':
            i += 1
            chars = []
            while i < n and rest[i] != '"':
                if rest[i] == "\\" and i + 1 < n and rest[i + 1] in ('"', "\\"):
                    chars.append(rest[i + 1])
                    i += 2
                else:
                    chars.append(rest[i])
                    i += 1
            i += 1  # skip closing quote
            pairs[key] = "".join(chars)
        else:
            start = i
            while i < n and rest[i] != " ":
                i += 1
            pairs[key] = rest[start:i]
    return pairs


class ConsoleRecord:
    """One Console Record (docs/console-protocol.md 3.1), transport-neutral:
    built from a parsed serial wire line or a decoded HTTP JSON record so
    verdict logic (below) never needs to know which adapter answered."""

    __slots__ = ("id", "type", "fields")

    def __init__(self, id_: int, type_: str, fields: dict[str, str]):
        self.id = id_
        self.type = type_
        self.fields = fields


_RECORD_LINE_RE = re.compile(r"^< id=(\d+) type=(\S+)(?: (.*))?$")


def parse_serial_record_line(line: str) -> ConsoleRecord | None:
    """`line` is one already-decoded, already-stripped wire line. Returns
    None for anything that is not a Console Record -- a log line, the
    banner, a bare prompt echo -- which a scripted read simply ignores."""
    m = _RECORD_LINE_RE.match(line)
    if not m:
        return None
    return ConsoleRecord(int(m.group(1)), m.group(2), _split_kv_pairs(m.group(3) or ""))


# JSON keys a POST /api/console record can carry, per the field-by-field
# match against src/web/api_console.cpp's handleConsolePost() response
# builder (:504-520): every ConsoleRecord field that ever gets set on the
# JSON object, across every record type it builds.
_HTTP_RECORD_FIELD_KEYS = ("operation", "name", "value", "status", "outcome", "reason")


def record_from_http_json(rec: dict) -> ConsoleRecord:
    """Build the same transport-neutral ConsoleRecord a serial wire line
    would parse into, from one element of a POST /api/console response's
    `records` array."""
    fields = {k: str(rec[k]) for k in _HTTP_RECORD_FIELD_KEYS if k in rec}
    return ConsoleRecord(int(rec["id"]), str(rec["type"]), fields)


def render_record_line(rec: ConsoleRecord) -> str:
    """Render a ConsoleRecord in the protocol's line grammar, so an HTTP
    transcript diffs line for line against a serial one for the same
    command. This is the JSON-to-wire-text direction; the reverse never
    happens (serial's transcript is the wire text verbatim, never
    reconstructed -- "Transcript = what the wire said").

    The grammar is read verbatim from the firmware's OWN serial emitter
    (src/tasks/console_task.cpp's onRecordBegin/Field/Item/Result/End,
    `< id=%lu type=<t> ...`), not from data/app.js's browser rendering:
    app.js re-quotes a value defensively for on-page display, which the
    firmware's serial sink never does at this layer (any quoting a field
    value carries, e.g. a WiFi SSID, was already applied upstream by the
    executor via consoleQuoteValue() before either sink saw it) -- matching
    app.js here would double-quote and diverge from serial, not converge
    with it.
    """
    if rec.type == "begin":
        return f"< id={rec.id} type=begin operation={rec.fields.get('operation', '')}"
    if rec.type == "field":
        return (f"< id={rec.id} type=field name={rec.fields.get('name', '')} "
                f"value={rec.fields.get('value', '')}")
    if rec.type == "item":
        return f"< id={rec.id} type=item value={rec.fields.get('value', '')}"
    if rec.type in ("result", "end"):
        line = (f"< id={rec.id} type={rec.type} "
                f"status={rec.fields.get('status', '')} outcome={rec.fields.get('outcome', '')}")
        if "reason" in rec.fields:
            line += f" reason={rec.fields['reason']}"
        if "dropped" in rec.fields:
            # Never actually emitted by the browser adapter (ADR 0036: it
            # builds its response whole and cannot drop) -- included so
            # this renderer stays a faithful, transport-neutral mirror of
            # the wire grammar rather than one that silently assumes the
            # field can't appear here.
            line += f" dropped={rec.fields['dropped']}"
        return line
    return f"< id={rec.id} type={rec.type}"


def build_sendlen_line(n: int, prefix: str) -> str:
    """Build an exactly-`n`-byte line for the overflow rows (serial refuses
    at 62 bytes, browser at 255 -- docs/console-protocol.md 1.3). `prefix` is
    kept whole and padded with filler up to length n, or truncated to n if
    it is already that long or longer."""
    if len(prefix) >= n:
        return prefix[:n]
    return prefix + ("x" * (n - len(prefix)))


DEFAULT_REATTACH_TIMEOUT = 30.0  # bounded wait for a detached port's by-id name to return


class SerialTransport:
    """Scripted-mode serial adapter: byte-exact writes, wire-order reads.

    Owns the one-time settle-before-first-send delay (acceptance criterion:
    "settle only before the first scripted serial send" -- listen and
    interactive never settle) and TCIFLUSH, which already happens inside
    open_posix_port() for every serial mode.

    Detach survives: `port`'s by-id identity is captured once at
    construction (via tools/resolve_upload_port.py's discover(), the same
    reuse rule as the provenance header), so a later detach can wait for
    THAT identity specifically rather than a device path that may
    renumber (ttyACM0 -> ttyACM1) on replug.
    """

    def __init__(self, fd: int, port: str, baud: int,
                 settle_seconds: float = DEFAULT_SETTLE_SECONDS,
                 color: bool = False,
                 reattach_timeout: float = DEFAULT_REATTACH_TIMEOUT):
        self.fd = fd
        self.port = port
        self.baud = baud
        self.settle_seconds = settle_seconds
        self.color = color
        self.reattach_timeout = reattach_timeout
        self.by_id = lookup_by_id(port)
        self._settled = False

    def _settle_once(self) -> None:
        if not self._settled:
            if self.settle_seconds > 0:
                time.sleep(self.settle_seconds)
            self._settled = True

    def _select_readable(self, timeout: float) -> bool:
        try:
            r, _, _ = select.select([self.fd], [], [], timeout)
        except OSError:
            self._reopen_after_detach()
            return False
        return bool(r)

    def _read_chunk_or_detach(self, size: int) -> bytes | None:
        """None means "nothing yet, keep looping" -- on the current fd, or
        on a freshly reattached one if a detach just happened. Never
        returns b"": that is _read_or_none()'s real-EOF/closed-peer signal
        (docstring there), and here it means the port itself is gone, not
        merely quiet, so it triggers reattachment instead of being handed
        to a caller that would otherwise treat it as "nothing happened".

        Deliberate asymmetry with _write_marked() below: a write failure
        retries the SAME payload against the reopened fd (nothing was ever
        sent, so resending is safe), but a read failure does not resend
        anything -- the peer may already have received and even answered
        the original write in the instant before it vanished, and
        resending would risk executing an action twice. A request whose
        reply never arrives because of a mid-read detach is reported as
        an honest, ordinary timeout on that one directive; only the PORT
        recovers here, not the in-flight exchange."""
        try:
            chunk = _read_or_none(self.fd, size)
        except OSError:
            chunk = b""
        if chunk == b"":
            self._reopen_after_detach()
            return None
        return chunk

    def _reopen_after_detach(self) -> None:
        target = f"by-id {self.by_id!r}" if self.by_id else f"{self.port} itself (no by-id name)"
        print(f"--- gap: {self.port} detached, waiting up to {self.reattach_timeout}s "
              f"for {target} to return ---", flush=True)
        try:
            os.close(self.fd)
        except OSError:
            pass

        deadline = time.monotonic() + self.reattach_timeout
        found: str | None = None
        while time.monotonic() < deadline and found is None:
            for device, stable in resolve_upload_port.discover():
                if self.by_id is not None:
                    if stable == self.by_id:
                        found = device
                        break
                elif device == self.port:
                    found = device
                    break
            if found is None:
                time.sleep(0.5)

        if found is None:
            raise ConsoleClientToolFailure(
                f"{self.port} did not reappear within {self.reattach_timeout}s")

        self.fd = open_posix_port(found, self.baud, writable=True)
        self.port = found
        print(f"--- reattached: {found} ---", flush=True)

    def _write_marked(self, payload: bytes) -> None:
        print(f"--- send {payload!r} ---", flush=True)
        try:
            _write_all(self.fd, payload)
        except OSError:
            self._reopen_after_detach()
            _write_all(self.fd, payload)

    def send_line(self, line: str, timeout: float) -> tuple[list[ConsoleRecord], bool]:
        """`send`/`sendlen`: line + CR, wait for the request to close.

        Reassembly per docs/console-protocol.md 3.2 (reference algorithm:
        tasks/console_bench.py's send_command()): the first record whose id
        follows the send owns this request; a differently-id'd record is a
        concurrent browser session's and never ends this read. A blank line
        seen while a group is open is a wire anomaly (ADR 0036), reported
        in-stream and marked, never treated as the loss signature itself.
        """
        self._settle_once()
        self._write_marked(line.encode("utf-8") + b"\r")
        return self._read_group(timeout)

    def send_raw(self, payload: bytes, listen_seconds: float) -> None:
        """`raw`/`key`: byte-exact, no CR/LF translation, no reassembly --
        then a listen window, per the acceptance criterion."""
        self._settle_once()
        self._write_marked(payload)
        self._drain_for(listen_seconds)

    def capture(self, seconds: float) -> None:
        """Standalone `listen <s>`: watch only, no send, no settle (settle
        exists to let an attach reprint land before OUR first send; a
        listen is exactly the case that must not delay behind it, or it
        loses the boot log it exists to catch)."""
        self._drain_for(seconds)

    def note_settle(self, seconds: float) -> None:
        """`settle <s>` directive: only meaningful before the first send;
        a later occurrence is accepted (never a usage error) but has no
        further effect, since `_settle_once()` never sleeps again."""
        self.settle_seconds = seconds

    def _read_group(self, timeout: float) -> tuple[list[ConsoleRecord], bool]:
        records: list[ConsoleRecord] = []
        req_id: int | None = None
        closed = False
        buf = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self._select_readable(0.1):
                continue
            chunk = self._read_chunk_or_detach(4096)
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                buf[:] = rest
                text = raw.decode("utf-8", "replace").rstrip("\r")
                print(colorize_record_line(text, self.color), flush=True)
                if text == "":
                    if req_id is not None:
                        print(f"[ANOMALY] blank line inside record group id={req_id}",
                              flush=True)
                    continue
                rec = parse_serial_record_line(text)
                if rec is None:
                    continue
                if req_id is None:
                    req_id = rec.id
                if rec.id != req_id:
                    continue
                records.append(rec)
                if rec.type in ("result", "end"):
                    closed = True
                    break
            if closed:
                break
        return records, closed

    def _drain_for(self, seconds: float) -> None:
        deadline = time.monotonic() + seconds
        buf = bytearray()
        while time.monotonic() < deadline:
            if not self._select_readable(0.1):
                continue
            chunk = self._read_chunk_or_detach(4096)
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                buf[:] = rest
                text = raw.decode("utf-8", "replace").rstrip("\r")
                print(colorize_record_line(text, self.color), flush=True)
        if buf:
            tail = bytes(buf).decode("utf-8", "replace")
            if tail:
                print(tail, flush=True)


def _http_error_message(body: bytes) -> str:
    """Best-effort extraction of the `error` field from one of
    handleConsolePost()'s `{"ok":false,"error":"..."}` bodies; falls back to
    the raw decoded body if it is not that shape, so a caller always has
    something readable to report."""
    try:
        parsed = json.loads(body)
        if isinstance(parsed, dict) and isinstance(parsed.get("error"), str):
            return parsed["error"]
    except ValueError:
        pass
    return body.decode("utf-8", "replace")


class HttpTransport:
    """Scripted-mode browser adapter: one `POST /api/console` per `send`,
    `command=<line>` form-urlencoded (docs/api.md; matches data/web_api.js's
    own `postForm`, the same encoding the dashboard's Live Logs box uses).

    raw/key/listen have no meaning over HTTP (there is no continuous stream
    to send bytes into or watch) and are refused outright, per the
    acceptance criterion marking them serial-only.
    """

    def __init__(self, base_url: str, color: bool = False):
        self.base_url = base_url.rstrip("/")
        self.color = color

    def send_line(self, line: str, timeout: float) -> tuple[list[ConsoleRecord], bool]:
        print(f"--- send {line.encode('utf-8')!r} ---", flush=True)
        data = urllib.parse.urlencode({"command": line}).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + "/api/console",
            data=data,
            method="POST",
            headers={"Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"},
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout) as resp:
                body = resp.read()
                status_code = resp.getcode()
        except urllib.error.HTTPError as e:
            with e:
                body = e.read()
                status_code = e.code
        except (urllib.error.URLError, socket.timeout, TimeoutError) as e:
            # Unreachable at the connection level (refused, DNS, host down):
            # no per-request outcome to report, the run cannot continue.
            raise ConsoleClientToolFailure(
                f"HTTP request to {self.base_url} failed: {e}") from e

        if status_code == 500:
            # src/web/api_console.cpp answers 500 for exactly two reasons
            # today: webSink.overflowed (the bounded path's record array or
            # value arena ran out before the group closed, :492-494) and the
            # final serializeJson() overflowing its 4096-byte buffer
            # (:535-536). Both are the adapter refusing size, matching the
            # acceptance criterion's "500 response too large" -> exit 4.
            message = _http_error_message(body)
            print(f"[ADAPTER-CAPPED] HTTP 500: {message}", flush=True)
            raise AdapterCapped(message)

        if status_code != 200:
            message = _http_error_message(body)
            print(f"[ERROR] unexpected HTTP {status_code}: {message}", flush=True)
            return [], False

        try:
            payload = json.loads(body)
        except ValueError as e:
            raise ConsoleClientToolFailure(
                f"malformed JSON from {self.base_url}/api/console: {e}") from e

        records = [record_from_http_json(r) for r in payload.get("records", [])]
        for rec in records:
            print(colorize_record_line(render_record_line(rec), self.color), flush=True)

        if payload.get("truncated"):
            # #240: item-level truncation on system.status.logs-style
            # answers -- a 200 with a complete, terminated group, but not
            # every item that existed. Distinct from the 500 above, same
            # exit bucket: the adapter told us it capped the answer.
            print("[ADAPTER-CAPPED] response envelope truncated=true", flush=True)
            raise AdapterCapped("truncated=true")

        closed = bool(records) and records[-1].type in ("result", "end")
        return records, closed

    def send_raw(self, payload: bytes, listen_seconds: float) -> None:
        raise ScriptUsageError(
            "raw/key directives are serial-only (no continuous stream to send "
            "bytes into over --http)")

    def capture(self, seconds: float) -> None:
        raise ScriptUsageError(
            "listen is serial-only (no continuous stream to watch over --http)")

    def note_settle(self, seconds: float) -> None:
        pass  # settle exists to let a serial attach-reprint land first; no-op over HTTP


class Directive:
    __slots__ = ("kind", "arg", "source")

    def __init__(self, kind: str, arg: str, source: str):
        self.kind = kind
        self.arg = arg
        self.source = source  # "<file>:<lineno>" or "--<flag>", for error messages


_DIRECTIVE_KINDS = frozenset(
    {"send", "raw", "key", "sendlen", "listen", "settle", "timeout", "pause", "row"}
)


def parse_directive_line(line: str, source: str) -> Directive | None:
    """One line of a script file. Returns None for a blank line or a `#`
    comment. Splits only on the FIRST run of whitespace: a `send` or `pause`
    argument keeps every embedded space (a command's own arguments, an
    operator's pause message) verbatim."""
    stripped = line.strip()
    if not stripped or stripped.startswith("#"):
        return None
    if stripped.startswith("@row"):
        return Directive("row", stripped[len("@row"):].strip(), source)
    parts = stripped.split(None, 1)
    kind = parts[0]
    arg = parts[1] if len(parts) > 1 else ""
    if kind not in _DIRECTIVE_KINDS:
        raise ScriptUsageError(f"{source}: unknown directive {kind!r}")
    return Directive(kind, arg, source)


def load_script_file(path: str) -> list[Directive]:
    directives: list[Directive] = []
    with open(path, "r", encoding="utf-8") as f:
        for lineno, raw_line in enumerate(f, start=1):
            d = parse_directive_line(raw_line, f"{path}:{lineno}")
            if d is not None:
                directives.append(d)
    return directives


class RowBlock:
    """One `@row <ticket> <name>` block: `directives` holds everything up
    to (not including) the next `@row` marker or EOF. The marker itself is
    rebuilt by row_marker_directive() when a selection is flattened back
    into one execution list, so `=== row ... ===` still prints in
    run_scripted() the same way whether or not a row was ever filtered."""

    __slots__ = ("ticket", "label", "directives")

    def __init__(self, ticket: str | None, label: str | None, directives: list[Directive]):
        self.ticket = ticket
        self.label = label
        self.directives = directives


def split_into_row_blocks(directives: list[Directive]) -> tuple[list[Directive], list[RowBlock]]:
    """(preamble, blocks). `preamble` is whatever precedes the first `@row`
    marker -- typically file-scoped `timeout`/`settle` setup -- and always
    runs; it is not itself a row, so --rows/--skip-manual never touch it."""
    preamble: list[Directive] = []
    blocks: list[RowBlock] = []
    current: RowBlock | None = None
    for d in directives:
        if d.kind == "row":
            parts = d.arg.split(None, 1)
            ticket = parts[0] if parts else None
            label = parts[1] if len(parts) > 1 else None
            current = RowBlock(ticket, label, [])
            blocks.append(current)
        elif current is None:
            preamble.append(d)
        else:
            current.directives.append(d)
    return preamble, blocks


def row_marker_directive(block: RowBlock) -> Directive:
    arg = f"{block.ticket} {block.label}" if block.label else (block.ticket or "")
    return Directive("row", arg, "@row")


def select_rows(blocks: list[RowBlock], rows: str | None, skip_manual: bool) -> list[RowBlock]:
    """`rows` is a comma-separated list of `@row` names, run in the order
    given (not file order) -- an explicit replay sequence. `skip_manual`
    then drops any block that contains a `pause` directive at all, whether
    or not it was itself named by `rows`."""
    selected = blocks
    if rows:
        wanted = [w.strip() for w in rows.split(",") if w.strip()]
        by_label = {b.label: b for b in blocks if b.label is not None}
        missing = [w for w in wanted if w not in by_label]
        if missing:
            available = ", ".join(sorted(by_label)) or "(none)"
            raise ScriptUsageError(
                f"--rows: unknown row name(s) {', '.join(missing)}; available: {available}"
            )
        selected = [by_label[w] for w in wanted]
    if skip_manual:
        selected = [b for b in selected if not any(d.kind == "pause" for d in b.directives)]
    return selected


def flatten_rows(preamble: list[Directive], blocks: list[RowBlock]) -> list[Directive]:
    out = list(preamble)
    for b in blocks:
        out.append(row_marker_directive(b))
        out.extend(b.directives)
    return out


def run_pause(text: str) -> None:
    if not sys.stdin.isatty():
        raise ScriptUsageError(
            f"pause: no controlling terminal to wait on (message: {text!r})"
        )
    print(f"[PAUSE] {text}", flush=True)
    input()


def run_scripted(transport: "SerialTransport | HttpTransport", directives: list[Directive],
                  initial_timeout: float = DEFAULT_SEND_TIMEOUT) -> int:
    """Execute `directives` against `transport` (either adapter). Returns the
    worst exit code seen across the whole run (EXIT_OK if every request
    closed with no loss and the adapter never capped an answer)."""
    worst = EXIT_OK
    current_timeout = initial_timeout
    current_listen = DEFAULT_LISTEN_SECONDS

    def note(records: list[ConsoleRecord], closed: bool, label: str) -> None:
        nonlocal worst
        if not closed:
            print(f"[TIMEOUT] {label} did not close within {current_timeout}s", flush=True)
            worst = max(worst, EXIT_TIMEOUT)
            return
        last = records[-1] if records else None
        dropped = last.fields.get("dropped") if last is not None else None
        if dropped not in (None, "0"):
            print(f"[LOSS] dropped={dropped} on closing record id={last.id}", flush=True)
            worst = max(worst, EXIT_LOSS)

    def send(line: str, timeout: float, label: str) -> None:
        nonlocal worst
        try:
            records, closed = transport.send_line(line, timeout)
        except AdapterCapped:
            # The transport already printed its own [ADAPTER-CAPPED] line
            # (it knows which of the two capped shapes fired); this is only
            # the exit-code side of that verdict.
            worst = max(worst, EXIT_ADAPTER_CAPPED)
            return
        note(records, closed, label)

    for d in directives:
        if d.kind == "row":
            print(f"=== row {d.arg} ===", flush=True)
        elif d.kind == "send":
            send(d.arg, current_timeout, f"send {d.arg!r}")
        elif d.kind == "sendlen":
            parts = d.arg.split(None, 1)
            if not parts:
                raise ScriptUsageError(f"{d.source}: sendlen needs a length")
            n = int(parts[0])
            prefix = parts[1] if len(parts) > 1 else ""
            line = build_sendlen_line(n, prefix)
            send(line, current_timeout, f"sendlen {n}")
        elif d.kind == "raw":
            transport.send_raw(unescape_raw(d.arg), current_listen)
        elif d.kind == "key":
            transport.send_raw(resolve_key_bytes(d.arg), current_listen)
        elif d.kind == "listen":
            current_listen = float(d.arg)
            transport.capture(current_listen)
        elif d.kind == "settle":
            transport.note_settle(float(d.arg))
        elif d.kind == "timeout":
            current_timeout = float(d.arg)
        elif d.kind == "pause":
            run_pause(d.arg)
        else:
            raise ScriptUsageError(f"{d.source}: unhandled directive {d.kind!r}")

    return worst


class _AppendDirective(argparse.Action):
    """Appends a Directive onto one shared `directives` list in declaration
    order, so --send/--raw/--key/--sendlen/--listen/--pause interleave on
    the command line exactly the way lines in a --script file would --
    composing a scripted run directly on the command line is the same
    engine, not a second one."""

    def __call__(self, parser, namespace, values, option_string=None):
        directives = getattr(namespace, "directives", None)
        if directives is None:
            directives = []
            setattr(namespace, "directives", directives)
        directives.append(Directive(self.dest, values, f"--{self.dest}"))


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "protoArtoo Console Client: boot-log capture, interactive serial "
            "terminal, and scripted mode on either Console Adapter (#264). The "
            "default POSIX backend does not reset the ESP32 on connect (measured "
            "0/5 unseated); --pyserial does, every time."
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
    parser.add_argument(
        "--script", default=None, metavar="FILE",
        help="Scripted mode: run one directive per line from FILE (serial via --port)."
    )
    parser.add_argument(
        "--send", action=_AppendDirective, metavar="LINE",
        help="Scripted directive: line + CR, wait for the request to close. Repeatable."
    )
    parser.add_argument(
        "--raw", action=_AppendDirective, metavar="ESCAPED-BYTES",
        help="Scripted directive: byte-exact send (serial only), backslash escapes honoured."
    )
    parser.add_argument(
        "--key", action=_AppendDirective, metavar="NAME[,NAME...]",
        help=f"Scripted directive: byte-exact key send (serial only). Known keys: "
             f"{', '.join(sorted(KEY_BYTES))}."
    )
    parser.add_argument(
        "--sendlen", action=_AppendDirective, metavar="N [PREFIX]",
        help="Scripted directive: build and send an exactly-N-byte line (overflow rows)."
    )
    parser.add_argument(
        "--listen", action=_AppendDirective, metavar="SECONDS",
        help="Scripted directive: capture only, for SECONDS (serial only)."
    )
    parser.add_argument(
        "--pause", action=_AppendDirective, metavar="TEXT",
        help="Scripted directive: print TEXT and wait for Enter on the controlling terminal."
    )
    parser.add_argument(
        "--settle", type=float, default=None,
        help=f"Scripted mode: seconds to wait before the first serial send "
             f"(default: {DEFAULT_SETTLE_SECONDS})."
    )
    parser.add_argument(
        "--reattach-timeout", type=float, default=None,
        help=f"Scripted serial mode: bounded seconds to wait for a detached port's "
             f"by-id name to return before giving up (default: {DEFAULT_REATTACH_TIMEOUT})."
    )
    parser.add_argument(
        "--http", default=None, metavar="BASE-URL",
        help="Scripted mode transport: the browser adapter's POST /api/console "
             "at BASE-URL, instead of --port. Interactive never goes over HTTP; "
             "raw/key/listen directives are refused."
    )
    parser.add_argument(
        "--board", default=None, metavar="LABEL",
        help="Operator assertion printed on the provenance header as "
             "'BOARD: LABEL (asserted)' -- never a verdict this tool makes itself "
             "(a CP2102 fronts any board; there is no env-to-board mapping to check against)."
    )
    parser.add_argument(
        "--image", default=None, metavar="LABEL",
        help="Provenance header's image identity when no HTTP status is available "
             "(see --status). Falls back to an explicit 'IMAGE: UNKNOWN' line if neither is given."
    )
    parser.add_argument(
        "--status", default=None, metavar="BASE-URL",
        help="Fetch firmwareVersion/fsVersion from BASE-URL/api/status for the "
             "provenance header's image identity, without switching transport to --http."
    )
    color_group = parser.add_mutually_exclusive_group()
    color_group.add_argument(
        "--color", action="store_true",
        help="Force colour on Console Record lines, in scripted and interactive mode "
             "(default: on only when stdout is a TTY)."
    )
    color_group.add_argument(
        "--no-color", action="store_true",
        help="Force colour off, even on a TTY. ANSI never reaches a redirected transcript either way."
    )
    parser.add_argument(
        "--rows", default=None, metavar="NAME[,NAME...]",
        help="Run only these @row blocks from --script, in the order given here "
             "(not file order). Requires the script to declare @row blocks."
    )
    parser.add_argument(
        "--skip-manual", action="store_true",
        help="Run only @row blocks that contain no pause directive (agent-runnable rows)."
    )
    args = parser.parse_args()

    script_directives: list[Directive] = []
    if args.script:
        script_directives.extend(load_script_file(args.script))
    script_directives.extend(getattr(args, "directives", None) or [])

    if script_directives:
        preamble, blocks = split_into_row_blocks(script_directives)
        if blocks:
            try:
                blocks = select_rows(blocks, args.rows, args.skip_manual)
            except ScriptUsageError as e:
                print(f"ERROR: {e}", file=sys.stderr)
                return EXIT_TOOL_FAILURE
            script_directives = flatten_rows(preamble, blocks)
        elif args.rows:
            print("ERROR: --rows given but the script has no @row blocks to select from",
                  file=sys.stderr)
            return EXIT_TOOL_FAILURE

    if script_directives:
        if args.pyserial:
            print("ERROR: scripted mode does not support --pyserial "
                  "(measured 7/7 resets; see docs/troubleshooting.md).", file=sys.stderr)
            return EXIT_TOOL_FAILURE
        if args.interactive or args.stream or args.until:
            print("ERROR: scripted mode cannot be combined with "
                  "--interactive/--stream/--until.", file=sys.stderr)
            return EXIT_TOOL_FAILURE

        initial_timeout = args.timeout if args.timeout is not None else DEFAULT_SEND_TIMEOUT
        color = resolve_color(args)

        if args.http:
            for line in build_provenance_header(args):
                print(line, flush=True)
            print(f"[console] {args.http} (HTTP, scripted)", file=sys.stderr)
            transport = HttpTransport(args.http, color=color)
            try:
                return run_scripted(transport, script_directives, initial_timeout)
            except (ScriptUsageError, ConsoleClientToolFailure) as e:
                print(f"ERROR: {e}", file=sys.stderr)
                return EXIT_TOOL_FAILURE

        try:
            fd = open_posix_port(args.port, args.baud, writable=True)
        except Exception as e:
            print(f"ERROR: could not open {args.port}: {e}", file=sys.stderr)
            return EXIT_TOOL_FAILURE
        for line in build_provenance_header(args):
            print(line, flush=True)
        print(
            f"[console] {args.port} @ {args.baud} baud "
            "(POSIX no-control-line open, scripted)",
            file=sys.stderr,
        )
        settle = args.settle if args.settle is not None else DEFAULT_SETTLE_SECONDS
        reattach_timeout = (args.reattach_timeout if args.reattach_timeout is not None
                            else DEFAULT_REATTACH_TIMEOUT)
        transport = SerialTransport(fd, args.port, args.baud, settle_seconds=settle,
                                     color=color, reattach_timeout=reattach_timeout)
        try:
            return run_scripted(transport, script_directives, initial_timeout)
        except (ScriptUsageError, ConsoleClientToolFailure) as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return EXIT_TOOL_FAILURE
        finally:
            os.close(fd)

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
            return interactive_fd(fd, color=resolve_color(args))
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
