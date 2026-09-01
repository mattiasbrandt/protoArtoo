#!/usr/bin/env python3
"""The machine-wide PlatformIO build lock, as a mechanism instead of a habit.

AGENTS.md: only one PlatformIO build may run on this machine at a time. Two
runs in one worktree corrupt SCons state and return a plausible wrong answer,
and a single core dir is not safe against concurrent package installs either.
That rule used to be enforced only by every agent remembering to type
`flock /tmp/protoartoo-pio.lock` in front of every command. Every `pio`
invocation in the Makefile and in tools/slice_verify.py goes through this
module instead, so the rule holds without anyone remembering it.

Two entry points, one mechanism:

  * CLI  — `python3 tools/pio_lock.py pio run -e protoArtoo` takes the lock and
           then *execs* the command, so the exec'd process holds the lock for
           its whole life and no wrapper lingers between make and pio.
  * API  — `with pio_lock.build_lock(): ...`, used by tools/slice_verify.py to
           hold the lock across its pio phases only, so the web suite and the
           mutation stage do not block other agents.

fcntl.flock() and flock(1) are both flock(2), so a hand-typed
`flock /tmp/protoartoo-pio.lock ...` still serialises against both.

Nesting is the trap
-------------------
flock(2) locks belong to an *open file description*: a second open() of the
same path is a different description, so an inner acquire waits on the outer
one, and flock(2) does not detect the deadlock (flock(2) NOTES). The old
convention `flock /tmp/protoartoo-pio.lock make build` is exactly that shape,
so this module has to recognise a nest instead of queueing behind itself. Two
signals, checked in this order:

  1. PROTOARTOO_PIO_LOCK_HELD=1 — the caller states it already holds the lock.
     It is set for every child process while we hold it, and it is the
     documented escape hatch for a contiguous multi-command window:
     `PROTOARTOO_PIO_LOCK_HELD=1 flock /tmp/protoartoo-pio.lock <commands>`.
  2. An inherited open fd on the lock file. flock(1) does not close its fd
     before exec'ing the command — that is what its -o flag is for — so a
     process started under `flock <lockfile> <command>` inherits an fd
     pointing at it. Finding one means an ancestor holds the lock and did not
     say so, which is the old convention typed verbatim; fail immediately with
     the fix rather than block until the timeout expires.

Signal 2 only ever produces a loud failure, never a decision to skip locking,
so a platform without /proc just falls back to waiting — the behaviour before
this module existed.

The lock file says who holds it
-------------------------------
flock(1) opens its lock file read-only and never truncates it, so the file
body is free for an ownership record: the holder writes one immediately after
acquiring, and a waiter reads it without taking the lock. Every field is
derived here rather than passed in — a field that depends on an agent
remembering to set it is wrong exactly when it matters. The record is what a
waiter is shown when it gives up, so "I am blocked" becomes "I am blocked by
this worktree building this target".

Exit codes for the CLI form: the command's own, or 3 for a refused nest, 4 for
a lock-wait timeout, 127 when the command cannot be executed.

Environment:
  PROTOARTOO_PIO_LOCK       lock file path (default /tmp/protoartoo-pio.lock)
  PROTOARTOO_PIO_LOCK_HELD  "1" when the caller already holds the lock
  PROTOARTOO_PIO_LOCK_WAIT  seconds to wait for the lock (default 3600)
  PROTOARTOO_LOCK_OWNER     optional free-text context for the record
"""

from __future__ import annotations

import contextlib
import fcntl
import os
import shlex
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

DEFAULT_LOCK_PATH = "/tmp/protoartoo-pio.lock"
LOCK_PATH_ENV = "PROTOARTOO_PIO_LOCK"
HELD_ENV = "PROTOARTOO_PIO_LOCK_HELD"
WAIT_ENV = "PROTOARTOO_PIO_LOCK_WAIT"
OWNER_ENV = "PROTOARTOO_LOCK_OWNER"
GIT_TIMEOUT = 10
# Above the longest legitimate wait: a gate run holds the lock across a full
# native suite and a full firmware build, and several agents can be queued
# behind it. A genuine nest is caught by the fd scan long before this fires,
# so a generous ceiling costs nothing and a short one would fail real builds.
DEFAULT_WAIT_SECONDS = 3600.0
POLL_SECONDS = 0.25

