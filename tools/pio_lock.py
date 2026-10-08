#!/usr/bin/env python3
"""The machine-wide PlatformIO build lock, as a mechanism instead of a habit.

AGENTS.md: only one PlatformIO build may run on this machine at a time. Two
runs in one worktree corrupt SCons state and return a plausible wrong answer,
and a single core dir is not safe against concurrent package installs either.
That rule used to be enforced only by every agent remembering to type
`flock /tmp/protor2-pio.lock` in front of every command. Every `pio`
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
`flock /tmp/protor2-pio.lock ...` still serialises against both.

Nesting is the trap
-------------------
flock(2) locks belong to an *open file description*: a second open() of the
same path is a different description, so an inner acquire waits on the outer
one, and flock(2) does not detect the deadlock (flock(2) NOTES). The old
convention `flock /tmp/protor2-pio.lock make build` is exactly that shape,
so this module has to recognise a nest instead of queueing behind itself. Two
signals, checked in this order:

  1. PROTOR2_PIO_LOCK_HELD=1 — the caller states it already holds the lock.
     It is set for every child process while we hold it, and it is the
     documented escape hatch for a contiguous multi-command window:
     `PROTOR2_PIO_LOCK_HELD=1 flock /tmp/protor2-pio.lock <commands>`.
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

The penv is checked before the lock is taken
--------------------------------------------
pioarduino runs every build inside `<core dir>/penv`, a second PlatformIO
install that is not the one on PATH and that the system package hold does not
cover. On 2026-10-05 that penv upgraded itself to Core 6.2.0 and every build
failed with "No module named 'SCons.Tool.FortranCommon'" while `pio --version`
still printed 6.1.19. A worker lost its slice to the traceback. So the pio lock
reads the penv's own version before it builds, and refuses with the one-line
fix instead of handing the build a broken toolchain. A core dir with no penv
yet is not a fault: the first build creates it.

The lock file also says what happened to the shared framework pool
------------------------------------------------------------------
A build whose env declares custom_sdkconfig, in a worktree with no
sdkconfig.defaults stamp, makes pioarduino rebuild the framework libs under
the core dir - the libs every other worktree on that core links. Nothing used
to say so. The record now names the pool and its sdkconfig's mtime, says when
this build is expected to rebuild it, and the next holder reports when the
pool changed since the previous record was written.

Exit codes for the CLI form: the command's own, or 3 for a refused nest, 4 for
a lock-wait timeout, 5 for a penv on the wrong PlatformIO Core, 127 when the
command cannot be executed.

Environment:
  PROTOR2_PIO_LOCK       lock file path (default /tmp/protor2-pio.lock)
  PROTOR2_PIO_LOCK_HELD  "1" when the caller already holds the lock
  PROTOR2_PIO_LOCK_WAIT  seconds to wait for the lock (default 3600)
  PROTOR2_LOCK_OWNER     optional free-text context for the record
"""

from __future__ import annotations

import contextlib
import fcntl
import json
import os
import re
import shlex
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

DEFAULT_LOCK_PATH = "/tmp/protor2-pio.lock"
LOCK_PATH_ENV = "PROTOR2_PIO_LOCK"
HELD_ENV = "PROTOR2_PIO_LOCK_HELD"
WAIT_ENV = "PROTOR2_PIO_LOCK_WAIT"
OWNER_ENV = "PROTOR2_LOCK_OWNER"
GIT_TIMEOUT = 10
# Above the longest legitimate wait: a gate run holds the lock across a full
# native suite and a full firmware build, and several agents can be queued
# behind it. A genuine nest is caught by the fd scan long before this fires,
# so a generous ceiling costs nothing and a short one would fail real builds.
DEFAULT_WAIT_SECONDS = 3600.0
POLL_SECONDS = 0.25

EXIT_NESTED = 3
EXIT_TIMEOUT = 4
EXIT_PENV = 5
EXIT_CANNOT_EXEC = 127

