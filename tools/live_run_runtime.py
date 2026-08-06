#!/usr/bin/env python3
"""Hardened primitives for live-controller evidence runs.

Extracted from tools/issue65_live_ab_runtime.py, which is where they were first
written and where they stayed long after they stopped being about issue #65.
Three tools that have nothing to do with that ticket already depend on them --
tools/webload_baseline_run.py (the ADR 0017 acceptance coordinator),
tools/response_deadline_probe.py and tools/webload_sse_stall.py -- so a harness
fix for one of those was landing in a file named after a closed A/B comparison.

What belongs here is anything a run against a live controller needs regardless
of which run it is: durable NDJSON and atomic JSON writers, the stop arbiter and
its locked reason vocabulary, serial classification and watching, the
ping/status/serial sampling loop, cooldown and primary-outcome classification,
evidence-root and artifact-identity capture, and bounded subprocess logging.

What does NOT belong here is anything that knows about a specific comparison:
per-commit worktrees, vendor package pinning, role tables. Those stay in
tools/issue65_live_ab_runtime.py, which now imports from this module.

The stop-reason and primary-outcome vocabularies moved verbatim. They are
compared against strings inside already-recorded evidence bundles, so tidying
them would silently invalidate comparisons against those bundles.
"""
from __future__ import annotations

import argparse
import copy
from contextlib import contextmanager
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shlex
import shutil
import subprocess
import sys
import tempfile
import termios
import threading
import time
from typing import Any, Callable, Mapping, Sequence
from urllib import error as urllib_error
from urllib import request as urllib_request

from typing import Protocol


class LiveRunError(RuntimeError):
    """A user-facing error from a live-controller run."""


class RunBundle(Protocol):
    """What the sampling loop actually needs from a bundle.

    MonitorLoop annotated this as EvidenceBundle, which was never true of every
    caller: webload_baseline_run.py passes its own Bundle. Only these two
    attributes are ever touched, so the annotation now says so.
    """

    @property
    def root(self) -> Path: ...

    @property
    def timeline(self) -> Any: ...


PING_INTERVAL_SECONDS = 1.0

STATUS_INTERVAL_SECONDS = 5.0

STATUS_DEADLINE_SECONDS = 1.0

HTTP_BLACKOUT_SECONDS = 30.0

RECENT_PING_SECONDS = 2.5

PING_LOSS_STOP_SECONDS = 5.0

RECENT_STATUS_SECONDS = 12.0

COOLDOWN_SECONDS = 15.0

COOLDOWN_TOLERANCE_BYTES = 2_000

COOLDOWN_HEAP_FLOOR_BYTES = 12_000

def should_stop_on_ping_loss(
    *, armed: bool, success: bool, loss_duration: float, status_recent: bool,
) -> bool:
    """Whether ICMP evidence is strong enough to end the run.

    Sustained loss, and only while HTTP is not simultaneously proving the
    controller alive. Either condition alone produces a false stop: a single
    dropped echo, or ICMP starved under a load the controller is otherwise
    serving fine. Kept separate from the sampler so both are assertable without
    a live controller.
    """
    if not armed or success:
        return False
    if status_recent:
        return False
    return loss_duration >= PING_LOSS_STOP_SECONDS

STOP_REASONS = frozenset((
    "panic",
    "unexpected reset",
    "positive allocation-failure evidence",
    "loss of ICMP",
    "HTTP Blackout",
    "operator interrupt",
))

PRIMARY_OUTCOMES = frozenset((
    "Usable Page",
    "Page Failure",
    "HTTP Blackout",
    "Unexpected controller failure",
    "UNKNOWN",
))

ALLOCATION_FAILURE_RE = re.compile(
    r"(?:alloc(?:ation)?(?:\s+\w+){0,3}\s+failed|failed\s+alloc|"
    r"out\s+of\s+memory|no\s+memory|heap\s+corrupt|CORRUPT\s+HEAP)",
    re.IGNORECASE,
)

PANIC_RE = re.compile(
    r"(?:Guru Meditation|panic(?:'ed)?|assert failed|abort\(\))",
    re.IGNORECASE,
)

_NDJSON_LOCK = threading.Lock()

_CONTROL_LOCK = threading.RLock()

_UNSET = object()

