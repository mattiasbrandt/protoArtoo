#!/usr/bin/env python3
# =============================================================================
# tools/phase4_hw_check.py
#
# Phase 4 hardware-validation runner for artoo_esp32.
#
# Runs a focused set of API/SSE checks and writes:
#   1) machine-readable JSON report
#   2) human-readable text summary
#
# Exit code:
#   0 = all checks passed
#   2 = one or more checks failed
#   1 = script/runtime error before report write
#
# Usage example:
#   python3 tools/phase4_hw_check.py --base-url http://10.0.0.22
# =============================================================================

from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


@dataclass
class CheckResult:
    check_id: str
    name: str
    ok: bool
    details: str
    data: dict[str, Any] = field(default_factory=dict)


@dataclass
class RunReport:
    run_id: str
    started_utc: str
    finished_utc: str
    base_url: str
    checks: list[CheckResult]
    pass_count: int
    fail_count: int
    exit_code: int


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def safe_json_loads(text: str) -> tuple[bool, Any]:
    try:
        return True, json.loads(text)
    except json.JSONDecodeError:
        return False, None


def build_url(base_url: str, path: str) -> str:
    base = base_url.rstrip("/")
    if not path.startswith("/"):
        path = "/" + path
    return base + path


def http_request(
    base_url: str,
    method: str,
    path: str,
    timeout_s: float,
    form: dict[str, Any] | None = None,
    headers: dict[str, str] | None = None,
) -> tuple[int, str]:
    request_headers = dict(headers or {})
    body = None
    if form is not None:
        body = urlencode({k: str(v) for k, v in form.items()}).encode("utf-8")
        request_headers.setdefault("Content-Type", "application/x-www-form-urlencoded")

    req = Request(build_url(base_url, path), data=body, headers=request_headers, method=method)

    try:
        with urlopen(req, timeout=timeout_s) as resp:
            payload = resp.read().decode("utf-8", errors="replace")
            return int(resp.status), payload
    except HTTPError as err:
        payload = err.read().decode("utf-8", errors="replace") if err.fp else ""
        return int(err.code), payload


def check_status_reachability(base_url: str, timeout_s: float) -> tuple[CheckResult, dict[str, Any] | None]:
    try:
        status_code, body = http_request(base_url, "GET", "/api/status", timeout_s)
    except (URLError, socket.timeout) as err:
        return (
            CheckResult(
                check_id="status_reachability",
                name="Status API reachability + version capture",
                ok=False,
                details=f"request failed: {err}",
            ),
            None,
        )

    ok_json, payload = safe_json_loads(body)
    if status_code != 200:
        return (
            CheckResult(
                check_id="status_reachability",
                name="Status API reachability + version capture",
                ok=False,
                details=f"unexpected status {status_code}",
                data={"response": body[:400]},
            ),
            None,
        )
    if not ok_json or not isinstance(payload, dict):
        return (
            CheckResult(
                check_id="status_reachability",
                name="Status API reachability + version capture",
                ok=False,
                details="response is not valid JSON object",
                data={"response": body[:400]},
            ),
            None,
        )

    version = payload.get("firmwareVersion")
    if not isinstance(version, str) or not version:
        return (
            CheckResult(
                check_id="status_reachability",
                name="Status API reachability + version capture",
                ok=False,
                details="missing or empty firmwareVersion",
                data={"keys": sorted(payload.keys())[:30]},
            ),
            payload,
        )

    return (
        CheckResult(
            check_id="status_reachability",
            name="Status API reachability + version capture",
            ok=True,
            details="/api/status reachable and version captured",
            data={"firmwareVersion": version},
        ),
        payload,
    )


def post_ok_json(base_url: str, path: str, timeout_s: float, form: dict[str, Any]) -> tuple[bool, str, dict[str, Any]]:
    try:
        status_code, body = http_request(base_url, "POST", path, timeout_s, form=form)
    except (URLError, socket.timeout) as err:
        return False, f"request failed: {err}", {}

    ok_json, payload = safe_json_loads(body)
    if status_code != 200:
        return False, f"unexpected status {status_code}", {"response": body[:300]}
    if not ok_json or not isinstance(payload, dict):
        return False, "response is not valid JSON object", {"response": body[:300]}
    if payload.get("ok") is not True:
        return False, "response JSON did not report ok=true", payload
    return True, "ok", payload


