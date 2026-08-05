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

--stage full drives two browser captures in sequence: the single-tab capture
(tools/webload_browser_capture.js) and then the multi-tab capture
(tools/webload_multitab_capture.js), each followed by its own cooldown. Both
land in outcome.json's `captures` list in the same shape, so a comparison run
is one harness invocation rather than a scripted capture plus a hand-driven
one.

--stage full still requires a real physical power cycle -- resetReason ==
POWERON is only reachable by removing power, and a deterministic cold heap is
what every cooldown verdict is measured against. It does not require anyone to
be at the terminal: the gate is serial evidence of USB disappearing and
re-enumerating, not a typed confirmation, so the stage runs headless and the
cable can be pulled any time within --cycle-wait-seconds.

The build under test is --expect-firmware (default: local HEAD). Pin it when
measuring a firmware other than the current tip -- an A/B across a migration
needs one checkout of this harness to measure two different firmwares.
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
MULTITAB_COLLECTOR = REPO_ROOT / "tools" / "webload_multitab_capture.js"
# 3 is the full Browser Load Profile: two steady tabs for the whole window plus
# a brief third-tab overlap, so the default run covers both the two-tab and the
# three-tab scenario without a second invocation. --multitab-tabs 2 drops the
# third tab when only the steady-state pair is wanted.
MULTITAB_DEFAULT_TABS = 3
MULTITAB_TAB_CHOICES = (2, 3)
# The operator has to walk to the droid and pull a cable, so the window is
# generous. Nothing is consumed by waiting: the run samples nothing that
# matters until the cycle is observed.
CYCLE_WAIT_SECONDS = 600.0
# Re-enumeration is mechanical once power is back, so it stays tight.
CYCLE_REENUMERATE_SECONDS = 30.0
CYCLE_PROGRESS_INTERVAL_SECONDS = 15.0
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
        "--expect-firmware",
        default=None,
        metavar="REV_OR_VERSION",
        help=(
            "which build the controller is expected to be running: a git rev, a bare "
            "short/full SHA, or a whole firmwareVersion string to copy-paste (default: "
            "local HEAD). The identity gate exists to stop you measuring a stale flash, "
            "but pinning it to HEAD means any commit invalidates it -- including commits "
            "that touch only tooling or web assets, and including this harness's own. It "
            "also blocks the thing a migration comparison needs: measuring two different "
            "firmwares from one checkout of the harness. Pass the build under test and "
            "unrelated commits stop mattering."
        ),
    )
    parser.add_argument(
        "--cycle-wait-seconds",
        type=float,
        default=CYCLE_WAIT_SECONDS,
        help=(
            f"how long to wait for the operator's physical power cycle (default: "
            f"{CYCLE_WAIT_SECONDS:.0f}s). The run gates on serial evidence of the cycle, "
            "not on a typed confirmation, so it needs no TTY and the cable can be pulled "
            "whenever within this window."
        ),
    )
    parser.add_argument(
        "--multitab-tabs",
        type=int,
        choices=MULTITAB_TAB_CHOICES,
        default=MULTITAB_DEFAULT_TABS,
        help=(
            f"peak tab count for the --stage full multi-tab capture (default: "
            f"{MULTITAB_DEFAULT_TABS}). 3 runs two steady tabs plus a brief third-tab "
            "overlap, covering both scenarios in one run; 2 keeps only the steady pair."
        ),
    )
    parser.add_argument(
        "--skip-multitab",
        action="store_true",
        help=(
            "omit the multi-tab capture from --stage full. Only for comparing against "
            "an evidence bundle captured before multi-tab was part of the sequence -- "
            "a run recorded with this flag is not comparable to a default run."
        ),
    )
    parser.add_argument(
        "--dev-reboot",
        action="store_true",
        help=(
            "harness-development iterations ONLY: try POST /api/reboot instead of a "
            "physical power cycle (falls back to waiting for a manual replug if "
            "unresponsive). "
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

    checks.append(check(
        "multitab-collector-exists", str(MULTITAB_COLLECTOR), MULTITAB_COLLECTOR.is_file(),
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


def resolve_expected_short_sha(spec: str | None) -> str:
    """Turn --expect-firmware into the short SHA the identity gate compares against.

    Accepts a git rev (branch, tag, HEAD~3, full SHA), a bare short SHA that
    need not exist in this checkout, or a whole firmwareVersion string copied
    from /api/status -- the last so an operator can paste what the device
    reports and pin exactly that, without hand-extracting the token.
    """
    if spec is None:
        return local_head_short_sha()
    embedded = VERSION_SHORT_SHA_RE.search(spec)
    if embedded:
        return embedded.group(1)[:7]
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "--short=7", f"{spec}^{{commit}}"],
        capture_output=True, text=True, check=False, timeout=5,
    )
    if result.returncode == 0:
        return result.stdout.strip()
    if re.fullmatch(r"[0-9a-f]{7,40}", spec):
        # A commit this checkout does not have. Pinning it is still meaningful:
        # the gate only ever compares the token the device reports.
        return spec[:7]
    raise BaselineRunError(
        f"--expect-firmware {spec!r} is not a git rev this checkout knows, a hex SHA, "
        "or a version string containing a -g<sha> token"
    )


def version_matches_expected(version_string: str | None, expected_short_sha: str) -> bool:
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
    return (
        match.group(1).startswith(expected_short_sha)
        or expected_short_sha.startswith(match.group(1))
    )


def run_identity_check(args: argparse.Namespace) -> dict[str, Any]:
    """Compare the device's running firmware identity against the build under test.

    Read-only: one GET /api/status (firmwareVersion/fsVersion/resetReason live
    there, not on /api/identity — /api/identity only carries droidName/
    mdnsUseName) plus one GET /api/identity for droidName. No writes. Answers
    whether run_build needs to flash before the baseline capture proceeds.

    The build under test is --expect-firmware, defaulting to local HEAD.
    """
    head_sha = resolve_expected_short_sha(getattr(args, "expect_firmware", None))
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
    matches = version_matches_expected(running_version, head_sha)
    return {
        "schemaVersion": 1,
        "issue": ISSUE,
        "stage": "identity",
        "runId": args.run_id,
        "reachable": True,
        "controller": args.controller,
        "expectedShortSha": head_sha,
        "expectFirmwareSpec": getattr(args, "expect_firmware", None),
        "localHeadShortSha": local_head_short_sha(),
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
    name: str,
    argv: list[str],
    log_path: Path,
    state_path: Path,
    bundle: Bundle,
    ownership_deadline_seconds: float,
) -> dict[str, Any]:
    """Own a browser collector subprocess exactly as #65's runtime does:
    bounded ownership deadline, hard-kill on overrun, read back page-state.json.

    Both collectors share the exit-code contract (0 usable, 2 evidence-artifact
    failure, 3 failure observed, 4 stopped) and both write a page-state.json
    carrying the same verdict keys, which is what lets one function drive them
    and produce one uniform capture record for outcome.json.

    The ownership deadline is per-capture rather than a module constant because
    the two collectors observe for different windows; the multi-tab collector
    reports the budget it needs through its own --dry-run plan.
    """
    started = bundle.timeline.record(
        "browser-capture-started", capture=name, argv=argv,
        ownershipDeadlineSeconds=ownership_deadline_seconds,
    )
    bundle.events.append(started)
    process: subprocess.Popen | None = None
    return_code = -1
    finished: dict[str, Any] | None = None
    try:
        with log_path.open("xb") as log:
            process = subprocess.Popen(
                argv, cwd=REPO_ROOT, stdout=log, stderr=subprocess.STDOUT, shell=False,
            )
            deadline = time.monotonic() + ownership_deadline_seconds
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
                    f"{name} collector exceeded its ownership deadline "
                    f"({ownership_deadline_seconds:.1f}s)"
                )
            return_code = int(process.returncode)
            log.flush()
            os.fsync(log.fileno())
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=2)
        finished = bundle.timeline.record(
            "browser-capture-finished", capture=name, returnCode=return_code,
        )
        bundle.events.append(finished)
    state: dict[str, object] | None = None
    if state_path.is_file():
        try:
            loaded = json.loads(state_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise BaselineRunError(f"{name} page state is invalid: {error}") from error
        if isinstance(loaded, dict):
            state = loaded
    if return_code == 2:
        raise BaselineRunError(f"{name} collector reported an evidence-artifact failure")
    if return_code not in (0, 3, 4):
        raise BaselineRunError(f"{name} collector exited unexpectedly with {return_code}")
    return {
        "name": name,
        "returnCode": return_code,
        "startedWallTime": started["wallTime"],
        "finishedWallTime": finished["wallTime"],
        "startedElapsedSeconds": started["elapsedMonotonicSeconds"],
        "finishedElapsedSeconds": finished["elapsedMonotonicSeconds"],
        "log": str(log_path),
        "pageState": str(state_path),
        "state": state,
    }


MULTITAB_PLAN_TIMEOUT_SECONDS = 30.0


def _multitab_argv(
    controller: str, tip_commit: str, out_dir: Path, control_file: Path, tabs: int,
) -> list[str]:
    return [
        "node", str(MULTITAB_COLLECTOR),
        "--url", f"http://{controller}/index.html",
        "--commit", tip_commit,
        "--out", str(out_dir),
        "--control-file", str(control_file),
        "--tabs", str(tabs),
    ]


def _multitab_plan(
    controller: str, tip_commit: str, out_dir: Path, control_file: Path, tabs: int,
) -> dict[str, Any]:
    """Ask the multi-tab collector what it intends to do, before running it.

    The scenario timings live in the collector. Rather than restating them here
    (where they would drift silently and the subprocess would be hard-killed
    mid-capture once they did), the harness reads the collector's own --dry-run
    plan and sizes the ownership deadline from its ownershipBudgetMs. The probe
    also fails fast on an unrunnable collector or a rejected argument before the
    run has committed to a browser launch.
    """
    argv = _multitab_argv(controller, tip_commit, out_dir, control_file, tabs) + ["--dry-run"]
    result = subprocess.run(
        argv, cwd=REPO_ROOT, capture_output=True, text=True, check=False,
        timeout=MULTITAB_PLAN_TIMEOUT_SECONDS,
    )
    if result.returncode != 0:
        raise BaselineRunError(
            f"multitab collector rejected its plan (exit {result.returncode}): "
            f"{result.stderr.strip().splitlines()[0] if result.stderr.strip() else 'no detail'}"
        )
    try:
        plan = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise BaselineRunError(f"multitab collector plan is not JSON: {error}") from error
    scenario = plan.get("scenario") if isinstance(plan, dict) else None
    if not isinstance(scenario, dict):
        raise BaselineRunError("multitab collector plan has no scenario object")
    budget_ms = scenario.get("ownershipBudgetMs")
    if not isinstance(budget_ms, int) or budget_ms <= 0:
        raise BaselineRunError("multitab collector plan has no positive ownershipBudgetMs")
    return plan


def _wait_for_event(
    event: threading.Event,
    timeout_seconds: float,
    arbiter: "r65.StopArbiter",
    waiting_for: str,
) -> bool:
    """Wait on a serial event, printing progress so an unattended run is legible."""
    deadline = time.monotonic() + timeout_seconds
    next_notice = time.monotonic() + CYCLE_PROGRESS_INTERVAL_SECONDS
    while time.monotonic() < deadline:
        if event.wait(0.25):
            return True
        if arbiter.stopped:
            return False
        now = time.monotonic()
        if now >= next_notice:
            print(
                f"  still waiting for {waiting_for} "
                f"({deadline - now:.0f}s left)",
                flush=True,
            )
            next_notice = now + CYCLE_PROGRESS_INTERVAL_SECONDS
    return event.is_set()


def _await_physical_power_cycle(
    serial: "ScopedSerialWatcher",
    arbiter: "r65.StopArbiter",
    run_id: str,
    wait_seconds: float,
    banner: str,
) -> None:
    """Block until serial evidence shows the controller was actually power-cycled.

    The run used to gate on the operator typing CYCLED <run-id>, which forced
    every accepted run to hold an interactive TTY for the whole cycle -- so the
    stage could not be driven headless, and an agent could not produce evidence
    at all without a human sitting at the same terminal.

    The typed confirmation never added information. The serial watcher already
    observes USB disappearing and re-enumerating, and the identity gate below
    still requires resetReason == POWERON, which only real power removal
    produces. Gating on that evidence instead is both headless and stricter: it
    cannot be satisfied by someone typing the words without pulling the cable.
    """
    print(banner, flush=True)
    if not _wait_for_event(
        serial.disconnected_after_connect, wait_seconds, arbiter,
        "the controller's USB to disappear",
    ):
        if arbiter.stopped:
            return
        raise BaselineRunError(
            f"no power cycle observed within {wait_seconds:.0f}s: serial evidence "
            f"never showed {run_id}'s controller USB disappearing"
        )
    print("  USB disappeared; waiting for the controller to come back.", flush=True)
    if not _wait_for_event(
        serial.reconnected, CYCLE_REENUMERATE_SECONDS, arbiter,
        "the controller's USB to re-enumerate",
    ):
        if arbiter.stopped:
            return
        raise BaselineRunError(
            "serial evidence did not observe USB re-enumeration after power cycle"
        )
    print("  Controller re-enumerated; verifying firmware identity.", flush=True)


def _attempt_power_cycle_recovery(
    controller: str, bundle: Bundle, wait_seconds: float,
) -> bool:
    """Record whether a physical power cycle brings the controller back.

    Like the run-start cycle, this no longer waits on a typed confirmation. It
    polls /api/status for the whole window, so the operator can pull the cable
    whenever they get to it and the recovery fact is established by the
    controller answering again -- which is the actual evidence -- rather than
    by someone asserting they replugged.
    """
    print(
        "\nHTTP Blackout is confirmed. Browser and status-monitor traffic are stopped.\n"
        "Physically unplug USB power, wait until fully unpowered, then reconnect it.\n"
        f"Polling /api/status for up to {wait_seconds:.0f}s; no confirmation needed.",
        flush=True,
    )
    deadline = time.monotonic() + wait_seconds
    next_notice = time.monotonic() + CYCLE_PROGRESS_INTERVAL_SECONDS
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
        now = time.monotonic()
        if now >= next_notice:
            print(
                f"  still waiting for the controller to answer "
                f"({deadline - now:.0f}s left)",
                flush=True,
            )
            next_notice = now + CYCLE_PROGRESS_INTERVAL_SECONDS
        time.sleep(2.0)
    return False


def _capture_named(captures: list[dict[str, Any]], name: str) -> dict[str, Any] | None:
    for capture in captures:
        if capture.get("name") == name:
            return capture
    return None


def _finalize(
    bundle: Bundle,
    arbiter: "r65.StopArbiter",
    run_id: str,
    primary: str,
    status_reachable: bool,
    recovery_facts: list[str],
    captures: list[dict[str, Any]],
    multitab_skipped: bool,
) -> dict[str, Any]:
    """Write outcome.json.

    `captures` is the machine-comparable section: one uniform record per browser
    capture (single-tab, multi-tab), each carrying its own wall/monotonic
    bracket, page state, pre-load heap, cooldown verdict and outcome. Top-level
    `browser`/`cooldown`/`primaryOutcome` continue to describe the single-tab
    capture alone, so bundles captured before multi-tab was part of the sequence
    stay directly comparable. `multitabSkipped` distinguishes a run that opted
    out from one recorded before multi-tab existed -- absence of a multi-tab
    record alone would not say which.
    """
    single = _capture_named(captures, "single-tab")
    multitab = _capture_named(captures, "multitab")
    outcome = {
        "schemaVersion": 1,
        "issue": ISSUE,
        "runId": run_id,
        "status": "COMPLETE",
        "primaryOutcome": primary,
        "stopReasons": [arbiter.reason] if arbiter.reason else [],
        "recoveryFacts": recovery_facts,
        "browser": single.get("state") if single else None,
        "cooldown": single.get("cooldown") if single else None,
        "multitabSkipped": multitab_skipped,
        "multitabOutcome": multitab.get("outcome") if multitab else None,
        "captures": captures,
        "statusReachableAtEnd": status_reachable,
    }
    r65.atomic_write_json(bundle.root / "outcome.json", outcome)
    bundle.write_manifest(
        runId=run_id, status="COMPLETE", stage="complete", primaryOutcome=primary,
    )
    print(json.dumps({
        "run": run_id, "primaryOutcome": primary, "stopReason": arbiter.reason,
        "recoveryFacts": recovery_facts,
        "captures": [
            {
                "name": capture["name"],
                "outcome": capture.get("outcome"),
                "returnCode": capture["returnCode"],
                "cooldownPassed": (capture.get("cooldown") or {}).get("passed"),
            }
            for capture in captures
        ],
        "multitabSkipped": multitab_skipped,
        "evidence": str(bundle.root),
    }, indent=2), flush=True)
    return outcome


def _run_capture_phase(
    *,
    name: str,
    stage: str,
    announcement: str,
    argv: list[str],
    log_path: Path,
    state_path: Path,
    ownership_deadline_seconds: float,
    run_id: str,
    bundle: Bundle,
    monitor: "r65.MonitorLoop",
    arbiter: "r65.StopArbiter",
) -> dict[str, Any]:
    """One browser capture phase: pre-load heap baseline -> the load itself ->
    post-load blackout settle -> that capture's own cooldown.

    Every capture in a run goes through here, so the single-tab and multi-tab
    captures are measured the same way and their records are directly
    comparable.

    Two details are load-bearing:

    - The load runs under the phase string "browser" whichever collector it is.
      r65.MonitorLoop._sample_status() keys both HTTP Blackout detection and
      statusReachable marking off exactly that value, so a distinct phase name
      would silently disable blackout detection for the multi-tab load. Captures
      are told apart by their wall/monotonic brackets and their own evidence
      directories, not by the phase tag.
    - Cooldown is evaluated on the samples this phase added, not the whole
      shared list, since a run now has more than one cooldown. The heap baseline
      is the one measured immediately before this capture, so each capture's
      cooldown verdict isolates that capture rather than inheriting an earlier
      one's failure to recover.
    """
    snapshot = monitor.snapshot()
    baseline_status = snapshot["latestStatus"]
    if not isinstance(baseline_status, dict):
        raise BaselineRunError(f"no status baseline exists before the {name} load")
    baseline_heap = baseline_status.get("heapLargest8bit")
    if not isinstance(baseline_heap, int):
        raise BaselineRunError(f"pre-{name} status has no integer heapLargest8bit")
    bundle.write_manifest(
        runId=run_id, status="IN_PROGRESS", stage=stage, preLoadStatus=baseline_status,
    )

    monitor.set_phase("browser")
    print(announcement, flush=True)
    capture = _run_browser_capture(
        name, argv, log_path, state_path, bundle, ownership_deadline_seconds,
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

    cooldown: dict[str, object] | None = None
    if not arbiter.stopped:
        # Read the offset before flipping the phase: the monitor thread only
        # appends while phase == "cooldown", so nothing can land in between.
        cooldown_offset = len(monitor.snapshot()["cooldownSamples"])
        monitor.set_phase("cooldown")
        cooldown_deadline = time.monotonic() + r65.COOLDOWN_SECONDS
        while time.monotonic() < cooldown_deadline and not arbiter.stopped:
            time.sleep(min(0.25, cooldown_deadline - time.monotonic()))
        cooldown_samples = monitor.snapshot()["cooldownSamples"]
        if isinstance(cooldown_samples, list):
            cooldown = r65.evaluate_cooldown(
                cooldown_samples[cooldown_offset:], baseline_heap,
            )

    capture["preLoadStatus"] = baseline_status
    capture["preLoadHeapLargest8bit"] = baseline_heap
    capture["cooldown"] = cooldown
    return capture


def run_full(args: argparse.Namespace) -> dict[str, Any]:
    """Power-cycle -> serial/ping/status/logs sampling -> 90s settle -> single
    index.html browser capture -> cooldown -> multi-tab browser capture ->
    cooldown -> outcome classification. Mirrors
    tools/issue65_live_ab_runtime.py's execute_run() control flow (that
    sequencing was hardened across #65's own iteration) adapted for one
    in-place run instead of a role-locked worktree deployment.

    The multi-tab capture is appended after the single-tab capture and its
    cooldown rather than interleaved with them, so the single-tab measurement
    stays identical to bundles captured before multi-tab was part of the
    sequence and the two remain comparable."""
    evidence_dir = EVIDENCE_ROOT / args.run_id
    r65.create_evidence_root(evidence_dir)
    (evidence_dir / "identity").mkdir(exist_ok=True)
    timeline = r65.Timeline.start()
    bundle = Bundle(root=evidence_dir, timeline=timeline)
    tip_commit = git_head()
    expected_short_sha = resolve_expected_short_sha(args.expect_firmware)

    arbiter = r65.StopArbiter(evidence_dir / "control.json", timeline, bundle.events)
    monitor = r65.MonitorLoop(args.controller, bundle, arbiter)
    serial = ScopedSerialWatcher(
        Path(args.serial_port), bundle, arbiter, lambda: monitor.phase,
    )
    logs_monitor = LogsMonitor(args.controller, bundle, lambda: monitor.phase)

    multitab_skipped = bool(args.skip_multitab)
    multitab_plan: dict[str, Any] | None = None
    if not multitab_skipped:
        # Resolve the multi-tab plan before the operator is asked to power-cycle:
        # a bad --multitab-tabs value or an unrunnable collector should cost a
        # command line, not a physical power cycle and a 90-second settle.
        multitab_plan = _multitab_plan(
            args.controller, tip_commit, evidence_dir / "multitab",
            evidence_dir / "control.json", args.multitab_tabs,
        )

    bundle.write_manifest(
        runId=args.run_id, controller=args.controller, serialPort=args.serial_port,
        tipCommit=tip_commit, devReboot=bool(args.dev_reboot),
        expectedShortSha=expected_short_sha,
        expectFirmwareSpec=args.expect_firmware,
        multitabSkipped=multitab_skipped,
        multitabScenario=(multitab_plan or {}).get("scenario"),
        stage="awaiting-physical-cycle", status="IN_PROGRESS",
    )

    captures: list[dict[str, Any]] = []
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
                _await_physical_power_cycle(
                    serial, arbiter, args.run_id, args.cycle_wait_seconds,
                    "POST /api/reboot unresponsive -- physically replug the controller "
                    "USB cable. Waiting for serial evidence of the cycle.",
                )
        else:
            _await_physical_power_cycle(
                serial, arbiter, args.run_id, args.cycle_wait_seconds,
                f"\nThis is an ACCEPTED EVIDENCE RUN ({args.run_id}). Physically unplug "
                "the controller USB power cable, wait until fully unpowered, then "
                "reconnect the same cable.\n"
                f"No confirmation to type: the run gates on serial evidence of the "
                f"cycle and waits up to {args.cycle_wait_seconds:.0f}s.",
            )

        if arbiter.stopped:
            return _finalize(
                bundle, arbiter, args.run_id, "UNKNOWN", False, [],
                captures, multitab_skipped,
            )

        try:
            fresh_status = monitor.wait_for_status(
                lambda status: (
                    version_matches_expected(status.get("firmwareVersion"), expected_short_sha)
                    and status.get("resetReason") == "POWERON"
                ),
                60.0,
            )
        except r65.Issue65RuntimeError:
            if arbiter.stopped:
                return _finalize(
                    bundle, arbiter, args.run_id, "UNKNOWN", False, [],
                    captures, multitab_skipped,
                )
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

        if not arbiter.stopped:
            captures.append(_run_capture_phase(
                name="single-tab",
                stage="browser",
                announcement="Starting the one visible /index.html browser load.",
                argv=[
                    "node", str(BROWSER_COLLECTOR),
                    "--url", f"http://{args.controller}/index.html",
                    "--commit", tip_commit,
                    "--out", str(evidence_dir / "browser"),
                    "--control-file", str(evidence_dir / "control.json"),
                ],
                log_path=evidence_dir / "browser.log",
                state_path=evidence_dir / "browser" / "page-state.json",
                ownership_deadline_seconds=BROWSER_CAPTURE_OWNERSHIP_DEADLINE_SECONDS,
                run_id=args.run_id, bundle=bundle, monitor=monitor, arbiter=arbiter,
            ))

        if not arbiter.stopped and multitab_plan is not None:
            scenario = multitab_plan["scenario"]
            captures.append(_run_capture_phase(
                name="multitab",
                stage="multitab",
                announcement=(
                    f"Starting the multi-tab browser load "
                    f"({scenario['steadyTabs']} steady tabs, peak {scenario['peakTabs']})."
                ),
                argv=_multitab_argv(
                    args.controller, tip_commit, evidence_dir / "multitab",
                    evidence_dir / "control.json", args.multitab_tabs,
                ),
                log_path=evidence_dir / "multitab.log",
                state_path=evidence_dir / "multitab" / "page-state.json",
                ownership_deadline_seconds=scenario["ownershipBudgetMs"] / 1000.0,
                run_id=args.run_id, bundle=bundle, monitor=monitor, arbiter=arbiter,
            ))

        snapshot = monitor.snapshot()
        last_success = snapshot["latestStatusSuccessMonotonic"]
        status_reachable = (
            isinstance(last_success, float)
            and time.monotonic() - last_success <= r65.STATUS_INTERVAL_SECONDS + 2.0
        )
        # Each capture is classified with the same classifier, so a multi-tab
        # verdict reads in the same vocabulary as the single-tab one. The run's
        # primaryOutcome stays the single-tab verdict: it is the field older
        # bundles in this epic are compared on, and widening it here would make
        # this run incomparable to them.
        for capture in captures:
            capture["outcome"] = r65.classify_primary_outcome(
                arbiter.reason, capture.get("state"), status_reachable,
            )
        single_tab = _capture_named(captures, "single-tab")
        primary = (
            single_tab["outcome"] if single_tab
            else r65.classify_primary_outcome(arbiter.reason, None, status_reachable)
        )
        recovery_facts: list[str] = []
        if arbiter.reason == "HTTP Blackout":
            monitor.stop_status_requests()
            monitor.stop()
            monitor.join()
            monitor_started = False
            if _attempt_power_cycle_recovery(
                args.controller, bundle, args.cycle_wait_seconds,
            ):
                recovery_facts.append("Power-Cycle Recovery")

        return _finalize(
            bundle, arbiter, args.run_id, primary, status_reachable,
            recovery_facts, captures, multitab_skipped,
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
            # No TTY requirement: the physical-cycle gate is serial evidence, not
            # a typed confirmation, so the stage runs headless and the operator
            # can pull the cable whenever within --cycle-wait-seconds.
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