@dataclass(frozen=True)
class Timeline:
    """Create records sharing wall-clock and monotonic chronology."""

    monotonic_origin_ns: int

    @classmethod
    def start(cls) -> Timeline:
        return cls(monotonic_origin_ns=time.monotonic_ns())

    def record(self, event: str, **fields: object) -> dict[str, object]:
        now_ns = time.monotonic_ns()
        return {
            "event": event,
            "wallTime": (
                datetime.now(timezone.utc)
                .isoformat(timespec="milliseconds")
                .replace("+00:00", "Z")
            ),
            "monotonicNs": now_ns,
            "elapsedMonotonicSeconds": (
                now_ns - self.monotonic_origin_ns
            ) / 1_000_000_000,
            **fields,
        }

def _append_bytes_durable(path: Path, payload: bytes) -> None:
    """Append one complete record and fsync it before returning."""
    path = Path(path)
    flags = os.O_WRONLY | os.O_CREAT | os.O_APPEND
    descriptor = os.open(path, flags, 0o644)
    try:
        view = memoryview(payload)
        while view:
            written = os.write(descriptor, view)
            if written <= 0:
                raise OSError("append made no progress")
            view = view[written:]
        os.fsync(descriptor)
    finally:
        os.close(descriptor)

def append_ndjson(
    path: Path,
    timeline: Timeline,
    event: str,
    **fields: object,
) -> dict[str, object]:
    """Durably append one Timeline record as compact newline-delimited JSON."""
    record = timeline.record(event, **fields)
    _append_ndjson_record(path, record)
    return record

def _append_ndjson_record(
    path: Path,
    record: Mapping[str, object],
) -> None:
    payload = (
        json.dumps(dict(record), separators=(",", ":"), ensure_ascii=False)
        + "\n"
    ).encode("utf-8")
    with _NDJSON_LOCK:
        _append_bytes_durable(Path(path), payload)

def _fsync_directory(directory: Path) -> None:
    descriptor = os.open(directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)

def atomic_write_json(path: Path, value: object) -> None:
    """Replace one JSON artifact atomically with a durable same-dir temporary."""
    path = Path(path)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            json.dump(value, temporary, indent=2)
            temporary.write("\n")
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, path)
        temporary_path = None
        _fsync_directory(path.parent)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)

def update_control_json(
    path: Path,
    *,
    stop_reason: object = _UNSET,
    status_reachable_at: object = _UNSET,
) -> dict[str, str]:
    """Atomically merge the two browser-control fields.

    An existing stopReason is immutable. Unknown fields are intentionally not
    propagated into this narrowly owned coordinator artifact.
    """
    path = Path(path)
    with _CONTROL_LOCK:
        current: object = {}
        if path.exists():
            try:
                current = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                raise LiveRunError(
                    f"control file is not valid JSON: {path}"
                ) from error
        if not isinstance(current, dict):
            raise LiveRunError("control file must contain one JSON object")
        merged = {
            key: value for key, value in current.items()
            if key in ("stopReason", "statusReachableAt")
            and isinstance(value, str)
        }
        if status_reachable_at is not _UNSET:
            if not isinstance(status_reachable_at, str) or not status_reachable_at:
                raise LiveRunError("statusReachableAt must be a timestamp string")
            merged["statusReachableAt"] = status_reachable_at
        if stop_reason is not _UNSET and "stopReason" not in merged:
            if stop_reason not in STOP_REASONS:
                raise LiveRunError(
                    f"stopReason is outside the locked vocabulary: {stop_reason}"
                )
            merged["stopReason"] = str(stop_reason)
        atomic_write_json(path, merged)
        return merged

