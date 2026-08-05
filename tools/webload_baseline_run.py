#!/usr/bin/env python3
"""Coordinate the GitHub issue #66 live index.html baseline run.

Unlike issue #65's 5-run A/B matrix (separate /tmp worktrees per historical
commit, role-locked build identity, B2 gating), issue #66 runs ONE baseline
capture against the current branch tip in-place. This script is deliberately
smaller than tools/issue65_live_ab_runtime.py for that reason, but reuses that
module's already-hardened, issue-agnostic primitives (durable NDJSON/atomic
JSON writers, StopArbiter, serial line classification, cooldown/outcome
classification, bounded subprocess logging, artifact identity capture) rather
than re-deriving them.

Stages (see --stage): preflight -> identity -> build (conditional) -> full
(power-cycle/sampling/browser-capture/cooldown/outcome/retry — added once the
earlier stages are verified against the live controller).
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import issue65_live_ab_runtime as r65  # noqa: E402  (reused, hardened primitives)

ISSUE = 66
REPO_ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_ROOT = REPO_ROOT / "tasks" / "evidence" / "webload"
BROWSER_COLLECTOR = REPO_ROOT / "tools" / "webload_browser_capture.js"
BASELINE_ANCESTOR_SHA = "4b239ff2e215f265151fe5523b6fbcd8320dca7e"
DEFAULT_CONTROLLER = "10.0.0.22"
DEFAULT_SERIAL_PORT = (
    "/dev/serial/by-id/"
    "usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0"
)
BUILD_ENV = "protoArtoo_chirp"


class BaselineRunError(RuntimeError):
    """A user-facing coordinator error."""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--stage", choices=("preflight", "identity", "build", "full"), default="preflight",
    )
    parser.add_argument("--run-id", default="run-1")
    parser.add_argument("--controller", default=DEFAULT_CONTROLLER)
    parser.add_argument("--serial-port", default=DEFAULT_SERIAL_PORT)
    parser.add_argument(
        "--build-env",
        default=BUILD_ENV,
        help=(
            f"PlatformIO base env to build/OTA-flash in the build stage (default: {BUILD_ENV}). "
            "The '_ota' suffix is appended automatically, matching this repo's *_ota env "
            "naming convention (e.g. protoArtoo_chirp -> protoArtoo_chirp_ota). Added for "
            "issue #53/#73 to target env:protoArtoo_psychichttp_prototype without silently "
            "rebuilding and reflashing production CHIRP firmware over a prototype build -- "
            "the identity stage compares only by git short-SHA, not by which env is flashed, "
            "so a stale (behind-HEAD) prototype build will look like 'buildRequired' regardless "
            "of --build-env; pass the right value explicitly rather than relying on the default."
        ),
    )
    parser.add_argument(
        "--force-build",
        action="store_true",
        help="run make ota-chirp even if /api/identity already matches the local tip",
    )
    parser.add_argument(
        "--dev-reboot",
        action="store_true",
        help=(
            "harness-development iterations ONLY: try POST /api/reboot instead of a "
            "physical power cycle (falls back to a manual-replug prompt if unresponsive). "
            "Never valid for an accepted run-1/run-2 evidence bundle -- the ticket's "
            "Power-Cycle Recovery vocabulary and comparability to #65 both depend on a "
            "real physical power cycle."
        ),
    )
    return parser


def git_head() -> str:
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False, timeout=5,
    )
    if result.returncode != 0:
        raise BaselineRunError(f"git rev-parse HEAD failed: {result.stderr.strip()}")
    return result.stdout.strip()


def git_is_descendant(ancestor_sha: str, head_sha: str) -> bool:
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "merge-base", "--is-ancestor", ancestor_sha, head_sha],
        capture_output=True, text=True, check=False, timeout=5,
    )
    return result.returncode == 0


def check(name: str, subject: str, ok: bool, detail: str = "") -> dict[str, str]:
    return {
        "name": name,
        "subject": subject,
        "status": "OK" if ok else "FAILED",
        "detail": detail,
    }


def run_preflight(args: argparse.Namespace) -> dict[str, Any]:
    """Report-only checks; no writes, no network mutation, no probes beyond reachability."""
    checks: list[dict[str, str]] = []

    head = git_head()
    is_descendant = git_is_descendant(BASELINE_ANCESTOR_SHA, head)
    checks.append(check(
        "git-head-is-baseline-or-descendant",
        f"HEAD={head}",
        is_descendant,
        f"expected {BASELINE_ANCESTOR_SHA} to be an ancestor of HEAD",
    ))

    serial_path = Path(args.serial_port)
    checks.append(check(
        "serial-port-exists", str(serial_path), serial_path.exists(),
    ))

    for tool in ("git", "pio", "ping", "node", "python3"):
        checks.append(check("command-available", tool, shutil.which(tool) is not None))

    checks.append(check(
        "browser-collector-exists", str(BROWSER_COLLECTOR), BROWSER_COLLECTOR.is_file(),
    ))

    evidence_dir = EVIDENCE_ROOT / args.run_id
    checks.append(check(
        "evidence-directory-absent", str(evidence_dir), not evidence_dir.exists(),
    ))

    ping_result = subprocess.run(
        ["ping", "-c", "1", "-W", "1", args.controller],
        capture_output=True, text=True, check=False, timeout=3,
    )
    checks.append(check(
        "controller-icmp-reachable", args.controller, ping_result.returncode == 0,
        ping_result.stdout.strip().splitlines()[-1] if ping_result.stdout else "no reply",
    ))

    all_ok = all(item["status"] == "OK" for item in checks)
    return {
        "schemaVersion": 1,
        "issue": ISSUE,
        "stage": "preflight",
        "runId": args.run_id,
        "readyForNextStage": all_ok,
        "checks": checks,
    }


VERSION_SHORT_SHA_RE = re.compile(r"-g([0-9a-f]{6,40})(?:[+-]|$)")


def local_head_short_sha() -> str:
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "--short=7", "HEAD"],
        capture_output=True, text=True, check=False, timeout=5,
    )
    if result.returncode != 0:
        raise BaselineRunError(f"git rev-parse --short HEAD failed: {result.stderr.strip()}")
    return result.stdout.strip()


def version_matches_head(version_string: str | None, head_short_sha: str) -> bool:
    """Compare by the git short-SHA embedded in a `git describe` version string.

    data/fw-version.json as committed is a stale snapshot from whenever it was
    last regenerated+committed (known limitation, see
    project_version_json_redesign_issue32) — its content lags HEAD between
    version-file commits, so a direct string compare against the checked-in
    file is unreliable. The `-g<shortsha>` token `git describe` embeds is a
    direct, unambiguous commit identity regardless of file staleness.
    """
    if not isinstance(version_string, str):
        return False
    match = VERSION_SHORT_SHA_RE.search(version_string)
    if not match:
        return False
    return match.group(1).startswith(head_short_sha) or head_short_sha.startswith(match.group(1))


def run_identity_check(args: argparse.Namespace) -> dict[str, Any]:
    """Compare the device's running firmware identity against the local tip.

    Read-only: one GET /api/status (firmwareVersion/fsVersion/resetReason live
    there, not on /api/identity — /api/identity only carries droidName/
    mdnsUseName) plus one GET /api/identity for droidName. No writes. Answers
    whether run_build needs to flash before the baseline capture proceeds.
    """
    head_sha = local_head_short_sha()
    origin = f"http://{args.controller}"
    try:
        status = r65._http_json(f"{origin}/api/status", timeout_seconds=3.0)
        identity = r65._http_json(f"{origin}/api/identity", timeout_seconds=3.0)
    except r65.Issue65RuntimeError as error:
        return {
            "schemaVersion": 1,
            "issue": ISSUE,
            "stage": "identity",
            "runId": args.run_id,
            "reachable": False,
            "error": str(error),
            "buildRequired": True,
        }
    running_version = status.get("firmwareVersion")
    matches = version_matches_head(running_version, head_sha)
    return {
        "schemaVersion": 1,
        "issue": ISSUE,
        "stage": "identity",
        "runId": args.run_id,
        "reachable": True,
        "controller": args.controller,
        "localHeadShortSha": head_sha,
        "runningFirmwareVersion": running_version,
        "matches": matches,
        "buildRequired": not matches,
        "status": status,
        "identity": identity,
    }


VERSION_FILES = ("data/fw-version.json", "data/fs-version.json")


def _restore_generated_versions() -> None:
    """Reset the pio-generated version files to HEAD's committed content.

    tools/extract_version.py (pre:-hooked into every pio invocation) writes
    a fresh `git describe --dirty` string into these two tracked files as a
    build side effect. Without restoring between invocations, a build's own
    first write leaves the tree "dirty" for its second pio invocation
    (uploadfs), poisoning that run's identity string with a spurious
    "-dirty" suffix — reproduced live on 2026-08-04 (issue #66 run-1) before
    this restore was added. Mirrors tools/issue65_live_ab.py's
    generatedVersionRestoreBeforeEveryPio discipline.
    """
    subprocess.run(
        ["git", "-C", str(REPO_ROOT), "restore", "--", *VERSION_FILES],
        check=True, timeout=5,
    )


def run_build(args: argparse.Namespace, evidence_dir: Path) -> dict[str, Any]:
    """Build+OTA-flash args.build_env against the current checkout, capturing
    build/upload logs and firmware/filesystem identity into the evidence
    bundle, mirroring the identity-capture shape #65 used
    (capture_artifact_identity). Defaults to BUILD_ENV (protoArtoo_chirp,
    issue #66's original target); pass --build-env to target a different env,
    e.g. protoArtoo_psychichttp_prototype for issue #73."""
    timeline = r65.Timeline.start()
    events: list[dict[str, object]] = []
    ota_env = f"{args.build_env}_ota"

    _restore_generated_versions()
    build_log = evidence_dir / "build.log"
    r65.run_logged(
        ["pio", "run", "--project-dir", str(REPO_ROOT), "-e", ota_env],
        cwd=REPO_ROOT, timeout_seconds=600, artifact=build_log,
        timeline=timeline, events=events, event="build",
    )

    upload_log = evidence_dir / "firmware-upload.log"
    r65.run_logged(
        [
            "python3", str(REPO_ROOT / "tools" / "ota_upload.py"),
            "--env", ota_env, "--host", args.controller,
            "--timeout", "60", "--transfer-timeout", "60",
            "--file", str(REPO_ROOT / ".pio" / "build" / ota_env / "firmware.bin"),
        ],
        cwd=REPO_ROOT, timeout_seconds=120, artifact=upload_log,
        timeline=timeline, events=events, event="firmware-upload",
    )

    _restore_generated_versions()
    uploadfs_log = evidence_dir / "uploadfs.log"
    r65.run_logged(
        [
            "pio", "run", "--project-dir", str(REPO_ROOT), "-e", ota_env,
            "-t", "uploadfs", "--upload-port", args.controller,
        ],
        cwd=REPO_ROOT, timeout_seconds=120, artifact=uploadfs_log,
        timeline=timeline, events=events, event="uploadfs",
    )

    identity_dir = evidence_dir / "identity"
    identity_dir.mkdir(parents=True, exist_ok=True)
    firmware_identity = r65.capture_artifact_identity(
        REPO_ROOT / "data" / "fw-version.json",
        REPO_ROOT / ".pio" / "build" / ota_env / "firmware.bin",
        identity_dir / "fw-version.json",
        identity_dir / "firmware.sha256",
    )
    filesystem_identity = r65.capture_artifact_identity(
        REPO_ROOT / "data" / "fs-version.json",
        REPO_ROOT / ".pio" / "build" / ota_env / "littlefs.bin",
        identity_dir / "fs-version.json",
        identity_dir / "littlefs.sha256",
    )
    _restore_generated_versions()
    return {
        "schemaVersion": 1,
        "issue": ISSUE,
        "stage": "build",
        "runId": args.run_id,
        "otaEnvironment": ota_env,
        "buildLog": str(build_log),
        "firmwareUploadLog": str(upload_log),
        "uploadfsLog": str(uploadfs_log),
        "firmwareIdentity": firmware_identity,
        "filesystemIdentity": filesystem_identity,
        "events": events,
    }


LOG_INTERVAL_SECONDS = 5.0
LOG_REQUEST_DEADLINE_SECONDS = 3.0
SETTLE_SECONDS = 90.0
BROWSER_CAPTURE_OWNERSHIP_DEADLINE_SECONDS = 35.0
# Boot banner / reset-reason / panic / coredump only, per issue #66's de-scoping
# requirement -- WARN/INFO correlation now comes from logs.ndjson (/api/logs),
# not serial, since #65 confirmed serial capture quality is unreliable for that.
SERIAL_KEEP_RE = re.compile(
    r"(?:^ets |^rst:|^boot:|^configsip:|^clk_drv:|^mode:|^load:|^entry 0x|"
    r"ESP-ROM:|Build:|reset_reason=|Guru Meditation|panic|Backtrace|"
    r"assert failed|abort\(\)|CORRUPT HEAP|coredump|Rebooting)",
    re.IGNORECASE,
)


@dataclass
class Bundle:
    """Minimal duck-typed evidence bundle: just .root/.timeline/.events, which
    is all r65.MonitorLoop and ScopedSerialWatcher touch. Deliberately not
    r65.EvidenceBundle -- that class's manifest/outcome schema is built around
    #65's A/B failedAllocationEvidence authorization contract, which #66 has
    no equivalent of."""

    root: Path
    timeline: "r65.Timeline"
    events: list = field(default_factory=list)
    _lock: threading.RLock = field(default_factory=threading.RLock, repr=False)

    def write_manifest(self, **fields: object) -> None:
        with self._lock:
            payload = {"schemaVersion": 1, "issue": ISSUE, **fields}
            r65.atomic_write_json(self.root / "manifest.json", payload)

    def record_failed_allocation(
        self, *, phase: str, raw_line: str, record: dict[str, object],
    ) -> None:
        with self._lock:
            r65.append_ndjson(
                self.root / "allocation-failures.ndjson", self.timeline,
                "allocation-failure", phase=phase, rawLine=raw_line,
                sourceWallTime=record.get("wallTime"),
            )


class ScopedSerialWatcher(r65.SerialWatcher):
    """Reconnect-safe capture identical to #65's SerialWatcher, but serial.log
    is de-scoped to boot banner / reset-reason / panic / coredump lines only.
    Stop-reason classification still runs on every observed line -- only the
    *written* artifact is filtered, per issue #66's requirement."""

    def _record_line(self, line: str) -> None:
        record = self.bundle.timeline.record(
            "serial-line", phase=self.phase(), line=line,
        )
        if SERIAL_KEEP_RE.search(line):
            text = f"{record['wallTime']} {record['elapsedMonotonicSeconds']:.6f} {line}\n"
            r65._append_bytes_durable(self.bundle.root / "serial.log", text.encode("utf-8"))
        stop_reason = r65.classify_serial_line(line)
        if stop_reason == "positive allocation-failure evidence":
            observation_phase = {"browser": "load", "cooldown": "cooldown"}.get(
                self.phase(), "pre-load",
            )
            self.bundle.record_failed_allocation(
                phase=observation_phase, raw_line=line, record=record,
            )
        if stop_reason is not None:
            self.arbiter.request_stop(
                stop_reason, source="serial", rawLine=line,
                wallTime=record["wallTime"], monotonicNs=record["monotonicNs"],
            )


class LogsMonitor:
    """Poll /api/logs on the same 5s cadence as /api/status, diffing samples
    since the endpoint has no cursor (full ring-buffer snapshot every call,
    src/web/api_status.cpp:119-127) -- only newly observed lines are appended
    to logs.ndjson, tagged with the sample timestamp and phase. New, issue #66
    only -- #65 never captured this endpoint at all.

    Not every --build-env ports /api/logs (e.g. the #73 PsychicHttp prototype
    never did). Issue #73's run-35 accepted-evidence attempt showed why that
    matters: an unconditional 404 every LOG_INTERVAL_SECONDS for the whole run
    is a real HTTP request/socket the target build still has to accept and
    reject, adding load with no equivalent on a build where the endpoint
    exists -- confounding any admission-pressure comparison between builds,
    not just harmless noise. On the FIRST sample only, a definitive 404 (this
    build does not serve the route, not a transient failure) disables further
    polling for the rest of the run so the evidence bundle doesn't carry
    build-specific dead weight into a cross-build comparison."""

    def __init__(self, controller: str, bundle: Bundle, phase_getter) -> None:
        self.controller = controller
        self.bundle = bundle
        self.phase = phase_getter
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run, name="webload-logs-monitor", daemon=True,
        )
        self._previous_lines: list[str] = []
        self._first_sample_done = False
        self._supported = True
        self.error: BaseException | None = None

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def join(self, timeout: float = 4.0) -> None:
        self._thread.join(timeout)
        if self._thread.is_alive():
            raise BaselineRunError("logs monitor did not stop within deadline")
        if self.error is not None:
            raise BaselineRunError(f"logs monitor failed: {self.error}")

    def _diff(self, current_lines: list[str]) -> list[str]:
        if not self._previous_lines:
            return list(current_lines)
        anchor = self._previous_lines[-1]
        try:
            offset = current_lines[::-1].index(anchor)
        except ValueError:
            # Ring buffer rotated past the previous sample's tail entirely;
            # cannot determine the exact new subset, so surface everything
            # currently visible rather than silently dropping lines.
            return list(current_lines)
        index = len(current_lines) - 1 - offset
        return current_lines[index + 1:]

    def _sample(self) -> None:
        try:
            body = _http_text(
                f"http://{self.controller}/api/logs", LOG_REQUEST_DEADLINE_SECONDS,
            )
            success = True
            error = None
        except r65.Issue65RuntimeError as request_error:
            body = None
            success = False
            error = str(request_error)

        new_lines: list[str] = []
        if success and body is not None:
            current_lines = [line for line in body.splitlines() if line]
            new_lines = self._diff(current_lines)
            self._previous_lines = current_lines

        r65.append_ndjson(
            self.bundle.root / "logs.ndjson", self.bundle.timeline, "logs-sample",
            phase=self.phase(), success=success, error=error,
            newLineCount=len(new_lines), newLines=new_lines,
        )

        if not self._first_sample_done:
            self._first_sample_done = True
            if not success and error is not None and "HTTP Error 404" in error:
                self._supported = False
                r65.append_ndjson(
                    self.bundle.root / "logs.ndjson", self.bundle.timeline,
                    "logs-endpoint-not-supported",
                    detail=(
                        "first /api/logs sample returned 404 -- this build does not "
                        "serve the route; disabling further polling for the rest of "
                        "this run instead of generating a guaranteed-fail request "
                        "every LOG_INTERVAL_SECONDS"
                    ),
                )

    def _run(self) -> None:
        next_sample = time.monotonic()
        try:
            while not self._stop.is_set():
                if self._supported:
                    now = time.monotonic()
                    if now >= next_sample:
                        self._sample()
                        next_sample = max(next_sample + LOG_INTERVAL_SECONDS, time.monotonic())
                    self._stop.wait(max(0.01, min(0.2, next_sample - time.monotonic())))
                else:
                    self._stop.wait(0.2)
        except BaseException as error:  # noqa: BLE001 - surfaced via join()
            self.error = error


def _http_text(url: str, timeout_seconds: float) -> str:
    """GET a plain-text endpoint. /api/logs returns text/plain, not JSON
    (src/web/api_status.cpp:119-127), so r65._http_json doesn't fit."""
    request = r65.urllib_request.Request(
        url, headers={"Cache-Control": "no-cache"}, method="GET",
    )
    try:
        with r65.urllib_request.urlopen(request, timeout=timeout_seconds) as response:
            if response.status != 200:
                raise r65.Issue65RuntimeError(f"{url} returned HTTP {response.status}")
            return response.read().decode("utf-8", errors="replace")
    except (r65.urllib_error.URLError, TimeoutError, OSError) as error:
        raise r65.Issue65RuntimeError(f"{url} is unreachable: {error}") from error


def _try_api_reboot(controller: str) -> bool:
    """--dev-reboot only. POST /api/reboot; True if the request was accepted."""
    try:
        request = r65.urllib_request.Request(
            f"http://{controller}/api/reboot", method="POST",
        )
        with r65.urllib_request.urlopen(request, timeout=3.0) as response:
            return response.status == 200
    except (r65.urllib_error.URLError, TimeoutError, OSError):
        return False


def _run_browser_capture(
    argv: list[str], log_path: Path, bundle: Bundle, arbiter: "r65.StopArbiter",
) -> tuple[int, dict[str, object] | None]:
    """Own the browser collector subprocess exactly as #65's runtime does:
    bounded ownership deadline, hard-kill on overrun, read back page-state.json."""
    bundle.events.append(bundle.timeline.record("browser-capture-started", argv=argv))
    process: subprocess.Popen | None = None
    return_code = -1
    try:
        with log_path.open("xb") as log:
            process = subprocess.Popen(
                argv, cwd=REPO_ROOT, stdout=log, stderr=subprocess.STDOUT, shell=False,
            )
            deadline = time.monotonic() + BROWSER_CAPTURE_OWNERSHIP_DEADLINE_SECONDS
            while process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.1)
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
                raise BaselineRunError(
                    "browser collector exceeded its ownership deadline"
                )
            return_code = int(process.returncode)
            log.flush()
            os.fsync(log.fileno())
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=2)
        bundle.events.append(bundle.timeline.record(
            "browser-capture-finished", returnCode=return_code,
        ))
    state_path = log_path.parent / "browser" / "page-state.json"
    state: dict[str, object] | None = None
    if state_path.is_file():
        try:
            loaded = json.loads(state_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise BaselineRunError(f"browser page state is invalid: {error}") from error
        if isinstance(loaded, dict):
            state = loaded
    if return_code == 2:
        raise BaselineRunError("browser collector reported an evidence-artifact failure")
    if return_code not in (0, 3, 4):
        raise BaselineRunError(f"browser collector exited unexpectedly with {return_code}")
    return return_code, state


def _attempt_power_cycle_recovery(controller: str, bundle: Bundle) -> bool:
    print(
        "\nHTTP Blackout is confirmed. Browser and status-monitor traffic are stopped.\n"
        "Physically unplug USB power, wait until fully unpowered, reconnect it,\n"
        "then type RECOVER and press Enter.",
        flush=True,
    )
    r65._prompt_exact("Recovery confirmation: ", "RECOVER")
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline:
        try:
            status = r65._http_json(
                f"http://{controller}/api/status", r65.STATUS_DEADLINE_SECONDS,
            )
            success, error = True, None
        except r65.Issue65RuntimeError as request_error:
            status, success, error = None, False, str(request_error)
        r65.append_ndjson(
            bundle.root / "recovery-status.ndjson", bundle.timeline,
            "recovery-status-sample", success=success, error=error, status=status,
        )
        if success:
            return True
        time.sleep(2.0)
    return False


def _finalize(
    bundle: Bundle,
    arbiter: "r65.StopArbiter",
    run_id: str,
    primary: str,
    browser_state: dict[str, object] | None,
    cooldown: dict[str, object] | None,
    status_reachable: bool,
    recovery_facts: list[str],
) -> dict[str, Any]:
    outcome = {
        "schemaVersion": 1,
        "issue": ISSUE,
        "runId": run_id,
        "status": "COMPLETE",
        "primaryOutcome": primary,
        "stopReasons": [arbiter.reason] if arbiter.reason else [],
        "recoveryFacts": recovery_facts,
        "browser": browser_state,
        "cooldown": cooldown,
        "statusReachableAtEnd": status_reachable,
    }
    r65.atomic_write_json(bundle.root / "outcome.json", outcome)
    bundle.write_manifest(
        runId=run_id, status="COMPLETE", stage="complete", primaryOutcome=primary,
    )
    print(json.dumps({
        "run": run_id, "primaryOutcome": primary, "stopReason": arbiter.reason,
        "recoveryFacts": recovery_facts, "evidence": str(bundle.root),
    }, indent=2), flush=True)
    return outcome


def run_full(args: argparse.Namespace) -> dict[str, Any]:
    """Power-cycle -> serial/ping/status/logs sampling -> 90s settle -> single
    index.html browser capture -> cooldown -> outcome classification. Mirrors
    tools/issue65_live_ab_runtime.py's execute_run() control flow (that
    sequencing was hardened across #65's own iteration) adapted for one
    in-place run instead of a role-locked worktree deployment."""
    evidence_dir = EVIDENCE_ROOT / args.run_id
    r65.create_evidence_root(evidence_dir)
    (evidence_dir / "identity").mkdir(exist_ok=True)
    timeline = r65.Timeline.start()
    bundle = Bundle(root=evidence_dir, timeline=timeline)
    tip_commit = git_head()
    head_short_sha = local_head_short_sha()

    arbiter = r65.StopArbiter(evidence_dir / "control.json", timeline, bundle.events)
    monitor = r65.MonitorLoop(args.controller, bundle, arbiter)
    serial = ScopedSerialWatcher(
        Path(args.serial_port), bundle, arbiter, lambda: monitor.phase,
    )
    logs_monitor = LogsMonitor(args.controller, bundle, lambda: monitor.phase)

    bundle.write_manifest(
        runId=args.run_id, controller=args.controller, serialPort=args.serial_port,
        tipCommit=tip_commit, devReboot=bool(args.dev_reboot),
        stage="awaiting-physical-cycle", status="IN_PROGRESS",
    )

    browser_state: dict[str, object] | None = None
    cooldown: dict[str, object] | None = None
    monitor_started = False
    serial_started = False
    logs_started = False
    try:
        serial.start(); serial_started = True
        monitor.start(); monitor_started = True
        logs_monitor.start(); logs_started = True
        if not r65._wait_for(lambda: serial.connected.is_set(), 5.0):
            raise BaselineRunError(f"serial watcher could not attach to {args.serial_port}")

        if args.dev_reboot:
            print(
                "\n[--dev-reboot] harness-development iteration -- NOT valid for an "
                "accepted baseline run. Trying POST /api/reboot first.",
                flush=True,
            )
            if not _try_api_reboot(args.controller):
                print(
                    "POST /api/reboot unresponsive -- physically replug USB now.",
                    flush=True,
                )
                r65._prompt_exact(
                    "Physical-cycle confirmation: ", f"CYCLED {args.run_id}",
                )
        else:
            print(
                "\nThis is an ACCEPTED EVIDENCE RUN. Physically unplug the controller "
                "USB power cable, wait until fully unpowered, reconnect the same cable, "
                f"then type CYCLED {args.run_id} and press Enter.",
                flush=True,
            )
            r65._prompt_exact("Physical-cycle confirmation: ", f"CYCLED {args.run_id}")

        if not r65._wait_for(
            lambda: serial.disconnected_after_connect.is_set(), 10.0, arbiter=arbiter,
        ):
            if arbiter.stopped:
                return _finalize(bundle, arbiter, args.run_id, "UNKNOWN", None, None, False, [])
            raise BaselineRunError(
                "serial evidence did not observe USB disappearance during power cycle"
            )
        if not r65._wait_for(
            lambda: serial.reconnected.is_set(), 20.0, arbiter=arbiter,
        ):
            if arbiter.stopped:
                return _finalize(bundle, arbiter, args.run_id, "UNKNOWN", None, None, False, [])
            raise BaselineRunError(
                "serial evidence did not observe USB re-enumeration after power cycle"
            )
        if arbiter.stopped:
            return _finalize(bundle, arbiter, args.run_id, "UNKNOWN", None, None, False, [])

        try:
            fresh_status = monitor.wait_for_status(
                lambda status: (
                    version_matches_head(status.get("firmwareVersion"), head_short_sha)
                    and status.get("resetReason") == "POWERON"
                ),
                60.0,
            )
        except r65.Issue65RuntimeError:
            if arbiter.stopped:
                return _finalize(bundle, arbiter, args.run_id, "UNKNOWN", None, None, False, [])
            raise
        bundle.write_manifest(
            runId=args.run_id, status="IN_PROGRESS", stage="identity-verified",
            verifiedStatus={
                key: fresh_status.get(key)
                for key in ("firmwareVersion", "fsVersion", "resetReason", "uptimeMs")
            },
        )
        monitor.arm()

        monitor.set_phase("settle")
        print("Matched identity verified; beginning fixed 90-second settle.", flush=True)
        settle_deadline = time.monotonic() + SETTLE_SECONDS
        while time.monotonic() < settle_deadline and not arbiter.stopped:
            time.sleep(min(0.25, settle_deadline - time.monotonic()))

        baseline_heap: int | None = None
        if not arbiter.stopped:
            snapshot = monitor.snapshot()
            baseline_status = snapshot["latestStatus"]
            if not isinstance(baseline_status, dict):
                raise BaselineRunError("no status baseline exists before browser load")
            baseline_heap = baseline_status.get("heapLargest8bit")
            if not isinstance(baseline_heap, int):
                raise BaselineRunError("pre-load status has no integer heapLargest8bit")
            bundle.write_manifest(
                runId=args.run_id, status="IN_PROGRESS", stage="browser",
                preLoadStatus=baseline_status,
            )

            monitor.set_phase("browser")
            print("Starting the one visible /index.html browser load.", flush=True)
            argv = [
                "node", str(BROWSER_COLLECTOR),
                "--url", f"http://{args.controller}/index.html",
                "--commit", tip_commit,
                "--out", str(evidence_dir / "browser"),
                "--control-file", str(evidence_dir / "control.json"),
            ]
            _return_code, browser_state = _run_browser_capture(
                argv, evidence_dir / "browser.log", bundle, arbiter,
            )

            loss_started = monitor.snapshot()["statusLossStartedMonotonic"]
            if isinstance(loss_started, float) and not arbiter.stopped:
                remaining = max(
                    0.0,
                    r65.HTTP_BLACKOUT_SECONDS - (time.monotonic() - loss_started) + 0.5,
                )
                r65._wait_for(
                    lambda: (
                        arbiter.stopped
                        or monitor.snapshot()["statusLossStartedMonotonic"] is None
                    ),
                    min(remaining, 6.0),
                )

            if not arbiter.stopped:
                monitor.set_phase("cooldown")
                cooldown_deadline = time.monotonic() + r65.COOLDOWN_SECONDS
                while time.monotonic() < cooldown_deadline and not arbiter.stopped:
                    time.sleep(min(0.25, cooldown_deadline - time.monotonic()))
                cooldown_samples = monitor.snapshot()["cooldownSamples"]
                if isinstance(cooldown_samples, list):
                    cooldown = r65.evaluate_cooldown(cooldown_samples, baseline_heap)

        snapshot = monitor.snapshot()
        last_success = snapshot["latestStatusSuccessMonotonic"]
        status_reachable = (
            isinstance(last_success, float)
            and time.monotonic() - last_success <= r65.STATUS_INTERVAL_SECONDS + 2.0
        )
        primary = r65.classify_primary_outcome(arbiter.reason, browser_state, status_reachable)
        recovery_facts: list[str] = []
        if arbiter.reason == "HTTP Blackout":
            monitor.stop_status_requests()
            monitor.stop()
            monitor.join()
            monitor_started = False
            if _attempt_power_cycle_recovery(args.controller, bundle):
                recovery_facts.append("Power-Cycle Recovery")

        return _finalize(
            bundle, arbiter, args.run_id, primary, browser_state, cooldown,
            status_reachable, recovery_facts,
        )
    except KeyboardInterrupt:
        if arbiter is not None:
            try:
                arbiter.request_stop("operator interrupt", source="keyboard")
            except BaseException:
                pass
        bundle.write_manifest(runId=args.run_id, status="ABORTED", stage="interrupted")
        raise BaselineRunError("operator interrupt")
    finally:
        cleanup_errors: list[str] = []
        if monitor_started:
            monitor.stop()
            try:
                monitor.join()
            except BaseException as error:  # noqa: BLE001
                cleanup_errors.append(str(error))
        if serial_started:
            serial.stop()
            try:
                serial.join()
            except BaseException as error:  # noqa: BLE001
                cleanup_errors.append(str(error))
        if logs_started:
            logs_monitor.stop()
            try:
                logs_monitor.join()
            except BaseException as error:  # noqa: BLE001
                cleanup_errors.append(str(error))
        if cleanup_errors:
            sys.stderr.write("WARNING: cleanup: " + "; ".join(cleanup_errors) + "\n")


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.stage == "preflight":
            report = run_preflight(args)
        elif args.stage == "identity":
            report = run_identity_check(args)
        elif args.stage == "build":
            identity_report = run_identity_check(args)
            if not identity_report.get("buildRequired") and not args.force_build:
                report = {
                    "schemaVersion": 1,
                    "issue": ISSUE,
                    "stage": "build",
                    "runId": args.run_id,
                    "skipped": True,
                    "reason": "running firmware already matches local tip",
                    "identity": identity_report,
                }
            else:
                evidence_dir = EVIDENCE_ROOT / args.run_id
                evidence_dir.mkdir(parents=True, exist_ok=True)
                report = run_build(args, evidence_dir)
                report["identityBeforeBuild"] = identity_report
        elif args.stage == "full":
            if not sys.stdin.isatty() or not sys.stdout.isatty():
                sys.stderr.write(
                    "ERROR: --stage full requires an interactive TTY for the "
                    "physical-cycle operator gate\n"
                )
                return 2
            report = run_full(args)
        else:
            raise BaselineRunError(f"unknown stage: {args.stage}")
    except (BaselineRunError, r65.Issue65RuntimeError) as error:
        sys.stderr.write(f"ERROR: {error}\n")
        return 1
    sys.stdout.write(f"{json.dumps(report, indent=2)}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
