#!/usr/bin/env python3
"""The automated half of a bench session, with a memory log the whole way through.

Phase 1 of a bench session (.claude/skills/bench-verification/SKILL.md) is the
agents' half: the Console sheet's agent-runnable rows, the console sweep, and
every per-surface Playwright script. This runs all of it, in that order, and
beside it polls GET /api/status once a second, so that at the end it can say
which step did what to the controller's heap.

Why the memory log is not optional (operator, 2026-09-29, #435): the round
that morning read green - the sheet exited 0, the sweep found 0 errors on 16
pages - while the controller had failed 11 allocations and its heap low-water
mark had fallen to 280 B. No 1 Hz sample saw either dip; only the firmware's own
cumulative counters did. So the verdict here comes from how far each counter
moved INSIDE each step, and the sampled series is context only.

What it runs, in order:
  idle          --idle-s seconds with nothing running: the baseline row.
  the sheet     tools/console_client.py --script <sheet> --skip-manual.
  the sweep     test/playwright/console-sweep.js against the droid.
  droid scripts every test/playwright/<surface>/<rule>.js whose `// bench-auto:`
                line says `droid`, ordered by the estop state it declares
                (none, then clear, then latched); `parts=1,2` runs it once per
                PART.
  fixture       every script whose line says `fixture <page>.html`, on a fixture
                server this runner starts on a port of its own, stopped by PID
                afterwards. The droid sits idle meanwhile, and the log keeps
                polling it.

A script's target is what the script declares, never a guess from the URLs in
its source: guessing by grepping for 127.0.0.1:4173 got it wrong both ways on
2026-09-29 (test/playwright/README.md, "`// bench-auto:` target line").

Playwright stays HEADED (the operator watches; SKILL.md section 5) and nothing
here waits on a key: every child gets stdin /dev/null and a timeout, and STEP /
SELFTEST* are stripped from the environment it inherits.

The estop. Three droid scripts need it clear and then latch it, and two need it
latched. Order alone cannot serve all of them, so before each droid script the
runner reads the estop and, if it is not what the script declares, puts it
there: it latches one through POST /api/estop, and it clears one through
POST /api/estop/clear ONLY when this run's own steps latched it. A latch that
was already standing when the run began is the operator's, and a run that
would need it cleared is refused before it starts instead.

Status fields are read through tools/soak.py's schemas and the compiled
admission floor through its resolve_admission_floor(), never restated here.

Exit codes:
  0  every step passed (NOT ASSESSED beside them allowed) and no memory flag
  1  a step failed, timed out or could not start, or a memory flag was raised
  2  the run could not start: a preflight refusal, said why
  130 interrupted; the report covers what ran

Operator documentation: the bench-verification skill, section 5, and
docs/console-client.md. `make bench-auto` is the usual entry point.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime
import http.client
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Optional
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))

import soak  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
PLAYWRIGHT_DIR = REPO_ROOT / "test" / "playwright"
SWEEP_SCRIPT = PLAYWRIGHT_DIR / "console-sweep.js"
CONSOLE_CLIENT = REPO_ROOT / "tools" / "console_client.py"
FIXTURE_SERVER = REPO_ROOT / "tools" / "serve_editor_fixture.py"

# The Playwright module the README runs every script with. A NODE_PATH in the
# environment wins; this is only the fallback, and it is checked, not assumed.
DEFAULT_NODE_PATH = Path.home() / ".npm" / "_npx" / "e41f203b7505f1fb" / "node_modules"

PROFILER_PATH = "/api/profiler"
ESTOP_PATH = "/api/estop"
ESTOP_CLEAR_PATH = "/api/estop/clear"

EXIT_OK = 0
EXIT_FINDINGS = 1
EXIT_REFUSED = 2
EXIT_INTERRUPTED = 130

# test/playwright/README.md: 0 every row PASS, 1 a FAIL or could not finish,
# 2 the precondition did not hold.
SCRIPT_EXIT_NOT_ASSESSED = 2

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"
RESULT_NOT_ASSESSED = "NOT ASSESSED"
RESULT_TIMEOUT = "TIMEOUT"
RESULT_NOT_RUN = "NOT RUN"

# Environment a child must not inherit from the operator's shell: each one
# either waits on a key (STEP), breaks a check on purpose (SELFTEST*,
# FORCE_FAIL), or points a script somewhere this runner did not choose.
STRIPPED_ENV = ("STEP", "FORCE_FAIL", "HEADLESS", "HEADED", "FIXTURE", "TARGET_URL",
                "BASE", "BASE_URL", "PART", "INDUCED", "PA_FIXTURE_PORT")
STRIPPED_ENV_PREFIXES = ("SELFTEST",)


# ---------------------------------------------------------------------------
# What each script declares
# ---------------------------------------------------------------------------

DECLARATION_RE = re.compile(r"^// bench-auto: (.*)$", re.M)
PAGE_RE = re.compile(r"^[a-z_]+\.html$")


class DeclarationError(Exception):
    pass


@dataclasses.dataclass(frozen=True)
class Declaration:
    script: str               # repo-relative path
    target: str               # "droid" or "fixture"
    page: Optional[str] = None
    estop: Optional[str] = None   # None, "clear" or "latched"
    parts: tuple[str, ...] = ()


def parse_declaration(script: str, text: str) -> Declaration:
    """The one `// bench-auto:` line in `text`, read strictly: an unknown word
    is refused rather than ignored, since an ignored `estop=latchd` would run a
    script in the wrong state and read as the script's own failure."""
    lines = DECLARATION_RE.findall(text)
    if not lines:
        raise DeclarationError(f"{script}: no `// bench-auto:` line")
    if len(lines) > 1:
        raise DeclarationError(f"{script}: {len(lines)} `// bench-auto:` lines, one is allowed")
    words = lines[0].split()
    if not words:
        raise DeclarationError(f"{script}: `// bench-auto:` names no target")
    target, rest = words[0], words[1:]
    if target == "fixture":
        if len(rest) != 1 or not PAGE_RE.match(rest[0]):
            raise DeclarationError(f"{script}: `fixture` takes exactly one page, e.g. `fixture seq.html`")
        return Declaration(script, "fixture", page=rest[0])
    if target != "droid":
        raise DeclarationError(f"{script}: unknown target {target!r} (droid or fixture)")
    estop: Optional[str] = None
    parts: tuple[str, ...] = ()
    for word in rest:
        key, _, value = word.partition("=")
        if key == "estop" and value in ("clear", "latched") and estop is None:
            estop = value
        elif key == "parts" and value and not parts:
            parts = tuple(value.split(","))
            if not all(parts):
                raise DeclarationError(f"{script}: empty entry in {word!r}")
        else:
            raise DeclarationError(f"{script}: cannot read {word!r} (estop=clear|latched, parts=1,2)")
    return Declaration(script, "droid", estop=estop, parts=parts)


