#!/usr/bin/env python3
"""Build the correlated evidence timeline for one issue #66 baseline run.

Reads ping.ndjson, status.ndjson, logs.ndjson, allocation-failures.ndjson,
serial.log, and browser/page-state.json from one run directory and emits a
single timestamp-ordered markdown table (timeline.md) — satisfying the
ticket's acceptance criterion 3 ("report correlates, not just narrates").
Specifically flags any /api/logs WARN/ERROR line, and calls out heap-floor
rejection lines (`rejecting <resource>: largest free block N < FLOOR`) against
where they fall relative to ping/status/DOM state changes.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Any

HEAP_FLOOR_RE = re.compile(r"rejecting .*: largest free block \d+ < \d+")
WARN_ERROR_RE = re.compile(r"^\[(W|E)\]")
SERIAL_LINE_RE = re.compile(r"^(\S+) ([\d.]+) (.*)$")


def read_ndjson(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    records = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return records


def read_json(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def flag(detail: str) -> str:
    if HEAP_FLOOR_RE.search(detail):
        return "**HEAP-FLOOR REJECTION**"
    if WARN_ERROR_RE.match(detail):
        return "WARN/ERROR"
    return ""


def rows_from_ping(records: list[dict[str, Any]]) -> list[tuple[str, str, str, str]]:
    return [
        (
            str(record.get("wallTime", "")), "ping",
            "ok" if record.get("success") else "FAILED",
            str(record.get("detail", "")),
        )
        for record in records
    ]


def rows_from_status(records: list[dict[str, Any]]) -> list[tuple[str, str, str, str]]:
    rows = []
    for record in records:
        status = record.get("status") or {}
        if record.get("success"):
            detail = (
                f"heapLargest8bit={status.get('heapLargest8bit')} "
                f"resetReason={status.get('resetReason')} "
                f"refusedHeapFloor={status.get('refusedHeapFloor')} "
                f"uptimeMs={status.get('uptimeMs')}"
            )
            event = "status ok"
        else:
            detail = str(record.get("error", ""))
            event = "status FAILED"
        rows.append((str(record.get("wallTime", "")), "status", event, detail))
    return rows


def rows_from_logs(records: list[dict[str, Any]]) -> list[tuple[str, str, str, str]]:
    rows = []
    for record in records:
        wall_time = str(record.get("wallTime", ""))
        if not record.get("success"):
            rows.append((wall_time, "logs", "poll FAILED", str(record.get("error", ""))))
            continue
        for line in record.get("newLines", []) or []:
            marker = flag(line)
            event = f"log line{f' [{marker}]' if marker else ''}"
            rows.append((wall_time, "logs", event, line))
    return rows


def rows_from_allocation_failures(records: list[dict[str, Any]]) -> list[tuple[str, str, str, str]]:
    return [
        (
            str(record.get("wallTime", "")), "allocation-failure",
            f"phase={record.get('phase')}", str(record.get("rawLine", "")),
        )
        for record in records
    ]


def rows_from_serial(path: Path) -> list[tuple[str, str, str, str]]:
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SERIAL_LINE_RE.match(line)
        if not match:
            continue
        wall_time, _elapsed, content = match.groups()
        rows.append((wall_time, "serial", "boot/reset/panic line", content))
    return rows


def rows_from_browser(
    state: dict[str, Any] | None, source: str = "browser",
) -> list[tuple[str, str, str, str]]:
    """Milestones from one capture's page-state.json.

    `source` labels which capture a row came from, since a run has more than one.
    The multi-tab collector writes the same verdict keys as the single-tab one,
    so both are read here without a second reader; milestones a given collector
    does not produce (the multi-tab scenario has no domCandidateAt/usableAt
    handshake) are simply absent and skipped.
    """
    if not state:
        return []
    rows = []
    milestones = (
        ("t0", "navigation started"),
        ("domCandidateAt", "DOM candidate ready"),
        ("usableAt", "usable (status-reachable confirmed)"),
        ("terminalAt", "observation terminated"),
    )
    for key, label in milestones:
        value = state.get(key)
        if isinstance(value, str) and value:
            rows.append((value, source, label, f"terminalReason={state.get('terminalReason')}"))
    sse_state = state.get("sseState")
    if sse_state:
        terminal_at = state.get("terminalAt")
        if isinstance(terminal_at, str):
            rows.append((terminal_at, source, "SSE final state", f"sseState={sse_state}"))
    return rows


def build_timeline(run_dir: Path) -> str:
    rows: list[tuple[str, str, str, str]] = []
    rows += rows_from_ping(read_ndjson(run_dir / "ping.ndjson"))
    rows += rows_from_status(read_ndjson(run_dir / "status.ndjson"))
    rows += rows_from_logs(read_ndjson(run_dir / "logs.ndjson"))
    rows += rows_from_allocation_failures(read_ndjson(run_dir / "allocation-failures.ndjson"))
    rows += rows_from_serial(run_dir / "serial.log")
    rows += rows_from_browser(read_json(run_dir / "browser" / "page-state.json"), "browser")
    rows += rows_from_browser(read_json(run_dir / "multitab" / "page-state.json"), "multitab")

    rows.sort(key=lambda row: row[0])

    heap_floor_hits = [row for row in rows if "HEAP-FLOOR REJECTION" in row[2]]

    lines = [
        f"# Web load correlated evidence timeline — {run_dir.name}",
        "",
        f"Total events: {len(rows)}. Heap-floor rejection lines: {len(heap_floor_hits)}.",
        "",
        "| Time (UTC) | Source | Event | Detail |",
        "|---|---|---|---|",
    ]
    for wall_time, source, event, detail in rows:
        safe_detail = detail.replace("|", "\\|").replace("\n", " ")
        lines.append(f"| {wall_time} | {source} | {event} | {safe_detail} |")

    if heap_floor_hits:
        lines += ["", "## Heap-floor rejection lines", ""]
        for wall_time, _source, _event, detail in heap_floor_hits:
            lines.append(f"- `{wall_time}` — {detail}")

    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path, help="e.g. tasks/evidence/webload/run-1")
    parser.add_argument("--out", type=Path, default=None, help="defaults to <run_dir>/timeline.md")
    args = parser.parse_args(argv)

    run_dir = args.run_dir
    if not run_dir.is_dir():
        sys.stderr.write(f"ERROR: not a directory: {run_dir}\n")
        return 1
    out_path = args.out or (run_dir / "timeline.md")
    out_path.write_text(build_timeline(run_dir), encoding="utf-8")
    sys.stdout.write(f"{out_path}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