class StopArbiter:
    """Thread-safe, first-wins stop ownership for one live fixture."""

    def __init__(
        self,
        control_path: Path,
        timeline: Timeline,
        events: list[dict[str, object]],
    ) -> None:
        self.control_path = Path(control_path)
        self.timeline = timeline
        self.events = events
        self._lock = threading.Lock()
        self._stopped = threading.Event()
        self._reason: str | None = None
        if self.control_path.exists():
            try:
                current = json.loads(self.control_path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                raise LiveRunError(
                    f"control file is not valid JSON: {self.control_path}"
                ) from error
            existing = current.get("stopReason") if isinstance(current, dict) else None
            if existing is not None:
                if existing not in STOP_REASONS:
                    raise LiveRunError(
                        "existing stopReason is outside the locked vocabulary"
                    )
                self._reason = existing
                self._stopped.set()

    @property
    def reason(self) -> str | None:
        with self._lock:
            return self._reason

    @property
    def stopped(self) -> bool:
        return self._stopped.is_set()

    def request_stop(self, reason: str, **evidence: object) -> bool:
        if reason not in STOP_REASONS:
            raise LiveRunError(
                f"stop reason is outside the locked vocabulary: {reason}"
            )
        with self._lock:
            if self._reason is not None:
                self.events.append(self.timeline.record(
                    "stop-request-ignored",
                    requestedReason=reason,
                    firstReason=self._reason,
                    **evidence,
                ))
                return False
            merged = update_control_json(
                self.control_path,
                stop_reason=reason,
            )
            self._reason = merged["stopReason"]
            self._stopped.set()
            self.events.append(self.timeline.record(
                "stop-requested",
                stopReason=self._reason,
                **evidence,
            ))
            return True

    def mark_status_reachable(self, wall_time: str) -> None:
        with self._lock:
            update_control_json(
                self.control_path,
                status_reachable_at=wall_time,
            )

def _configure_serial_port(descriptor: int, baud: int = 115200) -> None:
    baud_map = {
        9600: termios.B9600,
        19200: termios.B19200,
        38400: termios.B38400,
        57600: termios.B57600,
        115200: termios.B115200,
    }
    baud_constant = baud_map.get(baud)
    if baud_constant is None:
        raise LiveRunError(f"unsupported serial baud rate: {baud}")
    attrs = termios.tcgetattr(descriptor)
    iflag, oflag, cflag, lflag, _ispeed, _ospeed, control = attrs
    iflag &= ~(
        termios.IGNBRK | termios.BRKINT | termios.PARMRK
        | termios.ISTRIP | termios.INLCR | termios.IGNCR
        | termios.ICRNL | termios.IXON
    )
    oflag &= ~termios.OPOST
    lflag &= ~(
        termios.ECHO | termios.ECHONL | termios.ICANON
        | termios.ISIG | termios.IEXTEN
    )
    cflag &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
    cflag |= termios.CS8 | termios.CLOCAL | termios.CREAD
    cflag &= ~getattr(termios, "CRTSCTS", 0)
    cflag &= ~getattr(termios, "HUPCL", 0)
    control[termios.VMIN] = 0
    control[termios.VTIME] = 1
    termios.tcsetattr(
        descriptor,
        termios.TCSANOW,
        [iflag, oflag, cflag, lflag, baud_constant, baud_constant, control],
    )

def classify_serial_line(line: str) -> str | None:
    """Return only directly observable stop classes; silence proves nothing."""
    if ALLOCATION_FAILURE_RE.search(line):
        return "positive allocation-failure evidence"
    if PANIC_RE.search(line):
        return "panic"
    return None

class SerialWatcher:
    """Reconnect-safe, no-control-line serial capture for the physical fixture."""

    def __init__(
        self,
        port: Path,
        bundle: RunBundle,
        arbiter: StopArbiter,
        phase: Callable[[], str],
    ) -> None:
        self.port = Path(port)
        self.bundle = bundle
        self.arbiter = arbiter
        self.phase = phase
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run,
            name="live-run-serial",
            daemon=True,
        )
        self.connected = threading.Event()
        self.disconnected_after_connect = threading.Event()
        self.reconnected = threading.Event()
        self.error: BaseException | None = None
        self._connection_count = 0

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def join(self, timeout: float = 3.0) -> None:
        self._thread.join(timeout)
        if self._thread.is_alive():
            raise LiveRunError("serial watcher did not stop within deadline")
        if self.error is not None:
            raise LiveRunError(f"serial watcher failed: {self.error}")

    def _record_line(self, line: str) -> None:
        record = self.bundle.timeline.record(
            "serial-line",
            phase=self.phase(),
            line=line,
        )
        text = (
            f"{record['wallTime']} "
            f"{record['elapsedMonotonicSeconds']:.6f} {line}\n"
        )
        _append_bytes_durable(self.bundle.root / "serial.log", text.encode("utf-8"))
        stop_reason = classify_serial_line(line)
        if stop_reason == "positive allocation-failure evidence":
            observation_phase = {
                "browser": "load",
                "cooldown": "cooldown",
            }.get(self.phase(), "pre-load")
            self.bundle.record_failed_allocation(
                phase=observation_phase,
                raw_line=line,
                record=record,
            )
        if stop_reason is not None:
            self.arbiter.request_stop(
                stop_reason,
                source="serial",
                rawLine=line,
                wallTime=record["wallTime"],
                monotonicNs=record["monotonicNs"],
            )

    def _run(self) -> None:
        descriptor: int | None = None
        buffer = bytearray()
        try:
            while not self._stop.is_set():
                if descriptor is None:
                    try:
                        descriptor = os.open(
                            self.port,
                            os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK,
                        )
                        _configure_serial_port(descriptor)
                    except (FileNotFoundError, OSError, termios.error):
                        if descriptor is not None:
                            os.close(descriptor)
                            descriptor = None
                        self._stop.wait(0.1)
                        continue
                    self._connection_count += 1
                    self.connected.set()
                    if self._connection_count > 1:
                        self.reconnected.set()
                    self.bundle.events.append(self.bundle.timeline.record(
                        "serial-connected",
                        port=str(self.port),
                        connectionCount=self._connection_count,
                    ))
                try:
                    readable, _, _ = select.select([descriptor], [], [], 0.1)
                    if not readable:
                        continue
                    chunk = os.read(descriptor, 512)
                    if not chunk:
                        raise OSError("serial device returned EOF")
                    buffer.extend(chunk)
                    while b"\n" in buffer:
                        end = buffer.index(b"\n")
                        raw = bytes(buffer[:end])
                        del buffer[:end + 1]
                        self._record_line(
                            raw.decode("utf-8", errors="replace").rstrip("\r")
                        )
                except (OSError, ValueError):
                    if descriptor is not None:
                        os.close(descriptor)
                        descriptor = None
                    if self._connection_count > 0:
                        self.disconnected_after_connect.set()
                    self.bundle.events.append(self.bundle.timeline.record(
                        "serial-disconnected",
                        port=str(self.port),
                    ))
                    buffer.clear()
        except BaseException as error:
            self.error = error
            try:
                self.arbiter.request_stop(
                    "operator interrupt" if isinstance(error, KeyboardInterrupt)
                    else "unexpected reset",
                    source="serial-watcher",
                    error=str(error),
                )
            except BaseException:
                pass
        finally:
            if descriptor is not None:
                os.close(descriptor)