def discover_declarations(playwright_dir: Path = PLAYWRIGHT_DIR) -> list[Declaration]:
    """Every per-surface script's declaration, in path order. Every problem in
    the tree is collected and raised together, so one run names them all."""
    declarations: list[Declaration] = []
    problems: list[str] = []
    for path in sorted(playwright_dir.glob("*/*.js")):
        if path.parent.name.startswith("_"):
            continue
        script = path.relative_to(REPO_ROOT).as_posix() if path.is_relative_to(REPO_ROOT) else path.as_posix()
        try:
            declarations.append(parse_declaration(script, path.read_text(encoding="utf-8")))
        except DeclarationError as problem:
            problems.append(str(problem))
    if problems:
        raise DeclarationError("\n".join(problems))
    return declarations


@dataclasses.dataclass(frozen=True)
class ScriptStep:
    name: str
    script: str
    env: tuple[tuple[str, str], ...] = ()
    estop: Optional[str] = None


ESTOP_ORDER = {None: 0, "clear": 1, "latched": 2}


def droid_steps(declarations: list[Declaration]) -> list[ScriptStep]:
    """The droid scripts in run order: no estop need, then clear, then
    latched; path order inside each group. A script that latches the estop is
    followed by the runner's clear only when a later step needs it clear
    (see ensure_estop()), so this order keeps those clears to a minimum and
    puts every latched-estop script after the ones that latch it."""
    droid = sorted((d for d in declarations if d.target == "droid"),
                   key=lambda d: (ESTOP_ORDER[d.estop], d.script))
    steps: list[ScriptStep] = []
    for d in droid:
        short = d.script.removeprefix("test/playwright/")
        if d.parts:
            steps.extend(ScriptStep(f"{short} PART={part}", d.script, (("PART", part),), d.estop)
                         for part in d.parts)
        else:
            steps.append(ScriptStep(short, d.script, (), d.estop))
    return steps


def fixture_declarations(declarations: list[Declaration]) -> list[Declaration]:
    return [d for d in declarations if d.target == "fixture"]


# ---------------------------------------------------------------------------
# The memory log
# ---------------------------------------------------------------------------

def wall_clock() -> str:
    return datetime.datetime.now().astimezone().isoformat(timespec="milliseconds")


def get_json(client: soak.BenchClient, path: str) -> tuple[int, Optional[dict], str]:
    """(status, JSON object or None, first 200 characters of the body).

    soak.BenchClient.get_json() parses before it returns, so a 404 or 503
    answered with a plain-text body would surface as a JSON error and the
    status code - the part that matters here - would be lost. Transport
    faults still raise, exactly as BenchClient's do."""
    connection = http.client.HTTPConnection(client.device, client.port, timeout=client.connect_timeout_s)
    try:
        connection.request("GET", path)
        response = connection.getresponse()
        raw = response.read()
    finally:
        connection.close()
    text = raw.decode("utf-8", errors="replace")
    try:
        parsed = json.loads(text) if text else None
    except json.JSONDecodeError:
        parsed = None
    return response.status, (parsed if isinstance(parsed, dict) else None), text[:200]


def client_for(base_url: str, timeout_s: float) -> soak.BenchClient:
    parts = urlsplit(base_url)
    if parts.scheme != "http" or not parts.hostname:
        raise ValueError(f"{base_url!r} is not an http://host[:port] base URL")
    return soak.BenchClient(parts.hostname, parts.port or 80, connect_timeout_s=timeout_s)


