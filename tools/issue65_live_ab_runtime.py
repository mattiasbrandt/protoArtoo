#!/usr/bin/env python3
"""Plan or execute the locked Issue #65 live A/B fixture."""
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

import issue65_live_ab as planner


GIT_TIMEOUT_SECONDS = 5
REQUIRED_COMMANDS = (
    "git", "pio", "ping", "node", "python3", "sha256sum", "cp", "mkdir",
)
PING_INTERVAL_SECONDS = 1.0
STATUS_INTERVAL_SECONDS = 5.0
STATUS_DEADLINE_SECONDS = 1.0
HTTP_BLACKOUT_SECONDS = 30.0
RECENT_PING_SECONDS = 2.5
COOLDOWN_SECONDS = 15.0
COOLDOWN_TOLERANCE_BYTES = 2_000
COOLDOWN_HEAP_FLOOR_BYTES = 12_000
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
A_PARENT_COMMIT = "98b79a0053385d099eb3f57e185a60b90312646e"
A_PRIME_WORKTREE = Path("/tmp/protoartoo-issue65-A-prime")
A_PRIME_WEBRESPONSES_SHA256 = (
    "0540b496b34ee9c597c3d5e0d3fc3a241873fe546bb6cf5995c0d6fba16e258e"
)
A_FINAL_WEBRESPONSES_SHA256 = (
    "ba0c80be9bcd9ede4aff0a6594c05e9d772da586155453c3cb99aa8e3de33d61"
)
B_PRIME_PACKAGE_ROOT = Path("/tmp/protoartoo-issue65-B-prime")
B_PRIME_WEBRESPONSES_SHA256 = (
    "12055aa687cd0f6f719b65057c7a1d203baf6f0ef88e96b3fb4909ac68d0e6f8"
)
B_ORIGINAL_PATCHER_SHA256 = (
    "7ef8aac01b50b8e8e0a9a5be9aab1cc7254656b55223695abb7e36587b254864"
)
B_SHIM_PATCHER_SHA256 = (
    "2b58d3b48d5d1f7162ab945ba88c1957cdf4aae28f03d2583f5d8143f2cb9363"
)


class Issue65RuntimeError(RuntimeError):
    """A bounded deployment or evidence operation could not be completed."""


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