def _http_json(url: str, timeout_seconds: float) -> dict[str, object]:
    request = urllib_request.Request(
        url,
        headers={"Accept": "application/json", "Cache-Control": "no-cache"},
        method="GET",
    )
    try:
        with urllib_request.urlopen(request, timeout=timeout_seconds) as response:
            if response.status != 200:
                raise LiveRunError(
                    f"{url} returned HTTP {response.status}"
                )
            body = response.read()
    except (
        urllib_error.URLError,
        TimeoutError,
        OSError,
    ) as error:
        raise LiveRunError(f"{url} is unreachable: {error}") from error
    try:
        value = json.loads(body)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise LiveRunError(f"{url} returned invalid JSON") from error
    if not isinstance(value, dict):
        raise LiveRunError(f"{url} must return a JSON object")
    return value

def identity_mismatches(
    status: Mapping[str, object],
    identity: Mapping[str, object],
    deployment: Mapping[str, object],
    controller: str,
) -> list[str]:
    firmware_identity = deployment["firmware"]["generatedIdentity"]
    filesystem_identity = deployment["filesystem"]["generatedIdentity"]
    expected_firmware = firmware_identity.get("firmwareVersion")
    expected_filesystem = filesystem_identity.get("fsVersion")
    mismatches: list[str] = []
    if status.get("firmwareVersion") != expected_firmware:
        mismatches.append(
            f"firmwareVersion expected {expected_firmware!r}, "
            f"found {status.get('firmwareVersion')!r}"
        )
    if status.get("fsVersion") != expected_filesystem:
        mismatches.append(
            f"fsVersion expected {expected_filesystem!r}, "
            f"found {status.get('fsVersion')!r}"
        )
    if status.get("resetReason") != "POWERON":
        mismatches.append(
            f"resetReason expected 'POWERON', found {status.get('resetReason')!r}"
        )
    uptime = status.get("uptimeMs")
    if not isinstance(uptime, int) or uptime < 0:
        mismatches.append(f"uptimeMs is not a fresh physical boot: {uptime!r}")
    if not isinstance(identity.get("droidName"), str):
        mismatches.append("identity response has no droidName")
    if not controller:
        mismatches.append("controller address is empty")
    return mismatches