# The PlatformIO Core this tree is verified with. .github/workflows/verification.yml
# pins the same version and says why: 6.2.0's tool-scons 4.11 fails every env at
# the link step. Bump both together, deliberately, with a green run.
PINNED_PIO_VERSION = (6, 1, 19)
# tool-scons 4.8.1, in either form a 6.1.19 penv spells it: PlatformIO's own
# "~4.40801.0", or the pioarduino URL ending in scons-local-4.8.1.tar.gz.
PINNED_SCONS_MARKERS = ("40801", "4.8.1")
TOOLS_DIR = Path(__file__).resolve().parent
BUDGETS = TOOLS_DIR / "build_budgets.json"
DEFAULT_CORE_DIR = "~/.platformio"


def note(message: str) -> None:
    print(f"[pio-lock] {message}", file=sys.stderr, flush=True)


def lock_path() -> Path:
    return Path(os.environ.get(LOCK_PATH_ENV) or DEFAULT_LOCK_PATH)


def held_env_for(path: Path) -> str:
    """The environment variable that says "an ancestor holds this lock".

    The pio lock keeps its documented name. Any other lock file taken through
    build_lock(lock_path=...) - the web-test lock in tools/slice_verify.py - gets
    a name of its own, so holding one lock never reads as holding the other.
    """
    if path == lock_path():
        return HELD_ENV
    stem = "".join(ch if ch.isalnum() else "_" for ch in path.name.upper())
    return f"PROTOR2_LOCK_HELD_{stem}"


# build_lock()'s `lock_path` parameter shadows lock_path() inside it.
_pio_lock_path = lock_path


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


def ownership_record(
    command: list[str] | None, extra: list[tuple[str, str]] | None = None
) -> str:
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
    fields.extend(extra or [])
    return "".join(f"{name}: {value}\n" for name, value in fields)


def write_record(
    path: Path, command: list[str] | None, extra: list[tuple[str, str]] | None = None
) -> None:
    """Stamp the lock file with who holds it, as soon as it is held.

    Never cleared on release: the stale record is the useful part, because it
    tells the next agent which chip target last touched the shared framework
    pools. Do not "tidy" this into a cleanup path.
    """
    payload = ownership_record(command, extra).encode()
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


def core_dir_for(target: str, env: dict[str, str] | None = None) -> Path:
    """The PlatformIO core dir a build of `target` runs in.

    `env` is the environment the build will actually get. The in-process
    callers (tools/slice_verify.py, tools/check_build_budgets.py) hand the core
    dir to the child that way, not through ours, so a different core exported
    in the caller's shell must not decide which penv is checked. With no `env`
    it is ours: the Makefile sets PLATFORMIO_CORE_DIR in front of every pio
    command, so for the CLI form that is the truth. Neither set: the platforms
    registry in build_budgets.json, where the Makefile gets it from.
    """
    exported = (os.environ if env is None else env).get("PLATFORMIO_CORE_DIR")
    if exported:
        return Path(os.path.expanduser(exported))
    core = DEFAULT_CORE_DIR
    try:
        platforms = json.loads(BUDGETS.read_text()).get("platforms", {})
    except (OSError, ValueError):
        platforms = {}
    for spec in platforms.values():
        if target in spec.get("envs", []):
            core = spec.get("core_dir", core)
            break
    return Path(os.path.expanduser(core))


def _penv_site_packages(penv: Path) -> list[Path]:
    """The penv's site-packages, by the Python version its pyvenv.cfg names.

    A penv that outlived a Python upgrade keeps the old lib/python3.N beside
    the live one, and the old one's PlatformIO is not what builds.
    """
    try:
        cfg = (penv / "pyvenv.cfg").read_text()
    except OSError:
        cfg = ""
    match = re.search(r"^version(?:_info)?\s*=\s*(\d+)\.(\d+)", cfg, re.M)
    if match:
        live = penv / "lib" / f"python{match.group(1)}.{match.group(2)}" / "site-packages"
        return [live] if live.is_dir() else []
    return sorted(penv.glob("lib/python3*/site-packages"))


