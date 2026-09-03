"""Host-provable coverage for tools/console_client.py (#228, #264).

No board is involved anywhere in this file -- every "serial port" here is one
side of a real pty pair (pty.openpty()), and every "terminal" is either a pipe
(not a tty, exercising the is_tty=False skip path) or another pty (a real tty,
exercising the raw-mode enter/restore path). That is enough to drive the real
functions -- open_posix_port(), run_interactive(), interactive_fd() -- through
a real kernel fd without touching hardware or a controlling terminal.

What #228's pin (trap 1) is explicit about: this proves the HOST-SIDE
properties only -- termios save/restore, signal-to-clean-exit, that Ctrl-C
exits locally rather than reaching the serial fd (docs/console.md's already-
documented convention for every other supported Console terminal), and that
open_posix_port() never touches DTR/RTS regardless of the `writable` flag.
Whether attaching this tool to the real artoo-esp32 avoids a reset is #214's
measured, closed result for the underlying open (unchanged here) -- it is not
re-proven by this file, and this file makes no board-reset claim.
"""

import contextlib
import http.server
import importlib.util
import io
import os
import pty
import random
import select
import signal
import socket
import subprocess
import sys
import tempfile
import termios
import threading
import time
import types
import unittest
import urllib.parse
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO_ROOT / "tools" / "console_client.py"

_spec = importlib.util.spec_from_file_location("console_client", MODULE_PATH)
console_client = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(console_client)


@contextlib.contextmanager
def watchdog(seconds, message):
    """Fail loudly instead of hanging the suite if a blocking call regresses.

    Every test in this file that calls into interactive_fd()/run_interactive()
    with a real blocking select() underneath it is wrapped in this: a signal
    test suite that can hang forever on a regression is worse than one that
    has no coverage there at all, because it takes the whole run down with it
    instead of reporting red.
    """
    def _raise(_signum, _frame):
        raise AssertionError(message)

    old = signal.signal(signal.SIGALRM, _raise)
    signal.alarm(seconds)
    try:
        yield
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, old)


class OpenPosixPortWritable(unittest.TestCase):
    """The `writable` flag is the only difference from the proven-safe read path."""

    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.slave_path = os.ttyname(self.slave)
        self.addCleanup(self._close_quietly, self.master)
        self.addCleanup(self._close_quietly, self.slave)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def test_default_open_is_still_read_only(self):
        fd = console_client.open_posix_port(self.slave_path, 115200)
        self.addCleanup(self._close_quietly, fd)
        with self.assertRaises(OSError):
            os.write(fd, b"x")

    def test_writable_open_can_write_and_the_byte_arrives(self):
        fd = console_client.open_posix_port(self.slave_path, 115200, writable=True)
        self.addCleanup(self._close_quietly, fd)
        os.write(fd, b"hi")
        r, _, _ = select.select([self.master], [], [], 2.0)
        self.assertEqual(r, [self.master], "the write-capable fd never reached the pty master")
        self.assertEqual(os.read(self.master, 8), b"hi")

    def test_stale_input_queued_before_open_is_flushed_not_delivered(self):
        # #264: TCIFLUSH runs immediately after termios inside
        # open_posix_port(), in every serial mode. Queue bytes on the
        # master BEFORE opening the slave through this function at all, so
        # they are sitting in the kernel's input queue under whatever line
        # discipline was in effect before this process's raw-mode termios
        # (and the flush) applied -- exactly the "fragment from before this
        # process existed" the flush exists to discard.
        os.write(self.master, b"stale garbage from before this process existed\n")
        time.sleep(0.05)  # let the kernel queue it before open() flushes

        fd = console_client.open_posix_port(self.slave_path, 115200)
        self.addCleanup(self._close_quietly, fd)

        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            self.fail(f"stale pre-open input was not flushed: {os.read(fd, 256)!r}")


class RunInteractiveDuplex(unittest.TestCase):
    """Drives run_interactive() against real pty/pipe fds, in a background thread."""

    def setUp(self):
        self.serial_master, raw_slave = pty.openpty()
        slave_path = os.ttyname(raw_slave)
        os.close(raw_slave)  # the master fd is what keeps the pty pair alive
        # Reopen the slave through the real code path (open_posix_port), not
        # the bare pty fd: a fresh pty defaults to ONLCR (\n -> \r\n on
        # output), which would make a write of b"help\n" arrive as
        # b"help\r\n" and misrepresent this as run_interactive's behaviour.
        # open_posix_port() clears OPOST, exactly like the real serial port
        # this stands in for.
        self.serial_slave = console_client.open_posix_port(
            slave_path, 115200, writable=True)
        self.stdin_r, self.stdin_w = os.pipe()
        self.stdout_r, self.stdout_w = os.pipe()
        for fd in (self.serial_master, self.serial_slave, self.stdin_r,
                   self.stdin_w, self.stdout_r, self.stdout_w):
            self.addCleanup(self._close_quietly, fd)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def run_in_thread(self):
        result = {}

        def go():
            result["rc"] = console_client.run_interactive(
                self.serial_slave, self.stdin_r, self.stdout_w)

        t = threading.Thread(target=go, daemon=True)
        t.start()
        return t, result

    def test_stdin_bytes_reach_the_serial_side(self):
        t, result = self.run_in_thread()
        os.write(self.stdin_w, b"help\n")
        r, _, _ = select.select([self.serial_master], [], [], 2.0)
        self.assertEqual(r, [self.serial_master], "typed bytes never reached the serial fd")
        self.assertEqual(os.read(self.serial_master, 64), b"help\n")

        os.write(self.stdin_w, b"\x03")  # local exit key (Ctrl-C)
        t.join(timeout=5)
        self.assertFalse(t.is_alive(), "run_interactive did not exit on Ctrl-C")
        self.assertEqual(result["rc"], 0)

    def test_serial_bytes_reach_stdout(self):
        t, result = self.run_in_thread()
        os.write(self.serial_master, b"< id=1 type=result status=ok\n")
        r, _, _ = select.select([self.stdout_r], [], [], 2.0)
        self.assertEqual(r, [self.stdout_r], "serial bytes never reached stdout")
        self.assertEqual(os.read(self.stdout_r, 64), b"< id=1 type=result status=ok\n")

        os.write(self.stdin_w, b"\x03")
        t.join(timeout=5)
        self.assertEqual(result["rc"], 0)

    def test_bytes_after_the_exit_key_in_the_same_chunk_are_dropped_not_forwarded(self):
        t, result = self.run_in_thread()
        os.write(self.stdin_w, b"ab\x03cd")
        t.join(timeout=5)
        self.assertFalse(t.is_alive())
        self.assertEqual(result["rc"], 0)

        r, _, _ = select.select([self.serial_master], [], [], 1.0)
        self.assertEqual(r, [self.serial_master], "the bytes before Ctrl-C must still be forwarded")
        self.assertEqual(os.read(self.serial_master, 64), b"ab")
        # nothing further arrives -- "cd" (after the escape) must not be sent
        r2, _, _ = select.select([self.serial_master], [], [], 0.3)
        self.assertEqual(r2, [], "bytes typed after Ctrl-C must not reach the serial port")

    def test_ctrl_c_exits_locally_and_is_not_forwarded(self):
        # This is the corrected property: docs/console.md already documents
        # Ctrl-C as the LOCAL client's own exit key for every supported
        # Console terminal ("that is your terminal program's own default
        # key, not something the firmware does"). It must end the loop
        # immediately and must NOT reach the serial side.
        t, result = self.run_in_thread()
        os.write(self.stdin_w, b"\x03")
        t.join(timeout=5)
        self.assertFalse(t.is_alive(), "Ctrl-C did not exit the loop locally")
        self.assertEqual(result["rc"], 0)

        r, _, _ = select.select([self.serial_master], [], [], 0.3)
        self.assertEqual(r, [], "Ctrl-C must not be forwarded to the serial fd")

    def test_returns_1_when_the_serial_side_disappears(self):
        t, result = self.run_in_thread()
        os.close(self.serial_master)  # the "device" vanishes
        t.join(timeout=5)
        self.assertFalse(t.is_alive(), "run_interactive hung instead of detecting the closed port")
        self.assertEqual(result["rc"], 1)


# ---------------------------------------------------------------------------
# embedded-cli's device->screen editor traffic (#267)
#
# Read from lib/embedded-cli/src/embedded_cli.c, not invented: lineBreak
# (:235), escSeqCursorRight/Left (:241/:244), escSeqCursorSave/Restore
# (:247/:250), escSeqInsertChar (:253), escSeqDeleteChar (:256), moveCursor()'s
# "\x1B[<n><dir>", and clearCurrentLine()'s carriage return + spaces + carriage
# return. The composites below are the sequences the library writes for the
# three edits #267 names -- Tab completion, history Up/Down, mid-line
# Backspace -- assembled from those primitives. Their exact composition is not
# what the assertions turn on: the colouriser's contract is that ANY byte
# stream comes out unchanged except for SGR at record line boundaries, so
# these exist to make that claim concrete against realistic traffic.
# ---------------------------------------------------------------------------

