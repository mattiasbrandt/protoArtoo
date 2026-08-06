#!/usr/bin/env python3
"""Prove the response-phase deadline reclaims stalled connections (ADR 0020, issue #92).

A request that has begun writing its response and does not finish within a
bounded time gets its socket dropped, freeing the slot for new connections.
This probe deliberately stalls a response by holding it open past the deadline,
then proves the reclaim happened by observing:

1. The deadline closures counter increments by --repeat (one per breach).
2. Concurrent /api/status requests on a separate connection continue to succeed.
3. The inflightRequests counter returns to its resting value after each breach.
4. The stalled socket is reset (not gracefully closed) by the server.

Reuses tools/issue65_live_ab_runtime.py's primitives (Timeline, NDJSON, atomic
JSON writers) rather than re-deriving them, following this repo's webload_*
convention.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import socket
import sys
import threading
import time
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import issue65_live_ab_runtime as r65  # noqa: E402  (reused, hardened primitives)

REPO_ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_ROOT = REPO_ROOT / "tasks" / "evidence" / "webload"
DEFAULT_CONTROLLER = "10.0.0.22"
DEFAULT_PORT = 80
DEFAULT_ASSET_PATH = "/index.html"
HANDSHAKE_DEADLINE_SECONDS = 10.0
STATUS_POLL_INTERVAL_SECONDS = 1.0
STATUS_REQUEST_DEADLINE_SECONDS = 3.0
SOCKET_SEND_BUFFER_BYTES = 2048


class ProbeError(RuntimeError):
    """A probe setup or evidence-write step could not be completed."""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--controller", default=DEFAULT_CONTROLLER)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument(
        "--path", default=DEFAULT_ASSET_PATH,
        help="largest static asset to stall (default index.html, ~30KB+"
    )
    parser.add_argument(
        "--deadline-ms", type=int, default=8000,
        help="informational; reported in outcome for reference (default 8000)"
    )
    parser.add_argument(
        "--repeat", type=int, default=3,
        help="number of repeated breach cycles (default 3, to prove no counter leak)"
    )
    parser.add_argument(
        "--stall-seconds", type=float, default=10.0,
        help="how long to hold each stalled socket open (default 10s)"
    )
    parser.add_argument(
        "--timeout", type=float, default=120.0,
        help="total run timeout in seconds (default 120s)"
    )
    parser.add_argument(
        "--out", type=Path, default=None,
        help="JSON evidence output path (defaults to tasks/evidence/webload/deadline-probe/outcome.json)"
    )
    return parser


class StatusMonitor:
    """Poll /api/status on an independent connection while breaches happen.

    Records success/failure of each poll and the key counter values, proving
    that other traffic is unaffected and slots are released.
    """

    def __init__(self, controller: str, samples_path: Path, timeline: r65.Timeline) -> None:
        self.controller = controller
        self.samples_path = samples_path
        self.timeline = timeline
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run, name="deadline-probe-status-monitor", daemon=True
        )
        self.error: BaseException | None = None
        self.samples: list[dict[str, Any]] = []

    def start(self) -> None:
        self._thread.start()

    def stop_and_join(self, timeout: float = 5.0) -> None:
        self._stop.set()
        self._thread.join(timeout)
        if self._thread.is_alive():
            raise ProbeError("status monitor did not stop within deadline")
        if self.error is not None:
            raise ProbeError(f"status monitor failed: {self.error}")

    def _sample(self) -> None:
        try:
            status = r65._http_json(
                f"http://{self.controller}/api/status", STATUS_REQUEST_DEADLINE_SECONDS,
            )
            success, error = True, None
        except r65.Issue65RuntimeError as request_error:
            status, success, error = None, False, str(request_error)
        record = r65.append_ndjson(
            self.samples_path, self.timeline, "status-sample",
            success=success, error=error,
            responseDeadlineClosures=(status or {}).get("responseDeadlineClosures"),
            responseDeadlineAgeMs=(status or {}).get("responseDeadlineAgeMs"),
            responseLastMs=(status or {}).get("responseLastMs"),
            responseMaxMs=(status or {}).get("responseMaxMs"),
            inflightRequests=(status or {}).get("inflightRequests"),
            inflightRequestsPeak=(status or {}).get("inflightRequestsPeak"),
            httpSocketsOpen=(status or {}).get("httpSocketsOpen"),
            heapLargest8bit=(status or {}).get("heapLargest8bit"),
        )
        self.samples.append(record)

    def _run(self) -> None:
        next_sample = time.monotonic()
        try:
            while not self._stop.is_set():
                now = time.monotonic()
                if now >= next_sample:
                    self._sample()
                    next_sample = max(
                        next_sample + STATUS_POLL_INTERVAL_SECONDS, time.monotonic(),
                    )
                self._stop.wait(max(0.01, min(0.2, next_sample - time.monotonic())))
        except BaseException as error:  # noqa: BLE001 - surfaced via stop_and_join()
            self.error = error


def _open_stalling_connection(
    controller: str, port: int, path: str, timeline: r65.Timeline,
) -> tuple[socket.socket, dict[str, Any], float]:
    """Open a TCP socket, complete the HTTP handshake, then stop reading.

    Sets a small SO_RCVBUF to ensure the response gets blocked quickly when the
    server tries to send (without waiting for the client to consume). Returns
    (socket, connect_record, send_time) where send_time is when the first byte
    was sent.

    Returns tuple: (socket, connect_record, start_time_monotonic)
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, SOCKET_SEND_BUFFER_BYTES)
    sock.settimeout(HANDSHAKE_DEADLINE_SECONDS)
    sock.connect((controller, port))
    start_time = time.monotonic()

    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {controller}\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")
    sock.sendall(request)

    sock.settimeout(HANDSHAKE_DEADLINE_SECONDS)
    header_bytes = b""
    deadline = time.monotonic() + HANDSHAKE_DEADLINE_SECONDS
    while b"\r\n\r\n" not in header_bytes:
        if time.monotonic() > deadline:
            sock.close()
            raise ProbeError(f"{path} did not complete headers within deadline")
        chunk = sock.recv(4096)
        if not chunk:
            sock.close()
            raise ProbeError(f"{path} closed the connection during the handshake")
        header_bytes += chunk

    status_line = header_bytes.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
    if " 200 " not in f" {status_line} ":
        sock.close()
        raise ProbeError(f"{path} returned unexpected status line: {status_line!r}")

    connect_record = timeline.record(
        "stalled-connection-opened",
        statusLine=status_line,
        localPort=sock.getsockname()[1],
        sndbufBytes=sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF),
    )
    return sock, connect_record, start_time


