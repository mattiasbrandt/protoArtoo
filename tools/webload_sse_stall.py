#!/usr/bin/env python3
"""Stalled-SSE-client scenario for issue #73.

Neither a browser tab nor curl can produce a half-open SSE client on demand:
closing the tab sends a clean FIN, and normal reads keep draining the socket.
This script opens a raw TCP connection to /api/events, completes the HTTP
handshake to become a real subscribed SSE client (so it counts toward
sseClients and receives real broadcast traffic), then stops calling recv()
entirely while leaving the socket open -- exactly the "server keeps sending,
client stopped reading" condition #73 asks about. TCP flow control does the
rest: once the client's receive window fills, the server's next send() on
that socket blocks (ESPAsyncWebServer) or retries in a blocking loop
(PsychicEventSource, see #72/#73 findings) rather than returning immediately.

Meanwhile a second, independent connection polls /api/status on a fixed
cadence so heap/sseClients/uptimeMs are observable throughout the stall
without touching the stalled socket. Reuses tools/issue65_live_ab_runtime.py's
already-hardened Timeline/NDJSON/atomic-JSON primitives rather than
re-deriving them, per this repo's established webload_* convention.

This is a standalone scenario tool, not a replacement for
webload_baseline_run.py -- it produces its own evidence subtree
(tasks/evidence/webload/<run-id>/sse-stall/) that a run-id shared with a
webload_baseline_run.py --stage full capture can sit alongside.
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
DEFAULT_PATH = "/api/events"
HANDSHAKE_DEADLINE_SECONDS = 10.0
STATUS_POLL_INTERVAL_SECONDS = 2.0
STATUS_REQUEST_DEADLINE_SECONDS = 5.0


class SseStallError(RuntimeError):
    """A scenario setup or evidence-write step could not be completed."""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--controller", default=DEFAULT_CONTROLLER)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--path", default=DEFAULT_PATH)
    parser.add_argument("--run-id", required=True)
    parser.add_argument(
        "--stall-seconds", type=float, default=90.0,
        help="how long to hold the SSE socket open without reading it",
    )
    parser.add_argument(
        "--end-mode", choices=("drain", "abrupt"), default="drain",
        help=(
            "drain: recv() everything queued at the end and observe how much "
            "backlog piled up, then close cleanly. abrupt: close the socket "
            "immediately without reading, simulating a dropped connection "
            "(e.g. WiFi roam) rather than a tab that wakes back up."
        ),
    )
    return parser


def _open_sse_connection(
    controller: str, port: int, path: str, timeline: r65.Timeline,
) -> tuple[socket.socket, dict[str, Any]]:
    """Complete the HTTP handshake over a raw socket and confirm the
    response is a live text/event-stream before returning it un-drained."""
    sock = socket.create_connection((controller, port), timeout=HANDSHAKE_DEADLINE_SECONDS)
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {controller}\r\n"
        "Accept: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
    ).encode("ascii")
    sock.sendall(request)

    sock.settimeout(HANDSHAKE_DEADLINE_SECONDS)
    header_bytes = b""
    deadline = time.monotonic() + HANDSHAKE_DEADLINE_SECONDS
    while b"\r\n\r\n" not in header_bytes:
        if time.monotonic() > deadline:
            sock.close()
            raise SseStallError(f"{path} did not complete headers within deadline")
        chunk = sock.recv(4096)
        if not chunk:
            sock.close()
            raise SseStallError(f"{path} closed the connection during the handshake")
        header_bytes += chunk
    header_text, _, leftover = header_bytes.partition(b"\r\n\r\n")
    status_line = header_text.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
    if " 200 " not in f" {status_line} ":
        sock.close()
        raise SseStallError(f"{path} returned unexpected status line: {status_line!r}")
    if b"text/event-stream" not in header_text.lower():
        sock.close()
        raise SseStallError(f"{path} did not respond with text/event-stream: {header_text!r}")

    # Confirm at least one real event arrives before starting the stall --
    # otherwise a stall that "succeeds" could just mean the stream never
    # started, not that a live broadcast got stuck behind a full window.
    first_event = leftover
    if b"\n\n" not in first_event and b"data:" not in first_event:
        sock.settimeout(HANDSHAKE_DEADLINE_SECONDS)
        try:
            chunk = sock.recv(4096)
        except (TimeoutError, socket.timeout) as error:
            sock.close()
            raise SseStallError(
                f"{path} handshake completed but no event arrived within deadline"
            ) from error
        first_event += chunk

    connect_record = timeline.record(
        "sse-connected", statusLine=status_line,
        firstEventBytes=len(first_event), localPort=sock.getsockname()[1],
    )
    return sock, connect_record


class StatusSampler:
    """Poll /api/status on an independent connection while the SSE socket
    is stalled, so heap/sseClients/uptimeMs are observable without touching
    the socket under test. Mirrors webload_baseline_run.py's LogsMonitor
    shape (background thread, durable NDJSON append per sample)."""

    def __init__(self, controller: str, samples_path: Path, timeline: r65.Timeline) -> None:
        self.controller = controller
        self.samples_path = samples_path
        self.timeline = timeline
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="sse-stall-status-sampler", daemon=True)
        self.error: BaseException | None = None
        self.samples: list[dict[str, Any]] = []

    def start(self) -> None:
        self._thread.start()

    def stop_and_join(self, timeout: float = 5.0) -> None:
        self._stop.set()
        self._thread.join(timeout)
        if self._thread.is_alive():
            raise SseStallError("status sampler did not stop within deadline")
        if self.error is not None:
            raise SseStallError(f"status sampler failed: {self.error}")

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
            heapFree=(status or {}).get("heapFree"),
            heapLargest8bit=(status or {}).get("heapLargest8bit"),
            sseClients=(status or {}).get("sseClients"),
            uptimeMs=(status or {}).get("uptimeMs"),
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


def _summarize_samples(samples: list[dict[str, Any]]) -> dict[str, Any]:
    reachable = [s for s in samples if s.get("success")]
    unreachable_count = len(samples) - len(reachable)
    device_reset_detected = False
    previous_uptime: int | None = None
    for sample in reachable:
        uptime = sample.get("uptimeMs")
        if isinstance(uptime, int) and previous_uptime is not None and uptime < previous_uptime:
            device_reset_detected = True
        if isinstance(uptime, int):
            previous_uptime = uptime
    heap_values = [
        s["heapLargest8bit"] for s in reachable
        if isinstance(s.get("heapLargest8bit"), int)
    ]
    return {
        "sampleCount": len(samples),
        "unreachableCount": unreachable_count,
        "deviceResetDetected": device_reset_detected,
        "heapLargest8bitMin": min(heap_values) if heap_values else None,
        "heapLargest8bitMax": max(heap_values) if heap_values else None,
    }


def _drain(sock: socket.socket, deadline_seconds: float) -> tuple[int, int]:
    """Read whatever backlog is queued, bounded so a genuinely stuck server
    can't hang the scenario forever. Returns (byteCount, recvCallCount)."""
    sock.settimeout(1.0)
    total_bytes = 0
    total_calls = 0
    deadline = time.monotonic() + deadline_seconds
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(65536)
        except (TimeoutError, socket.timeout):
            break
        total_calls += 1
        if not chunk:
            break
        total_bytes += len(chunk)
    return total_bytes, total_calls