class _DurableEventList(list[dict[str, object]]):
    """Manifest event summary backed by an append-only events.ndjson journal."""

    def __init__(
        self,
        journal: Path,
        initial: Sequence[dict[str, object]] = (),
    ) -> None:
        super().__init__()
        self._journal = Path(journal)
        self._lock = threading.Lock()
        for record in initial:
            self.append(record)

    def append(self, record: dict[str, object]) -> None:
        with self._lock:
            _append_ndjson_record(self._journal, record)
            super().append(record)


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
                raise Issue65RuntimeError(
                    f"control file is not valid JSON: {path}"
                ) from error
        if not isinstance(current, dict):
            raise Issue65RuntimeError("control file must contain one JSON object")
        merged = {
            key: value for key, value in current.items()
            if key in ("stopReason", "statusReachableAt")
            and isinstance(value, str)
        }
        if status_reachable_at is not _UNSET:
            if not isinstance(status_reachable_at, str) or not status_reachable_at:
                raise Issue65RuntimeError("statusReachableAt must be a timestamp string")
            merged["statusReachableAt"] = status_reachable_at
        if stop_reason is not _UNSET and "stopReason" not in merged:
            if stop_reason not in STOP_REASONS:
                raise Issue65RuntimeError(
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
                raise Issue65RuntimeError(
                    f"control file is not valid JSON: {self.control_path}"
                ) from error
            existing = current.get("stopReason") if isinstance(current, dict) else None
            if existing is not None:
                if existing not in STOP_REASONS:
                    raise Issue65RuntimeError(
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
            raise Issue65RuntimeError(
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
        raise Issue65RuntimeError(f"unsupported serial baud rate: {baud}")
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
        bundle: EvidenceBundle,
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
            name="issue65-serial",
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
            raise Issue65RuntimeError("serial watcher did not stop within deadline")
        if self.error is not None:
            raise Issue65RuntimeError(f"serial watcher failed: {self.error}")

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
                raise Issue65RuntimeError(
                    f"{url} returned HTTP {response.status}"
                )
            body = response.read()
    except (
        urllib_error.URLError,
        TimeoutError,
        OSError,
    ) as error:
        raise Issue65RuntimeError(f"{url} is unreachable: {error}") from error
    try:
        value = json.loads(body)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise Issue65RuntimeError(f"{url} returned invalid JSON") from error
    if not isinstance(value, dict):
        raise Issue65RuntimeError(f"{url} must return a JSON object")
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
        bundle: EvidenceBundle,
        arbiter: StopArbiter,
    ) -> None:
        self.controller = controller
        self.bundle = bundle
        self.arbiter = arbiter
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run,
            name="issue65-network-monitor",
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
            raise Issue65RuntimeError("network monitor did not stop within deadline")
        if self.error is not None:
            raise Issue65RuntimeError(f"network monitor failed: {self.error}")

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
        raise Issue65RuntimeError("timed out waiting for qualifying /api/status")

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
        with self._lock:
            if success:
                self.latest_ping_success_mono = time.monotonic()
            armed = self.armed
        if armed and not success:
            self.arbiter.request_stop(
                "loss of ICMP",
                source="ping",
                wallTime=record["wallTime"],
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
        except Issue65RuntimeError as request_error:
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
        raise Issue65RuntimeError(
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
            raise Issue65RuntimeError(f"required artifact is absent: {required}")
    try:
        identity = json.loads(generated_identity_source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise Issue65RuntimeError(
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
        raise Issue65RuntimeError("command argv must be a non-empty list of strings")
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
                raise Issue65RuntimeError(
                    f"{event} timed out after {timeout_seconds} seconds"
                ) from error
            except OSError as error:
                raise Issue65RuntimeError(
                    f"{event} could not start: {error}"
                ) from error
            finally:
                log.flush()
                os.fsync(log.fileno())
    except Issue65RuntimeError:
        raise
    except OSError as error:
        raise Issue65RuntimeError(
            f"{event} evidence logging failed: {error}"
        ) from error
    finally:
        events.append(timeline.record(
            f"{event}-finished",
            returnCode=return_code,
            timedOut=timed_out,
        ))
    if result is None:
        raise Issue65RuntimeError(f"{event} did not return a command result")
    if result.returncode:
        raise Issue65RuntimeError(
            f"{event} exited with return code {result.returncode}; see {artifact}"
        )
    return result


def classify_worktree_status(
    porcelain: str,
    allowed_paths: Sequence[str] = planner.VERSION_FILES,
) -> tuple[list[str], list[str]]:
    """Split porcelain-v1 entries into generated-version and rogue changes."""
    allowed_set = set(allowed_paths)
    allowed: list[str] = []
    rogue: list[str] = []
    for entry in porcelain.splitlines():
        if (
            len(entry) >= 4
            and entry[:2] != "??"
            and entry[3:] in allowed_set
        ):
            allowed.append(entry)
        else:
            rogue.append(entry)
    return allowed, rogue


def _runtime_binding(report: Mapping[str, Any]) -> dict[str, object]:
    plan = report["plannerPlan"]
    run = plan["run"]
    target = plan["target"]
    locked = planner.RUNS[run["id"]]
    expected_root = planner.EVIDENCE_ROOT / run["id"]
    expected_argv = future_execution_argv(
        run["id"], target["controller"], target["serialPort"],
    )
    if (
        run["role"] != locked.role
        or run["commit"] != locked.commit
        or plan["paths"]["worktree"] != locked.worktree
        or Path(plan["paths"]["evidenceDirectory"]) != expected_root
        or Path(plan["expectedEvidenceBundle"]["root"]) != expected_root
        or plan["failedAllocationEvidence"] != planner.failed_allocation_evidence()
        or report["futureExecutionArgv"] != expected_argv
    ):
        raise Issue65RuntimeError(
            "report commands, paths, or evidence do not match the locked planner run"
        )
    return {
        "run": {
            "id": run["id"],
            "role": run["role"],
            "commit": run["commit"],
        },
        "target": {
            "controller": target["controller"],
            "serialPort": target["serialPort"],
        },
        "failedAllocationEvidence": copy.deepcopy(
            plan["failedAllocationEvidence"]
        ),
    }


@dataclass
class EvidenceBundle:
    """Durably retain deployment state and abort evidence for one locked run."""

    root: Path
    timeline: Timeline
    manifest: dict[str, Any]
    outcome: dict[str, Any]
    _lock: threading.RLock = field(default_factory=threading.RLock, repr=False)

    @classmethod
    def create(
        cls,
        report: Mapping[str, Any],
        stage: str = "deployment-initialized",
    ) -> EvidenceBundle:
        plan = report["plannerPlan"]
        root = Path(plan["expectedEvidenceBundle"]["root"])
        if root != Path(plan["paths"]["evidenceDirectory"]):
            raise Issue65RuntimeError("planner evidence paths do not agree")
        binding = _runtime_binding(report)
        timeline = Timeline.start()
        created = timeline.record("evidence-bundle-created", stage=stage)
        common = {
            "schemaVersion": 1,
            "issue": planner.ISSUE,
            **binding,
            "stage": stage,
            "status": "IN_PROGRESS",
        }
        create_evidence_root(root)
        durable_events = _DurableEventList(root / "events.ndjson", [created])
        bundle = cls(
            root=root,
            timeline=timeline,
            manifest={
                **copy.deepcopy(common),
                "events": durable_events,
                "artifacts": {},
            },
            outcome={
                **copy.deepcopy(common),
                "primaryOutcome": "UNKNOWN",
                "recoveryFacts": [],
                "stopReasons": [],
            },
        )
        try:
            (root / "identity").mkdir()
            atomic_write_json(root / "manifest.json", bundle.manifest)
            atomic_write_json(root / "outcome.json", bundle.outcome)
        except BaseException as error:
            bundle.abort(error, stage="evidence-initialization-aborted")
            raise
        return bundle

    @property
    def events(self) -> list[dict[str, object]]:
        return self.manifest["events"]

    def update_stage(self, stage: str) -> None:
        with self._lock:
            self.manifest["stage"] = stage
            self.outcome["stage"] = stage
            self.events.append(self.timeline.record("stage", stage=stage))
            atomic_write_json(self.root / "manifest.json", self.manifest)
            atomic_write_json(self.root / "outcome.json", self.outcome)

    def record_failed_allocation(
        self,
        *,
        phase: str,
        raw_line: str,
        record: Mapping[str, object],
    ) -> None:
        """Persist positive evidence without claiming a global zero counter."""
        with self._lock:
            event = {
                "phase": phase,
                "rawLine": raw_line,
                "wallTime": record["wallTime"],
                "monotonicNs": record["monotonicNs"],
            }
            for target in (self.manifest, self.outcome):
                evidence = target["failedAllocationEvidence"]
                evidence["positiveEvents"].append(copy.deepcopy(event))
                if phase in evidence["observations"]:
                    evidence["observations"][phase] = "POSITIVE_EVIDENCE"
            atomic_write_json(self.root / "manifest.json", self.manifest)
            atomic_write_json(self.root / "outcome.json", self.outcome)

    def finalize(
        self,
        primary_outcome: str,
        *,
        stop_reason: str | None,
        recovery_facts: Sequence[str] = (),
        summary: Mapping[str, object] | None = None,
    ) -> None:
        if primary_outcome not in PRIMARY_OUTCOMES:
            raise Issue65RuntimeError("primary outcome is outside locked vocabulary")
        if any(fact != "Power-Cycle Recovery" for fact in recovery_facts):
            raise Issue65RuntimeError("recovery fact is outside locked vocabulary")
        with self._lock:
            final = self.timeline.record(
                "run-finished",
                primaryOutcome=primary_outcome,
                stopReason=stop_reason,
            )
            self.events.append(final)
            self.manifest.update(
                status="COMPLETE",
                stage="complete",
                primaryOutcome=primary_outcome,
            )
            self.outcome.update(
                status="COMPLETE",
                stage="complete",
                primaryOutcome=primary_outcome,
                recoveryFacts=list(recovery_facts),
                stopReasons=[stop_reason] if stop_reason else [],
            )
            if summary is not None:
                self.manifest["runSummary"] = copy.deepcopy(dict(summary))
                self.outcome["runSummary"] = copy.deepcopy(dict(summary))
            atomic_write_json(self.root / "manifest.json", self.manifest)
            atomic_write_json(self.root / "outcome.json", self.outcome)

    def abort(self, error: BaseException, *, stage: str) -> None:
        with self._lock:
            abort = {
                "type": type(error).__name__,
                "message": str(error),
                "timeline": self.timeline.record("aborted", stage=stage),
            }
            self.manifest.update(status="ABORTED", stage=stage, abort=abort)
            self.outcome.update(
                status="ABORTED", stage=stage, abort=copy.deepcopy(abort),
            )
            self.events.append(abort["timeline"])
            for name, value in (
                ("manifest.json", self.manifest),
                ("outcome.json", self.outcome),
            ):
                try:
                    atomic_write_json(self.root / name, value)
                except (OSError, TypeError, ValueError):
                    pass


def _assert_exact_worktree(worktree: Path, expected_commit: str) -> None:
    head_ok, head = git_query(worktree, "rev-parse", "HEAD")
    if not head_ok or head != expected_commit:
        raise Issue65RuntimeError(
            f"worktree HEAD must be exact locked commit {expected_commit}; "
            f"found {head}"
        )
    status_ok, porcelain = git_query(
        worktree, "status", "--porcelain", "--untracked-files=all",
    )
    if not status_ok:
        raise Issue65RuntimeError(f"could not inspect worktree status: {porcelain}")
    _, rogue = classify_worktree_status(porcelain)
    if rogue:
        raise Issue65RuntimeError(
            f"worktree has disallowed changes: {', '.join(rogue)}"
        )


def ensure_exact_worktree(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
) -> Path:
    """Create an absent detached tree or validate the existing locked tree."""
    binding = _runtime_binding(report)
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    expected_commit = binding["run"]["commit"]
    if not os.path.lexists(worktree):
        run_logged(
            list(report["futureExecutionArgv"]["worktreeAdd"]),
            cwd=planner.REPO_ROOT,
            timeout_seconds=60,
            artifact=bundle.root / "worktree.log",
            timeline=bundle.timeline,
            events=bundle.events,
            event="worktree-add",
        )
    elif not worktree.is_dir():
        raise Issue65RuntimeError(
            f"locked worktree path exists but is not a directory: {worktree}"
        )
    _assert_exact_worktree(worktree, str(expected_commit))
    return worktree


def _restore_generated_versions(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    log: Path,
    append: bool,
) -> None:
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    run_logged(
        list(
            report["futureExecutionArgv"][
                "generatedVersionRestoreBeforeEveryPio"
            ]
        ),
        cwd=worktree,
        timeout_seconds=30,
        artifact=log,
        timeline=bundle.timeline,
        events=bundle.events,
        event="generated-version-restore",
        append=append,
    )
    status_ok, porcelain = git_query(
        worktree, "status", "--porcelain", "--untracked-files=all",
    )
    if not status_ok or porcelain:
        raise Issue65RuntimeError(
            "generated-version restore did not leave the worktree clean: "
            + porcelain
        )


def _restore_a_historical_webresponses(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    before_event: str,
) -> None:
    """Recreate the generated dependency state from which A originally built.

    A's MSS-cap patch is not idempotent across separate PlatformIO hook
    invocations. Its historical build succeeded because the dependency cache
    already contained the immediately preceding commit's patch state. Restore
    only that generated WebResponses.cpp before each A PlatformIO command; the
    untouched A hook then applies its own final transformation.
    """
    run = report["plannerPlan"]["run"]
    if run["role"] != "A":
        return

    _assert_exact_worktree(A_PRIME_WORKTREE, A_PARENT_COMMIT)
    environment = planner.OTA_ENV
    relative = Path(
        ".pio/libdeps"
    ) / environment / "ESPAsyncWebServer/src/WebResponses.cpp"
    source = A_PRIME_WORKTREE / relative
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    destination = worktree / relative
    if not source.is_file() or not destination.is_file():
        raise Issue65RuntimeError(
            "A historical generated dependency is absent; recreate "
            f"{A_PRIME_WORKTREE} at {A_PARENT_COMMIT} and build "
            f"{environment} before retrying"
        )

    source_digest = sha256_file(source)
    destination_digest = sha256_file(destination)
    if source_digest != A_PRIME_WEBRESPONSES_SHA256:
        raise Issue65RuntimeError(
            "A historical generated dependency does not match the reviewed "
            f"parent state: {source_digest}"
        )
    if destination_digest not in (
        A_PRIME_WEBRESPONSES_SHA256,
        A_FINAL_WEBRESPONSES_SHA256,
    ):
        raise Issue65RuntimeError(
            "A generated dependency is neither the reviewed parent nor A "
            f"final state: {destination_digest}"
        )

    _replace_file_atomically(source, destination)

    record = bundle.timeline.record(
        "a-historical-generated-dependency-restored",
        beforeEvent=before_event,
        sourceCommit=A_PARENT_COMMIT,
        source=str(source),
        destination=str(destination),
        previousSha256=destination_digest,
        restoredSha256=source_digest,
    )
    bundle.events.append(record)
    fixture = bundle.manifest.setdefault("generatedBuildFixture", {})
    fixture.update({
        "roleAOnly": True,
        "sourceCommit": A_PARENT_COMMIT,
        "sourceWorktree": str(A_PRIME_WORKTREE),
        "webResponsesSha256": A_PRIME_WEBRESPONSES_SHA256,
        "restoredBeforeEveryPio": True,
    })
    atomic_write_json(bundle.root / "manifest.json", bundle.manifest)


def _replace_file_atomically(source: Path, destination: Path) -> None:
    """Atomically replace one file from a reviewed source and fsync it."""
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".issue65-replace.",
        suffix=destination.suffix,
        dir=destination.parent,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output_file:
            with source.open("rb") as input_file:
                shutil.copyfileobj(input_file, output_file)
            output_file.flush()
            os.fsync(output_file.fileno())
        os.replace(temporary, destination)
        _fsync_directory(destination.parent)
    finally:
        if temporary.exists():
            temporary.unlink()


@contextmanager
def _temporary_git_identity_wrapper(
    worktree: Path,
    *,
    scratch_directory: Path,
    expected_descriptor: str,
    expected_actual_descriptor: str | None = None,
):
    """Return locked clean identity for one exact build-time Git query.

    Temporary comparison-build changes must be visible to PlatformIO without
    adding ``-dirty`` to the fixed commit identity. A temporary PATH entry
    intercepts only the exact query made by ``extract_version.py`` and
    delegates every other Git invocation to the real executable.
    """
    worktree = Path(worktree)
    scratch_directory = Path(scratch_directory)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+_-]*", expected_descriptor):
        raise Issue65RuntimeError(
            f"invalid clean Git descriptor for build identity: {expected_descriptor!r}"
        )
    real_git = shutil.which("git")
    if not real_git:
        raise Issue65RuntimeError("real git executable is unavailable")
    real_git_path = Path(real_git).resolve()
    try:
        actual_descriptor = subprocess.check_output(
            [
                str(real_git_path),
                "describe",
                "--tags",
                "--always",
                "--long",
                "--dirty",
            ],
            cwd=worktree,
            stderr=subprocess.DEVNULL,
            timeout=GIT_TIMEOUT_SECONDS,
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        raise Issue65RuntimeError(
            "could not validate the shimmed Git descriptor"
        ) from error
    required_actual_descriptor = (
        expected_actual_descriptor
        if expected_actual_descriptor is not None
        else f"{expected_descriptor}-dirty"
    )
    if actual_descriptor != required_actual_descriptor:
        raise Issue65RuntimeError(
            "the build worktree does not match the authorized identity "
            f"fixture: expected {required_actual_descriptor}, "
            f"found {actual_descriptor}"
        )
    wrapper_directory = scratch_directory / "git-identity-bin"
    wrapper = wrapper_directory / "git"
    if wrapper_directory.exists():
        raise Issue65RuntimeError(
            f"temporary Git identity wrapper already exists: {wrapper_directory}"
        )
    wrapper_directory.mkdir()
    script = (
        "#!/bin/sh\n"
        "if [ \"$#\" -eq 5 ] && [ \"$1\" = \"describe\" ] && "
        "[ \"$2\" = \"--tags\" ] && [ \"$3\" = \"--always\" ] && "
        "[ \"$4\" = \"--long\" ] && [ \"$5\" = \"--dirty\" ]; then\n"
        f"  printf '%s\\n' {shlex.quote(expected_descriptor)}\n"
        "else\n"
        f"  exec {shlex.quote(str(real_git_path))} \"$@\"\n"
        "fi\n"
    )
    try:
        with wrapper.open("x", encoding="utf-8") as output_file:
            output_file.write(script)
            output_file.flush()
            os.fsync(output_file.fileno())
        wrapper.chmod(0o700)
        _fsync_directory(wrapper_directory)
        environment = os.environ.copy()
        environment["PATH"] = (
            str(wrapper_directory)
            + os.pathsep
            + environment.get("PATH", os.defpath)
        )
        yield environment
    finally:
        wrapper.unlink(missing_ok=True)
        wrapper_directory.rmdir()


def _restore_b_pristine_webresponses(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    before_event: str,
) -> None:
    """Restore pristine pinned vendor input before each exact-B PIO command.

    Exact B recognizes the final MSS-cap output in its zero-read stage, but
    its following diagnostics stage still rejects that already-correct final
    state on a repeated pre-build pass. Restoring the pinned vendor file keeps
    commit B untouched while recreating the input its first successful patch
    pass consumed.
    """
    run = report["plannerPlan"]["run"]
    if run["role"] != "B":
        return

    environment = planner.OTA_ENV
    relative = Path(
        ".pio/libdeps"
    ) / environment / "ESPAsyncWebServer/src/WebResponses.cpp"
    source = (
        B_PRIME_PACKAGE_ROOT
        / "ESPAsyncWebServer/src/WebResponses.cpp"
    )
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    destination = worktree / relative
    if not source.is_file() or not destination.is_file():
        raise Issue65RuntimeError(
            "B pristine generated dependency is absent; install "
            "ESP32Async/ESPAsyncWebServer@3.11.2 with --skip-dependencies "
            f"into {B_PRIME_PACKAGE_ROOT} before retrying"
        )

    source_digest = sha256_file(source)
    destination_digest = sha256_file(destination)
    if source_digest != B_PRIME_WEBRESPONSES_SHA256:
        raise Issue65RuntimeError(
            "B pristine generated dependency does not match the reviewed "
            f"PlatformIO Registry state: {source_digest}"
        )
    if destination_digest not in (
        B_PRIME_WEBRESPONSES_SHA256,
        A_FINAL_WEBRESPONSES_SHA256,
    ):
        raise Issue65RuntimeError(
            "B generated dependency is neither the reviewed pristine nor "
            f"final state: {destination_digest}"
        )

    _replace_file_atomically(source, destination)

    record = bundle.timeline.record(
        "b-pristine-generated-dependency-restored",
        beforeEvent=before_event,
        source=str(source),
        destination=str(destination),
        previousSha256=destination_digest,
        restoredSha256=source_digest,
    )
    bundle.events.append(record)
    fixture = bundle.manifest.setdefault("generatedBuildFixture", {})
    fixture.update({
        "roleBOnly": True,
        "sourcePackage": "ESP32Async/ESPAsyncWebServer@3.11.2",
        "sourcePackageRoot": str(B_PRIME_PACKAGE_ROOT),
        "webResponsesSha256": B_PRIME_WEBRESPONSES_SHA256,
        "restoredBeforeEveryPio": True,
    })
    atomic_write_json(bundle.root / "manifest.json", bundle.manifest)


@contextmanager
def _temporary_b_build_shim(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    before_event: str,
):
    """Apply B's authorized recognition-only build shim, then restore it."""
    run = report["plannerPlan"]["run"]
    if run["role"] != "B":
        yield None
        return

    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    source = planner.REPO_ROOT / "tools/patch_async_sse.py"
    destination = worktree / "tools/patch_async_sse.py"
    backup_directory = bundle.root / "generated-build-fixture"
    backup_directory.mkdir(exist_ok=True)
    backup = backup_directory / f"{before_event}-original-patch_async_sse.py"

    if not source.is_file() or not destination.is_file():
        raise Issue65RuntimeError(
            "B build shim source or exact-B patcher is absent"
        )
    if backup.exists():
        raise Issue65RuntimeError(
            f"B build shim backup already exists: {backup}"
        )

    source_digest = sha256_file(source)
    destination_digest = sha256_file(destination)
    if source_digest != B_SHIM_PATCHER_SHA256:
        raise Issue65RuntimeError(
            "B build shim does not match the authorized recognition-only "
            f"state: {source_digest}"
        )
    if destination_digest != B_ORIGINAL_PATCHER_SHA256:
        raise Issue65RuntimeError(
            "exact B patcher does not match the locked original state: "
            f"{destination_digest}"
        )
    descriptor_ok, clean_descriptor = git_query(
        worktree,
        "describe",
        "--tags",
        "--always",
        "--long",
        "--dirty",
    )
    if (
        not descriptor_ok
        or not clean_descriptor
        or clean_descriptor.endswith("-dirty")
    ):
        raise Issue65RuntimeError(
            "exact B worktree did not have a clean Git descriptor before "
            f"the temporary build shim: {clean_descriptor}"
        )

    _replace_file_atomically(destination, backup)
    shim_record: dict[str, object] | None = None
    try:
        _replace_file_atomically(source, destination)
        if sha256_file(destination) != B_SHIM_PATCHER_SHA256:
            raise Issue65RuntimeError(
                "B build shim replacement did not persist"
            )

        applied = bundle.timeline.record(
            "b-build-idempotency-shim-applied",
            beforeEvent=before_event,
            source=str(source),
            destination=str(destination),
            backup=str(backup),
            originalSha256=destination_digest,
            shimSha256=source_digest,
        )
        bundle.events.append(applied)
        shim_record = {
            "beforeEvent": before_event,
            "authorization": "explicit operator approval 2026-08-04",
            "scope": "build-hook idempotency recognition only",
            "originalSha256": B_ORIGINAL_PATCHER_SHA256,
            "shimSha256": B_SHIM_PATCHER_SHA256,
            "backup": str(backup),
            "restored": False,
        }
        bundle.manifest.setdefault(
            "temporaryBuildShims", []
        ).append(shim_record)
        atomic_write_json(bundle.root / "manifest.json", bundle.manifest)
        with _temporary_git_identity_wrapper(
            worktree,
            scratch_directory=backup_directory,
            expected_descriptor=clean_descriptor,
        ) as environment:
            applied["gitIdentityWrapper"] = environment["PATH"].split(
                os.pathsep, 1,
            )[0]
            applied["cleanGitDescriptor"] = clean_descriptor
            atomic_write_json(bundle.root / "manifest.json", bundle.manifest)
            yield environment
    finally:
        _replace_file_atomically(backup, destination)
        restored_digest = sha256_file(destination)
        status_ok, patcher_status = git_query(
            worktree,
            "status",
            "--porcelain",
            "--",
            "tools/patch_async_sse.py",
        )
        if (
            restored_digest != B_ORIGINAL_PATCHER_SHA256
            or not status_ok
            or patcher_status
        ):
            raise Issue65RuntimeError(
                "exact B patcher was not cleanly restored after the "
                f"temporary build shim: sha256={restored_digest} "
                f"status={patcher_status}"
            )
        if shim_record is not None:
            shim_record["restored"] = True
        restored = bundle.timeline.record(
            "b-build-idempotency-shim-restored",
            beforeEvent=before_event,
            destination=str(destination),
            restoredSha256=restored_digest,
            trackedFileClean=True,
        )
        bundle.events.append(restored)
        atomic_write_json(bundle.root / "manifest.json", bundle.manifest)


@contextmanager
def _temporary_build_environment(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    before_event: str,
):
    """Provide the locked build environment required by B and rollback runs."""
    run = report["plannerPlan"]["run"]
    if run["role"] == "B":
        with _temporary_b_build_shim(
            report,
            bundle,
            before_event=before_event,
        ) as environment:
            yield environment
        return
    if run["role"] != "R":
        yield None
        return

    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    descriptor_ok, clean_descriptor = git_query(
        worktree,
        "describe",
        "--tags",
        "--always",
        "--long",
        "--dirty",
    )
    if (
        not descriptor_ok
        or not clean_descriptor
        or clean_descriptor.endswith("-dirty")
    ):
        raise Issue65RuntimeError(
            "rollback worktree did not have a clean Git descriptor before "
            f"{before_event}: {clean_descriptor}"
        )

    record = {
        "beforeEvent": before_event,
        "scope": "generated-version identity stability across nested builds",
        "cleanGitDescriptor": clean_descriptor,
        "removed": False,
    }
    bundle.manifest.setdefault(
        "temporaryBuildIdentityWrappers", []
    ).append(record)
    atomic_write_json(bundle.root / "manifest.json", bundle.manifest)
    try:
        with _temporary_git_identity_wrapper(
            worktree,
            scratch_directory=bundle.root,
            expected_descriptor=clean_descriptor,
            expected_actual_descriptor=clean_descriptor,
        ) as environment:
            record["path"] = environment["PATH"].split(os.pathsep, 1)[0]
            atomic_write_json(bundle.root / "manifest.json", bundle.manifest)
            yield environment
    finally:
        record["removed"] = True
        atomic_write_json(bundle.root / "manifest.json", bundle.manifest)


def _verify_b_final_webresponses(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    after_event: str,
) -> None:
    """Require exact B to finish each PIO command at reviewed final output."""
    run = report["plannerPlan"]["run"]
    if run["role"] != "B":
        return

    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    generated = (
        worktree
        / ".pio/libdeps"
        / planner.OTA_ENV
        / "ESPAsyncWebServer/src/WebResponses.cpp"
    )
    if not generated.is_file():
        raise Issue65RuntimeError(
            "B generated WebResponses.cpp is absent after "
            f"{after_event}"
        )
    generated_digest = sha256_file(generated)
    if generated_digest != A_FINAL_WEBRESPONSES_SHA256:
        raise Issue65RuntimeError(
            "B generated WebResponses.cpp did not finish at the reviewed "
            f"final state after {after_event}: {generated_digest}"
        )
    verified = bundle.timeline.record(
        "b-generated-dependency-final-state-verified",
        afterEvent=after_event,
        path=str(generated),
        sha256=generated_digest,
    )
    bundle.events.append(verified)
    atomic_write_json(bundle.root / "manifest.json", bundle.manifest)


def _identity_contract(
    report: Mapping[str, Any],
    name: str,
) -> dict[str, Path]:
    contract = report["plannerPlan"]["expectedEvidenceBundle"][
        "artifactIdentity"
    ][name]
    return {
        "source": Path(contract["generatedIdentitySource"]),
        "image": Path(contract["image"]),
        "identity": Path(contract["identityArtifact"]),
        "digest": Path(contract["digestArtifact"]),
    }


def _require_artifacts(paths: Sequence[Path]) -> None:
    missing = [str(path) for path in paths if not Path(path).is_file()]
    if missing:
        raise Issue65RuntimeError(
            f"deployment evidence is incomplete: {', '.join(missing)}"
        )


def deploy_pair(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    build_timeout_seconds: float = 600,
    upload_timeout_seconds: float = 180,
) -> dict[str, object]:
    """Build and deploy the locked firmware/filesystem pair in planner order."""
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    build_log = bundle.root / "build.log"
    uploadfs_log = bundle.root / "uploadfs.log"
    firmware_upload_log = bundle.root / "firmware-upload.log"
    stage = "deployment-started"
    try:
        bundle.update_stage(stage)
        ensure_exact_worktree(report, bundle)

        stage = "firmware-build"
        bundle.update_stage(stage)
        _restore_generated_versions(
            report, bundle, log=build_log, append=False,
        )
        _restore_a_historical_webresponses(
            report, bundle, before_event="firmware-build",
        )
        _restore_b_pristine_webresponses(
            report, bundle, before_event="firmware-build",
        )
        with _temporary_build_environment(
            report, bundle, before_event="firmware-build",
        ) as build_environment:
            run_logged(
                list(report["futureExecutionArgv"]["buildFirmware"]),
                cwd=worktree,
                timeout_seconds=build_timeout_seconds,
                artifact=build_log,
                timeline=bundle.timeline,
                events=bundle.events,
                event="firmware-build",
                append=True,
                environment=build_environment,
            )
        _verify_b_final_webresponses(
            report, bundle, after_event="firmware-build",
        )
        firmware_contract = _identity_contract(report, "firmware")
        firmware = capture_artifact_identity(
            firmware_contract["source"],
            firmware_contract["image"],
            firmware_contract["identity"],
            firmware_contract["digest"],
        )

        stage = "filesystem-upload"
        bundle.update_stage(stage)
        _restore_generated_versions(
            report, bundle, log=uploadfs_log, append=False,
        )
        _restore_a_historical_webresponses(
            report, bundle, before_event="filesystem-upload",
        )
        _restore_b_pristine_webresponses(
            report, bundle, before_event="filesystem-upload",
        )
        with _temporary_build_environment(
            report, bundle, before_event="filesystem-upload",
        ) as build_environment:
            run_logged(
                list(report["futureExecutionArgv"]["uploadFilesystem"]),
                cwd=worktree,
                timeout_seconds=upload_timeout_seconds,
                artifact=uploadfs_log,
                timeline=bundle.timeline,
                events=bundle.events,
                event="filesystem-upload",
                append=True,
                environment=build_environment,
            )
        _verify_b_final_webresponses(
            report, bundle, after_event="filesystem-upload",
        )
        filesystem_contract = _identity_contract(report, "filesystem")
        filesystem = capture_artifact_identity(
            filesystem_contract["source"],
            filesystem_contract["image"],
            filesystem_contract["identity"],
            filesystem_contract["digest"],
        )

        stage = "firmware-upload"
        bundle.update_stage(stage)
        _restore_generated_versions(
            report, bundle, log=uploadfs_log, append=True,
        )
        if sha256_file(firmware_contract["image"]) != firmware["sha256"]:
            raise Issue65RuntimeError(
                "prebuilt firmware changed after identity capture"
            )
        run_logged(
            list(report["futureExecutionArgv"]["uploadPrebuiltFirmware"]),
            cwd=worktree,
            timeout_seconds=upload_timeout_seconds,
            artifact=firmware_upload_log,
            timeline=bundle.timeline,
            events=bundle.events,
            event="firmware-upload",
        )

        required = [
            build_log,
            uploadfs_log,
            firmware_upload_log,
            firmware_contract["identity"],
            firmware_contract["digest"],
            filesystem_contract["identity"],
            filesystem_contract["digest"],
        ]
        _require_artifacts(required)
        bundle.manifest["artifacts"]["firmware"] = firmware
        bundle.manifest["artifacts"]["filesystem"] = filesystem
        bundle.update_stage("deployment-complete")
        return {"firmware": firmware, "filesystem": filesystem}
    except BaseException as error:
        bundle.abort(error, stage=f"{stage}-aborted")
        raise


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


def _prompt_exact(prompt: str, expected: str) -> None:
    response = input(prompt).strip()
    if response != expected:
        raise Issue65RuntimeError(
            f"operator confirmation must be exactly {expected!r}; got {response!r}"
        )


def _persist_live_identity(
    bundle: EvidenceBundle,
    status: Mapping[str, object],
    identity: Mapping[str, object],
    controller: str,
    serial_port: str,
) -> dict[str, object]:
    serial_path = Path(serial_port)
    record = {
        "controller": controller,
        "controllerUrl": f"http://{controller}",
        "serialPort": serial_port,
        "serialResolvedPath": (
            str(serial_path.resolve()) if serial_path.exists() else None
        ),
        "identity": copy.deepcopy(dict(identity)),
        "statusIdentity": {
            key: status.get(key)
            for key in (
                "firmwareVersion",
                "fsVersion",
                "resetReason",
                "uptimeMs",
            )
        },
    }
    with bundle._lock:
        bundle.manifest["liveIdentity"] = copy.deepcopy(record)
        bundle.outcome["liveIdentity"] = copy.deepcopy(record)
        atomic_write_json(bundle.root / "manifest.json", bundle.manifest)
        atomic_write_json(bundle.root / "outcome.json", bundle.outcome)
    return record


def _run_browser_capture(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    arbiter: StopArbiter,
) -> tuple[int, dict[str, object] | None]:
    argv = list(report["futureExecutionArgv"]["browserCapture"])
    log_path = bundle.root / "browser.log"
    bundle.events.append(bundle.timeline.record(
        "browser-capture-started",
        argv=argv,
    ))
    process: subprocess.Popen[bytes] | None = None
    return_code = -1
    try:
        with log_path.open("xb") as log:
            process = subprocess.Popen(
                argv,
                cwd=planner.REPO_ROOT,
                stdout=log,
                stderr=subprocess.STDOUT,
                shell=False,
            )
            deadline = time.monotonic() + 35.0
            while process.poll() is None and time.monotonic() < deadline:
                if arbiter.stopped:
                    # The collector reads the first-wins stop from control.json
                    # and performs its own bounded artifact capture/close.
                    pass
                time.sleep(0.1)
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
                raise Issue65RuntimeError(
                    "browser collector exceeded its 35-second ownership deadline"
                )
            return_code = int(process.returncode)
            log.flush()
            os.fsync(log.fileno())
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=2)
        bundle.events.append(bundle.timeline.record(
            "browser-capture-finished",
            returnCode=return_code,
        ))
    state_path = bundle.root / "browser" / "page-state.json"
    state: dict[str, object] | None = None
    if state_path.is_file():
        try:
            loaded = json.loads(state_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            raise Issue65RuntimeError(
                f"browser page state is invalid: {error}"
            ) from error
        if isinstance(loaded, dict):
            state = loaded
    if return_code == 2:
        raise Issue65RuntimeError(
            "browser collector reported an evidence-artifact failure"
        )
    if return_code not in (0, 3, 4):
        raise Issue65RuntimeError(
            f"browser collector exited unexpectedly with {return_code}"
        )
    return return_code, state


def _attempt_power_cycle_recovery(
    controller: str,
    bundle: EvidenceBundle,
) -> bool:
    print(
        "\nHTTP Blackout is confirmed. Browser and status-monitor traffic are stopped.\n"
        "Physically unplug USB power, wait until fully unpowered, reconnect it,\n"
        "then type RECOVER and press Enter.",
        flush=True,
    )
    _prompt_exact("Recovery confirmation: ", "RECOVER")
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline:
        try:
            status = _http_json(
                f"http://{controller}/api/status",
                STATUS_DEADLINE_SECONDS,
            )
            success = True
            error = None
        except Issue65RuntimeError as request_error:
            status = None
            success = False
            error = str(request_error)
        append_ndjson(
            bundle.root / "recovery-status.ndjson",
            bundle.timeline,
            "recovery-status-sample",
            success=success,
            error=error,
            status=status,
        )
        if success:
            return True
        time.sleep(2.0)
    return False


def _finalize_early_stop(
    bundle: EvidenceBundle,
    arbiter: StopArbiter,
    run_id: str,
    phase: str,
) -> int:
    primary = classify_primary_outcome(arbiter.reason, None, False)
    bundle.finalize(
        primary,
        stop_reason=arbiter.reason,
        summary={"stoppedBeforeBrowser": True, "phase": phase},
    )
    print(json.dumps({
        "run": run_id,
        "primaryOutcome": primary,
        "stopReason": arbiter.reason,
        "evidence": str(bundle.root),
    }, indent=2), flush=True)
    return 0


def execute_run(report: Mapping[str, Any]) -> int:
    plan = report["plannerPlan"]
    run_id = str(plan["run"]["id"])
    controller = str(plan["target"]["controller"])
    serial_port = str(plan["target"]["serialPort"])
    bundle: EvidenceBundle | None = None
    monitor: MonitorLoop | None = None
    serial: SerialWatcher | None = None
    arbiter: StopArbiter | None = None
    phase_lock = threading.Lock()
    phase_value = "deployment"

    def current_phase() -> str:
        with phase_lock:
            return phase_value

    def set_phase(value: str) -> None:
        nonlocal phase_value
        with phase_lock:
            phase_value = value
        if monitor is not None:
            monitor.set_phase(value)
        if bundle is not None:
            bundle.update_stage(value)

    try:
        bundle = EvidenceBundle.create(report)
        deployment = deploy_pair(report, bundle)
        arbiter = StopArbiter(
            bundle.root / "control.json",
            bundle.timeline,
            bundle.events,
        )
        monitor = MonitorLoop(controller, bundle, arbiter)
        serial = SerialWatcher(
            Path(serial_port),
            bundle,
            arbiter,
            current_phase,
        )
        set_phase("awaiting-physical-cycle")
        serial.start()
        monitor.start()
        if not _wait_for(lambda: serial.connected.is_set(), 5.0):
            raise Issue65RuntimeError(
                f"serial watcher could not attach to {serial_port}"
            )

        print(
            "\nDeployment completed. The fixed live timeline is running.\n"
            "Now physically unplug the controller USB power cable, wait until the\n"
            "controller is fully unpowered, reconnect the same cable, then type\n"
            f"CYCLED {run_id} and press Enter.",
            flush=True,
        )
        _prompt_exact("Physical-cycle confirmation: ", f"CYCLED {run_id}")
        if not _wait_for(
            lambda: serial.disconnected_after_connect.is_set(),
            10.0,
            arbiter=arbiter,
        ):
            if arbiter.stopped:
                return _finalize_early_stop(
                    bundle, arbiter, run_id, "physical-cycle",
                )
            raise Issue65RuntimeError(
                "serial evidence did not observe USB disappearance during power cycle"
            )
        if not _wait_for(
            lambda: serial.reconnected.is_set(),
            20.0,
            arbiter=arbiter,
        ):
            if arbiter.stopped:
                return _finalize_early_stop(
                    bundle, arbiter, run_id, "physical-cycle",
                )
            raise Issue65RuntimeError(
                "serial evidence did not observe USB re-enumeration after power cycle"
            )
        if arbiter.stopped:
            return _finalize_early_stop(
                bundle,
                arbiter,
                run_id,
                "physical-cycle",
            )

        expected_firmware = deployment["firmware"]["generatedIdentity"][
            "firmwareVersion"
        ]
        expected_filesystem = deployment["filesystem"]["generatedIdentity"][
            "fsVersion"
        ]
        try:
            fresh_status = monitor.wait_for_status(
                lambda status: (
                    status.get("firmwareVersion") == expected_firmware
                    and status.get("fsVersion") == expected_filesystem
                    and status.get("resetReason") == "POWERON"
                ),
                60.0,
            )
        except Issue65RuntimeError:
            if arbiter.stopped:
                return _finalize_early_stop(
                    bundle,
                    arbiter,
                    run_id,
                    "identity-verification",
                )
            raise
        identity = _http_json(
            f"http://{controller}/api/identity",
            STATUS_DEADLINE_SECONDS,
        )
        mismatches = identity_mismatches(
            fresh_status,
            identity,
            deployment,
            controller,
        )
        if mismatches:
            raise Issue65RuntimeError(
                "deployed identity verification failed: " + "; ".join(mismatches)
            )
        _persist_live_identity(
            bundle,
            fresh_status,
            identity,
            controller,
            serial_port,
        )
        monitor.arm()

        set_phase("settle")
        print("Matched deployment verified; beginning fixed 90-second settle.", flush=True)
        settle_deadline = time.monotonic() + 90.0
        while time.monotonic() < settle_deadline and not arbiter.stopped:
            time.sleep(min(0.25, settle_deadline - time.monotonic()))

        browser_state: dict[str, object] | None = None
        if not arbiter.stopped:
            snapshot = monitor.snapshot()
            baseline_status = snapshot["latestStatus"]
            if not isinstance(baseline_status, dict):
                raise Issue65RuntimeError("no status baseline exists before browser load")
            baseline_heap = baseline_status.get("heapLargest8bit")
            if not isinstance(baseline_heap, int):
                raise Issue65RuntimeError(
                    "pre-load status has no integer heapLargest8bit"
                )
            with bundle._lock:
                bundle.manifest["preLoadStatus"] = copy.deepcopy(baseline_status)
                bundle.outcome["preLoadStatus"] = copy.deepcopy(baseline_status)
            set_phase("browser")
            print("Starting the one visible /wifi.html browser load.", flush=True)
            _return_code, browser_state = _run_browser_capture(
                report,
                bundle,
                arbiter,
            )

            # If the browser failed while status loss is in progress, retain the
            # browser phase just long enough to satisfy or falsify the exact
            # 30-second HTTP Blackout definition.
            loss_started = monitor.snapshot()["statusLossStartedMonotonic"]
            if isinstance(loss_started, float) and not arbiter.stopped:
                remaining = max(
                    0.0,
                    HTTP_BLACKOUT_SECONDS - (time.monotonic() - loss_started) + 0.5,
                )
                _wait_for(
                    lambda: (
                        arbiter.stopped
                        or monitor.snapshot()["statusLossStartedMonotonic"] is None
                    ),
                    min(remaining, 6.0),
                )

            cooldown: dict[str, object] | None = None
            if not arbiter.stopped:
                set_phase("cooldown")
                cooldown_deadline = time.monotonic() + COOLDOWN_SECONDS
                while time.monotonic() < cooldown_deadline and not arbiter.stopped:
                    time.sleep(min(0.25, cooldown_deadline - time.monotonic()))
                cooldown_samples = monitor.snapshot()["cooldownSamples"]
                if isinstance(cooldown_samples, list):
                    cooldown = evaluate_cooldown(cooldown_samples, baseline_heap)
                    for sample in cooldown_samples:
                        _append_ndjson_record(
                            bundle.root / "cooldown-status.ndjson",
                            sample["record"],
                        )
            else:
                cooldown = None
        else:
            baseline_status = None
            baseline_heap = None
            cooldown = None

        snapshot = monitor.snapshot()
        last_success = snapshot["latestStatusSuccessMonotonic"]
        status_reachable = (
            isinstance(last_success, float)
            and time.monotonic() - last_success <= STATUS_INTERVAL_SECONDS + 2.0
        )
        primary = classify_primary_outcome(
            arbiter.reason,
            browser_state,
            status_reachable,
        )
        recovery_facts: list[str] = []
        if arbiter.reason == "HTTP Blackout":
            monitor.stop_status_requests()
            monitor.stop()
            monitor.join()
            monitor = None
            if _attempt_power_cycle_recovery(controller, bundle):
                recovery_facts.append("Power-Cycle Recovery")

        summary = {
            "browser": copy.deepcopy(browser_state),
            "finalStatus": copy.deepcopy(snapshot["latestStatus"]),
            "cooldown": copy.deepcopy(cooldown),
            "statusReachableAtEnd": status_reachable,
        }
        bundle.finalize(
            primary,
            stop_reason=arbiter.reason,
            recovery_facts=recovery_facts,
            summary=summary,
        )
        print(
            json.dumps({
                "run": run_id,
                "primaryOutcome": primary,
                "stopReason": arbiter.reason,
                "recoveryFacts": recovery_facts,
                "evidence": str(bundle.root),
            }, indent=2),
            flush=True,
        )
        return 0
    except KeyboardInterrupt as error:
        if arbiter is not None:
            try:
                arbiter.request_stop("operator interrupt", source="keyboard")
            except BaseException:
                pass
        if bundle is not None:
            bundle.abort(error, stage=f"{current_phase()}-interrupted")
        print("\nERROR: operator interrupt", file=sys.stderr)
        return 130
    except BaseException as error:
        if bundle is not None and bundle.manifest.get("status") != "ABORTED":
            bundle.abort(error, stage=f"{current_phase()}-aborted")
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    finally:
        cleanup_errors: list[str] = []
        if monitor is not None:
            monitor.stop()
            try:
                monitor.join()
            except BaseException as error:
                cleanup_errors.append(str(error))
        if serial is not None:
            serial.stop()
            try:
                serial.join()
            except BaseException as error:
                cleanup_errors.append(str(error))
        if cleanup_errors:
            print(
                "WARNING: cleanup: " + "; ".join(cleanup_errors),
                file=sys.stderr,
            )


def build_parser() -> planner.PlannerArgumentParser:
    parser = planner.PlannerArgumentParser(
        description=(
            "Report admission state or execute one locked Issue #65 live A/B run."
        )
    )
    parser.add_argument("--run", required=True, choices=tuple(planner.RUNS))
    parser.add_argument("--controller", default=planner.DEFAULT_CONTROLLER)
    parser.add_argument("--serial-port", default=planner.DEFAULT_SERIAL_PORT)
    parser.add_argument("--execute", action="store_true")
    return parser


def check(
    name: str, subject: str, passed: bool, failure: str, detail: str,
) -> dict[str, str]:
    return {
        "name": name,
        "subject": subject,
        "status": "PASS" if passed else "BLOCKED",
        "failure": "" if passed else failure,
        "detail": detail,
    }


def ready_create_check(name: str, subject: str) -> dict[str, str]:
    return {
        "name": name,
        "subject": subject,
        "status": "READY_CREATE",
        "failure": "",
        "detail": "detached worktree is absent and may be created by a future executor",
    }


def git_query(worktree: Path, *arguments: str) -> tuple[bool, str]:
    """Run one bounded, read-only git query without a shell."""
    try:
        result = subprocess.run(
            ["git", "-C", str(worktree), *arguments],
            capture_output=True,
            check=False,
            text=True,
            timeout=GIT_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        return False, "git query timed out"
    except OSError as error:
        return False, f"git query unavailable: {error}"
    if result.returncode:
        return False, result.stderr.strip() or "git query failed"
    return True, result.stdout.strip()


def command_checks() -> list[dict[str, str]]:
    checks = []
    for name in REQUIRED_COMMANDS:
        resolved = shutil.which(name)
        checks.append(check(
            "command-available", name, resolved is not None,
            "COMMAND_UNAVAILABLE", resolved or f"{name} was not found on PATH",
        ))
    return checks


def worktree_checks(
    worktree: Path, expected_commit: str,
) -> list[dict[str, str]]:
    if not worktree.is_dir():
        return [
            ready_create_check(name, str(worktree))
            for name in ("worktree-exact-commit", "worktree-clean")
        ]

    head_ok, head = git_query(worktree, "rev-parse", "HEAD")
    exact = head_ok and head == expected_commit
    exact_detail = (
        f"HEAD is exact locked commit {expected_commit}" if exact
        else f"expected {expected_commit}, found {head}"
        if head_ok else head
    )
    status_ok, porcelain = git_query(
        worktree, "status", "--porcelain", "--untracked-files=all",
    )
    entries = porcelain.splitlines() if status_ok else []
    allowed_paths = set(planner.VERSION_FILES)
    disallowed = [
        entry for entry in entries
        if len(entry) < 4 or entry[:2] == "??" or entry[3:] not in allowed_paths
    ]
    admissible = status_ok and not disallowed
    admissible_detail = (
        porcelain if not status_ok
        else "worktree has no tracked or untracked changes" if not entries
        else "only tracked generated version JSON files are dirty"
        if admissible
        else f"disallowed worktree changes: {', '.join(disallowed)}"
    )
    return [
        check(
            "worktree-exact-commit", str(worktree), exact,
            "WORKTREE_COMMIT_MISMATCH", exact_detail,
        ),
        check(
            "worktree-clean", str(worktree), admissible,
            "WORKTREE_DIRTY", admissible_detail,
        ),
    ]


def validate_prior_outcome(
    outcome: object,
    expected_run: str,
    terminal_outcomes: frozenset[str],
) -> tuple[bool, str]:
    """Validate the locked terminal-outcome schema used for run ordering."""
    if not isinstance(outcome, dict):
        return False, "prior outcome must be a JSON object"
    run = outcome.get("run")
    if not isinstance(run, dict):
        return False, "prior outcome requires top-level run object"
    if run.get("id") != expected_run:
        return False, f"prior outcome run.id must be {expected_run}"
    expected_commit = planner.RUNS[expected_run].commit
    if run.get("commit") != expected_commit:
        return False, f"prior outcome run.commit must be {expected_commit}"
    primary = outcome.get("primaryOutcome")
    if not isinstance(primary, str) or primary not in terminal_outcomes:
        return False, "primaryOutcome must be a terminal planner outcome"
    return True, (
        f"terminal {expected_run} outcome matches locked commit and planner "
        "vocabulary"
    )


def prior_outcome_checks(
    run_id: str,
    terminal_outcomes: frozenset[str],
    evidence_root: Path = planner.EVIDENCE_ROOT,
) -> list[dict[str, str]]:
    checks = []
    for prior_run in planner.RUNS[run_id].required_completed_runs:
        artifact = evidence_root / prior_run / "outcome.json"
        valid = False
        detail = "required prior outcome artifact is absent"
        if artifact.is_file():
            try:
                outcome = json.loads(artifact.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                detail = f"prior outcome artifact is invalid: {error}"
            else:
                valid, detail = validate_prior_outcome(
                    outcome, prior_run, terminal_outcomes,
                )
        checks.append(check(
            "prior-outcome-artifact", str(artifact), valid,
            "RUN_ORDER_BLOCKED", detail,
        ))
    return checks


def future_execution_argv(
    run_id: str, controller: str, serial_port: str,
) -> dict[str, list[str]]:
    """Return inert argv arrays for a future, separately reviewed executor."""
    locked = planner.RUNS[run_id]
    tree = locked.worktree
    evidence = planner.EVIDENCE_ROOT / run_id
    identity = evidence / "identity"
    environment = planner.OTA_ENV
    return {
        "worktreeAdd": [
            "git", "worktree", "add", "--detach", tree, locked.commit,
        ],
        "generatedVersionRestoreBeforeEveryPio": [
            "git", "-C", tree, "restore", f"--source={locked.commit}", "--",
            *planner.VERSION_FILES,
        ],
        "buildFirmware": [
            "pio", "run", "--project-dir", tree, "-e", environment,
        ],
        "prepareIdentityEvidence": ["mkdir", "-p", str(identity)],
        "copyFirmwareIdentity": [
            "cp", f"{tree}/data/fw-version.json",
            str(identity / "fw-version.json"),
        ],
        "hashFirmware": [
            "sha256sum", f"{tree}/.pio/build/{environment}/firmware.bin",
        ],
        "uploadFilesystem": [
            "pio", "run", "--project-dir", tree, "-e", environment,
            "-t", "uploadfs", "--upload-port", controller,
        ],
        "copyFilesystemIdentity": [
            "cp", f"{tree}/data/fs-version.json",
            str(identity / "fs-version.json"),
        ],
        "hashFilesystem": [
            "sha256sum", f"{tree}/.pio/build/{environment}/littlefs.bin",
        ],
        "uploadPrebuiltFirmware": [
            "python3", f"{tree}/tools/ota_upload.py", "--env", environment,
            "--host", controller, "--timeout", "60", "--transfer-timeout", "60",
            "--file", f"{tree}/.pio/build/{environment}/firmware.bin",
        ],
        "serialMonitor": [
            "python3", f"{tree}/tools/serial_monitor.py",
            "--port", serial_port, "--stream",
        ],
        "pingSample": ["ping", "-c", "1", "-W", "1", controller],
        "browserCapture": [
            "node", str(planner.BROWSER_COLLECTOR), "--run", run_id,
            "--url", f"http://{controller}/wifi.html",
            "--out", str(evidence / "browser"),
            "--control-file", str(evidence / "control.json"),
        ],
    }


def build_report(args: argparse.Namespace) -> dict[str, object]:
    controller = planner.validate_controller(args.controller)
    serial_port = planner.validate_serial_port(args.serial_port)
    locked_plan = planner.build_plan(argparse.Namespace(
        run=args.run,
        controller=controller,
        serial_port=serial_port,
        allow_b2=args.run == "B2",
        dry_run=True,
    ))
    locked = planner.RUNS[args.run]
    evidence = planner.EVIDENCE_ROOT / args.run
    local_checks = command_checks()
    allocation_contract = locked_plan["preflight"]["failedAllocationContract"]

    collector_exists = planner.BROWSER_COLLECTOR.is_file()
    local_checks.append(check(
        "browser-collector-exists", str(planner.BROWSER_COLLECTOR),
        collector_exists, "BROWSER_COLLECTOR_ABSENT",
        "browser collector is present" if collector_exists
        else "browser collector is absent",
    ))
    serial_exists = Path(serial_port).exists()
    local_checks.append(check(
        "serial-port-exists", serial_port, serial_exists, "SERIAL_PORT_ABSENT",
        "serial path is present" if serial_exists else "serial path is absent",
    ))
    evidence_absent = not evidence.exists()
    local_checks.append(check(
        "evidence-directory-absent", str(evidence), evidence_absent,
        "BLOCKED_NO_OVERWRITE",
        "evidence directory is absent" if evidence_absent
        else "evidence directory already exists",
    ))
    local_checks.extend(worktree_checks(Path(locked.worktree), locked.commit))
    terminal_outcomes = frozenset(
        outcome for outcome in locked_plan["vocabulary"]["outcomes"]
        if outcome != "UNKNOWN"
    )
    local_checks.extend(prior_outcome_checks(args.run, terminal_outcomes))
    local_checks.append(check(
        "failed-allocation-contract",
        allocation_contract["authorizationUrl"],
        allocation_contract["status"] == "AUTHORIZED",
        "FAILED_ALLOCATION_CONTRACT_UNAUTHORIZED",
        (
            "positive evidence remains a stop condition; global zero failed "
            "allocations cannot be inferred. "
            + locked_plan["failedAllocationEvidence"]["limitation"]
        ),
    ))

    blockers = [
        {
            "code": item["failure"], "check": item["name"],
            "subject": item["subject"], "detail": item["detail"],
        }
        for item in local_checks if item["status"] == "BLOCKED"
    ]
    if args.run == "B2":
        blockers.append({
            "code": "B2_NOT_ADMITTED",
            "check": "b2-admission",
            "subject": "B2",
            "detail": (
                "B2 requires post-A1/B1/A2 review and is never admitted by "
                "this runtime foundation"
            ),
        })

    return {
        "schemaVersion": 1,
        "issue": planner.ISSUE,
        "mode": "runtime-admission",
        "executeRequested": bool(args.execute),
        "sideEffects": {
            "writes": 0,
            "networkRequests": 0,
            "serialOpens": 0,
            "worktreeMutations": 0,
            "browserLaunches": 0,
            "builds": 0,
            "uploads": 0,
            "probes": 0,
        },
        "readyForExecute": False,
        "admissionPassed": not blockers,
        "plannerPlan": locked_plan,
        "failedAllocationContract": allocation_contract,
        "failedAllocationEvidence": locked_plan["failedAllocationEvidence"],
        "localChecks": local_checks,
        "blockers": blockers,
        "futureExecutionArgv": future_execution_argv(
            args.run, controller, serial_port,
        ),
    }


def main(argv: list[str]) -> int:
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
        report = build_report(args)
    except planner.PlanError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    if args.execute:
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            print(
                "ERROR: --execute requires an interactive TTY for operator gates",
                file=sys.stderr,
            )
            return 2
        if report["blockers"]:
            print(json.dumps({
                "error": "RUNTIME_ADMISSION_BLOCKED",
                "blockers": report["blockers"],
            }, indent=2), file=sys.stderr)
            return 2
        run_id = report["plannerPlan"]["run"]["id"]
        print(
            "\nThis will create the fixed evidence bundle, build and upload the\n"
            "matched firmware/filesystem pair, open serial, start the fixed ping\n"
            "and status monitors, and later launch one headed Chromium load.\n"
            "No step can overwrite an existing run bundle.",
            flush=True,
        )
        try:
            _prompt_exact(
                f"Close every other controller tab and stop unrelated polling.\n"
                f"Type RUN {run_id} to begin deployment: ",
                f"RUN {run_id}",
            )
        except (Issue65RuntimeError, KeyboardInterrupt) as error:
            print(f"ERROR: {error}", file=sys.stderr)
            return 2
        return execute_run(report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