def cooldown_sample_qualifies(value: object, baseline: int) -> bool:
    return (
        isinstance(value, int)
        and value >= COOLDOWN_HEAP_FLOOR_BYTES
        and abs(value - baseline) <= COOLDOWN_TOLERANCE_BYTES
    )

def evaluate_cooldown(
    samples: Sequence[Mapping[str, object]],
    baseline: int,
) -> dict[str, object]:
    consecutive = 0
    qualifying = 0
    for sample in samples:
        status = sample.get("status")
        value = status.get("heapLargest8bit") if isinstance(status, dict) else None
        if cooldown_sample_qualifies(value, baseline):
            qualifying += 1
            consecutive += 1
            if consecutive >= 2:
                return {
                    "passed": True,
                    "baselineHeapLargest8bit": baseline,
                    "qualifyingSamples": qualifying,
                    "sampleCount": len(samples),
                }
        else:
            consecutive = 0
    return {
        "passed": False,
        "baselineHeapLargest8bit": baseline,
        "qualifyingSamples": qualifying,
        "sampleCount": len(samples),
    }

def classify_primary_outcome(
    stop_reason: str | None,
    browser_state: Mapping[str, object] | None,
    status_reachable: bool,
) -> str:
    if stop_reason == "HTTP Blackout":
        return "HTTP Blackout"
    if stop_reason in (
        "panic",
        "unexpected reset",
        "positive allocation-failure evidence",
        "loss of ICMP",
    ):
        return "Unexpected controller failure"
    if (
        isinstance(browser_state, Mapping)
        and browser_state.get("captureStatus") == "usable"
        and browser_state.get("browserGatesPassed") is True
        and status_reachable
    ):
        return "Usable Page"
    if browser_state is not None and status_reachable:
        return "Page Failure"
    return "UNKNOWN"

