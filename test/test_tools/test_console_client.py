"""Host-provable coverage for tools/console_client.py's --interactive mode (#228).

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
import importlib.util
import os
import pty
import select
import signal
import termios
import threading
import time
import unittest
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


if __name__ == "__main__":
    unittest.main()