def check_audio_roundtrip(
    base_url: str,
    timeout_s: float,
    status_payload: dict[str, Any] | None,
) -> CheckResult:
    details: list[str] = []
    data: dict[str, Any] = {"subchecks": []}
    all_ok = True

    # GET /api/audio/tracks
    try:
        status_code, body = http_request(base_url, "GET", "/api/audio/tracks", timeout_s)
    except (URLError, socket.timeout) as err:
        return CheckResult(
            check_id="audio_roundtrip",
            name="Audio API roundtrip (/api/audio, /api/mood, /api/audio/tracks)",
            ok=False,
            details=f"tracks request failed: {err}",
        )

    ok_json, tracks = safe_json_loads(body)
    required_keys = {
        "scream",
        "faint",
        "leia",
        "cantina_s",
        "sw_theme",
        "imp_march",
        "cantina_l",
        "startup",
        "rand_min",
        "rand_max",
        "volume",
    }
    tracks_ok = (
        status_code == 200
        and ok_json
        and isinstance(tracks, dict)
        and required_keys.issubset(tracks.keys())
    )
    if not tracks_ok:
        all_ok = False
        details.append("GET /api/audio/tracks failed schema/status check")
        data["subchecks"].append(
            {
                "name": "get_tracks",
                "ok": False,
                "status": status_code,
                "response": body[:300],
            }
        )
        current_volume = 20
    else:
        current_volume = int(tracks.get("volume", 20))
        data["subchecks"].append(
            {
                "name": "get_tracks",
                "ok": True,
                "status": status_code,
                "volume": current_volume,
            }
        )

    # POST /api/audio action=stop
    ok, msg, sub = post_ok_json(base_url, "/api/audio", timeout_s, {"action": "stop"})
    data["subchecks"].append({"name": "audio_stop", "ok": ok, "note": msg, **sub})
    all_ok = all_ok and ok

    # POST /api/audio action=volume level=<current>
    ok, msg, sub = post_ok_json(
        base_url,
        "/api/audio",
        timeout_s,
        {"action": "volume", "level": current_volume},
    )
    data["subchecks"].append({"name": "audio_volume", "ok": ok, "note": msg, **sub})
    all_ok = all_ok and ok

    # POST /api/audio action=dollar cmd=$s
    ok, msg, sub = post_ok_json(
        base_url,
        "/api/audio",
        timeout_s,
        {"action": "dollar", "cmd": "$s"},
    )
    data["subchecks"].append({"name": "audio_dollar_stop", "ok": ok, "note": msg, **sub})
    all_ok = all_ok and ok

    # POST /api/mood using safe fixed test mood with optional restore
    before_mood = status_payload.get("activeMood") if isinstance(status_payload, dict) else None
    test_mood = 11
    ok, msg, sub = post_ok_json(base_url, "/api/mood", timeout_s, {"mood": test_mood})
    data["subchecks"].append({"name": "mood_apply_test", "ok": ok, "note": msg, **sub})
    all_ok = all_ok and ok

    # Best-effort restore if we know previous mood and it was valid.
    restore_info: dict[str, Any] = {"name": "mood_restore", "attempted": False, "ok": True}
    if isinstance(before_mood, int) and before_mood in {10, 11, 13, 14} and before_mood != test_mood:
        restore_info["attempted"] = True
        ok_restore, msg_restore, sub_restore = post_ok_json(
            base_url,
            "/api/mood",
            timeout_s,
            {"mood": before_mood},
        )
        restore_info["ok"] = ok_restore
        restore_info["note"] = msg_restore
        restore_info.update(sub_restore)
        if not ok_restore:
            all_ok = False
    data["subchecks"].append(restore_info)

    if not all_ok:
        details.append("one or more audio/mood subchecks failed")

    return CheckResult(
        check_id="audio_roundtrip",
        name="Audio API roundtrip (/api/audio, /api/mood, /api/audio/tracks)",
        ok=all_ok,
        details="; ".join(details) if details else "audio endpoints responded with expected success contracts",
        data=data,
    )