CLI_LINE_BREAK = b"\r\n"
CLI_PROMPT = b"> "
CLI_CURSOR_LEFT = b"\x1b[D"
CLI_CURSOR_SAVE = b"\x1b[s"
CLI_CURSOR_RESTORE = b"\x1b[u"
CLI_DELETE_CHAR = b"\x1b[P"


def cli_clear_line(width: int) -> bytes:
    """clearCurrentLine(): carriage return, `width` spaces, carriage return."""
    return b"\r" + b" " * width + b"\r"


# Tab with several candidates: clear the line, list each candidate on its own
# line, reprint the invitation and the buffered command (onAutocompleteRequest).
TAB_COMPLETION_STREAM = (
    cli_clear_line(14)
    + b"system.status.health" + CLI_LINE_BREAK
    + b"system.status.rc" + CLI_LINE_BREAK
    + CLI_PROMPT + b"system.status."
)

# History Up: clear the line, invitation, the recalled command, then the live
# autocompletion pass (cursor save, suggestion tail, spaces over the previous
# suggestion, cursor restore).
HISTORY_UP_STREAM = (
    cli_clear_line(20)
    + CLI_PROMPT + b"drive.action.move"
    + CLI_CURSOR_SAVE + b" speed=" + b"   " + CLI_CURSOR_RESTORE
)

# Backspace mid-line: cursor left, delete character (onControlInput's
# '\b'/0x7F branch), then the live autocompletion pass again.
BACKSPACE_STREAM = (
    CLI_CURSOR_LEFT + CLI_DELETE_CHAR
    + CLI_CURSOR_SAVE + b"h" + CLI_CURSOR_RESTORE
)


def strip_sgr(data: bytes) -> bytes:
    """Remove exactly the three SGR sequences the colouriser may insert."""
    for code in (console_client.SGR_RECORD, console_client.SGR_RECORD_ERR,
                 console_client.SGR_RESET):
        data = data.replace(code.encode("ascii"), b"")
    return data


def feed_in_chunks(colorizer, data: bytes, splits) -> bytes:
    """Feed `data` to `colorizer` split at `splits`, returning everything it
    wrote out, including whatever a final flush() releases."""
    out = bytearray()
    prev = 0
    for point in list(splits) + [len(data)]:
        out += colorizer.feed(data[prev:point])
        prev = point
    out += colorizer.flush()
    return bytes(out)


class InteractiveRecordColour(unittest.TestCase):
    """#267 defect 2: colour in interactive mode.

    run_interactive() is a byte pump whose device->screen direction also
    carries embedded-cli's redraw sequences, so the risk the ticket names is
    corrupting that redraw. The contract InteractiveRecordColorizer keeps
    instead of parsing the stream into lines is asserted here: strip the SGR
    it inserts and you have its input back, byte for byte, whatever the
    chunk boundaries -- which is what makes "the redraw still works" a
    property rather than an opinion.
    """

    def test_disabled_is_a_byte_exact_passthrough(self):
        c = console_client.InteractiveRecordColorizer(enabled=False)
        stream = (b"< id=1 type=result status=err outcome=invalid\r\n"
                  + TAB_COMPLETION_STREAM)

        self.assertEqual(c.feed(stream), stream)
        self.assertEqual(c.flush(), b"", "a disabled colouriser must never hold a byte")
        self.assertIsNone(c.select_timeout(),
                          "a disabled colouriser must never make the caller poll")

    def test_record_line_is_wrapped_in_cyan_inside_its_terminator(self):
        c = console_client.InteractiveRecordColorizer(enabled=True)
        line = b"< id=1 type=field name=heapFree value=42120"

        out = c.feed(line + b"\r\n")

        self.assertEqual(
            out,
            console_client.SGR_RECORD.encode() + line
            + console_client.SGR_RESET.encode() + b"\r\n",
            "the reset must close the record BEFORE its terminator, so the "
            "colour never spills onto the next line")

    def test_status_err_record_is_red(self):
        c = console_client.InteractiveRecordColorizer(enabled=True)
        line = b"< id=2 type=result status=err outcome=invalid reason=unknown-operation"

        out = c.feed(line + b"\r\n")

        self.assertTrue(out.startswith(console_client.SGR_RECORD_ERR.encode()))
        self.assertEqual(strip_sgr(out), line + b"\r\n")

    def test_log_lines_prompt_and_redraw_are_never_coloured(self):
        c = console_client.InteractiveRecordColorizer(enabled=True)
        stream = (b"[INFO][SafetyMonitor] estop clear\r\n"
                  + CLI_PROMPT + b"system.stat"
                  + TAB_COMPLETION_STREAM + HISTORY_UP_STREAM + BACKSPACE_STREAM)

        out = feed_in_chunks(c, stream, [])

        self.assertEqual(out, stream,
                         "nothing that is not a Console Record may be touched")

    def test_redraw_traffic_survives_every_chunk_boundary(self):
        """The invariant, over the editor traffic #267 names by name, split at
        every single byte boundary: output minus SGR is the input."""
        stream = (
            cli_clear_line(11)
            + b"[WARN] rc link degraded" + CLI_LINE_BREAK
            + CLI_PROMPT + b"system.stat"
            + TAB_COMPLETION_STREAM
            + b"< id=7 type=begin operation=system.status.health\r\n"
            + b"< id=7 type=field name=estop value=false\r\n"
            + b"< id=7 type=end status=err outcome=failed\r\n"
            + HISTORY_UP_STREAM
            + BACKSPACE_STREAM
        )
        self.assertEqual(strip_sgr(stream), stream,
                         "fixture must not itself contain SGR, or the check is circular")

        for split in range(len(stream) + 1):
            c = console_client.InteractiveRecordColorizer(enabled=True)
            out = feed_in_chunks(c, stream, [split])
            self.assertEqual(strip_sgr(out), stream,
                             f"bytes changed when the stream split at {split}")

    def test_the_invariant_holds_for_adversarial_streams(self):
        """The fixtures above are assembled by hand from embedded_cli.c, so
        the invariant should not depend on how faithful they are. This feeds
        pseudo-random streams built from the bytes that actually decide the
        colouriser's state -- the record prefix's own characters, the two
        terminators, ESC and the bracket that starts every cursor sequence --
        at random chunk boundaries, and asserts the same property: strip the
        SGR and the input comes back byte for byte.

        Seeded, so a failure is reproducible rather than a flake."""
        rng = random.Random(20260904)
        alphabet = [b"<", b" ", b"i", b"d", b"=", b"\r", b"\n", b"\x1b",
                    b"[", b"D", b"s", b"u", b"P", b"x", b"1", b"e", b"r"]
        record = b"< id=5 type=result status=err outcome=invalid"
        colored_any = False

        for _ in range(300):
            stream = bytearray()
            while len(stream) < 120:
                if rng.random() < 0.1:
                    stream += record + b"\r\n"
                else:
                    stream += rng.choice(alphabet)
            stream = bytes(stream)
            self.assertEqual(strip_sgr(stream), stream,
                             "generated fixture must not itself contain SGR")

            splits = sorted(rng.sample(range(len(stream) + 1),
                                       k=rng.randint(0, 6)))
            c = console_client.InteractiveRecordColorizer(enabled=True)
            out = feed_in_chunks(c, stream, splits)

            self.assertEqual(strip_sgr(out), stream,
                             f"bytes changed for splits {splits} on {stream!r}")
            colored_any = colored_any or console_client.SGR_RESET.encode() in out

        self.assertTrue(colored_any,
                        "no record was ever coloured -- the invariant held vacuously")

    def test_records_are_coloured_whatever_the_chunk_boundary(self):
        stream = (b"< id=7 type=begin operation=system.status.health\r\n"
                  b"< id=7 type=end status=err outcome=failed\r\n")

        for split in range(len(stream) + 1):
            c = console_client.InteractiveRecordColorizer(enabled=True)
            out = feed_in_chunks(c, stream, [split])
            self.assertEqual(out.count(console_client.SGR_RECORD.encode()), 1,
                             f"split at {split} lost the ordinary record's colour")
            self.assertEqual(out.count(console_client.SGR_RECORD_ERR.encode()), 1,
                             f"split at {split} lost the error record's colour")
            self.assertEqual(out.count(console_client.SGR_RESET.encode()), 2,
                             f"split at {split} left a colour unclosed")

    def test_sgr_is_inserted_only_at_a_line_boundary(self):
        """An SGR opener may only appear at the very start of the stream or
        directly after a CR or LF -- the one position at which an escape
        sequence provably cannot be in progress, which is what keeps the
        insertion out of the middle of a cursor sequence."""
        stream = (BACKSPACE_STREAM
                  + b"< id=9 type=item value=drive.action.move\r\n"
                  + HISTORY_UP_STREAM
                  + b"< id=9 type=end status=ok outcome=completed\r\n")
        c = console_client.InteractiveRecordColorizer(enabled=True)
        out = feed_in_chunks(c, stream, [])

        for code in (console_client.SGR_RECORD.encode(),
                     console_client.SGR_RECORD_ERR.encode()):
            start = 0
            while True:
                at = out.find(code, start)
                if at == -1:
                    break
                self.assertTrue(at == 0 or out[at - 1:at] in (b"\r", b"\n"),
                                f"colour opened mid-line at offset {at}")
                start = at + len(code)

    def test_a_candidate_that_is_not_a_record_is_released_verbatim(self):
        c = console_client.InteractiveRecordColorizer(enabled=True)

        out = c.feed(b"< idle marker\r\n")

        self.assertEqual(out, b"< idle marker\r\n")
        self.assertEqual(c.flush(), b"")

    def test_a_held_candidate_asks_the_caller_to_poll_and_is_never_lost(self):
        c = console_client.InteractiveRecordColorizer(enabled=True)

        held = c.feed(b"< id")

        self.assertEqual(held, b"", "a candidate prefix must be held, not printed half-coloured")
        self.assertEqual(c.select_timeout(), c.HOLD_TIMEOUT_S,
                         "a holding colouriser must make the caller time its select() out")
        self.assertEqual(c.flush(), b"< id", "held bytes must be released verbatim, never dropped")
        self.assertIsNone(c.select_timeout())

    def test_a_record_line_over_the_hold_limit_is_released_uncoloured(self):
        c = console_client.InteractiveRecordColorizer(enabled=True)
        prefix = b"< id=1 "
        # One byte short of the limit: still a candidate, still held whole.
        almost = prefix + b"x" * (c.HOLD_LIMIT - len(prefix) - 1)
        self.assertEqual(c.feed(almost), b"", "a candidate under the limit must stay held")
        self.assertEqual(c.select_timeout(), c.HOLD_TIMEOUT_S)

        # The byte that reaches the limit releases the whole hold, uncoloured,
        # and the colouriser stops holding rather than growing without bound.
        out = c.feed(b"x")

        self.assertEqual(out, almost + b"x")
        self.assertNotIn(console_client.SGR_RECORD.encode(), out,
                         "a line released at the limit is not a completed record")
        self.assertIsNone(c.select_timeout(), "nothing may still be held at the limit")

        # Whatever follows is ordinary pass-through: nothing is lost.
        tail = b"yyy\r\n"
        self.assertEqual(c.feed(tail), tail)
        self.assertEqual(c.flush(), b"")

    def test_a_record_mid_entry_is_left_alone(self):
        """A record written while the operator is mid-entry does not clear the
        input line (include/console_serial_output.h), so it does not start at
        column 0 and must not be coloured as though it did."""
        c = console_client.InteractiveRecordColorizer(enabled=True)
        stream = CLI_PROMPT + b"system.stat" + b"< id=4 type=result status=ok\r\n"

        out = c.feed(stream)

        self.assertEqual(out, stream)