def penv_version(core: Path) -> str:
    """The PlatformIO Core version the penv under `core` builds with, or "none"."""
    for site in _penv_site_packages(core / "penv"):
        try:
            text = (site / "platformio" / "__init__.py").read_text()
        except OSError:
            continue
        match = re.search(r"^VERSION\s*=\s*\(([^)]*)\)", text, re.M)
        if match:
            return ".".join(part.strip() for part in match.group(1).split(",") if part.strip())
    return "none"


def penv_problem(core: Path) -> str | None:
    """Why the penv under `core` would break a build, or None when it will not."""
    penv = core / "penv"
    for site in _penv_site_packages(penv):
        init = site / "platformio" / "__init__.py"
        try:
            text = init.read_text()
        except OSError:
            continue
        match = re.search(r"^VERSION\s*=\s*\(([^)]*)\)", text, re.M)
        if not match:
            return f"{init} has no VERSION tuple; cannot tell which PlatformIO Core builds here"
        try:
            version = tuple(int(part) for part in match.group(1).replace(" ", "").split(",") if part)
        except ValueError:
            return f"{init} has VERSION = ({match.group(1)}), not a version this check can read"
        want = ".".join(map(str, PINNED_PIO_VERSION))
        if version != PINNED_PIO_VERSION:
            return f"{penv} holds PlatformIO Core {'.'.join(map(str, version))}; this tree builds with {want}"
        try:
            deps = (site / "platformio" / "dependencies.py").read_text()
        except OSError:
            return None
        scons = re.search(r'"tool-scons"\s*:\s*(\(.*?\)|"[^"]*")', deps, re.S)
        if scons and not any(marker in scons.group(1) for marker in PINNED_SCONS_MARKERS):
            spec = " ".join(scons.group(1).split())
            return f"{penv} is Core {want} but pins tool-scons {spec}, not 4.8.1"
    return None


def refuse_bad_penv(core: Path, problem: str) -> None:
    note(f"refusing to build: {problem}.")
    note("The build runs inside that penv, not the `pio` on PATH, so `pio --version`")
    note("does not show this. Left alone, every env fails at the link step with")
    note("\"No module named 'SCons.Tool.FortranCommon'\". The fix:")
    pin = ".".join(map(str, PINNED_PIO_VERSION))
    # Through the penv's own uv, on the `pioarduino` dist. pioarduino creates
    # the penv with `uv venv`, which has no pip, and the P4 platform decides
    # whether to upgrade by reading that dist's version, so `pip install
    # platformio==` either fails or leaves the 6.2.0 dist listed. Both penvs
    # on the bench carry uv (2026-10-06). --reinstall-package rewrites the
    # files even when the dist already reads 6.1.19 but platformio/ moved
    # under it, which a plain install leaves alone (#473).
    penv = core / "penv" / "bin"
    note(f"  {penv / 'uv'} pip install --python {penv / 'python'} "
         f"--reinstall-package pioarduino pioarduino=={pin}")
    raise SystemExit(EXIT_PENV)


def check_penv(command: list[str] | None, env: dict[str, str] | None = None) -> None:
    core = core_dir_for(build_target(command), env)
    problem = penv_problem(core)
    if problem:
        refuse_bad_penv(core, problem)


def pool_fields(command: list[str] | None) -> list[tuple[str, str]]:
    """The shared framework pool this build links, for the record.

    Empty for a target that declares no custom_sdkconfig (native, a typo, a
    command with no -e): pioarduino only rebuilds the pool for one that does.
    Never raises - a record field is not worth failing a build over.
    """
    target = build_target(command)
    if target == "-":
        return []
    try:
        import check_framework_envelope as envelope

        if not envelope.declared_overrides(envelope.read_ini(), target):
            return []
        sdkconfig = envelope.resolved_sdkconfig_path(target)
    except Exception as err:  # noqa: BLE001 - see docstring; the note says what broke
        note(f"could not resolve the framework pool for {target}: {err}")
        return []
    try:
        mtime = f"{sdkconfig.stat().st_mtime:.6f}"
    except OSError:
        mtime = "absent"
    fields = [("pool", str(sdkconfig.parent)), ("pool_mtime", mtime)]
    worktree = Path(_git(["rev-parse", "--show-toplevel"]) or os.getcwd())
    if not (worktree / "sdkconfig.defaults").exists():
        message = (
            "expected - this worktree has no sdkconfig.defaults stamp, so pioarduino"
            " rebuilds the shared libs every other worktree on this core links"
        )
        note(f"this build will rebuild the shared framework pool {sdkconfig.parent}:")
        note(f"  {message}")
        fields.append(("pool_rebuild", message))
    return fields