EXIT_NESTED = 3
EXIT_TIMEOUT = 4
EXIT_CANNOT_EXEC = 127


def note(message: str) -> None:
    print(f"[pio-lock] {message}", file=sys.stderr, flush=True)


def lock_path() -> Path:
    return Path(os.environ.get(LOCK_PATH_ENV) or DEFAULT_LOCK_PATH)


def wait_seconds() -> float:
    raw = os.environ.get(WAIT_ENV)
    if not raw:
        return DEFAULT_WAIT_SECONDS
    try:
        value = float(raw)
    except ValueError:
        raise SystemExit(f"[pio-lock] {WAIT_ENV}={raw!r} is not a number")
    if value < 0:
        raise SystemExit(f"[pio-lock] {WAIT_ENV}={raw!r} must not be negative")
    return value


def _git(args: list[str]) -> str:
    """A git value for the record, or "" when there is none to be had.

    The lock is not a git operation: a cwd outside a repository, or a git that
    fails for any other reason, must degrade the record rather than the build.
    """
    try:
        proc = subprocess.run(
            ["git", *args], capture_output=True, text=True, timeout=GIT_TIMEOUT
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    return proc.stdout.strip() if proc.returncode == 0 else ""


def build_target(command: list[str] | None) -> str:
    """The PlatformIO env the command is about to build.

    The most useful field in the record: it names which shared framework pool
    the holder is touching, which is the context the pristine-libs fault needs.
    """
    if command:
        for index, arg in enumerate(command):
            if arg in ("-e", "--environment") and index + 1 < len(command):
                return command[index + 1]
            if arg.startswith("--environment="):
                return arg.split("=", 1)[1]
    # `pio run -t clean` names no env; BUILD_ENV only shows up here when the
    # caller exported it, and "-" is honest about the rest.
    return os.environ.get("BUILD_ENV") or "-"


def ownership_record(command: list[str] | None) -> str:
    """The holder's identity, derived — never passed in, never remembered."""
    fields = [
        ("pid", str(os.getpid())),
        ("acquired", datetime.now().astimezone().isoformat(timespec="seconds")),
        ("worktree", _git(["rev-parse", "--show-toplevel"]) or os.getcwd()),
        ("branch", _git(["rev-parse", "--abbrev-ref", "HEAD"]) or "-"),
        ("target", build_target(command)),
        ("command", shlex.join(command) if command else "-"),
    ]
    owner = os.environ.get(OWNER_ENV)
    if owner:
        fields.append(("owner", owner))
    return "".join(f"{name}: {value}\n" for name, value in fields)


def write_record(path: Path, command: list[str] | None) -> None:
    """Stamp the lock file with who holds it, as soon as it is held.

    Never cleared on release: the stale record is the useful part, because it
    tells the next agent which chip target last touched the shared framework
    pools. Do not "tidy" this into a cleanup path.
    """
    payload = ownership_record(command).encode()
    try:
        # Write first and trim afterwards rather than opening with O_TRUNC:
        # a waiter reads this file without any lock, and truncate-then-write
        # would give it a window in which the record is empty.
        fd = os.open(path, os.O_WRONLY | os.O_CREAT, 0o666)
        try:
            os.write(fd, payload)
            os.ftruncate(fd, len(payload))
        finally:
            os.close(fd)
    except OSError as err:
        # A record we could not write is a worse day for the next agent, not a
        # reason to fail a build that already holds the lock.
        note(f"could not record ownership in {path}: {err}")


def read_record(path: Path) -> str:
    """The record as last written, read without taking the lock."""
    try:
        text = path.read_text()
    except OSError as err:
        return f"(unreadable: {err})"
    return text.strip() or "(no owner recorded)"


def report_record(path: Path) -> None:
    note("lock record — the last holder, whose pid may already be gone:")
    for line in read_record(path).splitlines():
        note(f"  {line}")


def inherited_lock_fd(path: Path) -> int | None:
    """The fd number of an already-open descriptor on the lock file, if any.

    An fd we did not open ourselves means an ancestor process opened it —
    in practice `flock(1)`, which leaves the descriptor open across exec.
    Linux-only (/proc); everywhere else this returns None and the caller
    falls back to waiting.
    """
    target = str(path.resolve())
    try:
        entries = list(Path("/proc/self/fd").iterdir())
    except OSError:
        return None
    for entry in entries:
        try:
            resolved = os.readlink(entry)
        except OSError:
            # The fd was closed between listing the directory and reading the
            # link. It is gone, so it is not an ancestor's lock; keep looking.
            continue
        if resolved == target:
            return int(entry.name)
    return None


def refuse_nested(path: Path) -> None:
    note(f"refusing to nest the build lock on {path}.")
    note("This process inherited an open fd on the lock file, so an outer")
    note("`flock` is already wrapping it and an inner acquire would block")
    note("forever (flock(2) locks are per open file description).")
    note("The Makefile and tools/slice_verify.py take this lock themselves")
    note("now, so the fix is to drop the outer `flock` and run the command")
    note("plainly. To keep one contiguous window across several commands:")
    note(f"  {HELD_ENV}=1 flock {path} <commands>")
    raise SystemExit(EXIT_NESTED)


def acquire(path: Path, timeout: float, command: list[str] | None = None) -> int:
    """Take the lock, waiting up to `timeout` seconds; return the held fd.

    A poll loop rather than a blocking flock(2) so the wait can be both
    bounded and announced without installing a signal handler — a build that
    is queued behind another agent should say so, and say who it is waiting
    on, instead of looking hung.
    """
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o666)
    # The CLI form execs the command while holding this fd, and Python marks
    # its own descriptors close-on-exec (PEP 446); without this the lock would
    # be dropped at the moment the build starts.
    os.set_inheritable(fd, True)
    deadline = time.monotonic() + timeout
    announced = False
    while True:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            if not announced:
                announced = True
                note(
                    f"another build holds {path};"
                    f" waiting up to {timeout:.0f}s for it"
                )
                report_record(path)
            if time.monotonic() >= deadline:
                os.close(fd)
                note(f"gave up waiting for {path} after {timeout:.0f}s.")
                note("Another PlatformIO build has held it that long, or an")
                note(f"outer `flock` is nesting it — see {WAIT_ENV} to wait")
                note("longer.")
                report_record(path)
                raise SystemExit(EXIT_TIMEOUT)
            time.sleep(POLL_SECONDS)
            continue
        # Immediately after acquiring, so the window in which the file is
        # blank or still names the previous holder is as small as it can be.
        write_record(path, command)
        return fd