class InteractiveColourEndToEnd(unittest.TestCase):
    """The same thing through run_interactive() itself, over real fds."""

    def setUp(self):
        self.serial_master, raw_slave = pty.openpty()
        slave_path = os.ttyname(raw_slave)
        os.close(raw_slave)
        self.serial_slave = console_client.open_posix_port(slave_path, 115200, writable=True)
        self.stdin_r, self.stdin_w = os.pipe()
        self.stdout_r, self.stdout_w = os.pipe()
        for fd in (self.serial_master, self.serial_slave, self.stdin_r,
                   self.stdin_w, self.stdout_r, self.stdout_w):
            self.addCleanup(self._close_quietly, fd)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def run_in_thread(self, color):
        result = {}

        def go():
            result["rc"] = console_client.run_interactive(
                self.serial_slave, self.stdin_r, self.stdout_w, color=color)

        t = threading.Thread(target=go, daemon=True)
        t.start()
        return t, result

    def _read_stdout(self, expected_len, timeout=2.0):
        got = bytearray()
        deadline = time.monotonic() + timeout
        while len(got) < expected_len and time.monotonic() < deadline:
            r, _, _ = select.select([self.stdout_r], [], [], 0.2)
            if r:
                got += os.read(self.stdout_r, 4096)
        return bytes(got)

    def test_colour_on_records_reach_the_screen_wrapped(self):
        t, result = self.run_in_thread(color=True)
        line = b"< id=1 type=result status=ok outcome=queued"
        os.write(self.serial_master, line + b"\r\n")

        expected = (console_client.SGR_RECORD.encode() + line
                    + console_client.SGR_RESET.encode() + b"\r\n")
        with watchdog(10, "run_interactive never wrote the coloured record"):
            self.assertEqual(self._read_stdout(len(expected)), expected)

        os.write(self.stdin_w, b"\x03")
        t.join(timeout=5)
        self.assertEqual(result["rc"], 0)

    def test_colour_off_is_byte_identical_to_the_wire(self):
        t, result = self.run_in_thread(color=False)
        wire = b"< id=1 type=result status=err outcome=invalid\r\n"
        os.write(self.serial_master, wire)

        with watchdog(10, "run_interactive never wrote the record"):
            self.assertEqual(self._read_stdout(len(wire)), wire)

        os.write(self.stdin_w, b"\x03")
        t.join(timeout=5)
        self.assertEqual(result["rc"], 0)

    def test_input_stays_byte_exact_with_colour_on(self):
        """Non-negotiable: keystrokes reach the firmware unchanged. Colour is
        a device->screen concern and must never touch this direction."""
        t, result = self.run_in_thread(color=True)
        typed = b"system.status.health\t\x1b[A\x7f\r"
        os.write(self.stdin_w, typed)

        got = bytearray()
        with watchdog(10, "typed bytes never reached the serial fd"):
            while len(got) < len(typed):
                r, _, _ = select.select([self.serial_master], [], [], 2.0)
                if not r:
                    break
                got += os.read(self.serial_master, 256)
        self.assertEqual(bytes(got), typed)

        os.write(self.stdin_w, b"\x03")
        t.join(timeout=5)
        self.assertEqual(result["rc"], 0)

    def test_a_half_arrived_record_still_reaches_the_screen(self):
        """The hold expires on a quiet port: a partial line is printed
        uncoloured rather than sitting invisible until the next byte."""
        t, result = self.run_in_thread(color=True)
        os.write(self.serial_master, b"< id")

        with watchdog(10, "the held candidate was never released"):
            self.assertEqual(self._read_stdout(4), b"< id")

        os.write(self.stdin_w, b"\x03")
        t.join(timeout=5)
        self.assertEqual(result["rc"], 0)


class InteractiveFdTermiosAndSignals(unittest.TestCase):
    """interactive_fd() owns raw-mode enter/restore and signal conversion (trap 2)."""

    def setUp(self):
        self.serial_master, self.serial_slave = pty.openpty()
        self.addCleanup(self._close_quietly, self.serial_master)
        self.addCleanup(self._close_quietly, self.serial_slave)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def test_skips_termios_when_stdin_is_not_a_tty(self):
        # A pipe is not a tty. If interactive_fd() ever called tty.setraw() on
        # it unconditionally (removing the is_tty guard), tcgetattr()/setraw()
        # raise termios.error on a non-tty fd and this test goes red -- that is
        # the mutation this test is proven against.
        stdin_r, stdin_w = os.pipe()
        stdout_r, stdout_w = os.pipe()
        self.addCleanup(self._close_quietly, stdin_r)
        self.addCleanup(self._close_quietly, stdin_w)
        self.addCleanup(self._close_quietly, stdout_r)
        self.addCleanup(self._close_quietly, stdout_w)

        os.write(stdin_w, b"\x03")

        class FakeStdin:
            def fileno(self):
                return stdin_r

        class FakeStdout:
            def fileno(self):
                return stdout_w

        with mock.patch.object(console_client.sys, "stdin", FakeStdin()), \
             mock.patch.object(console_client.sys, "stdout", FakeStdout()):
            rc = console_client.interactive_fd(self.serial_slave)

        self.assertEqual(rc, 0)

    def test_restores_raw_mode_on_a_real_tty_after_a_clean_exit(self):
        stdin_master, stdin_slave = pty.openpty()
        stdout_r, stdout_w = os.pipe()
        self.addCleanup(self._close_quietly, stdin_master)
        self.addCleanup(self._close_quietly, stdin_slave)
        self.addCleanup(self._close_quietly, stdout_r)
        self.addCleanup(self._close_quietly, stdout_w)

        before = termios.tcgetattr(stdin_slave)

        class FakeStdin:
            def fileno(self):
                return stdin_slave

        class FakeStdout:
            def fileno(self):
                return stdout_w

        def send_exit_key_soon():
            # Written from a delayed thread, not before the call: a pty
            # slave starts in canonical (cooked) mode, and a lone control
            # byte with no trailing newline written while still canonical
            # sits in the kernel's uncommitted-line buffer and is never
            # delivered to a reader -- interactive_fd() must be the one to
            # switch the slave to raw mode (via tty.setraw()) before this
            # byte is written, or it is silently lost rather than read.
            time.sleep(0.2)
            os.write(stdin_master, b"\x03")

        threading.Thread(target=send_exit_key_soon, daemon=True).start()

        with watchdog(5, "interactive_fd() did not exit on Ctrl-C"):
            with mock.patch.object(console_client.sys, "stdin", FakeStdin()), \
                 mock.patch.object(console_client.sys, "stdout", FakeStdout()):
                rc = console_client.interactive_fd(self.serial_slave)

        after = termios.tcgetattr(stdin_slave)
        self.assertEqual(rc, 0)
        self.assertEqual(before, after, "raw mode was entered but not fully restored")

    def test_restores_raw_mode_even_when_the_loop_raises(self):
        # Proves the finally-block guarantee independent of run_interactive's
        # own control flow: force a real exception through interactive_fd()
        # and confirm termios is still put back, and that the exception is
        # propagated rather than swallowed (trap 3).
        stdin_master, stdin_slave = pty.openpty()
        stdout_r, stdout_w = os.pipe()
        self.addCleanup(self._close_quietly, stdin_master)
        self.addCleanup(self._close_quietly, stdin_slave)
        self.addCleanup(self._close_quietly, stdout_r)
        self.addCleanup(self._close_quietly, stdout_w)

        before = termios.tcgetattr(stdin_slave)

        class FakeStdin:
            def fileno(self):
                return stdin_slave

        class FakeStdout:
            def fileno(self):
                return stdout_w

        with mock.patch.object(console_client.sys, "stdin", FakeStdin()), \
             mock.patch.object(console_client.sys, "stdout", FakeStdout()), \
             mock.patch.object(console_client, "run_interactive",
                                side_effect=RuntimeError("boom")):
            with self.assertRaises(RuntimeError):
                console_client.interactive_fd(self.serial_slave)

        after = termios.tcgetattr(stdin_slave)
        self.assertEqual(before, after, "raw mode was not restored after an exception")

    def test_sigterm_is_converted_to_a_clean_exit_not_process_death(self):
        # SIGTERM's default disposition kills the process without running any
        # `finally` block. interactive_fd() must intercept it, or a supervisor
        # (or an operator's `kill`) sending SIGTERM leaves the terminal raw.
        stdin_master, stdin_slave = pty.openpty()  # never written to; loop just blocks
        stdout_r, stdout_w = os.pipe()
        self.addCleanup(self._close_quietly, stdin_master)
        self.addCleanup(self._close_quietly, stdin_slave)
        self.addCleanup(self._close_quietly, stdout_r)
        self.addCleanup(self._close_quietly, stdout_w)

        before = termios.tcgetattr(stdin_slave)

        class FakeStdin:
            def fileno(self):
                return stdin_slave

        class FakeStdout:
            def fileno(self):
                return stdout_w

        def send_sigterm_soon():
            time.sleep(0.2)
            os.kill(os.getpid(), signal.SIGTERM)

        threading.Thread(target=send_sigterm_soon, daemon=True).start()
        with watchdog(10, "interactive_fd() did not return within the watchdog window"):
            with mock.patch.object(console_client.sys, "stdin", FakeStdin()), \
                 mock.patch.object(console_client.sys, "stdout", FakeStdout()):
                rc = console_client.interactive_fd(self.serial_slave)

        after = termios.tcgetattr(stdin_slave)
        self.assertEqual(rc, 0)
        self.assertEqual(before, after, "raw mode was not restored after SIGTERM")
        # The handler interactive_fd() installed must not leak past its own call.
        self.assertEqual(signal.getsignal(signal.SIGTERM), signal.SIG_DFL)