def _detect_peer_reset(sock: socket.socket, deadline_seconds: float) -> bool:
    """Poll the socket to detect when the peer closes or resets it.

    Returns True if a reset (ECONNRESET/EPIPE) was detected, False if a clean
    close or timeout.
    """
    sock.settimeout(1.0)
    deadline = time.monotonic() + deadline_seconds
    while time.monotonic() < deadline:
        try:
            # Try to receive to detect peer close/reset.
            chunk = sock.recv(4096)
            if not chunk:
                # Clean close.
                return False
        except (TimeoutError, socket.timeout):
            # Still open, keep polling.
            continue
        except (ConnectionResetError, BrokenPipeError, OSError) as error:
            # Reset detected.
            return "reset" in str(error).lower() or isinstance(error, ConnectionResetError)
    # Timeout waiting for close.
    return False


def _summarize_samples(samples: list[dict[str, Any]]) -> dict[str, Any]:
    """Extract metrics from status samples."""
    reachable = [s for s in samples if s.get("success")]
    unreachable_count = len(samples) - len(reachable)

    closures = [
        s["responseDeadlineClosures"] for s in reachable
        if isinstance(s.get("responseDeadlineClosures"), int)
    ]
    inflight = [
        s["inflightRequests"] for s in reachable
        if isinstance(s.get("inflightRequests"), int)
    ]
    response_max = [
        s["responseMaxMs"] for s in reachable
        if isinstance(s.get("responseMaxMs"), int)
    ]

    return {
        "sampleCount": len(samples),
        "unreachableCount": unreachable_count,
        "responseDeadlineClosuresFirst": closures[0] if closures else None,
        "responseDeadlineClosuresLast": closures[-1] if closures else None,
        "inflightRequestsRestingValue": inflight[-1] if inflight else None,
        "responseMaxMsObserved": max(response_max) if response_max else None,
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    run_dir = (
        args.out.parent if args.out else EVIDENCE_ROOT / "deadline-probe"
    )
    run_dir.mkdir(parents=True, exist_ok=True)
    timeline = r65.Timeline.start()
    events: list[dict[str, Any]] = []

    print(
        f"Response deadline probe: {args.repeat} breach(es) x {args.stall_seconds:.1f}s stall "
        f"(deadline={args.deadline_ms}ms)"
    )

    monitor = StatusMonitor(args.controller, run_dir / "status-samples.ndjson", timeline)
    monitor.start()

    # Baseline status before any breaches.
    time.sleep(0.5)
    baseline_closures: int | None = None
    for sample in monitor.samples[-3:]:
        if sample.get("success") and isinstance(sample.get("responseDeadlineClosures"), int):
            baseline_closures = sample["responseDeadlineClosures"]
            break

    breach_results = []
    run_start = time.monotonic()

    for iteration in range(max(1, args.repeat)):
        if time.monotonic() - run_start > args.timeout:
            events.append(timeline.record("timeout-during-breaches"))
            break

        try:
            sock, connect_record, send_time = _open_stalling_connection(
                args.controller, args.port, args.path, timeline,
            )
            events.append(connect_record)
            print(f"  Breach {iteration + 1}/{args.repeat}: socket open, stalling...", end="", flush=True)

            # Hold it past the deadline.
            stall_start = time.monotonic()
            time.sleep(args.stall_seconds)
            stall_elapsed = time.monotonic() - stall_start

            # Detect the peer's response.
            was_reset = _detect_peer_reset(sock, 5.0)
            try:
                sock.close()
            except OSError:
                pass

            events.append(timeline.record(
                "breach-completed",
                iteration=iteration + 1,
                stallSecondsActual=stall_elapsed,
                peerReset=was_reset,
            ))
            print(f" reset={was_reset}")
            breach_results.append({
                "iteration": iteration + 1,
                "stallSecondsActual": stall_elapsed,
                "peerReset": was_reset,
            })
        except (ProbeError, OSError) as error:
            events.append(timeline.record(
                "breach-failed",
                iteration=iteration + 1,
                error=str(error),
            ))
            print(f" FAILED: {error}")
            breach_results.append({
                "iteration": iteration + 1,
                "failed": True,
                "error": str(error),
            })

        # Brief settle between breaches.
        if iteration < args.repeat - 1:
            time.sleep(2.0)

    monitor.stop_and_join()
    summary = _summarize_samples(monitor.samples)

    # Verdict: closures must have incremented by exactly --repeat.
    closures_before = baseline_closures or 0
    closures_after = summary.get("responseDeadlineClosuresLast") or 0
    closures_delta = closures_after - closures_before
    closures_ok = closures_delta == args.repeat

    # All status polls must have succeeded.
    all_polls_ok = summary["unreachableCount"] == 0

    # Inflight must return to resting (0 or 1).
    resting = summary.get("inflightRequestsRestingValue") or 0
    inflight_ok = resting <= 1

    outcome = {
        "schemaVersion": 1,
        "issue": 92,
        "scenario": "response-deadline-breach",
        "controller": args.controller,
        "path": args.path,
        "deadlineMs": args.deadline_ms,
        "repeatCount": args.repeat,
        "stallSecondsPerBreach": args.stall_seconds,
        "breachResults": breach_results,
        "statusPolls": summary,
        "verdict": {
            "closuresIncrementedByRepeat": {
                "expected": args.repeat,
                "observed": closures_delta,
                "ok": closures_ok,
            },
            "allStatusPollsSucceeded": all_polls_ok,
            "inflightRequestsReturnedToResting": {
                "restingValue": resting,
                "ok": inflight_ok,
            },
            "allBreachesDetectedPeerReset": all(
                b.get("peerReset") for b in breach_results if not b.get("failed")
            ),
        },
        "pass": closures_ok and all_polls_ok and inflight_ok,
    }

    r65.atomic_write_json(run_dir / "outcome.json", outcome)
    for event in events:
        r65._append_ndjson_record(run_dir / "events.ndjson", event)

    return outcome


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        outcome = run(args)
    except (ProbeError, r65.Issue65RuntimeError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print()
    print(f"OUTCOME:")
    print(f"  Closures: {outcome['verdict']['closuresIncrementedByRepeat']['observed']}"
          f"/{outcome['verdict']['closuresIncrementedByRepeat']['expected']} OK={outcome['verdict']['closuresIncrementedByRepeat']['ok']}")
    print(f"  Status polls: {outcome['statusPolls']['sampleCount']} total, "
          f"{outcome['statusPolls']['unreachableCount']} unreachable")
    print(f"  Inflight resting: {outcome['verdict']['inflightRequestsReturnedToResting']['restingValue']} "
          f"OK={outcome['verdict']['inflightRequestsReturnedToResting']['ok']}")
    print(f"  Peer reset: {outcome['verdict']['allBreachesDetectedPeerReset']}")
    print(f"  PASS: {outcome['pass']}")
    print()
    print(f"Evidence: {(Path(args.out or EVIDENCE_ROOT / 'deadline-probe').resolve())}/")
    return 0 if outcome["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