class MemoryLog:
    """Polls /api/status on its own thread and writes every sample, stamped
    with the wall clock and the step running at the time, to samples.jsonl.

    A step is closed by one more poll taken on the caller's thread the moment
    the step ends (a boundary sample, tagged with the step that just ended), so
    a counter that moved in the last second of a step is attributed to that
    step and not to the next one. A failed step changes nothing here: the
    poller does not know or care how a step ended.

    At each boundary it also asks GET /api/profiler for the last failed
    allocation, on an image that serves it. The first 404 is recorded once, as
    "this image has no profiler", and the route is not asked again.
    """

    def __init__(self, client: soak.BenchClient, schema: soak.StatusSchema,
                 run_dir: Path, interval_s: float) -> None:
        self.client = client
        self.schema = schema
        self.interval_s = interval_s
        self.samples: list[dict] = []
        self.profiler: list[dict] = []
        self.profiler_absent_note: Optional[str] = None
        self.last_body: Optional[dict] = None
        self._samples_file = (run_dir / "samples.jsonl").open("a", encoding="utf-8")
        self._profiler_file = (run_dir / "profiler.jsonl").open("a", encoding="utf-8")
        self._lock = threading.Lock()
        self._step = ""
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="memory-log", daemon=True)

    def start(self, first_step: str) -> None:
        self._step = first_step
        self._poll("poll")
        self._thread.start()

    def _run(self) -> None:
        while not self._stop.wait(self.interval_s):
            self._poll("poll")

    def _poll(self, kind: str, step: Optional[str] = None) -> dict:
        with self._lock:
            sample: dict = {"t": wall_clock(), "step": step or self._step, "kind": kind}
            try:
                status, body, text = get_json(self.client, soak.DEFAULT_STATUS_PATH)
            except soak.TRANSPORT_EXCEPTIONS as fault:
                sample.update(status="unanswered", error=f"{type(fault).__name__}: {fault}")
            else:
                if status != 200 or body is None:
                    sample.update(status="unanswered",
                                  error=f"GET /api/status answered {status}: {text[:80]!r}")
                else:
                    anomalies: list[str] = []
                    sample["status"] = "ok"
                    sample["reading"] = self.schema.memory_reading(body, anomalies)
                    estop = body.get("estop")
                    sample["estop"] = estop if isinstance(estop, bool) else None
                    reason = body.get("resetReason")
                    sample["resetReason"] = reason if isinstance(reason, str) else None
                    if anomalies:
                        sample["anomalies"] = anomalies
                    self.last_body = body
            self.samples.append(sample)
            self._samples_file.write(json.dumps(sample) + "\n")
            self._samples_file.flush()
            return sample

    def close_step(self, ended: str, following: str) -> None:
        """Take the boundary sample for `ended`, read the profiler, and tag
        every later sample with `following`."""
        self._poll("boundary", step=ended)
        self._read_profiler(ended)
        with self._lock:
            self._step = following

    def _read_profiler(self, step: str) -> None:
        if self.profiler_absent_note is not None:
            return
        entry: dict = {"t": wall_clock(), "step": step}
        try:
            status, body, text = get_json(self.client, PROFILER_PATH)
        except soak.TRANSPORT_EXCEPTIONS as fault:
            entry["error"] = f"{type(fault).__name__}: {fault}"
        else:
            if status == 404:
                self.profiler_absent_note = (
                    f"this image has no profiler: GET {PROFILER_PATH} answered 404 at {entry['t']} "
                    "(only the _profiler builds serve it)")
                entry["note"] = self.profiler_absent_note
            elif status != 200 or body is None:
                entry["error"] = f"GET {PROFILER_PATH} answered {status}: {text[:80]!r}"
            else:
                # src/web/api_profiler.cpp:518-529: lastFail is emitted only
                # once failedAllocs > 0, so its absence is "none yet".
                entry["failedAllocs"] = body.get("failedAllocs")
                if isinstance(body.get("lastFail"), dict):
                    entry["lastFail"] = body["lastFail"]
        self.profiler.append(entry)
        self._profiler_file.write(json.dumps(entry) + "\n")
        self._profiler_file.flush()

    def stop(self) -> None:
        self._stop.set()
        if self._thread.is_alive():
            self._thread.join(timeout=self.client.connect_timeout_s + self.interval_s + 5)
        self._samples_file.close()
        self._profiler_file.close()


# ---------------------------------------------------------------------------
# The verdict: what each step did to the counters
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class StepMemory:
    step: str
    polls: int = 0
    unanswered: int = 0
    min_heap_free: Optional[int] = None
    min_heap_largest_block: Optional[int] = None
    min_buffer_reading: Optional[int] = None
    heap_min_end: Optional[int] = None
    failed_allocs_end: Optional[int] = None
    advanced: dict = dataclasses.field(default_factory=dict)
    resets: list = dataclasses.field(default_factory=list)
    new_heap_min_low: Optional[int] = None


@dataclasses.dataclass(frozen=True)
class Flag:
    step: str
    kind: str
    message: str


def _minimum(current: Optional[int], value: Optional[int]) -> Optional[int]:
    if value is None:
        return current
    return value if current is None else min(current, value)