class ReadOrNoneAndWriteAll(unittest.TestCase):
    """Direct coverage for the two non-blocking-fd helpers run_interactive() relies on.

    BlockingIOError is a subclass of OSError (docstring at the call sites), so
    the distinction between "retry, nothing happened" and "the peer is gone" or
    "a real I/O error" is easy to lose by mutation; these drive the helpers
    against real non-blocking pipe fds rather than mocks.
    """

    def test_read_or_none_returns_none_on_a_spurious_non_blocking_wakeup(self):
        r, w = os.pipe()
        os.set_blocking(r, False)
        self.addCleanup(os.close, r)
        self.addCleanup(os.close, w)

        self.assertIsNone(console_client._read_or_none(r, 64))

    def test_read_or_none_returns_the_bytes_when_data_is_available(self):
        r, w = os.pipe()
        self.addCleanup(os.close, r)
        self.addCleanup(os.close, w)
        os.write(w, b"x")

        self.assertEqual(console_client._read_or_none(r, 64), b"x")

    def test_write_all_drains_through_backpressure_via_retry(self):
        # A pipe's kernel buffer is bounded (commonly 64 KiB on Linux). Push
        # more than that through a non-blocking write end so at least one
        # os.write() call inside _write_all() must hit BlockingIOError and
        # retry, and confirm every byte still arrives, in order, rather than
        # being dropped, truncated, or raised as an error.
        r, w = os.pipe()
        os.set_blocking(w, False)
        self.addCleanup(os.close, r)
        self.addCleanup(os.close, w)
        payload = bytes(range(256)) * 1024  # 256 KiB, well past the pipe buffer

        received = {}

        def drain():
            got = bytearray()
            while len(got) < len(payload):
                got.extend(os.read(r, 65536))
            received["data"] = bytes(got)

        t = threading.Thread(target=drain, daemon=True)
        t.start()
        with watchdog(10, "_write_all() did not drain a backpressured pipe"):
            console_client._write_all(w, payload)
            t.join(timeout=9)

        self.assertFalse(t.is_alive(), "the reader thread never saw all the bytes")
        self.assertEqual(received.get("data"), payload)


class FakeConsolePeer:
    """Stands in for the firmware side of a pty pair for scripted-mode tests.

    Runs a background reader thread on the master fd, accumulating bytes
    until a CR/LF (matching a real command line's terminator) and looking
    the decoded line up in `responses` to write back -- or, for raw/key
    tests where there is no line terminator at all, every raw chunk
    received is appended to `raw_received` so a test can assert on the
    exact bytes the "firmware" side saw.
    """

    def __init__(self, master_fd: int, responses: dict[str, bytes] | None = None):
        self.master_fd = master_fd
        self.responses = responses or {}
        self.raw_received: list[bytes] = []
        self._buf = bytearray()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        os.set_blocking(self.master_fd, False)
        while not self._stop.is_set():
            r, _, _ = select.select([self.master_fd], [], [], 0.05)
            if not r:
                continue
            try:
                chunk = os.read(self.master_fd, 256)
            except (BlockingIOError, OSError):
                continue
            if not chunk:
                continue
            self.raw_received.append(chunk)
            self._buf.extend(chunk)
            while b"\r" in self._buf or b"\n" in self._buf:
                idx = min((i for i in
                           (self._buf.find(b"\r"), self._buf.find(b"\n"))
                           if i != -1), default=-1)
                if idx == -1:
                    break
                line = bytes(self._buf[:idx]).decode("utf-8", "replace")
                del self._buf[:idx + 1]
                reply = self.responses.get(line)
                if reply is not None:
                    os.write(self.master_fd, reply)

    def close(self):
        self._stop.set()
        self._thread.join(timeout=2)