def check_dome_link_observation(base_url: str, timeout_s: float) -> CheckResult:
    try:
        status_code, body = http_request(base_url, "GET", "/api/status", timeout_s)
    except (URLError, socket.timeout) as err:
        return CheckResult(
            check_id="dome_link_observation",
            name="Dome-link heartbeat/state observation",
            ok=False,
            details=f"status request failed: {err}",
        )

    ok_json, payload = safe_json_loads(body)
    if status_code != 200 or not ok_json or not isinstance(payload, dict):
        return CheckResult(
            check_id="dome_link_observation",
            name="Dome-link heartbeat/state observation",
            ok=False,
            details="/api/status not available as valid JSON for dome_link extraction",
            data={"status": status_code, "response": body[:300]},
        )

    dome_link = payload.get("dome_link")
    if not isinstance(dome_link, dict):
        return CheckResult(
            check_id="dome_link_observation",
            name="Dome-link heartbeat/state observation",
            ok=False,
            details="missing dome_link block in /api/status",
            data={"keys": sorted(payload.keys())[:40]},
        )

    allowed_states = {"disabled", "not_seen", "connected", "lost"}
    state = dome_link.get("state")
    hb_tx = dome_link.get("hb_tx")
    hb_rx = dome_link.get("hb_rx")
    last_rx_ms = dome_link.get("last_rx_ms")

    ok = (
        isinstance(state, str)
        and state in allowed_states
        and isinstance(hb_tx, int)
        and isinstance(hb_rx, int)
        and isinstance(last_rx_ms, int)
    )

    return CheckResult(
        check_id="dome_link_observation",
        name="Dome-link heartbeat/state observation",
        ok=ok,
        details=(
            f"dome_link observed: state={state} hb_tx={hb_tx} hb_rx={hb_rx} last_rx_ms={last_rx_ms}"
            if ok
            else "dome_link block present but missing/invalid state fields"
        ),
        data={"dome_link": dome_link},
    )


def check_sse_stream(base_url: str, timeout_s: float, duration_s: float) -> CheckResult:
    req = Request(
        build_url(base_url, "/api/events"),
        method="GET",
        headers={"Accept": "text/event-stream"},
    )

    counts: dict[str, int] = {"status": 0, "rc": 0, "log": 0}
    samples: dict[str, str] = {}
    current_event = ""

    try:
        with urlopen(req, timeout=timeout_s) as resp:
            end_at = time.time() + duration_s
            while time.time() < end_at:
                raw = resp.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                if line.startswith("event:"):
                    current_event = line.split(":", 1)[1].strip()
                    if current_event not in counts:
                        counts[current_event] = 0
                elif line.startswith("data:") and current_event:
                    counts[current_event] = counts.get(current_event, 0) + 1
                    samples.setdefault(current_event, line[5:].strip()[:160])
    except (URLError, socket.timeout) as err:
        return CheckResult(
            check_id="sse_stream",
            name="SSE stream sanity (/api/events)",
            ok=False,
            details=f"SSE request failed: {err}",
        )

    ok = counts.get("status", 0) > 0 and counts.get("rc", 0) > 0
    detail = (
        f"captured status={counts.get('status', 0)} rc={counts.get('rc', 0)} log={counts.get('log', 0)} events"
        if ok
        else f"missing required events: status={counts.get('status', 0)} rc={counts.get('rc', 0)}"
    )
    return CheckResult(
        check_id="sse_stream",
        name="SSE stream sanity (/api/events)",
        ok=ok,
        details=detail,
        data={"counts": counts, "samples": samples},
    )