class MonitorLoop:
    """One thread owns the fixed ping/status cadence and blackout timing."""

    def __init__(
        self,
        controller: str,
        bundle: RunBundle,
        arbiter: StopArbiter,
    ) -> None:
        self.controller = controller
        self.bundle = bundle
        self.arbiter = arbiter
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run,
            name="live-run-network-monitor",
            daemon=True,
        )
        self._lock = threading.RLock()
        self.phase = "pre-cycle"
        self.armed = False
        self.browser_active = False
        self.status_requests_enabled = True
        self.latest_status: dict[str, object] | None = None
        self.latest_status_record: dict[str, object] | None = None
        self.latest_status_success_mono: float | None = None
        self.latest_ping_success_mono: float | None = None
        self.ping_loss_started_mono: float | None = None
        self.status_loss_started_mono: float | None = None
        self.cooldown_samples: list[dict[str, object]] = []
        self.error: BaseException | None = None

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def join(self, timeout: float = 4.0) -> None:
        self._thread.join(timeout)
        if self._thread.is_alive():
            raise LiveRunError("network monitor did not stop within deadline")
        if self.error is not None:
            raise LiveRunError(f"network monitor failed: {self.error}")

    def set_phase(self, phase: str) -> None:
        with self._lock:
            self.phase = phase
            self.browser_active = phase == "browser"

    def arm(self) -> None:
        with self._lock:
            self.armed = True

    def stop_status_requests(self) -> None:
        with self._lock:
            self.status_requests_enabled = False

    def snapshot(self) -> dict[str, object]:
        with self._lock:
            return {
                "phase": self.phase,
                "armed": self.armed,
                "latestStatus": copy.deepcopy(self.latest_status),
                "latestStatusRecord": copy.deepcopy(self.latest_status_record),
                "latestStatusSuccessMonotonic": self.latest_status_success_mono,
                "latestPingSuccessMonotonic": self.latest_ping_success_mono,
                "pingLossStartedMonotonic": self.ping_loss_started_mono,
                "statusLossStartedMonotonic": self.status_loss_started_mono,
                "cooldownSamples": copy.deepcopy(self.cooldown_samples),
            }

    def wait_for_status(
        self,
        predicate: Callable[[Mapping[str, object]], bool],
        timeout_seconds: float,
    ) -> dict[str, object]:
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline and not self.arbiter.stopped:
            with self._lock:
                status = copy.deepcopy(self.latest_status)
            if isinstance(status, dict) and predicate(status):
                return status
            self._stop.wait(0.1)
        raise LiveRunError("timed out waiting for qualifying /api/status")

    def _sample_ping(self) -> None:
        started = time.monotonic()
        try:
            result = subprocess.run(
                ["ping", "-c", "1", "-W", "1", self.controller],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
                timeout=2,
                shell=False,
            )
            success = result.returncode == 0
            detail = f"exit-{result.returncode}"
        except (subprocess.TimeoutExpired, OSError) as error:
            success = False
            detail = str(error)
        record = append_ndjson(
            self.bundle.root / "ping.ndjson",
            self.bundle.timeline,
            "ping-sample",
            phase=self.phase,
            success=success,
            detail=detail,
            durationSeconds=time.monotonic() - started,
        )
        now = time.monotonic()
        with self._lock:
            if success:
                self.latest_ping_success_mono = now
                self.ping_loss_started_mono = None
                loss_duration = 0.0
            else:
                if self.ping_loss_started_mono is None:
                    self.ping_loss_started_mono = now
                loss_duration = now - self.ping_loss_started_mono
            status_recent = (
                self.latest_status_success_mono is not None
                and now - self.latest_status_success_mono <= RECENT_STATUS_SECONDS
            )
            armed = self.armed
        if should_stop_on_ping_loss(
            armed=armed, success=success,
            loss_duration=loss_duration, status_recent=status_recent,
        ):
            self.arbiter.request_stop(
                "loss of ICMP",
                source="ping",
                wallTime=record["wallTime"],
                lossDurationSeconds=round(loss_duration, 3),
            )

    def _sample_status(self) -> None:
        with self._lock:
            if not self.status_requests_enabled:
                return
            phase = self.phase
            armed = self.armed
            previous = copy.deepcopy(self.latest_status)
        try:
            status = _http_json(
                f"http://{self.controller}/api/status",
                STATUS_DEADLINE_SECONDS,
            )
            success = True
            error = None
        except LiveRunError as request_error:
            status = None
            success = False
            error = str(request_error)
        record = append_ndjson(
            self.bundle.root / "status.ndjson",
            self.bundle.timeline,
            "status-sample",
            phase=phase,
            success=success,
            error=error,
            status=status,
        )
        now = time.monotonic()
        with self._lock:
            ping_recent = (
                self.latest_ping_success_mono is not None
                and now - self.latest_ping_success_mono <= RECENT_PING_SECONDS
            )
            if success and isinstance(status, dict):
                self.latest_status = status
                self.latest_status_record = record
                self.latest_status_success_mono = now
                self.status_loss_started_mono = None
                if phase == "cooldown":
                    self.cooldown_samples.append({
                        "record": copy.deepcopy(record),
                        "status": copy.deepcopy(status),
                    })
                browser_active = self.browser_active
            else:
                if self.status_loss_started_mono is None:
                    self.status_loss_started_mono = now
                loss_duration = now - self.status_loss_started_mono
                browser_active = False
        if success and isinstance(status, dict):
            if (
                armed
                and isinstance(previous, dict)
                and isinstance(previous.get("uptimeMs"), int)
                and isinstance(status.get("uptimeMs"), int)
                and status["uptimeMs"] < previous["uptimeMs"]
            ):
                self.arbiter.request_stop(
                    "unexpected reset",
                    source="status",
                    previousUptimeMs=previous["uptimeMs"],
                    uptimeMs=status["uptimeMs"],
                )
            if browser_active and not self.arbiter.stopped:
                self.arbiter.mark_status_reachable(str(record["wallTime"]))
        elif (
            armed
            and phase == "browser"
            and loss_duration >= HTTP_BLACKOUT_SECONDS
            and ping_recent
        ):
            self.arbiter.request_stop(
                "HTTP Blackout",
                source="status",
                continuousLossSeconds=loss_duration,
            )
            self.stop_status_requests()

    def _run(self) -> None:
        next_ping = time.monotonic()
        next_status = time.monotonic()
        try:
            while not self._stop.is_set():
                now = time.monotonic()
                if now >= next_ping:
                    self._sample_ping()
                    next_ping = max(next_ping + PING_INTERVAL_SECONDS, time.monotonic())
                now = time.monotonic()
                if now >= next_status:
                    self._sample_status()
                    next_status = max(
                        next_status + STATUS_INTERVAL_SECONDS,
                        time.monotonic(),
                    )
                deadline = min(next_ping, next_status)
                self._stop.wait(max(0.01, min(0.1, deadline - time.monotonic())))
        except BaseException as error:
            self.error = error
            try:
                self.arbiter.request_stop(
                    "operator interrupt" if isinstance(error, KeyboardInterrupt)
                    else "unexpected reset",
                    source="network-monitor",
                    error=str(error),
                )
            except BaseException:
                pass