@contextlib.contextmanager
def build_lock(command: list[str] | None = None):
    """Hold the machine-wide PlatformIO build lock for the duration of the block.

    `command` is what the caller is about to run; it is recorded in the lock
    file so a blocked agent can see what it is waiting on. A no-op when the
    caller already holds the lock (PROTOARTOO_PIO_LOCK_HELD), and a loud
    failure when it detects that an outer `flock(1)` holds it without having
    said so.
    """
    path = lock_path()
    if os.environ.get(HELD_ENV) == "1":
        # The outer holder is usually a hand-typed `flock(1)`, which cannot
        # write a record of its own; ours names the worktree and target that
        # are actually building inside its window.
        write_record(path, command)
        yield
        return
    if inherited_lock_fd(path) is not None:
        refuse_nested(path)
    fd = acquire(path, wait_seconds(), command)
    previous = os.environ.get(HELD_ENV)
    # Everything spawned under us is inside the lock; saying so keeps a nested
    # `make` or gate run from queueing behind the lock we are already holding.
    os.environ[HELD_ENV] = "1"
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop(HELD_ENV, None)
        else:
            os.environ[HELD_ENV] = previous
        os.close(fd)  # closing the last descriptor releases the flock(2) lock


def main(argv: list[str]) -> int:
    if not argv:
        note("usage: pio_lock.py <command> [args...]")
        return 2
    with build_lock(argv):
        try:
            os.execvp(argv[0], argv)
        except OSError as err:
            note(f"cannot execute {argv[0]}: {err}")
            return EXIT_CANNOT_EXEC
    # Unreachable on success: execvp replaces this process image, which is
    # what keeps the lock held for exactly as long as the command runs.
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