def run(args: argparse.Namespace) -> dict[str, Any]:
    run_dir = EVIDENCE_ROOT / args.run_id / "sse-stall"
    run_dir.mkdir(parents=True, exist_ok=False)
    timeline = r65.Timeline.start()
    events: list[dict[str, Any]] = []

    sock, connect_record = _open_sse_connection(args.controller, args.port, args.path, timeline)
    events.append(connect_record)
    print(f"SSE connected ({connect_record['statusLine']}); starting {args.stall_seconds:.0f}s stall...")

    sampler = StatusSampler(args.controller, run_dir / "status-samples.ndjson", timeline)
    sampler.start()

    stall_start = time.monotonic()
    # Deliberately no recv() calls on `sock` here -- that omission IS the
    # scenario. The socket stays open and TCP-alive while the peer (the
    # controller) keeps trying to push SSE broadcasts into it.
    time.sleep(max(0.0, args.stall_seconds))
    stall_elapsed = time.monotonic() - stall_start

    sampler.stop_and_join()
    events.append(timeline.record("stall-window-elapsed", elapsedSeconds=stall_elapsed))

    drained_bytes = 0
    drained_calls = 0
    if args.end_mode == "drain":
        drained_bytes, drained_calls = _drain(sock, deadline_seconds=5.0)
        events.append(timeline.record(
            "drain-complete", byteCount=drained_bytes, recvCallCount=drained_calls,
        ))
    sock.close()
    events.append(timeline.record("sse-socket-closed", endMode=args.end_mode))

    summary = _summarize_samples(sampler.samples)
    outcome = {
        "schemaVersion": 1,
        "issue": 73,
        "scenario": "sse-stall",
        "runId": args.run_id,
        "controller": args.controller,
        "path": args.path,
        "stallSecondsRequested": args.stall_seconds,
        "stallSecondsActual": stall_elapsed,
        "endMode": args.end_mode,
        "drainedByteCount": drained_bytes,
        "drainedRecvCallCount": drained_calls,
        **summary,
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
    except (SseStallError, r65.Issue65RuntimeError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(
        f"Done. samples={outcome['sampleCount']} "
        f"unreachable={outcome['unreachableCount']} "
        f"deviceResetDetected={outcome['deviceResetDetected']} "
        f"heapLargest8bit(min/max)={outcome['heapLargest8bitMin']}/{outcome['heapLargest8bitMax']} "
        f"drained={outcome['drainedByteCount']}B in {outcome['drainedRecvCallCount']} calls\n"
        f"Evidence: tasks/evidence/webload/{args.run_id}/sse-stall/"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