class ScriptedSerialEngine(unittest.TestCase):
    """#264: SerialTransport's send/reassembly, raw/key, listen, settle, and
    run_scripted()'s exit-code aggregation on the serial path -- against a
    real pty pair and a synthetic peer, never mocked I/O."""

    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.addCleanup(self._close_quietly, self.master)
        self.addCleanup(self._close_quietly, self.slave)
        self.slave_path = os.ttyname(self.slave)
        self.fd = console_client.open_posix_port(self.slave_path, 115200, writable=True)
        self.addCleanup(self._close_quietly, self.fd)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def _peer(self, responses=None):
        peer = FakeConsolePeer(self.master, responses)
        self.addCleanup(peer.close)
        return peer

    def test_send_reassembles_a_multi_record_group_in_wire_order(self):
        self._peer({
            "system.status.health": (
                b"< id=1 type=begin operation=system.status.health\n"
                b"< id=1 type=field name=estop value=false\n"
                b"< id=1 type=end status=ok outcome=completed\n"
            ),
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            records, closed = transport.send_line("system.status.health", timeout=2.0)

        self.assertTrue(closed)
        self.assertEqual([r.type for r in records], ["begin", "field", "end"])
        self.assertEqual(records[1].fields["name"], "estop")
        printed = out.getvalue()
        self.assertIn("--- send b'system.status.health\\r' ---", printed)
        # Wire order: the marker precedes the echoed records in the transcript.
        self.assertLess(printed.index("--- send"), printed.index("type=begin"))

    def test_a_foreign_id_interleaved_mid_group_never_ends_the_read(self):
        self._peer({
            "system.status.health": (
                b"< id=1 type=begin operation=system.status.health\n"
                b"< id=99 type=result status=ok outcome=queued\n"  # concurrent browser session
                b"< id=1 type=end status=ok outcome=completed\n"
            ),
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)

        with contextlib.redirect_stdout(io.StringIO()):
            records, closed = transport.send_line("system.status.health", timeout=2.0)

        self.assertTrue(closed)
        self.assertEqual([r.id for r in records], [1, 1])

    def test_no_closing_record_times_out(self):
        self._peer({
            "system.status.health": b"< id=1 type=begin operation=system.status.health\n",
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)

        with contextlib.redirect_stdout(io.StringIO()):
            records, closed = transport.send_line("system.status.health", timeout=0.5)

        self.assertFalse(closed)

    def test_dropped_on_the_closing_record_is_flagged_as_loss_not_timeout(self):
        self._peer({
            "system.status.health": (
                b"< id=1 type=begin operation=system.status.health\n"
                b"< id=1 type=end status=ok outcome=completed dropped=2\n"
            ),
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)
        directives = [console_client.Directive("send", "system.status.health", "--send")]

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            worst = console_client.run_scripted(transport, directives)

        self.assertEqual(worst, console_client.EXIT_LOSS)
        self.assertIn("[LOSS] dropped=2", out.getvalue())

    def test_blank_line_inside_a_group_is_an_anomaly_not_the_loss_signature(self):
        self._peer({
            "system.status.health": (
                b"< id=1 type=begin operation=system.status.health\n"
                b"\n"
                b"< id=1 type=end status=ok outcome=completed\n"
            ),
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)
        directives = [console_client.Directive("send", "system.status.health", "--send")]

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            worst = console_client.run_scripted(transport, directives)

        self.assertEqual(worst, console_client.EXIT_OK,
                          "a blank line alone must never change the exit code")
        self.assertIn("[ANOMALY] blank line inside record group id=1", out.getvalue())

    def test_raw_send_is_byte_exact_and_marked_in_wire_position(self):
        peer = self._peer()
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            transport.send_raw(console_client.unescape_raw("sys\\t"), listen_seconds=0.2)

        self.assertIn(r"--- send b'sys\t' ---", out.getvalue())
        self.assertEqual(b"".join(peer.raw_received), b"sys\t")

    def test_key_directive_sends_the_tab_byte_embedded_cli_expects(self):
        peer = self._peer()
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)

        with contextlib.redirect_stdout(io.StringIO()):
            transport.send_raw(console_client.resolve_key_bytes("tab"), listen_seconds=0.2)

        self.assertEqual(b"".join(peer.raw_received), b"\t")

    def test_settle_delays_only_the_first_send(self):
        self._peer({"a": b"< id=1 type=result status=ok outcome=queued\n"})
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0.3)

        with contextlib.redirect_stdout(io.StringIO()):
            t0 = time.monotonic()
            transport.send_line("a", timeout=2.0)
            first_elapsed = time.monotonic() - t0

            t1 = time.monotonic()
            transport.send_line("a", timeout=2.0)
            second_elapsed = time.monotonic() - t1

        self.assertGreaterEqual(first_elapsed, 0.3)
        self.assertLess(second_elapsed, 0.15, "settle must not repeat after the first send")

    def test_run_scripted_serial_takes_the_worst_exit_code_across_directives(self):
        self._peer({
            "ok-one": b"< id=1 type=result status=ok outcome=queued\n",
            # no response registered for "will-timeout" -- times out by design
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200, settle_seconds=0)
        directives = [
            console_client.Directive("send", "ok-one", "--send"),
            console_client.Directive("timeout", "0.3", "--timeout"),
            console_client.Directive("send", "will-timeout", "--send"),
        ]

        with contextlib.redirect_stdout(io.StringIO()):
            worst = console_client.run_scripted(transport, directives)

        self.assertEqual(worst, console_client.EXIT_TIMEOUT)

    def test_status_err_alone_never_changes_the_exit_code(self):
        # docs/console-protocol.md's own verdict rule: `line-too-long`,
        # `secret-not-settable`, `out-of-range` etc. are expected RESULTS of
        # rows, not tool failures -- only `dropped=` or a timeout do that.
        self._peer({
            "bad-op": b"< id=1 type=result status=err outcome=invalid reason=unknown-operation\n",
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200,
                                                     settle_seconds=0)
        directives = [console_client.Directive("send", "bad-op", "--send")]

        with contextlib.redirect_stdout(io.StringIO()):
            worst = console_client.run_scripted(transport, directives)

        self.assertEqual(worst, console_client.EXIT_OK)


class CrLfTerminatedWire(unittest.TestCase):
    """#267: the firmware now terminates every framed serial line with CR LF
    (src/console/console_serial_output.cpp, matching embedded-cli's own
    lineBreak) so records stop staircasing in the raw-mode terminal a Console
    session requires. The scripted parser was already written to tolerate that
    -- SerialTransport._read_group()/_drain_for() rstrip("\r"), the capture
    path rstrip()s -- and #267's acceptance criterion says prove it rather
    than assume it. Everything below feeds the peer CR LF, never a bare LF,
    and asserts on the PARSED values and the printed transcript, which is
    where a surviving CR would show up (a `dropped=2\r` reads as loss for the
    wrong reason; a `dropped=0\r` would read as loss when nothing was lost).

    Same pty pair, same real fds as ScriptedSerialEngine above: the client
    side is opened through open_posix_port(), which clears ICRNL/INLCR/IGNCR
    on the slave, so a CR written by the peer arrives as a CR and is not
    quietly translated by the line discipline before the code under test
    ever sees it.
    """

    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.addCleanup(self._close_quietly, self.master)
        self.addCleanup(self._close_quietly, self.slave)
        self.slave_path = os.ttyname(self.slave)
        self.fd = console_client.open_posix_port(self.slave_path, 115200, writable=True)
        self.addCleanup(self._close_quietly, self.fd)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def _peer(self, responses=None):
        peer = FakeConsolePeer(self.master, responses)
        self.addCleanup(peer.close)
        return peer

    def test_send_group_parses_records_terminated_cr_lf(self):
        self._peer({
            "system.status.health": (
                b"< id=1 type=begin operation=system.status.health\r\n"
                b"< id=1 type=field name=heapFree value=42120\r\n"
                b"< id=1 type=end status=ok outcome=completed\r\n"
            ),
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200,
                                                   settle_seconds=0)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            records, closed = transport.send_line("system.status.health", timeout=2.0)

        self.assertTrue(closed, "a CR LF terminated group must still close")
        self.assertEqual([r.type for r in records], ["begin", "field", "end"])
        # The CR must not survive into a field value: it is line-terminator
        # whitespace, not part of the last token on the line.
        self.assertEqual(records[0].fields["operation"], "system.status.health")
        self.assertEqual(records[1].fields["value"], "42120")
        self.assertEqual(records[2].fields["outcome"], "completed")
        printed = out.getvalue()
        self.assertIn("< id=1 type=end status=ok outcome=completed\n", printed)
        self.assertNotIn("\r", printed, "no carriage return may reach the transcript")

    def test_dropped_on_a_cr_lf_closing_record_still_reads_as_loss(self):
        self._peer({
            "system.status.health": (
                b"< id=1 type=begin operation=system.status.health\r\n"
                b"< id=1 type=end status=ok outcome=completed dropped=2\r\n"
            ),
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200,
                                                   settle_seconds=0)
        directives = [console_client.Directive("send", "system.status.health", "--send")]

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            worst = console_client.run_scripted(transport, directives)

        self.assertEqual(worst, console_client.EXIT_LOSS)
        # Exact: `dropped=2` and nothing else. ADR 0036's drop count is the
        # last token on the line, so it is the field a surviving CR lands in.
        self.assertIn("[LOSS] dropped=2 on closing record id=1", out.getvalue())

    def test_listen_window_prints_cr_lf_lines_without_the_carriage_return(self):
        self._peer({
            "listen-trigger": (
                b"[INFO][ConsoleTask] active\r\n"
                b"< id=3 type=result status=ok outcome=queued\r\n"
            ),
        })
        transport = console_client.SerialTransport(self.fd, self.slave_path, 115200,
                                                   settle_seconds=0)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            transport.send_raw(b"listen-trigger\r", listen_seconds=0.4)

        printed = out.getvalue()
        self.assertIn("[INFO][ConsoleTask] active\n", printed)
        self.assertIn("< id=3 type=result status=ok outcome=queued\n", printed)
        self.assertNotIn("\r\n", printed, "the listen window must not echo the wire CR")

    def test_capture_until_matches_a_cr_lf_terminated_line(self):
        """The read-only capture path (`--until`, the boot-log capture every
        epic uses) shares the wire with records and now sees CR LF on the
        firmware's pre-console-task log lines too."""
        os.write(self.master, b"boot: stage one\r\ninit complete\r\n")

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = console_client.read_lines_fd(self.fd, time.time() + 2.0, "init complete")

        self.assertEqual(rc, 0, "--until must match a CR LF terminated line")
        printed = out.getvalue()
        self.assertIn("boot: stage one\n", printed)
        self.assertNotIn("\r", printed)


class DetachAndReattach(unittest.TestCase):
    """#264 "Detach survives": a real I/O error on the serial fd (measured
    below, not assumed -- closing a pty's master end makes the slave's next
    write raise EIO and its next read return EOF, matching a real unplugged
    USB-serial adapter) waits (bounded) for the port's by-id name to
    return via tools/resolve_upload_port.py's discover(), reopens through
    the same open_posix_port(), and writes a gap marker to the transcript.
    """

    def setUp(self):
        self.master1, self.slave1 = pty.openpty()
        self.slave1_path = os.ttyname(self.slave1)
        self.master2, self.slave2 = pty.openpty()
        self.slave2_path = os.ttyname(self.slave2)
        for fd in (self.master1, self.slave1, self.master2, self.slave2):
            self.addCleanup(self._close_quietly, fd)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def test_a_write_error_reattaches_by_id_and_marks_the_gap(self):
        fd1 = console_client.open_posix_port(self.slave1_path, 115200, writable=True)
        transport = console_client.SerialTransport(fd1, self.slave1_path, 115200,
                                                     settle_seconds=0)
        transport.by_id = "usb-Fake-if00"  # pretend this port has a stable identity

        # The detach: closing the master is what makes the NEXT write on
        # the slave fd raise EIO, measured above -- not simulated with a
        # mock of the write call itself.
        os.close(self.master1)

        peer2 = FakeConsolePeer(self.master2, {"a": b"< id=1 type=result status=ok outcome=queued\n"})
        self.addCleanup(peer2.close)

        calls = {"n": 0}

        def fake_discover():
            calls["n"] += 1
            if calls["n"] < 2:
                return []  # not back yet
            return [(self.slave2_path, "usb-Fake-if00")]

        with mock.patch.object(console_client.resolve_upload_port, "discover",
                                side_effect=fake_discover), \
             mock.patch.object(console_client.time, "sleep", return_value=None):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                records, closed = transport.send_line("a", timeout=2.0)

        self.assertTrue(closed, "the resend after reattach must still reassemble normally")
        self.assertEqual(transport.port, self.slave2_path)
        printed = out.getvalue()
        self.assertIn("--- gap:", printed)
        self.assertIn("usb-Fake-if00", printed)
        self.assertIn(f"--- reattached: {self.slave2_path} ---", printed)

    def test_a_read_eof_reattaches_the_port_but_does_not_resend_the_in_flight_request(self):
        # The write succeeds (master1 is still open at that instant); the
        # detach lands a moment later, while THIS request is waiting on its
        # reply -- exercising _read_chunk_or_detach's EOF branch, distinct
        # from the write-failure path proven above. time.sleep() is left
        # real here (not mocked) so this background timer and the reattach
        # loop's own poll delay do not race each other through one patched
        # function.
        fd1 = console_client.open_posix_port(self.slave1_path, 115200, writable=True)
        transport = console_client.SerialTransport(
            fd1, self.slave1_path, 115200, settle_seconds=0, reattach_timeout=5.0)
        transport.by_id = "usb-Fake-if00"

        def detach_shortly():
            time.sleep(0.1)
            os.close(self.master1)

        threading.Thread(target=detach_shortly, daemon=True).start()

        calls = {"n": 0}

        def fake_discover():
            calls["n"] += 1
            if calls["n"] < 2:
                return []
            return [(self.slave2_path, "usb-Fake-if00")]

        with mock.patch.object(console_client.resolve_upload_port, "discover",
                                side_effect=fake_discover):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                records, closed = transport.send_line("a", timeout=1.5)

        self.assertFalse(
            closed, "nothing was ever sent to the reattached port for THIS request")
        printed = out.getvalue()
        self.assertIn("--- gap:", printed)
        self.assertIn("--- reattached:", printed)
        self.assertEqual(transport.port, self.slave2_path)

        # The port itself has recovered: the NEXT directive works normally.
        peer2 = FakeConsolePeer(self.master2, {"b": b"< id=2 type=result status=ok outcome=queued\n"})
        self.addCleanup(peer2.close)
        with contextlib.redirect_stdout(io.StringIO()):
            records2, closed2 = transport.send_line("b", timeout=2.0)
        self.assertTrue(closed2, "a later directive must still work against the reattached port")

    def test_giving_up_after_the_bounded_wait_is_a_tool_failure(self):
        fd1 = console_client.open_posix_port(self.slave1_path, 115200, writable=True)
        transport = console_client.SerialTransport(
            fd1, self.slave1_path, 115200, settle_seconds=0, reattach_timeout=0.2)
        transport.by_id = "usb-Fake-if00"
        os.close(self.master1)

        with mock.patch.object(console_client.resolve_upload_port, "discover", return_value=[]), \
             mock.patch.object(console_client.time, "sleep", return_value=None):
            with self.assertRaises(console_client.ConsoleClientToolFailure):
                with contextlib.redirect_stdout(io.StringIO()):
                    transport.send_line("a", timeout=2.0)

    def test_a_port_with_no_by_id_name_waits_for_the_same_device_path(self):
        fd1 = console_client.open_posix_port(self.slave1_path, 115200, writable=True)
        transport = console_client.SerialTransport(fd1, self.slave1_path, 115200,
                                                     settle_seconds=0, reattach_timeout=0.2)
        self.assertIsNone(transport.by_id, "a pty path has no by-id entry to find")
        os.close(self.master1)

        with mock.patch.object(console_client.resolve_upload_port, "discover",
                                return_value=[(self.slave2_path, None)]), \
             mock.patch.object(console_client.time, "sleep", return_value=None):
            with self.assertRaises(console_client.ConsoleClientToolFailure):
                with contextlib.redirect_stdout(io.StringIO()):
                    transport.send_line("a", timeout=2.0)
        # Refusing here is correct: discover() never reports the ORIGINAL
        # path back (it reports a different one, self.slave2_path), and
        # with no by-id name there is nothing else to match against.


def _directives(*pairs):
    return [console_client.Directive(kind, arg, "test") for kind, arg in pairs]


class RowsSelection(unittest.TestCase):
    """#264: @row blocks, --rows (an explicit replay order, not file order),
    and --skip-manual (drop any block containing a pause)."""

    def test_split_separates_preamble_from_labelled_blocks(self):
        directives = _directives(
            ("timeout", "5"),
            ("row", "217 boot-check"),
            ("send", "system.status.health"),
            ("row", "260 detach-replug"),
            ("pause", "unplug the cable"),
            ("send", "system.status.health"),
        )
        preamble, blocks = console_client.split_into_row_blocks(directives)

        self.assertEqual([(d.kind, d.arg) for d in preamble], [("timeout", "5")])
        self.assertEqual([(b.ticket, b.label) for b in blocks],
                          [("217", "boot-check"), ("260", "detach-replug")])
        self.assertEqual([d.kind for d in blocks[0].directives], ["send"])
        self.assertEqual([d.kind for d in blocks[1].directives], ["pause", "send"])

    def test_rows_selects_in_the_order_given_not_file_order(self):
        directives = _directives(
            ("row", "217 boot-check"), ("send", "a"),
            ("row", "260 detach-replug"), ("send", "b"),
        )
        _, blocks = console_client.split_into_row_blocks(directives)

        selected = console_client.select_rows(blocks, "detach-replug,boot-check", False)

        self.assertEqual([b.label for b in selected], ["detach-replug", "boot-check"])

    def test_rows_with_an_unknown_name_raises_and_names_the_available_ones(self):
        directives = _directives(("row", "217 boot-check"), ("send", "a"))
        _, blocks = console_client.split_into_row_blocks(directives)

        with self.assertRaises(console_client.ScriptUsageError) as ctx:
            console_client.select_rows(blocks, "nonexistent", False)
        self.assertIn("nonexistent", str(ctx.exception))
        self.assertIn("boot-check", str(ctx.exception))

    def test_skip_manual_drops_only_blocks_containing_pause(self):
        directives = _directives(
            ("row", "217 boot-check"), ("send", "a"),
            ("row", "260 detach-replug"), ("pause", "unplug"), ("send", "b"),
        )
        _, blocks = console_client.split_into_row_blocks(directives)

        selected = console_client.select_rows(blocks, None, True)

        self.assertEqual([b.label for b in selected], ["boot-check"])

    def test_flatten_reprints_the_row_marker_for_run_scripted(self):
        directives = _directives(("row", "217 boot-check"), ("send", "a"))
        preamble, blocks = console_client.split_into_row_blocks(directives)

        flat = console_client.flatten_rows(preamble, blocks)

        self.assertEqual([(d.kind, d.arg) for d in flat],
                          [("row", "217 boot-check"), ("send", "a")])

    def test_end_to_end_skip_manual_over_http_runs_only_the_agent_row(self):
        script_path = os.path.join(
            tempfile.mkdtemp(), "rows.txt")
        with open(script_path, "w") as f:
            f.write(
                "@row 217 boot-check\n"
                "send a\n"
                "@row 260 detach-replug\n"
                "pause unplug the cable\n"
                "send b\n"
            )
        responses = {"a": (200, b'{"records":[{"id":1,"type":"result","status":"ok","outcome":"queued"}]}')}
        stub = _ConsoleStub(responses)
        self.addCleanup(stub.close)

        r = subprocess.run(
            [sys.executable, str(MODULE_PATH), "--http", stub.base_url,
             "--script", script_path, "--skip-manual"],
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("=== row 217 boot-check ===", r.stdout)
        self.assertNotIn("detach-replug", r.stdout)
        self.assertNotIn("[PAUSE]", r.stdout)


class ShippedBenchRows(unittest.TestCase):
    """#264: tools/bench_rows/{firebeetle2,artoo_esp32}.txt -- the
    replayable sheets #215/#216 own from here -- parse cleanly, declare no
    duplicate row names (--rows selects by name; a collision would make one
    row unreachable), and --skip-manual leaves at least one row runnable on
    each file (a sheet that is entirely manual would make the "and its
    agents drive" outcome false)."""

    BENCH_ROWS_DIR = REPO_ROOT / "tools" / "bench_rows"

    def _load(self, filename):
        directives = console_client.load_script_file(str(self.BENCH_ROWS_DIR / filename))
        return console_client.split_into_row_blocks(directives)

    def test_firebeetle2_rows_parse_with_no_duplicate_names(self):
        _, blocks = self._load("firebeetle2.txt")
        labels = [b.label for b in blocks]
        self.assertTrue(labels, "expected at least one @row block")
        self.assertEqual(len(labels), len(set(labels)), f"duplicate row names: {labels}")

    def test_artoo_esp32_rows_parse_with_no_duplicate_names(self):
        _, blocks = self._load("artoo_esp32.txt")
        labels = [b.label for b in blocks]
        self.assertTrue(labels, "expected at least one @row block")
        self.assertEqual(len(labels), len(set(labels)), f"duplicate row names: {labels}")

    def test_firebeetle2_has_at_least_one_agent_runnable_row(self):
        _, blocks = self._load("firebeetle2.txt")
        selected = console_client.select_rows(blocks, None, True)
        self.assertTrue(selected, "--skip-manual left nothing runnable")

    def test_artoo_esp32_has_at_least_one_agent_runnable_row(self):
        _, blocks = self._load("artoo_esp32.txt")
        selected = console_client.select_rows(blocks, None, True)
        self.assertTrue(selected, "--skip-manual left nothing runnable")

    def test_every_row_directive_is_a_known_kind(self):
        # load_script_file() already raises on an unknown directive keyword
        # (a bare command missing its `send` prefix is the classic mistake
        # here); this just proves both files actually get that far for
        # every row, not only the ones exercised above.
        for filename in ("firebeetle2.txt", "artoo_esp32.txt"):
            directives = console_client.load_script_file(str(self.BENCH_ROWS_DIR / filename))
            self.assertTrue(directives, filename)


class _ConsoleStubHandler(http.server.BaseHTTPRequestHandler):
    """Reproduces src/web/api_console.cpp's POST /api/console response
    shapes for exactly the commands a test registers, so HttpTransport is
    proven against the real three wire shapes (200, 500, 200+truncated)
    without a live board -- #266's panic makes a live-board transcript
    unavailable today."""

    def log_message(self, format, *args):
        pass  # keep test output on the actual assertions, not access logs

    def do_POST(self):
        if self.path != "/api/console":
            self.send_response(404)
            self.end_headers()
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        form = urllib.parse.parse_qs(body.decode("utf-8"))
        command = form.get("command", [""])[0]
        fixture = self.server.responses.get(command)
        if fixture is None:
            status, response_body = 404, b'{"ok":false,"error":"no fixture for command"}'
        else:
            status, response_body = fixture
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        self.wfile.write(response_body)


class _ConsoleStub:
    def __init__(self, responses: dict[str, tuple[int, bytes]]):
        self.httpd = http.server.HTTPServer(("127.0.0.1", 0), _ConsoleStubHandler)
        self.httpd.responses = responses
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.httpd.server_port}"

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=2)


class HttpTransportAgainstAStub(unittest.TestCase):
    """#264: HttpTransport against a local stub reproducing the three
    src/web/api_console.cpp response shapes the coordinator pin names --
    the normal 200, the 500 "response too large for this adapter" record
    overflow, and the 200 envelope carrying "truncated":true. Live-board
    HTTP is unavailable (#266's panic); this is the stub proof the ticket
    asks for in its place.
    """

    def setUp(self):
        self.responses: dict[str, tuple[int, bytes]] = {}
        self.stub = _ConsoleStub(self.responses)
        self.addCleanup(self.stub.close)

    def test_a_normal_response_renders_the_wire_line_grammar(self):
        self.responses["system.status.health"] = (
            200,
            b'{"records":[{"id":1,"type":"begin","operation":"system.status.health"},'
            b'{"id":1,"type":"field","name":"estop","value":"false"},'
            b'{"id":1,"type":"end","status":"ok","outcome":"completed"}]}',
        )
        transport = console_client.HttpTransport(self.stub.base_url)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            records, closed = transport.send_line("system.status.health", timeout=2.0)

        self.assertTrue(closed)
        self.assertEqual([r.type for r in records], ["begin", "field", "end"])
        printed = out.getvalue()
        self.assertIn("< id=1 type=begin operation=system.status.health", printed)
        self.assertIn("< id=1 type=field name=estop value=false", printed)
        self.assertIn("< id=1 type=end status=ok outcome=completed", printed)

    def test_a_500_response_is_reported_as_adapter_capped_not_a_timeout(self):
        self.responses["operations"] = (
            500, b'{"ok":false,"error":"response too large for this adapter"}',
        )
        transport = console_client.HttpTransport(self.stub.base_url)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            with self.assertRaises(console_client.AdapterCapped) as ctx:
                transport.send_line("operations", timeout=2.0)

        self.assertIn("response too large for this adapter", str(ctx.exception))
        self.assertIn("[ADAPTER-CAPPED]", out.getvalue())

    def test_a_truncated_envelope_still_prints_its_records_then_caps(self):
        self.responses["system.status.logs"] = (
            200,
            b'{"records":[{"id":1,"type":"item","value":"line one"},'
            b'{"id":1,"type":"end","status":"ok","outcome":"completed"}],'
            b'"truncated":true}',
        )
        transport = console_client.HttpTransport(self.stub.base_url)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            with self.assertRaises(console_client.AdapterCapped):
                transport.send_line("system.status.logs", timeout=2.0)

        printed = out.getvalue()
        self.assertIn("< id=1 type=item value=line one", printed)
        self.assertIn("[ADAPTER-CAPPED] response envelope truncated=true", printed)

    def test_run_scripted_maps_adapter_capped_to_exit_4(self):
        self.responses["operations"] = (
            500, b'{"ok":false,"error":"response too large for this adapter"}',
        )
        transport = console_client.HttpTransport(self.stub.base_url)
        directives = [console_client.Directive("send", "operations", "--send")]

        with contextlib.redirect_stdout(io.StringIO()):
            worst = console_client.run_scripted(transport, directives)

        self.assertEqual(worst, console_client.EXIT_ADAPTER_CAPPED)

    def test_raw_key_listen_are_refused_over_http(self):
        transport = console_client.HttpTransport(self.stub.base_url)
        with self.assertRaises(console_client.ScriptUsageError):
            transport.send_raw(b"\t", 0.1)
        with self.assertRaises(console_client.ScriptUsageError):
            transport.capture(0.1)

    def test_unreachable_host_is_a_tool_failure_not_a_per_request_outcome(self):
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()  # nothing listens here now
        transport = console_client.HttpTransport(f"http://127.0.0.1:{port}")

        with self.assertRaises(console_client.ConsoleClientToolFailure):
            with contextlib.redirect_stdout(io.StringIO()):
                transport.send_line("system.status.health", timeout=1.0)

    def test_colour_defaults_off_when_stdout_is_piped(self):
        # A real end-to-end process, not colorize_record_line() in
        # isolation: subprocess.run(capture_output=True) makes stdout a
        # pipe, which is exactly the case main()'s own
        # `sys.stdout.isatty()` decision (no --color/--no-color given) must
        # resolve to False for -- an error record proves it, since that is
        # the one line colorize_record_line() would otherwise wrap in
        # ANSI red.
        self.responses["bad-op"] = (
            200,
            b'{"records":[{"id":1,"type":"result","status":"err",'
            b'"outcome":"invalid","reason":"unknown-operation"}]}',
        )

        r = subprocess.run(
            [sys.executable, str(MODULE_PATH), "--http", self.stub.base_url, "--send", "bad-op"],
            capture_output=True, text=True, timeout=10,
        )

        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("status=err", r.stdout)
        self.assertNotIn("\x1b[", r.stdout, "ANSI reached a piped (non-TTY) transcript")


def _fake_args(**overrides):
    """A minimal stand-in for argparse's Namespace -- only the attributes
    build_provenance_header()/its helpers actually read."""
    base = dict(port="/dev/ttyFAKE0", baud=115200, http=None, status=None,
                board=None, image=None)
    base.update(overrides)
    return types.SimpleNamespace(**base)


class ProvenanceAndColor(unittest.TestCase):
    """#264: the provenance header's mandatory-image-identity cascade,
    by-id lookup, and the two-state colour rule."""

    def test_colorize_leaves_non_record_lines_untouched(self):
        for line in ("[TIMEOUT] send did not close", "--- send b'x' ---",
                     "[54012][W][WebServer] heap floor", ""):
            self.assertEqual(console_client.colorize_record_line(line, True), line)

    def test_colorize_disabled_is_always_a_no_op(self):
        line = "< id=1 type=end status=err outcome=invalid"
        self.assertEqual(console_client.colorize_record_line(line, False), line)

    def test_colorize_marks_an_ok_record_and_an_error_record_with_different_colours(self):
        ok = console_client.colorize_record_line(
            "< id=1 type=end status=ok outcome=completed", True)
        err = console_client.colorize_record_line(
            "< id=1 type=end status=err outcome=invalid", True)
        ok_code = ok.split("< id=", 1)[0]
        err_code = err.split("< id=", 1)[0]
        self.assertTrue(ok_code, "an ok record must still be coloured (state 1 of the two)")
        self.assertTrue(err_code, "an error record must still be coloured (state 2 of the two)")
        self.assertNotEqual(ok_code, err_code,
                             "ok and error records must use different ANSI codes")

    def test_lookup_by_id_finds_the_stable_name_for_the_requested_device(self):
        with tempfile.TemporaryDirectory() as root:
            device = os.path.join(root, "ttyACM0")
            open(device, "w").close()
            by_id_dir = os.path.join(root, "by-id")
            os.mkdir(by_id_dir)
            os.symlink(device, os.path.join(by_id_dir, "usb-Espressif-if00"))
            with mock.patch.object(
                console_client.resolve_upload_port, "discover",
                return_value=[(device, "usb-Espressif-if00")],
            ):
                self.assertEqual(console_client.lookup_by_id(device), "usb-Espressif-if00")

    def test_lookup_by_id_returns_none_for_an_unlisted_device(self):
        with mock.patch.object(console_client.resolve_upload_port, "discover", return_value=[]):
            self.assertIsNone(console_client.lookup_by_id("/dev/ttyNOPE"))

    def test_image_identity_prefers_http_status_over_the_image_flag(self):
        args = _fake_args(image="operator-label")
        with mock.patch.object(console_client, "fetch_json_status",
                                return_value={"firmwareVersion": "v9", "fsVersion": "fs-v9"}), \
             mock.patch.object(console_client, "git_head_and_dirty", return_value=("abcdef01", False)), \
             mock.patch.object(console_client, "lookup_by_id", return_value=None):
            args.status = "http://10.0.0.5"
            header = console_client.build_provenance_header(args)
        self.assertIn("IMAGE: firmwareVersion=v9 fsVersion=fs-v9", header)

    def test_image_identity_falls_back_to_the_image_flag_when_status_is_unreachable(self):
        args = _fake_args(image="operator-label", status="http://10.0.0.5")
        with mock.patch.object(console_client, "fetch_json_status", return_value=None), \
             mock.patch.object(console_client, "git_head_and_dirty", return_value=("abcdef01", False)), \
             mock.patch.object(console_client, "lookup_by_id", return_value=None):
            header = console_client.build_provenance_header(args)
        self.assertIn("IMAGE: operator-label", header)

    def test_image_identity_is_an_explicit_unknown_line_absent_both(self):
        args = _fake_args()
        with mock.patch.object(console_client, "git_head_and_dirty", return_value=("abcdef01", False)), \
             mock.patch.object(console_client, "lookup_by_id", return_value=None):
            header = console_client.build_provenance_header(args)
        self.assertIn("IMAGE: UNKNOWN (not evidence)", header)

    def test_board_line_states_the_operator_assertion_without_a_verdict(self):
        args = _fake_args(board="firebeetle2")
        with mock.patch.object(console_client, "git_head_and_dirty", return_value=("abcdef01", False)), \
             mock.patch.object(console_client, "lookup_by_id", return_value=None):
            header = console_client.build_provenance_header(args)
        self.assertIn("BOARD: firebeetle2 (asserted)", header)

    def test_board_line_is_explicit_when_not_asserted(self):
        args = _fake_args()
        with mock.patch.object(console_client, "git_head_and_dirty", return_value=("abcdef01", False)), \
             mock.patch.object(console_client, "lookup_by_id", return_value=None):
            header = console_client.build_provenance_header(args)
        self.assertIn("BOARD: (not asserted)", header)

    def test_repo_line_is_explicit_unknown_when_git_metadata_is_unavailable(self):
        args = _fake_args()
        with mock.patch.object(console_client, "git_head_and_dirty", return_value=None), \
             mock.patch.object(console_client, "lookup_by_id", return_value=None):
            header = console_client.build_provenance_header(args)
        self.assertIn("REPO: UNKNOWN", header)

    def test_git_head_and_dirty_reads_the_real_repo(self):
        result = console_client.git_head_and_dirty(str(REPO_ROOT))
        self.assertIsNotNone(result)
        sha, dirty = result
        self.assertRegex(sha, r"^[0-9a-f]{8}$")
        self.assertIsInstance(dirty, bool)

    def test_git_head_and_dirty_is_none_outside_a_git_checkout(self):
        with tempfile.TemporaryDirectory() as root:
            self.assertIsNone(console_client.git_head_and_dirty(root))


class DirectiveParsingAndHelpers(unittest.TestCase):
    """#264: the small pure-logic pieces the scripted engine is built from."""

    def test_blank_and_comment_lines_are_skipped(self):
        self.assertIsNone(console_client.parse_directive_line("", "f:1"))
        self.assertIsNone(console_client.parse_directive_line("   ", "f:2"))
        self.assertIsNone(console_client.parse_directive_line("# a comment", "f:3"))

    def test_row_marker_is_split_from_send_lines(self):
        d = console_client.parse_directive_line("@row 260 detach-replug", "f:1")
        self.assertEqual((d.kind, d.arg), ("row", "260 detach-replug"))

    def test_send_argument_keeps_embedded_spaces_and_quotes(self):
        d = console_client.parse_directive_line(
            'send wifi.config.settings mode=client sta-ssid="Workshop WiFi"', "f:1")
        self.assertEqual(d.kind, "send")
        self.assertEqual(d.arg, 'wifi.config.settings mode=client sta-ssid="Workshop WiFi"')

    def test_unknown_directive_raises(self):
        with self.assertRaises(console_client.ScriptUsageError):
            console_client.parse_directive_line("frobnicate foo", "f:1")

    def test_resolve_key_bytes_maps_known_names(self):
        self.assertEqual(console_client.resolve_key_bytes("tab"), b"\t")
        self.assertEqual(console_client.resolve_key_bytes("up,up,enter"), b"\x1b[A\x1b[A\r")

    def test_resolve_key_bytes_rejects_unknown_name(self):
        with self.assertRaises(console_client.ScriptUsageError):
            console_client.resolve_key_bytes("pageup")

    def test_unescape_raw_matches_backslash_escape_grammar(self):
        self.assertEqual(console_client.unescape_raw("sys\\t"), b"sys\t")
        self.assertEqual(console_client.unescape_raw("a\\r"), b"a\r")

    def test_build_sendlen_line_pads_with_filler(self):
        line = console_client.build_sendlen_line(10, "ab")
        self.assertEqual(len(line), 10)
        self.assertTrue(line.startswith("ab"))

    def test_build_sendlen_line_truncates_an_oversized_prefix(self):
        self.assertEqual(console_client.build_sendlen_line(3, "abcdef"), "abc")

    def test_pause_without_a_controlling_terminal_fails_loudly(self):
        with mock.patch.object(console_client.sys.stdin, "isatty", return_value=False):
            with self.assertRaises(console_client.ScriptUsageError):
                console_client.run_pause("unplug the cable")

    def test_pause_with_a_controlling_terminal_waits_for_enter(self):
        with mock.patch.object(console_client.sys.stdin, "isatty", return_value=True), \
             mock.patch("builtins.input", return_value="") as mocked_input:
            console_client.run_pause("unplug the cable")
        mocked_input.assert_called_once()


class ReadLinesFdBurstDrain(unittest.TestCase):
    """#264: a burst delivered in ONE os.read() call must not strand its
    later lines behind a single-line-per-select()-wakeup drain.

    Proven against a real pty pair, not a mocked read(): the whole point is
    that a short multi-line burst genuinely arrives as one kernel read (well
    under the 256-byte request), which is exactly the case the old
    read_fd_line()/read_lines_fd() pair mishandled -- see their docstrings.
    """

    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.addCleanup(self._close_quietly, self.master)
        self.addCleanup(self._close_quietly, self.slave)

    @staticmethod
    def _close_quietly(fd):
        try:
            os.close(fd)
        except OSError:
            pass

    def _open_reader(self):
        slave_path = os.ttyname(self.slave)
        return console_client.open_posix_port(slave_path, 115200)

    def test_a_burst_delivered_in_one_read_prints_every_line(self):
        fd = self._open_reader()
        self.addCleanup(self._close_quietly, fd)
        os.write(self.master, b"line one\nline two\nline three\n")
        # Give the kernel a moment to queue the whole burst before the first
        # select() wake-up -- what makes one os.read() see all of it.
        time.sleep(0.05)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = console_client.read_lines_fd(fd, time.time() + 1.0, None)

        self.assertEqual(rc, 0)
        self.assertEqual(out.getvalue().splitlines(),
                          ["line one", "line two", "line three"])

    def test_until_matches_the_last_line_of_a_burst_not_just_the_first(self):
        fd = self._open_reader()
        self.addCleanup(self._close_quietly, fd)
        os.write(self.master, b"line one\nline two\ninit complete\n")
        time.sleep(0.05)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = console_client.read_lines_fd(fd, time.time() + 1.0, "init complete")

        self.assertEqual(rc, 0, "the --until string was already in the buffer but was never reached")
        self.assertIn("init complete", out.getvalue())

    def test_a_trailing_partial_chunk_at_the_deadline_is_still_printed(self):
        fd = self._open_reader()
        self.addCleanup(self._close_quietly, fd)
        os.write(self.master, b"line one\nno newline yet")
        time.sleep(0.05)

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = console_client.read_lines_fd(fd, time.time() + 0.3, None)

        self.assertEqual(rc, 0)
        self.assertEqual(out.getvalue().splitlines(),
                          ["line one", "no newline yet"],
                          "the unterminated tail was discarded at the deadline")


if __name__ == "__main__":
    unittest.main()