def create_evidence_root(root: Path) -> Path:
    """Atomically claim a fresh run directory without overwriting evidence."""
    root = Path(root)
    root.parent.mkdir(parents=True, exist_ok=True)
    try:
        root.mkdir()
    except FileExistsError as error:
        raise LiveRunError(
            f"evidence root already exists; refusing overwrite: {root}"
        ) from error
    _fsync_directory(root.parent)
    return root

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def _copy_file_no_overwrite(source: Path, destination: Path) -> None:
    with Path(source).open("rb") as input_file:
        with Path(destination).open("xb") as output_file:
            shutil.copyfileobj(input_file, output_file)
            output_file.flush()
            os.fsync(output_file.fileno())
    _fsync_directory(Path(destination).parent)

def capture_artifact_identity(
    generated_identity_source: Path,
    image: Path,
    identity_artifact: Path,
    digest_artifact: Path,
) -> dict[str, object]:
    """Capture generated JSON and the paired binary digest without shell tools."""
    generated_identity_source = Path(generated_identity_source)
    image = Path(image)
    identity_artifact = Path(identity_artifact)
    digest_artifact = Path(digest_artifact)
    for required in (generated_identity_source, image):
        if not required.is_file():
            raise LiveRunError(f"required artifact is absent: {required}")
    try:
        identity = json.loads(generated_identity_source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LiveRunError(
            f"generated identity is not valid JSON: {generated_identity_source}"
        ) from error
    _copy_file_no_overwrite(generated_identity_source, identity_artifact)
    digest = sha256_file(image)
    with digest_artifact.open("x", encoding="ascii") as output_file:
        output_file.write(f"{digest}  {image}\n")
        output_file.flush()
        os.fsync(output_file.fileno())
    _fsync_directory(digest_artifact.parent)
    return {
        "generatedIdentity": identity,
        "sha256": digest,
        "image": str(image),
        "imageBytes": image.stat().st_size,
        "identityArtifact": str(identity_artifact),
        "digestArtifact": str(digest_artifact),
    }

def run_logged(
    argv: list[str],
    *,
    cwd: Path,
    timeout_seconds: float,
    artifact: Path,
    timeline: Timeline,
    events: list[dict[str, object]],
    event: str,
    append: bool = False,
    environment: Mapping[str, str] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    """Run bounded list argv, combining output in one named evidence artifact."""
    if (
        not isinstance(argv, list)
        or not argv
        or any(not isinstance(part, str) or not part for part in argv)
    ):
        raise LiveRunError("command argv must be a non-empty list of strings")
    cwd = Path(cwd)
    artifact = Path(artifact)
    command_event = timeline.record(
        f"{event}-started",
        argv=list(argv),
        cwd=str(cwd),
        artifact=str(artifact),
    )
    events.append(command_event)
    return_code: int | None = None
    result: subprocess.CompletedProcess[bytes] | None = None
    timed_out = False
    try:
        with artifact.open("ab" if append else "xb") as log:
            try:
                result = subprocess.run(
                    argv,
                    cwd=cwd,
                    env=environment,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=False,
                    timeout=timeout_seconds,
                    shell=False,
                )
                return_code = result.returncode
            except subprocess.TimeoutExpired as error:
                timed_out = True
                raise LiveRunError(
                    f"{event} timed out after {timeout_seconds} seconds"
                ) from error
            except OSError as error:
                raise LiveRunError(
                    f"{event} could not start: {error}"
                ) from error
            finally:
                log.flush()
                os.fsync(log.fileno())
    except LiveRunError:
        raise
    except OSError as error:
        raise LiveRunError(
            f"{event} evidence logging failed: {error}"
        ) from error
    finally:
        events.append(timeline.record(
            f"{event}-finished",
            returnCode=return_code,
            timedOut=timed_out,
        ))
    if result is None:
        raise LiveRunError(f"{event} did not return a command result")
    if result.returncode:
        raise LiveRunError(
            f"{event} exited with return code {result.returncode}; see {artifact}"
        )
    return result

def _wait_for(
    predicate: Callable[[], bool],
    timeout_seconds: float,
    *,
    arbiter: StopArbiter | None = None,
) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        if arbiter is not None and arbiter.stopped:
            return False
        time.sleep(0.1)
    return predicate()