def write_reports(report: RunReport, out_dir: str) -> tuple[str, str]:
    os.makedirs(out_dir, exist_ok=True)
    json_path = os.path.join(out_dir, f"{report.run_id}.json")
    txt_path = os.path.join(out_dir, f"{report.run_id}.txt")

    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(
            {
                "run_id": report.run_id,
                "started_utc": report.started_utc,
                "finished_utc": report.finished_utc,
                "base_url": report.base_url,
                "checks": [asdict(c) for c in report.checks],
                "summary": {
                    "pass_count": report.pass_count,
                    "fail_count": report.fail_count,
                    "exit_code": report.exit_code,
                },
            },
            f,
            indent=2,
            sort_keys=False,
        )

    lines = [
        f"Phase 4 hardware check report: {report.run_id}",
        f"Started (UTC): {report.started_utc}",
        f"Finished (UTC): {report.finished_utc}",
        f"Target: {report.base_url}",
        "",
        f"Summary: pass={report.pass_count} fail={report.fail_count} exit_code={report.exit_code}",
        "",
        "Checks:",
    ]

    for c in report.checks:
        state = "PASS" if c.ok else "FAIL"
        lines.append(f"- [{state}] {c.check_id}: {c.name}")
        lines.append(f"  {c.details}")

    with open(txt_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    return json_path, txt_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="artoo_esp32 Phase 4 hardware validation check runner"
    )
    parser.add_argument(
        "--base-url",
        default="http://10.0.0.22",
        help="Base URL for the controller (default: http://10.0.0.22)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Per-request timeout in seconds (default: 5.0)",
    )
    parser.add_argument(
        "--sse-duration",
        type=float,
        default=6.0,
        help="Seconds to collect SSE events from /api/events (default: 6.0)",
    )
    parser.add_argument(
        "--out-dir",
        default="artifacts/phase4_hw",
        help="Directory for report artifacts (default: artifacts/phase4_hw)",
    )
    parser.add_argument(
        "--label",
        default="",
        help="Optional run label prefix for report filename",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    started = utc_now_iso()

    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    prefix = args.label.strip().replace(" ", "_")
    run_id = f"{prefix + '_' if prefix else ''}phase4_hw_{stamp}"

    checks: list[CheckResult] = []

    try:
        status_check, status_payload = check_status_reachability(args.base_url, args.timeout)
        checks.append(status_check)

        checks.append(check_audio_roundtrip(args.base_url, args.timeout, status_payload))
        checks.append(check_dome_link_observation(args.base_url, args.timeout))
        checks.append(check_sse_stream(args.base_url, args.timeout, args.sse_duration))

        pass_count = sum(1 for c in checks if c.ok)
        fail_count = len(checks) - pass_count
        exit_code = 0 if fail_count == 0 else 2

        report = RunReport(
            run_id=run_id,
            started_utc=started,
            finished_utc=utc_now_iso(),
            base_url=args.base_url,
            checks=checks,
            pass_count=pass_count,
            fail_count=fail_count,
            exit_code=exit_code,
        )

        json_path, txt_path = write_reports(report, args.out_dir)
        print(f"[phase4-hw] report json: {json_path}")
        print(f"[phase4-hw] report text: {txt_path}")
        print(f"[phase4-hw] pass={pass_count} fail={fail_count} exit={exit_code}")
        return exit_code

    except Exception as err:
        # Best effort: write a crash report so failures are still observable.
        crash_check = CheckResult(
            check_id="runner_error",
            name="Runner internal error",
            ok=False,
            details=str(err),
        )
        report = RunReport(
            run_id=run_id,
            started_utc=started,
            finished_utc=utc_now_iso(),
            base_url=args.base_url,
            checks=checks + [crash_check],
            pass_count=sum(1 for c in checks if c.ok),
            fail_count=sum(1 for c in checks if not c.ok) + 1,
            exit_code=1,
        )
        try:
            json_path, txt_path = write_reports(report, args.out_dir)
            print(f"[phase4-hw] crash report json: {json_path}")
            print(f"[phase4-hw] crash report text: {txt_path}")
        except Exception:
            pass
        print(f"ERROR: phase4 hardware check runner failed: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