def summarize_memory(samples: list[dict], steps: list[str], schema: soak.StatusSchema,
                     floor: soak.AdmissionFloor) -> tuple[list[StepMemory], list[Flag]]:
    """One StepMemory per step, and the flags, from the samples in the order
    they were taken.

    The counters (failedAllocs, refusedHeapFloor, refusedHeapFloorDiag) and
    the heapMin low-water mark are cumulative within a boot, so a step is
    charged with how far they moved between the last answered sample before
    it and its own last one. Across a reset (restart_field going backwards)
    the counters start again from zero, so the value after the reset is what
    the new boot has counted and is added whole.

    heapMin's new lows are reported and never judged: there is no threshold
    for them anywhere in the firmware, and none is invented here. The one
    number judged is the lowest Buffer Reading (schema.heap_field), against
    the ordinary admission floor this build refuses page loads at - the same
    rule as tools/soak.py's heap verdict.
    """
    counters = [field for field in (schema.failed_allocs_field, schema.refused_heap_floor_field,
                                    schema.refused_heap_floor_diag_field) if field]
    restart = schema.restart_field
    rows = {name: StepMemory(name) for name in steps}
    flags: list[Flag] = []
    previous: Optional[dict] = None
    boot_low: Optional[int] = None

    for sample in samples:
        row = rows.get(sample["step"])
        if row is None:
            continue
        row.polls += 1
        if sample.get("status") != "ok":
            row.unanswered += 1
            continue
        reading = sample["reading"]
        reset = (previous is not None and restart in previous and restart in reading
                 and reading[restart] < previous[restart])
        if reset:
            row.resets.append({"from": previous[restart], "to": reading[restart],
                               "resetReason": sample.get("resetReason")})
            boot_low = None
        if previous is not None:
            for field in counters:
                if field in reading and field in previous:
                    moved = reading[field] if reset else max(0, reading[field] - previous[field])
                    if moved:
                        row.advanced[field] = row.advanced.get(field, 0) + moved
        heap_min = reading.get(schema.heap_min_field) if schema.heap_min_field else None
        if heap_min is not None:
            if boot_low is None:
                boot_low = heap_min
            elif heap_min < boot_low:
                boot_low = heap_min
                row.new_heap_min_low = heap_min
            row.heap_min_end = heap_min
        row.min_heap_free = _minimum(row.min_heap_free, reading.get(schema.heap_free_field))
        if schema.heap_largest_block_field:
            row.min_heap_largest_block = _minimum(row.min_heap_largest_block,
                                                  reading.get(schema.heap_largest_block_field))
        row.min_buffer_reading = _minimum(row.min_buffer_reading, reading.get(schema.heap_field))
        if schema.failed_allocs_field and schema.failed_allocs_field in reading:
            row.failed_allocs_end = reading[schema.failed_allocs_field]
        previous = reading

    labels = {
        schema.failed_allocs_field: "failed allocations",
        schema.refused_heap_floor_field: "ordinary requests refused at the heap floor",
        schema.refused_heap_floor_diag_field: "read-only diagnostics refused at the heap floor",
    }
    for name in steps:
        row = rows[name]
        for field in counters:
            if row.advanced.get(field):
                flags.append(Flag(name, field, f"{field} advanced by {row.advanced[field]}: "
                                               f"{row.advanced[field]} {labels[field]} during this step"))
        for reset in row.resets:
            flags.append(Flag(name, "reset", f"{restart} went backwards ({reset['from']} -> "
                                              f"{reset['to']}): the controller restarted, reset reason "
                                              f"{reset['resetReason'] or 'UNKNOWN'}"))
        if row.unanswered:
            flags.append(Flag(name, "unanswered", f"{row.unanswered} of {row.polls} polls of "
                                                   f"{soak.DEFAULT_STATUS_PATH} went unanswered"))
        if row.min_buffer_reading is not None and row.min_buffer_reading < floor.ordinary_bytes:
            flags.append(Flag(name, "floor", (
                f"{schema.heap_field} fell to {row.min_buffer_reading}, below the "
                f"{floor.ordinary_bytes}-byte admission floor this image refuses ordinary requests "
                f"at ({soak.ADMISSION_FLOOR_MACRO}, build environment {floor.env!r}, "
                f"{floor.sources[soak.ADMISSION_FLOOR_MACRO]}): an operator's page load would "
                "have been shed")))
    return [rows[name] for name in steps], flags


# ---------------------------------------------------------------------------
# Running a step
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class StepResult:
    name: str
    section: str
    result: str = RESULT_NOT_RUN
    exit_code: Optional[int] = None
    seconds: float = 0.0
    log: str = ""
    note: str = ""


def child_env(extra: dict[str, str], node_path: Optional[str]) -> dict[str, str]:
    env = {key: value for key, value in os.environ.items()
           if key not in STRIPPED_ENV and not key.startswith(STRIPPED_ENV_PREFIXES)}
    if node_path:
        env["NODE_PATH"] = node_path
    env.update(extra)
    return env


def log_name(step: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.=-]+", "_", step).strip("_") + ".log"


