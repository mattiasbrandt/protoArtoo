#!/usr/bin/env python3
"""Print the locked, no-write execution plan for GitHub issue #65 live A/B runs.

This planner validates immutable inputs and emits the fixture consumed by
issue65_live_ab_runtime.py. It never touches worktrees, evidence directories,
the controller, the serial port, or the network.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit


ISSUE = 65
REPO_ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_ROOT = REPO_ROOT / "tasks" / "evidence" / "issue-65"
BROWSER_COLLECTOR = REPO_ROOT / "tools" / "issue65_browser_capture.js"
RUNTIME_COORDINATOR = REPO_ROOT / "tools" / "issue65_live_ab_runtime.py"
DEFAULT_CONTROLLER = "10.0.0.22"
DEFAULT_SERIAL_PORT = "/dev/ttyUSB0"
PRODUCTION_ENV = "artoo_esp32_chirp"
OTA_ENV = "artoo_esp32_chirp_ota"
VERSION_FILES = ("data/fw-version.json", "data/fs-version.json")
FAILED_ALLOC_CONTRACT_URL = (
    "https://github.com/mattiasbrandt/artoo_esp32/issues/65"
    "#issuecomment-5172626006"
)
FAILED_ALLOC_AVAILABILITY = "unavailable-in-fixed-production-build"
FAILED_ALLOC_LIMITATION = (
    "It may distinguish build-following page/HTTP/reset behavior only as far as "
    "the available correlated evidence permits; it may not claim that either "
    "build had zero failed allocations."
)
CONTROLLER_RE = re.compile(
    r"^(?=.{1,253}$)(?!-)[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*$"
)
PATH_RE = re.compile(r"^/[A-Za-z0-9._/-]+$")


class PlanError(ValueError):
    """A user-facing validation error."""


class PlannerArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        """Normalize argparse failures to the documented exit code 1."""
        raise PlanError(message)


@dataclass(frozen=True)
class LockedRun:
    role: str
    commit: str
    worktree: str
    required_completed_runs: tuple[str, ...]


A_SHA = "1b2bed8e86cbfbe4756427233e75875ca7f189e3"
B_SHA = "956c9360d51cc2cb49f5935e35568ca18784c562"
R_SHA = "128ab4581fa8ccf1112b13615ce219cff1cb463f"
A_TREE = "/tmp/protoartoo-issue65-A"
B_TREE = "/tmp/protoartoo-issue65-B"
R_TREE = "/tmp/protoartoo-issue65-R"
RUNS = {
    "A1": LockedRun("A", A_SHA, A_TREE, ()),
    "B1": LockedRun("B", B_SHA, B_TREE, ("A1",)),
    "A2": LockedRun("A", A_SHA, A_TREE, ("A1", "B1")),
    "B2": LockedRun("B", B_SHA, B_TREE, ("A1", "B1", "A2")),
    "R1": LockedRun("R", R_SHA, R_TREE, ("A1", "B1", "A2")),
}
PRIOR_ORDER = ("A1", "B1", "A2", "R1")


def build_parser() -> PlannerArgumentParser:
    parser = PlannerArgumentParser(
        description=(
            "Print the immutable Issue 65 live A/B plan. This tool is plan-only; "
            "runtime execution is owned by issue65_live_ab_runtime.py."
        )
    )
    parser.add_argument("--run", required=True, choices=tuple(RUNS))
    parser.add_argument("--controller", default=DEFAULT_CONTROLLER)
    parser.add_argument("--serial-port", default=DEFAULT_SERIAL_PORT)
    parser.add_argument(
        "--allow-b2",
        action="store_true",
        help="request B2 planning review after A1, B1, and A2",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print JSON without writes, probes, or network access (required)",
    )
    return parser


def validate_controller(value: str) -> str:
    if not value or value != value.strip() or any(char.isspace() for char in value):
        raise PlanError("--controller must be one IPv4 address or hostname")
    parsed = urlsplit(f"//{value}")
    try:
        port = parsed.port
    except ValueError as error:
        raise PlanError("--controller contains an invalid port") from error
    invalid = (parsed.username is not None or parsed.password is not None or
               port is not None or parsed.path or parsed.query or parsed.fragment or
               not parsed.hostname or parsed.hostname != value or
               not CONTROLLER_RE.fullmatch(value) or value.endswith("-"))
    if invalid:
        raise PlanError(
            "--controller must not include a scheme, port, path, query, or fragment"
        )
    return value


def validate_serial_port(value: str) -> str:
    path = Path(value)
    if not path.is_absolute() or ".." in path.parts or not PATH_RE.fullmatch(value):
        raise PlanError("--serial-port must be a safe absolute path without '..'")
    return str(path)


def failed_allocation_evidence() -> dict[str, object]:
    """Return the issue-authorized immutable-build evidence contract."""
    return {
        "globalFailedAllocs": None,
        "availability": FAILED_ALLOC_AVAILABILITY,
        "observations": {
            "pre-load": None,
            "load": None,
            "cooldown": None,
        },
        "positiveEvidenceOnly": True,
        "positiveEvents": [],
        "proxiesUsed": False,
        "writeErrMemIsFailedAllocs": False,
        "authorizationUrl": FAILED_ALLOC_CONTRACT_URL,
        "limitation": FAILED_ALLOC_LIMITATION,
    }


def command(*parts: object) -> str:
    """Join planner-owned, validated command tokens for display."""
    return " ".join(str(part) for part in parts)


def build_plan(args: argparse.Namespace) -> dict[str, object]:
    locked = RUNS[args.run]
    controller = validate_controller(args.controller)
    serial_port = validate_serial_port(args.serial_port)
    failed_alloc = failed_allocation_evidence()
    if args.run == "B2" and not args.allow_b2:
        raise PlanError("B2 is gated; pass --allow-b2 after A1, B1, and A2 complete")
    if args.run != "B2" and args.allow_b2:
        raise PlanError("--allow-b2 is valid only with --run B2")

    worktree = locked.worktree
    evidence = EVIDENCE_ROOT / args.run
    origin = f"http://{controller}"
    restore = command(
        "git", "-C", worktree, "restore", f"--source={locked.commit}",
        "--", *VERSION_FILES,
    )
    build = command("pio", "run", "--project-dir", worktree, "-e", OTA_ENV)
    uploadfs = command(
        "pio", "run", "--project-dir", worktree, "-e", OTA_ENV,
        "-t", "uploadfs", "--upload-port", controller,
    )
    firmware_upload = command(
        "python3", f"{worktree}/tools/ota_upload.py", "--env", OTA_ENV,
        "--host", controller, "--timeout", 60, "--transfer-timeout", 60,
        "--file", f"{worktree}/.pio/build/{OTA_ENV}/firmware.bin",
    )
    browser_capture = command(
        "node", BROWSER_COLLECTOR, "--run", args.run,
        "--url", f"{origin}/wifi.html", "--out", evidence / "browser",
        "--control-file", evidence / "control.json",
    )
    identity_dir = evidence / "identity"
    prepare_identity_dir = command("mkdir", "-p", identity_dir)
    copy_firmware_identity = command(
        "cp", f"{worktree}/data/fw-version.json", identity_dir / "fw-version.json"
    )
    hash_firmware = command(
        "sha256sum", f"{worktree}/.pio/build/{OTA_ENV}/firmware.bin",
        ">", identity_dir / "firmware.sha256",
    )
    copy_filesystem_identity = command(
        "cp", f"{worktree}/data/fs-version.json", identity_dir / "fs-version.json"
    )
    hash_filesystem = command(
        "sha256sum", f"{worktree}/.pio/build/{OTA_ENV}/littlefs.bin",
        ">", identity_dir / "littlefs.sha256",
    )
    blockers = [
        {
            "code": "PREFLIGHT_NOT_PROBED",
            "status": "UNRESOLVED",
            "detail": (
                "dry-run does not inspect commands, serial port, collector, "
                "worktrees, evidence paths, or controller reachability"
            ),
        },
    ]
    if locked.required_completed_runs:
        blockers.append(
            {
                "code": "RUN_ORDER_UNVERIFIED",
                "status": "BLOCKED" if args.run == "B2" else "UNRESOLVED",
                "detail": (
                    "B2 remains ambiguous after A1/B1/A2 and requires explicit "
                    "post-run review before admission"
                    if args.run == "B2"
                    else "completion evidence required for "
                    + ", ".join(locked.required_completed_runs)
                ),
            }
        )
    evidence_files = (
        "manifest.json", "build.log", "uploadfs.log", "firmware-upload.log",
        "serial.log", "ping.ndjson", "status.ndjson", "control.json",
        "cooldown-status.ndjson", "outcome.json", "identity/fw-version.json",
        "identity/fs-version.json", "identity/firmware.sha256",
        "identity/littlefs.sha256", "browser/browser-manifest.json",
        "browser/network.ndjson", "browser/console.ndjson",
        "browser/page-errors.ndjson", "browser/page-state.json", "browser/dom.html",
        "browser/final-viewport.png", "browser/final-full.png",
    )
    preflight_checks = [
        {"name": "serial-port-exists", "subject": serial_port, "status": "UNRESOLVED"},
        *[
            {"name": "command-available", "subject": name, "status": "UNRESOLVED"}
            for name in ("git", "pio", "ping", "node")
        ],
        {
            "name": "browser-collector-exists",
            "subject": str(BROWSER_COLLECTOR),
            "status": "UNRESOLVED",
        },
        {
            "name": "evidence-directory-absent",
            "subject": str(evidence),
            "status": "UNRESOLVED",
            "failure": "BLOCKED_NO_OVERWRITE",
        },
    ]

    return {
        "schemaVersion": 1,
        "issue": ISSUE,
        "mode": "plan-only",
        "dryRun": True,
        "sideEffects": {"writes": 0, "networkRequests": 0, "probes": 0},
        "readyForExecute": False,
        "run": {
            "id": args.run,
            "role": locked.role,
            "commit": locked.commit,
            "priorOrder": list(PRIOR_ORDER),
            "requiredCompletedRuns": list(locked.required_completed_runs),
            "b2Requested": args.run == "B2" and args.allow_b2,
            "b2Allowed": False,
        },
        "target": {
            "controller": controller,
            "serialPort": serial_port,
            "powerMode": "USB cable only",
            "productionEnvironment": PRODUCTION_ENV,
            "otaBuildEnvironment": OTA_ENV,
            "otaExtendsProductionEnvironment": True,
        },
        "paths": {
            "worktree": worktree,
            "evidenceDirectory": str(evidence),
            "browserCollector": str(BROWSER_COLLECTOR),
            "runtimeCoordinator": str(RUNTIME_COORDINATOR),
        },
        "preflight": {
            "probePolicy": "REPORT_ONLY_DO_NOT_PROBE_IN_DRY_RUN",
            "checks": preflight_checks,
            "failedAllocationContract": {
                "status": "AUTHORIZED",
                "authorizationUrl": FAILED_ALLOC_CONTRACT_URL,
                "globalCounterEvaluable": False,
                "positiveEvidenceStopsRun": True,
            },
            "blockers": blockers,
        },
        "failedAllocationEvidence": failed_alloc,
        "commands": {
            "worktreeAdd": command(
                "git", "worktree", "add", "--detach", worktree, locked.commit
            ),
            "generatedVersionRestoreBeforeEveryPio": restore,
            "prepareIdentityEvidence": prepare_identity_dir,
            "buildFirmware": [
                restore,
                build,
                prepare_identity_dir,
                copy_firmware_identity,
                hash_firmware,
                restore,
            ],
            "uploadFilesystemWithCleanRestore": [
                restore,
                uploadfs,
                prepare_identity_dir,
                copy_filesystem_identity,
                hash_filesystem,
                restore,
            ],
            "uploadPrebuiltFirmware": firmware_upload,
            "browserCapture": browser_capture,
            "chronology": (
                "record build completion, filesystem upload start/end, firmware "
                "upload start/end, physical cycle, settle, browser, and cooldown "
                "timestamps in manifest.json"
            ),
        },
        "fixture": {
            "physicalCycle": {
                "requiredBeforeRun": True,
                "steps": [
                    "Disconnect the controller USB power cable.",
                    "Wait until the controller is fully unpowered.",
                    (
                        "Start the serial watcher/capture before reconnect; if the "
                        "port is absent, attach immediately when it re-enumerates."
                    ),
                    "Reconnect USB power; do not connect other hardware.",
                    "Retain serial capture through cooldown or any stop condition.",
                ],
                "powerCycleIsRecovery": False,
            },
            "serial": {
                "command": command(
                    "python3", f"{worktree}/tools/serial_monitor.py",
                    "--port", serial_port, "--stream",
                ),
                "artifact": str(evidence / "serial.log"),
                "lifetime": "pre-reconnect/re-enumeration through cooldown or stop",
            },
            "ping": {
                "command": command("ping", "-c", 1, "-W", 1, controller),
                "artifact": str(evidence / "ping.ndjson"),
                "meaning": "network-layer reachability only",
                "sampleEverySeconds": 1,
                "maxOutstanding": 1,
            },
            "settle": {
                "durationSeconds": 90,
                "statusSamplingStartsBeforeSettle": True,
                "browserPageLoads": 0,
                "unrelatedHttpRequests": 0,
            },
            "status": {
                "url": f"{origin}/api/status",
                "artifact": str(evidence / "status.ndjson"),
                "sampleEverySeconds": 5,
                "requestDeadlineSeconds": 1,
                "maxOutstanding": 1,
                "startsBeforeSettle": True,
                "unchangedAcrossRuns": ["A1", "B1", "A2", "B2", "R1"],
                "diagnosticLossStopAfterSeconds": 30,
                "globalFailedAllocs": None,
                "failedAllocationAvailability": FAILED_ALLOC_AVAILABILITY,
                "positiveAllocationEvidenceStopsRun": True,
                "writeErrMemIsFailedAllocs": False,
            },
            "browser": {
                "command": browser_capture,
                "navigationCount": 1,
                "observationSeconds": 30,
                "controlFile": str(evidence / "control.json"),
            },
            "cooldown": {
                "artifact": str(evidence / "cooldown-status.ndjson"),
                "sampleEverySeconds": 5,
                "normalMaximumSeconds": 15,
                "requiredConsecutiveQualifyingSamples": 2,
                "success": (
                    "two consecutive heapLargest8bit samples within +/-2000 bytes "
                    "of the session baseline and not below 12000 bytes"
                ),
                "cadenceDecision": (
                    "ADR 0017 describes 2-3 second cooldown sampling, but Issue "
                    "#65 locks one unchanged 5-second status cadence; #65 wins."
                ),
            },
            "crossRunControls": {
                "sameBrowser": True,
                "sameBrowserVersion": True,
                "sameStatusAndPingCadence": True,
                "recordUploadTimestampsAndFullChronology": True,
            },
            "outOfScope": ["rapid-refresh fixture"],
        },
        "vocabulary": {
            "outcomes": [
                "Usable Page",
                "Page Failure",
                "HTTP Blackout",
                "Unexpected controller failure",
                "UNKNOWN",
            ],
            "recoveryFacts": ["Power-Cycle Recovery"],
            "stopReasons": [
                "panic",
                "unexpected reset",
                "positive allocation-failure evidence",
                "loss of ICMP",
                "HTTP Blackout",
                "operator interrupt",
            ],
            "definitions": {
                "Usable Page": (
                    "the operator page reaches the locked usable gate while "
                    "/api/status remains reachable"
                ),
                "Page Failure": (
                    "the operator page is unusable or fails to finish loading "
                    "while /api/status still responds"
                ),
                "HTTP Blackout": (
                    "the operator UI and /api/status are continuously unreachable "
                    "for 30 seconds while ping continues"
                ),
                "Unexpected controller failure": (
                    "panic, unexpected reset, positive allocation-failure "
                    "evidence, or loss of ICMP stops the run"
                ),
                "Power-Cycle Recovery": (
                    "physically removing and restoring controller power restores "
                    "HTTP; this is evidence of failed self-recovery, not a pass"
                ),
                "UNKNOWN": "the captured evidence is insufficient to classify",
            },
        },
        "expectedEvidenceBundle": {
            "root": str(evidence),
            "noOverwrite": True,
            "files": [str(evidence / name) for name in evidence_files],
            "artifactIdentity": {
                "expectedCommitFullSha": locked.commit,
                "firmware": {
                    "generatedIdentitySource": f"{worktree}/data/fw-version.json",
                    "identityArtifact": str(evidence / "identity/fw-version.json"),
                    "image": f"{worktree}/.pio/build/{OTA_ENV}/firmware.bin",
                    "digestArtifact": str(evidence / "identity/firmware.sha256"),
                    "manifestDigest": "SHA256(firmware.bin)",
                },
                "filesystem": {
                    "generatedIdentitySource": f"{worktree}/data/fs-version.json",
                    "identityArtifact": str(evidence / "identity/fs-version.json"),
                    "image": f"{worktree}/.pio/build/{OTA_ENV}/littlefs.bin",
                    "digestArtifact": str(evidence / "identity/littlefs.sha256"),
                    "manifestDigest": "SHA256(littlefs.bin)",
                },
                "captureBeforeGeneratedVersionRestore": True,
                "manifestRecordsUploadTimestampsAndChronology": True,
                "manifestBindings": [
                    "run.role",
                    "run.commit full SHA",
                    "identity/fw-version.json",
                    "identity/fs-version.json",
                    "identity/firmware.sha256",
                    "identity/littlefs.sha256",
                ],
            },
        },
    }


def main(argv: list[str]) -> int:
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
        if not args.dry_run:
            raise PlanError(
                "this tool is plan-only; use --dry-run or invoke "
                "tools/issue65_live_ab_runtime.py with explicit --execute"
            )
        plan = build_plan(args)
    except PlanError as error:
        sys.stderr.write(f"ERROR: {error}\n")
        return 1
    sys.stdout.write(f"{json.dumps(plan, indent=2)}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