def _record_fields(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in text.splitlines():
        name, sep, value = line.partition(": ")
        if sep:
            fields[name.strip()] = value.strip()
    return fields


def pool_change_since(previous: str) -> tuple[str, str] | None:
    """A field saying the previous holder's pool moved under it, or None.

    The CLI form execs the build, so nothing of ours runs after it to look.
    The next holder does: the previous record names the pool and its mtime,
    and a different mtime now means that build - or an unlocked one after
    it - rebuilt the libs every worktree on that core links.
    """
    fields = _record_fields(previous)
    pool, then = fields.get("pool"), fields.get("pool_mtime")
    if not pool or not then:
        return None
    try:
        now = f"{(Path(pool) / 'sdkconfig').stat().st_mtime:.6f}"
    except OSError:
        now = "absent"
    if now == then:
        return None
    who = f"{fields.get('worktree', '?')} building {fields.get('target', '?')}"
    note(f"the shared framework pool {pool} changed since the last holder took the")
    note(f"lock ({who}, {fields.get('acquired', '?')}): that build, or an unlocked")
    note("build after it, rebuilt it. Every worktree on that core now links its libs.")
    return ("previous_build_rebuilt_pool", f"{pool} ({who})")


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


def held_pool_fields(path: Path, command: list[str] | None) -> list[tuple[str, str]]:
    """Pool fields for a record about to replace the one at `path`.

    Only for the pio lock: the web-test lock never builds firmware.
    """
    fields = []
    changed = pool_change_since(read_record(path))
    if changed:
        fields.append(changed)
    return fields + pool_fields(command)


def acquire(
    path: Path,
    timeout: float,
    command: list[str] | None = None,
    pool: bool = False,
) -> int:
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
        write_record(path, command, held_pool_fields(path, command) if pool else None)
        return fd


@contextlib.contextmanager
def build_lock(
    command: list[str] | None = None,
    lock_path: Path | None = None,
    env: dict[str, str] | None = None,
):
    """Hold the machine-wide PlatformIO build lock for the duration of the block.

    `command` is what the caller is about to run; it is recorded in the lock
    file so a blocked agent can see what it is waiting on. A no-op when the
    caller already holds the lock (PROTOR2_PIO_LOCK_HELD), and a loud
    failure when it detects that an outer `flock(1)` holds it without having
    said so.

    `env` is the environment the command will run with, when it is not ours:
    the penv check reads the core dir from it.

    `lock_path` takes a different lock file with the same mechanism; the
    default is the pio lock. It is a separate lock, not a nested one: its held
    marker is held_env_for(lock_path), never PROTOR2_PIO_LOCK_HELD.
    """
    path = lock_path if lock_path is not None else _pio_lock_path()
    is_pio_lock = path == _pio_lock_path()
    if is_pio_lock:
        # Before the lock: a broken penv fails in a second either way, and
        # refusing here does not make the agents queued behind us wait for it.
        check_penv(command, env)
    held_env = held_env_for(path)
    if os.environ.get(held_env) == "1":
        # The outer holder is usually a hand-typed `flock(1)`, which cannot
        # write a record of its own; ours names the worktree and target that
        # are actually building inside its window.
        write_record(path, command, held_pool_fields(path, command) if is_pio_lock else None)
        yield
        return
    if inherited_lock_fd(path) is not None:
        refuse_nested(path)
    fd = acquire(path, wait_seconds(), command, pool=is_pio_lock)
    previous = os.environ.get(held_env)
    # Everything spawned under us is inside the lock; saying so keeps a nested
    # `make` or gate run from queueing behind the lock we are already holding.
    os.environ[held_env] = "1"
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop(held_env, None)
        else:
            os.environ[held_env] = previous
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