def run_process(cmd: list[str], env: dict[str, str], timeout_s: float,
                log_path: Path) -> tuple[Optional[int], bool]:
    """Run one child to completion or timeout, its output copied line by line
    to this terminal and to log_path. (exit code, timed out). The child gets
    its own process group, so a timeout takes a headed browser down with the
    script that opened it."""
    with log_path.open("wb") as log:
        log.write(f"$ {' '.join(cmd)}\n".encode())
        log.flush()
        child = subprocess.Popen(cmd, cwd=REPO_ROOT, env=env, stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 start_new_session=True)

        def copy() -> None:
            for line in iter(child.stdout.readline, b""):
                log.write(line)
                log.flush()
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()

        copier = threading.Thread(target=copy, daemon=True)
        copier.start()
        timed_out = False
        try:
            child.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            timed_out = True
            stop_group(child)
        except BaseException:
            stop_group(child)
            raise
        finally:
            copier.join(timeout=10)
        if timed_out:
            log.write(f"\n[bench_auto] timed out after {timeout_s:.0f}s; process group stopped\n".encode())
        return (None if timed_out else child.returncode), timed_out


def stop_group(child: subprocess.Popen) -> None:
    for sig, wait_s in ((signal.SIGTERM, 5), (signal.SIGKILL, 5)):
        try:
            os.killpg(child.pid, sig)
        except ProcessLookupError:
            return
        try:
            child.wait(timeout=wait_s)
            return
        except subprocess.TimeoutExpired:
            continue


def classify(exit_code: Optional[int], timed_out: bool, playwright: bool) -> str:
    if timed_out:
        return RESULT_TIMEOUT
    if exit_code == 0:
        return RESULT_PASS
    if playwright and exit_code == SCRIPT_EXIT_NOT_ASSESSED:
        return RESULT_NOT_ASSESSED
    return RESULT_FAIL


