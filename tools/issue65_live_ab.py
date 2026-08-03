#!/usr/bin/env python3
"""Print the locked, no-write execution plan for GitHub issue #65 live A/B runs.

This planning-only slice validates immutable inputs and emits a JSON fixture for
the later runtime coordinator. It never touches worktrees, evidence directories,
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
DEFAULT_CONTROLLER = "10.0.0.22"
DEFAULT_SERIAL_PORT = "/dev/ttyUSB0"
PRODUCTION_ENV = "protoArtoo_chirp"
OTA_ENV = "protoArtoo_chirp_ota"
VERSION_FILES = ("data/fw-version.json", "data/fs-version.json")
AUTH_PREFIX = "https://github.com/mattiasbrandt/protoArtoo/issues/65#issuecomment-"
FIELD_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)+$")
AUTH_RE = re.compile(
    r"^https://github\.com/mattiasbrandt/protoArtoo/issues/65#issuecomment-[0-9]+$"
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
A_TREE = "/tmp/protoartoo-issue65-A"
B_TREE = "/tmp/protoartoo-issue65-B"
RUNS = {
    "A1": LockedRun("A", A_SHA, A_TREE, ()),
    "B1": LockedRun("B", B_SHA, B_TREE, ("A1",)),
    "A2": LockedRun("A", A_SHA, A_TREE, ("A1", "B1")),
    "B2": LockedRun("B", B_SHA, B_TREE, ("A1", "B1", "A2")),
}
PRIOR_ORDER = ("A1", "B1", "A2")


def build_parser() -> PlannerArgumentParser:
    parser = PlannerArgumentParser(
        description=(
            "Print the immutable Issue 65 live A/B plan. Runtime execution is "
            "not implemented; --dry-run is currently required."
        )
    )
    parser.add_argument("--run", required=True, choices=tuple(RUNS))
    parser.add_argument("--controller", default=DEFAULT_CONTROLLER)
    parser.add_argument("--serial-port", default=DEFAULT_SERIAL_PORT)
    parser.add_argument(
        "--failed-alloc-field",
        help="authorized dotted production failed-allocation counter field",
    )
    parser.add_argument(
        "--authorization-url",
        help="Issue #65 comment URL authorizing the production counter field",
    )
    parser.add_argument(
        "--allow-b2", action="store_true", help="admit B2 after A1, B1, and A2"
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


def validate_failed_alloc_source(args: argparse.Namespace) -> dict[str, object]:
    field, authorization = args.failed_alloc_field, args.authorization_url
    if bool(field) != bool(authorization):
        raise PlanError(
            "--failed-alloc-field and --authorization-url must be provided together"
        )
    if not field:
        return {
            "status": "BLOCKED",
            "field": None,
            "authorizationUrl": None,
            "reason": (
                "Both a dotted --failed-alloc-field and an Issue #65 comment "
                "authorization URL are required."
            ),
            "writeErrMemIsFailedAllocs": False,
        }
    if not FIELD_RE.fullmatch(field):
        raise PlanError("--failed-alloc-field must be a dotted field path")
    if field.split(".")[-1] == "writeErrMem":
        raise PlanError(
            "writeErrMem records write-memory errors; it must never be labeled "
            "or used as failedAllocs"
        )
    if not authorization.startswith(AUTH_PREFIX):
        raise PlanError(f"--authorization-url must start with {AUTH_PREFIX}")
    if not AUTH_RE.fullmatch(authorization):
        raise PlanError(
            "--authorization-url must be an exact numeric Issue #65 comment URL"
        )
    return {
        "status": "REQUIRES_LIVE_SCHEMA_VERIFICATION",
        "field": field,
        "authorizationUrl": authorization,
        "reason": (
            "Authorization is present, but the field requires live schema "
            "verification before execute."
        ),
        "writeErrMemIsFailedAllocs": False,
    }


def command(*parts: object) -> str:
    """Join planner-owned, validated command tokens for display."""
    return " ".join(str(part) for part in parts)


def build_plan(args: argparse.Namespace) -> dict[str, object]:
    locked = RUNS[args.run]
    controller = validate_controller(args.controller)
    serial_port = validate_serial_port(args.serial_port)
    failed_alloc = validate_failed_alloc_source(args)
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
    blockers = [
        {
            "code": "RUNTIME_NOT_IMPLEMENTED",
            "status": "BLOCKED",
            "detail": "runtime execution not implemented in this slice",
        },
        {
            "code": "PREFLIGHT_NOT_PROBED",
            "status": "UNRESOLVED",
            "detail": (
                "dry-run does not inspect commands, serial port, collector, "
                "worktrees, evidence paths, or controller reachability"
            ),
        },
        {
            "code": "FAILED_ALLOC_SOURCE",
            "status": failed_alloc["status"],
            "detail": failed_alloc["reason"],
        },
    ]
    if locked.required_completed_runs:
        blockers.append(
            {
                "code": "RUN_ORDER_UNVERIFIED",
                "status": "UNRESOLVED",
                "detail": "completion evidence required for "
                + ", ".join(locked.required_completed_runs),
            }
        )
    evidence_files = (
        "manifest.json", "build.log", "uploadfs.log", "firmware-upload.log",
        "serial.log", "ping.ndjson", "status.ndjson", "control.json",
        "cooldown-status.ndjson", "outcome.json", "browser/browser-manifest.json",
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
            "b2Allowed": args.run == "B2" and args.allow_b2,
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
        },
        "preflight": {
            "probePolicy": "REPORT_ONLY_DO_NOT_PROBE_IN_DRY_RUN",
            "checks": preflight_checks,
            "failedAllocationSource": failed_alloc,
            "blockers": blockers,
        },
        "commands": {
            "worktreeAdd": command(
                "git", "worktree", "add", "--detach", worktree, locked.commit
            ),
            "generatedVersionRestoreBeforeEveryPio": restore,
            "buildFirmware": [restore, build, restore],
            "uploadFilesystemWithCleanRestore": [restore, uploadfs, restore],
            "uploadPrebuiltFirmware": firmware_upload,
            "browserCapture": browser_capture,
        },
        "fixture": {
            "physicalCycle": {
                "requiredBeforeRun": True,
                "steps": [
                    "Disconnect the controller USB power cable.",
                    "Wait until the controller is fully unpowered.",
                    "Reconnect USB power; do not connect other hardware.",
                    "Start serial capture before judging boot completion.",
                ],
                "powerCycleIsRecovery": False,
            },
            "serial": {
                "command": command(
                    "python3", f"{worktree}/tools/serial_monitor.py",
                    "--port", serial_port, "--duration", 45,
                ),
                "artifact": str(evidence / "serial.log"),
            },
            "ping": {
                "command": command("ping", "-c", 1, "-W", 1, controller),
                "artifact": str(evidence / "ping.ndjson"),
                "meaning": "network-layer reachability only",
            },
            "status": {
                "url": f"{origin}/api/status",
                "artifact": str(evidence / "status.ndjson"),
                "diagnosticLossStopAfterSeconds": 30,
                "failedAllocationField": failed_alloc["field"],
                "schemaVerificationRequired": True,
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
                "sampleEverySeconds": "2-3",
                "normalMaximumSeconds": 15,
                "success": (
                    "two consecutive heapLargest8bit samples within +/-2000 bytes "
                    "of the session baseline and not below 12000 bytes"
                ),
                "rapidRefreshThreeTabDuration": "OPEN_PENDING_CONTROLLED_REPEAT",
            },
        },
        "vocabulary": {
            "outcomes": [
                "PASS", "PAGE_FAILURE", "HTTP_BLACKOUT", "STOPPED", "BLOCKED"
            ],
            "browserCaptureStatuses": [
                "usable", "browser-failure-observed", "stopped"
            ],
            "stopReasons": [
                "panic", "unexpected-reset", "sustained-diagnostics-loss",
                "operator-interrupt", "external-termination",
            ],
            "definitions": {
                "PAGE_FAILURE": "operator page unusable while /api/status responds",
                "HTTP_BLACKOUT": (
                    "operator page and /api/status unreachable while ping responds"
                ),
                "POWER_CYCLE_RECOVERY": (
                    "physical power removal restores HTTP; evidence of failed "
                    "self-recovery, never a pass"
                ),
            },
        },
        "expectedEvidenceBundle": {
            "root": str(evidence),
            "noOverwrite": True,
            "files": [str(evidence / name) for name in evidence_files],
        },
    }


def main(argv: list[str]) -> int:
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
        if not args.dry_run:
            raise PlanError("runtime execution not implemented in this slice; use --dry-run")
        plan = build_plan(args)
    except PlanError as error:
        sys.stderr.write(f"ERROR: {error}\n")
        return 1
    sys.stdout.write(f"{json.dumps(plan, indent=2)}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