class FixtureServer:
    """tools/serve_editor_fixture.py on a port of this run's own, so another
    worktree's server on 4173 can never answer for this tree's data/."""

    def __init__(self, run_dir: Path) -> None:
        self.run_dir = run_dir
        self.process: Optional[subprocess.Popen] = None
        self.port = 0
        self._log = None

    @property
    def base(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def start(self) -> None:
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        self.port = probe.getsockname()[1]
        probe.close()
        self._log = (self.run_dir / "fixture-server.log").open("wb")
        env = child_env({"PA_FIXTURE_PORT": str(self.port)}, None)
        self.process = subprocess.Popen([sys.executable, str(FIXTURE_SERVER)], cwd=REPO_ROOT,
                                        env=env, stdin=subprocess.DEVNULL, stdout=self._log,
                                        stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"the fixture server exited {self.process.returncode} "
                                   f"(see {self.run_dir / 'fixture-server.log'})")
            try:
                connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2)
                connection.request("GET", "/index.html")
                answered = connection.getresponse().status == 200
                connection.close()
                if answered:
                    print(f"[bench_auto] fixture server pid {self.process.pid} on {self.base}", flush=True)
                    return
            except OSError:
                pass
            time.sleep(0.2)
        self.stop()
        raise RuntimeError(f"the fixture server did not answer on {self.base} within 15 s")

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            os.kill(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.kill(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
            print(f"[bench_auto] fixture server pid {self.process.pid} stopped", flush=True)
        if self._log is not None:
            self._log.close()
            self._log = None


# ---------------------------------------------------------------------------
# The estop
# ---------------------------------------------------------------------------

def read_estop(client: soak.BenchClient) -> tuple[Optional[bool], str]:
    try:
        status, body, _ = get_json(client, soak.DEFAULT_STATUS_PATH)
    except soak.TRANSPORT_EXCEPTIONS as fault:
        return None, f"{type(fault).__name__}: {fault}"
    estop = body.get("estop") if body is not None else None
    if status != 200 or not isinstance(estop, bool):
        return None, f"GET /api/status answered {status} without a boolean estop"
    return estop, ""


def ensure_estop(client: soak.BenchClient, need: Optional[str], step: str,
                 may_clear: bool) -> Optional[str]:
    """Put the estop where `step` declares it must be. Returns what was done,
    in words, or None when nothing was needed. Never raises: a droid that
    cannot be read or moved here is reported, and the script's own
    precondition then says NOT ASSESSED for itself."""
    if need is None:
        return None
    latched, why = read_estop(client)
    if latched is None:
        return f"before {step}: the estop could not be read ({why}); left as it was"
    if need == "latched" and not latched:
        path = ESTOP_PATH
    elif need == "clear" and latched:
        if not may_clear:
            return (f"before {step}: the estop is latched and was latched before this run began; "
                    "the runner clears only a latch its own steps set, so it was left latched")
        path = ESTOP_CLEAR_PATH
    else:
        return None
    try:
        status, _ = client.post_json(path)
    except (*soak.TRANSPORT_EXCEPTIONS, json.JSONDecodeError) as fault:
        return f"before {step}: POST {path} failed ({type(fault).__name__}: {fault})"
    now, why = read_estop(client)
    reached = (now is True) if need == "latched" else (now is False)
    return (f"before {step} (needs it {need}): POST {path} answered {status}; the estop now reads "
            f"{'latched' if now else 'clear' if now is False else 'UNKNOWN (' + why + ')'}"
            f"{'' if reached else ' - NOT where the script needs it'}")


# ---------------------------------------------------------------------------
# The report
# ---------------------------------------------------------------------------

def _cell(value: Any) -> str:
    return "-" if value is None else f"{value:,}" if isinstance(value, int) else str(value)


def render_report(results: list[StepResult], memory: list[StepMemory], flags: list[Flag],
                  profiler: list[dict], profiler_absent: Optional[str], estop_actions: list[str],
                  schema: soak.StatusSchema, floor: soak.AdmissionFloor, header: list[str]) -> str:
    by_step = {row.step: row for row in memory}
    out = list(header)
    for section, title in (("droid", "On the droid"), ("fixture", "Fixture scripts (own fixture server; the droid idle)")):
        rows = [r for r in results if r.section == section]
        if not rows:
            continue
        out += ["", f"## {title}", "",
                f"| Step | Result | Polls | min {schema.heap_free_field} | min "
                f"{schema.heap_largest_block_field} | min {schema.heap_field} | "
                f"{schema.heap_min_field} at end | {schema.failed_allocs_field} at end |",
                "|---|---|---|---|---|---|---|---|"]
        for r in rows:
            m = by_step.get(r.name, StepMemory(r.name))
            result = r.result if r.exit_code is None else f"{r.result} (exit {r.exit_code})"
            polls = f"{m.polls}" + (f" ({m.unanswered} unanswered)" if m.unanswered else "")
            out.append(f"| {r.name} | {result} | {polls} | {_cell(m.min_heap_free)} | "
                       f"{_cell(m.min_heap_largest_block)} | {_cell(m.min_buffer_reading)} | "
                       f"{_cell(m.heap_min_end)} | {_cell(m.failed_allocs_end)} |")
    out += ["", f"## Memory flags ({len(flags)})", ""]
    out += [f"- **{f.step}** [{f.kind}]: {f.message}" for f in flags] or ["- none"]
    lows = [m for m in memory if m.new_heap_min_low is not None]
    out += ["", f"## New {schema.heap_min_field} lows (reported, not judged)", ""]
    out += [f"- {m.step}: {m.new_heap_min_low:,}" for m in lows] or ["- none"]
    out += ["", "## Last failed allocation (GET /api/profiler, at each step's end)", ""]
    if profiler_absent:
        out.append(f"- {profiler_absent}")
    shown = False
    previous_fail = None
    for entry in profiler:
        if "error" in entry:
            out.append(f"- {entry['step']}: not read ({entry['error']})")
            shown = True
        elif "lastFail" in entry and entry["lastFail"] != previous_fail:
            fail = entry["lastFail"]
            caps = fail.get("caps")
            caps_text = f"0x{caps:x}" if isinstance(caps, int) else caps
            out.append(f"- {entry['step']}: failedAllocs {entry.get('failedAllocs')}, lastFail size "
                       f"{fail.get('size')} caps {caps_text} bt {' '.join(fail.get('bt') or [])}")
            previous_fail = fail
            shown = True
    if not shown and not profiler_absent:
        out.append("- no failed allocation reported")
    out += ["", "## Estop actions by the runner", ""]
    out += [f"- {action}" for action in estop_actions] or ["- none"]
    failed = [r for r in results if r.result in (RESULT_FAIL, RESULT_TIMEOUT)]
    unassessed = [r for r in results if r.result == RESULT_NOT_ASSESSED]
    out += ["", f"## Failed steps ({len(failed)})", ""]
    out += [f"- {r.name}: {r.result}{' - ' + r.note if r.note else ''} (log {r.log})" for r in failed] or ["- none"]
    out += ["", f"## Not assessed: precondition did not hold ({len(unassessed)})", ""]
    out += [f"- {r.name} (log {r.log})" for r in unassessed] or ["- none"]
    out += ["", f"Admission floor judged against: {floor.ordinary_bytes} B ordinary "
                f"({floor.env}, {floor.sources[soak.ADMISSION_FLOOR_MACRO]})."]
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# The run
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--image", required=True, choices=("artoo", "shipping"),
                        help="the product image on the board, as tools/soak.py names it (declared, never sniffed)")
    parser.add_argument("--build-env", default=None,
                        help="PlatformIO env whose admission floor is judged against "
                             "(default: the image's own, as tools/soak.py)")
    parser.add_argument("--droid", default=None, metavar="URL",
                        help="the droid, e.g. http://10.0.0.22: scripts' BASE_URL, the sheet's http steps")
    parser.add_argument("--status-base", default=None, metavar="URL",
                        help="where GET /api/status is polled and the estop read (default: --droid)")
    parser.add_argument("--sheet", required=True, help="the board's tools/bench_rows/<board>.txt")
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port", help="the Console's serial port (make bench-auto resolves it)")
    transport.add_argument("--console-http", metavar="URL",
                           help="the Console over POST /api/console at URL instead of serial "
                                "(--offline: defaults to this run's fixture server)")
    parser.add_argument("--rows", default=None, help="a bounded subset of the sheet's rows, in this order")
    parser.add_argument("--run-dir", default=None,
                        help="where the logs, samples and report go (default output/bench-auto/<image>-<time>)")
    parser.add_argument("--idle-s", type=float, default=20.0, help="baseline seconds before the sheet")
    parser.add_argument("--poll-interval-s", type=float, default=1.0)
    parser.add_argument("--poll-timeout-s", type=float, default=5.0)
    parser.add_argument("--script-timeout-s", type=float, default=300.0)
    parser.add_argument("--sweep-timeout-s", type=float, default=600.0)
    parser.add_argument("--sheet-timeout-s", type=float, default=1800.0)
    parser.add_argument("--offline", action="store_true",
                        help="the offline proof: the droid IS this run's fixture server, every droid "
                             "script and the sweep take FIXTURE=1 (each meets its own fixture droid, so the "
                             "estop is not managed), and --status-base must name a server that answers "
                             "/api/status")
    return parser


def refuse(message: str) -> int:
    print(f"REFUSED: {message}", file=sys.stderr)
    return EXIT_REFUSED


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    if args.offline:
        if args.droid:
            return refuse("--offline makes this run's fixture server the droid; drop --droid")
        if not args.status_base:
            return refuse("--offline needs --status-base: the fixture server answers no /api/status")
    elif not args.droid:
        return refuse("--droid is required (http://<droid>)")
    elif not (args.port or args.console_http):
        return refuse("the sheet needs the Console: --port <serial> or --console-http <url>")

    # -- preflight: every refusal happens before anything touches the droid --
    node = shutil.which("node")
    if node is None:
        return refuse("node is not on PATH")
    node_path = os.environ.get("NODE_PATH") or str(DEFAULT_NODE_PATH)
    if not any((Path(entry) / "playwright").is_dir() for entry in node_path.split(os.pathsep) if entry):
        return refuse(f"no playwright module under NODE_PATH={node_path} (test/playwright/README.md)")
    try:
        declarations = discover_declarations()
    except DeclarationError as problem:
        return refuse(f"a script's target could not be read:\n{problem}")
    sheet = Path(args.sheet)
    if not sheet.is_file():
        return refuse(f"no sheet at {sheet}")
    schema = soak.SCHEMAS[args.image]
    build_env = args.build_env or schema.build_env
    try:
        floor = soak.resolve_admission_floor(build_env)
    except soak.BuildConstantUnresolved as unresolved:
        return refuse(f"no admission floor to judge against: {unresolved}")

    status_base = (args.status_base or args.droid).rstrip("/")
    try:
        status_client = client_for(status_base, args.poll_timeout_s)
    except ValueError as bad:
        return refuse(str(bad))
    try:
        code, body, text = get_json(status_client, soak.DEFAULT_STATUS_PATH)
    except soak.TRANSPORT_EXCEPTIONS as fault:
        return refuse(f"GET {status_base}/api/status failed: {fault}")
    if code != 200 or body is None:
        return refuse(f"GET {status_base}/api/status answered {code}: {text[:80]!r}")
    mismatches = schema.structural_mismatches(body)
    if mismatches:
        likely = soak.identify_schema(body)
        return refuse(f"the payload is not the declared {args.image} image"
                      f"{f' (it reads as {likely.name})' if likely else ''}: " + "; ".join(mismatches))

    steps = droid_steps(declarations)
    fixtures = fixture_declarations(declarations)
    started_clear = False
    if not args.offline:
        latched = body.get("estop")
        if not isinstance(latched, bool):
            return refuse("GET /api/status carries no boolean estop to order the scripts by")
        needs_clear = [s.name for s in steps if s.estop == "clear"]
        if latched and needs_clear:
            return refuse("the estop is LATCHED, and these scripts need it clear: "
                          f"{', '.join(needs_clear)}. The runner clears only a latch its own steps set. "
                          "Clear it on Foot Drive or the Dashboard, then run again.")
        started_clear = not latched

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = Path(args.run_dir or REPO_ROOT / "output" / "bench-auto" / f"{args.image}-{stamp}")
    if run_dir.exists() and any(run_dir.iterdir()):
        return refuse(f"{run_dir} already holds a run; name a new --run-dir")
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "logs").mkdir()

    header = [
        f"# bench_auto: {args.image} image, {wall_clock()}",
        "",
        f"- droid: {'the fixture server this run starts (--offline)' if args.offline else args.droid}; "
        f"status polled at {status_base} every {args.poll_interval_s:g} s",
        f"- firmwareVersion {body.get('firmwareVersion', 'UNKNOWN')}, fsVersion {body.get('fsVersion', 'UNKNOWN')}, "
        f"resetReason {body.get('resetReason', 'UNKNOWN')}, uptimeMs {body.get('uptimeMs', 'UNKNOWN')}",
        f"- sheet {sheet} (--skip-manual{', --rows ' + args.rows if args.rows else ''}); "
        f"{len(steps)} droid steps, {len(fixtures)} fixture scripts",
        f"- run dir {run_dir}: samples.jsonl, profiler.jsonl, logs/, report.md, summary.json",
    ]
    print("\n".join(header), flush=True)

    results: list[StepResult] = []
    estop_actions: list[str] = []
    log = MemoryLog(status_client, schema, run_dir, args.poll_interval_s)
    server = FixtureServer(run_dir)
    interrupted = False
    plan: list[str] = []

    def run_step(result: StepResult, cmd: list[str], env: dict[str, str], timeout_s: float,
                 playwright: bool) -> None:
        log_path = run_dir / "logs" / log_name(result.name)
        result.log = str(log_path.relative_to(run_dir))
        print(f"\n[bench_auto] === {result.section}: {result.name}", flush=True)
        started = time.monotonic()
        try:
            exit_code, timed_out = run_process(cmd, env, timeout_s, log_path)
        except OSError as fault:
            result.result, result.note = RESULT_FAIL, f"could not start: {fault}"
        else:
            result.exit_code = exit_code
            result.result = classify(exit_code, timed_out, playwright)
            if timed_out:
                result.note = f"no exit within {timeout_s:.0f} s"
        result.seconds = round(time.monotonic() - started, 1)
        print(f"[bench_auto] {result.name}: {result.result} in {result.seconds} s", flush=True)

    try:
        if args.offline:
            server.start()
        droid = server.base if args.offline else args.droid.rstrip("/")
        droid_client = None if args.offline else client_for(droid, args.poll_timeout_s)
        fixture_flag = {"FIXTURE": "1"} if args.offline else {}

        plan = (["idle"] if args.idle_s > 0 else []) + ["sheet", "console-sweep.js"]
        plan += [s.name for s in steps] + [f"fixture: {d.script.removeprefix('test/playwright/')}" for d in fixtures]
        log.start(plan[0])

        def advance(ended: str) -> None:
            following = plan[plan.index(ended) + 1] if plan.index(ended) + 1 < len(plan) else ended
            log.close_step(ended, following)

        if args.idle_s > 0:
            print(f"\n[bench_auto] === idle baseline, {args.idle_s:g} s", flush=True)
            time.sleep(args.idle_s)
            results.append(StepResult("idle", "droid", RESULT_PASS, 0, args.idle_s))
            advance("idle")

        sheet_cmd = [sys.executable, str(CONSOLE_CLIENT), "--script", str(sheet), "--skip-manual",
                     "--run-dir", str(run_dir / "sheet"), "--status", status_base]
        sheet_cmd += ["--port", args.port] if args.port else ["--http", args.console_http or server.base]
        if not args.offline:
            sheet_cmd += ["--http-base", droid]
        if args.rows:
            sheet_cmd += ["--rows", args.rows]
        sheet_result = StepResult("sheet", "droid")
        run_step(sheet_result, sheet_cmd, child_env({}, None), args.sheet_timeout_s, playwright=False)
        results.append(sheet_result)
        advance("sheet")

        sweep = StepResult("console-sweep.js", "droid")
        run_step(sweep, [node, str(SWEEP_SCRIPT)],
                 child_env({"BASE": droid, "HEADED": "1", **fixture_flag}, node_path),
                 args.sweep_timeout_s, playwright=True)
        results.append(sweep)
        advance("console-sweep.js")

        for step in steps:
            if droid_client is not None:
                action = ensure_estop(droid_client, step.estop, step.name, may_clear=started_clear)
                if action:
                    estop_actions.append(action)
                    print(f"[bench_auto] {action}", flush=True)
            result = StepResult(step.name, "droid")
            env = {"BASE_URL": droid, "HEADLESS": "false", **dict(step.env), **fixture_flag}
            run_step(result, [node, str(REPO_ROOT / step.script)], child_env(env, node_path),
                     args.script_timeout_s, playwright=True)
            results.append(result)
            advance(step.name)

        if fixtures:
            if not args.offline:
                server.start()
            for d in fixtures:
                name = f"fixture: {d.script.removeprefix('test/playwright/')}"
                result = StepResult(name, "fixture")
                env = {"FIXTURE": "1", "HEADLESS": "false", "BASE_URL": server.base,
                       "TARGET_URL": f"{server.base}/{d.page}"}
                run_step(result, [node, str(REPO_ROOT / d.script)], child_env(env, node_path),
                         args.script_timeout_s, playwright=True)
                results.append(result)
                advance(name)
    except KeyboardInterrupt:
        interrupted = True
        print("\n[bench_auto] interrupted: stopping and reporting what ran", file=sys.stderr, flush=True)
    except RuntimeError as fault:
        results.append(StepResult("runner", "droid", RESULT_FAIL, note=str(fault)))
        print(f"[bench_auto] {fault}", file=sys.stderr, flush=True)
    finally:
        server.stop()
        log.stop()

    # Every step that ran or was running, in plan order: an interrupted step
    # has samples and no result, and its samples still count.
    sampled = {sample["step"] for sample in log.samples}
    ran = {r.name for r in results}
    memory, flags = summarize_memory(log.samples, [n for n in plan if n in ran or n in sampled],
                                     schema, floor)
    report = render_report(results, memory, flags, log.profiler, log.profiler_absent_note,
                           estop_actions, schema, floor, header)
    (run_dir / "report.md").write_text(report, encoding="utf-8")
    (run_dir / "summary.json").write_text(json.dumps({
        "image": args.image, "buildEnv": build_env, "admissionFloor": floor.report(),
        "steps": [dataclasses.asdict(r) for r in results],
        "memory": [dataclasses.asdict(m) for m in memory],
        "flags": [dataclasses.asdict(f) for f in flags],
        "profilerAbsent": log.profiler_absent_note,
        "estopActions": estop_actions,
        "interrupted": interrupted,
    }, indent=2) + "\n", encoding="utf-8")
    print("\n" + report, flush=True)

    if interrupted:
        return EXIT_INTERRUPTED
    failed = any(r.result in (RESULT_FAIL, RESULT_TIMEOUT) for r in results)
    return EXIT_FINDINGS if failed or flags else EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
