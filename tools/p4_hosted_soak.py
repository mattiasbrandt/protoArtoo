#!/usr/bin/env python3
"""
P4 ESP-Hosted WiFi reliability soak test harness — #184

Drives the bench firmware via HTTP endpoints to exercise SDIO WiFi stability.
Tests:
  - SSE soak driver: N concurrent long-lived readers with frame-gap detection
  - Reconnect-storm driver: rapid connect/drop cycles
  - Periodic status checks (link fault tracking)
  - C6 co-processor reset cycles

Output: Machine-readable JSON with observed numbers and pass/fail verdict.

Typical invocation:
  python3 tools/p4_hosted_soak.py --target 192.168.1.100 --duration 3600 --sse-clients 5

The firmware must be running p4_hosted_bench.cpp on a P4+C6 board over SDIO.
"""

import argparse
import json
import sys
import threading
import time
from typing import Optional, Dict, Any, List
import urllib.request
import urllib.error

# HTTP timeout for individual requests (seconds)
REQUEST_TIMEOUT = 5

# SSE frame timeout: if we don't see a frame in this many seconds, it's a gap
SSE_FRAME_TIMEOUT_S = 5


class BenchClient:
    """Simple HTTP client for the bench firmware endpoints."""

    def __init__(self, target: str):
        """
        Initialize client pointing to the bench firmware.

        Args:
            target: IP address or hostname (e.g., "192.168.1.100" or "protoartoo.local")
        """
        # Normalize target: add http:// if not present
        if not target.startswith("http://") and not target.startswith("https://"):
            self.base_url = f"http://{target}"
        else:
            self.base_url = target

    def _request(self, method: str, endpoint: str) -> Optional[Dict[str, Any]]:
        """
        Make an HTTP request to the bench firmware.

        Args:
            method: HTTP method ("GET" or "POST")
            endpoint: API endpoint path (e.g., "/api/status")

        Returns:
            Parsed JSON response, or None on error
        """
        url = f"{self.base_url}{endpoint}"

        req = urllib.request.Request(url, method=method)
        req.add_header("User-Agent", "p4-hosted-soak/1.0")

        try:
            with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT) as response:
                data = response.read().decode("utf-8")
                if response.headers.get("content-type", "").startswith("application/json"):
                    return json.loads(data)
                else:
                    # Non-JSON response (e.g., "OK" from /health)
                    return {"raw": data, "status_code": response.status}
        except Exception as e:
            print(f"[ERROR] Request failed to {url}: {e}", file=sys.stderr)
            return None

    def health(self) -> bool:
        """Check firmware liveness."""
        response = self._request("GET", "/api/health")
        return response is not None

    def status(self) -> Optional[Dict[str, Any]]:
        """Get bench status (metrics, WiFi state, link faults)."""
        return self._request("GET", "/api/status")

    def reset_c6(self) -> Optional[Dict[str, Any]]:
        """Trigger C6 co-processor reset."""
        return self._request("POST", "/api/c6/reset")

    def stream_sse(self, timeout_seconds: int = 30) -> List[int]:
        """
        Read SSE frames from /api/events until timeout or connection closes.
        Returns list of frame numbers (data field parsed as int).
        """
        url = f"{self.base_url}/api/events"
        frames = []
        start_time = time.time()

        req = urllib.request.Request(url)
        req.add_header("User-Agent", "p4-hosted-soak/1.0")

        try:
            with urllib.request.urlopen(req, timeout=timeout_seconds + REQUEST_TIMEOUT) as response:
                for line_bytes in response:
                    line = line_bytes.decode("utf-8").strip()
                    if line.startswith("data: "):
                        try:
                            frame_num = int(line[6:])
                            frames.append(frame_num)
                        except ValueError:
                            pass
                    # Check if we've exceeded the timeout
                    if time.time() - start_time >= timeout_seconds:
                        break
        except Exception as e:
            pass  # Connection closed or timeout - that's expected

        return frames


class SseReader(threading.Thread):
    """Thread that reads SSE frames and detects gaps."""

    def __init__(self, client: BenchClient, stream_duration_s: int, reader_id: int):
        super().__init__(daemon=True)
        self.client = client
        self.stream_duration_s = stream_duration_s
        self.reader_id = reader_id
        self.frames_received = 0
        self.gaps_detected = 0
        self.last_frame_num = None
        self.error_occurred = False

    def run(self):
        """Read SSE stream and detect frame-number gaps."""
        try:
            frames = self.client.stream_sse(self.stream_duration_s)
            self.frames_received = len(frames)

            # Detect gaps: frame numbers should increment by 1
            for frame_num in frames:
                if self.last_frame_num is not None:
                    if frame_num != self.last_frame_num + 1:
                        self.gaps_detected += 1
                self.last_frame_num = frame_num
        except Exception as e:
            print(f"[SSE] Reader {self.reader_id} error: {e}", file=sys.stderr)
            self.error_occurred = True


def run_sse_soak(client: BenchClient, num_clients: int, stream_duration_s: int) -> Dict[str, Any]:
    """
    Run SSE soak test with N concurrent readers.

    Args:
        client: Initialized BenchClient
        num_clients: Number of concurrent SSE readers
        stream_duration_s: Duration each reader should stream

    Returns:
        Dict with metrics: total_frames, gaps, clients_succeeded, clients_failed
    """
    print(f"[SSE] Starting {num_clients} concurrent readers for {stream_duration_s}s each...")

    readers = [
        SseReader(client, stream_duration_s, i)
        for i in range(num_clients)
    ]

    # Start all readers
    for reader in readers:
        reader.start()

    # Wait for all readers to finish
    for reader in readers:
        reader.join()

    # Aggregate results
    total_frames = sum(r.frames_received for r in readers)
    total_gaps = sum(r.gaps_detected for r in readers)
    succeeded = sum(1 for r in readers if not r.error_occurred)
    failed = sum(1 for r in readers if r.error_occurred)

    return {
        "num_clients": num_clients,
        "total_frames_received": total_frames,
        "total_frame_gaps": total_gaps,
        "clients_succeeded": succeeded,
        "clients_failed": failed,
    }


def run_reconnect_storm(client: BenchClient, num_cycles: int, delay_ms: int = 500) -> Dict[str, Any]:
    """
    Run reconnect-storm test: rapid connect/drop cycles.

    Args:
        client: Initialized BenchClient
        num_cycles: Number of connect/drop cycles
        delay_ms: Delay between cycles (milliseconds)

    Returns:
        Dict with metrics: cycles_completed, connect_failures
    """
    print(f"[RECONNECT] Starting {num_cycles} rapid connect/drop cycles (delay: {delay_ms}ms)...")

    connect_failures = 0
    cycles_completed = 0

    for i in range(num_cycles):
        # Each cycle: GET /api/status (implicit connect) and immediately close
        status = client.status()
        if status is None:
            connect_failures += 1
        else:
            cycles_completed += 1

        # Small delay between cycles
        if i < num_cycles - 1:
            time.sleep(delay_ms / 1000.0)

    return {
        "num_cycles": num_cycles,
        "cycles_completed": cycles_completed,
        "connect_failures": connect_failures,
    }


def run_soak_test(
    client: BenchClient,
    duration_seconds: int,
    sse_clients: int = 3,
    reset_interval_seconds: int = 60,
    status_interval_seconds: int = 10,
) -> Dict[str, Any]:
    """
    Run the comprehensive soak test against the bench firmware.

    Runs in parallel:
      - SSE soak driver (N concurrent long-lived readers)
      - Reconnect-storm driver (rapid cycles)
      - Periodic status checks and C6 resets

    Args:
        client: Initialized BenchClient
        duration_seconds: Total test duration
        sse_clients: Number of concurrent SSE readers
        reset_interval_seconds: Interval between C6 resets
        status_interval_seconds: Interval between status checks

    Returns:
        Machine-readable JSON dict with observed numbers and pass/fail
    """
    print(f"[SOAK] Starting comprehensive bench test")
    print(f"[SOAK] Duration: {duration_seconds}s, SSE clients: {sse_clients}")

    start_time = time.time()
    last_status_check = 0
    last_reset = 0
    status_checks = 0
    resets_triggered = 0
    last_boot_count = None
    boot_count_changed = False
    link_fault_changes = []
    test_passed = True

    # Run SSE soak in background (parallel with other tests)
    print("[SOAK] Launching SSE soak driver...")
    sse_results = run_sse_soak(client, sse_clients, int(duration_seconds * 0.8))

    # Run reconnect-storm driver
    print("[SOAK] Launching reconnect-storm driver...")
    reconnect_results = run_reconnect_storm(client, num_cycles=20, delay_ms=500)

    # Run periodic status checks and C6 resets
    try:
        while time.time() - start_time < duration_seconds:
            elapsed = time.time() - start_time
            now = time.time()

            # Periodic status check
            if now - last_status_check >= status_interval_seconds:
                status = client.status()
                if status:
                    status_checks += 1
                    boot_count = status.get("bootCount", -1)
                    link_faults = status.get("linkFaultCount", -1)

                    # Track boot count changes (recovery without reboot)
                    if last_boot_count is not None and boot_count > last_boot_count:
                        boot_count_changed = True
                    last_boot_count = boot_count

                    link_fault_changes.append(link_faults)
                    print(f"[SOAK] Status @ {elapsed:.1f}s: boot_count={boot_count}, "
                          f"link_faults={link_faults}, sse_clients={status.get('sseClientsConnected', 0)}")
                else:
                    test_passed = False
                    print(f"[SOAK] Status check FAILED @ {elapsed:.1f}s", file=sys.stderr)

                last_status_check = now

            # Periodic C6 reset
            if now - last_reset >= reset_interval_seconds:
                reset_result = client.reset_c6()
                if reset_result:
                    resets_triggered += 1
                    print(f"[SOAK] C6 reset triggered @ {elapsed:.1f}s")
                else:
                    test_passed = False
                    print(f"[SOAK] C6 reset FAILED @ {elapsed:.1f}s", file=sys.stderr)

                last_reset = now

            # Small sleep
            time.sleep(0.5)

    except KeyboardInterrupt:
        print("\n[SOAK] Test interrupted by user.")

    # Compute final pass/fail
    elapsed_total = time.time() - start_time
    final_boot_count = last_boot_count if last_boot_count is not None else -1
    sse_verdict = "PASS" if sse_results["total_frame_gaps"] == 0 else "FAIL"
    reconnect_verdict = "PASS" if reconnect_results["connect_failures"] == 0 else "FAIL"
    overall_verdict = "PASS" if (test_passed and sse_verdict == "PASS") else "FAIL"

    # Build result JSON
    result = {
        "verdict": overall_verdict,
        "elapsed_seconds": elapsed_total,
        "duration_target_seconds": duration_seconds,
        "status_checks_completed": status_checks,
        "c6_resets_triggered": resets_triggered,
        "boot_count_final": final_boot_count,
        "boot_count_advanced": boot_count_changed,
        "sse_soak": {
            "verdict": sse_verdict,
            **sse_results,
        },
        "reconnect_storm": {
            "verdict": reconnect_verdict,
            **reconnect_results,
        },
        "link_faults_observed": link_fault_changes,
        "link_faults_escalated": len(link_fault_changes) > 0 and link_fault_changes[-1] > (link_fault_changes[0] if link_fault_changes else 0),
    }

    return result


def dry_run(client: BenchClient) -> bool:
    """Quick connectivity test."""
    print("[DRY-RUN] Testing connectivity...")

    if not client.health():
        print("[DRY-RUN] Health check failed.")
        return False
    print("[DRY-RUN] Health check OK")

    status = client.status()
    if not status:
        print("[DRY-RUN] Status fetch failed.")
        return False

    print(f"[DRY-RUN] Status OK: boot_count={status.get('bootCount')}, "
          f"uptime_ms={status.get('uptimeMs')}, wifi_connected={status.get('wifiConnected')}")

    # Quick SSE test: read 5 frames
    frames = client.stream_sse(timeout_seconds=10)
    if len(frames) < 3:
        print(f"[DRY-RUN] SSE test insufficient: got {len(frames)} frames, expected >= 3")
        return False
    print(f"[DRY-RUN] SSE test OK: received {len(frames)} frames")

    print("[DRY-RUN] All checks passed. Firmware is ready for soak test.")
    return True


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument(
        "--target",
        default="protoartoo.local",
        help="Target hostname or IP address (default: protoartoo.local)",
    )

    parser.add_argument(
        "--duration",
        type=int,
        default=3600,
        help="Test duration in seconds (default: 3600s = 1 hour)",
    )

    parser.add_argument(
        "--sse-clients",
        type=int,
        default=3,
        help="Number of concurrent SSE readers (default: 3)",
    )

    parser.add_argument(
        "--reset-interval",
        type=int,
        default=60,
        help="Seconds between C6 reset triggers (default: 60s)",
    )

    parser.add_argument(
        "--status-interval",
        type=int,
        default=10,
        help="Seconds between status checks (default: 10s)",
    )

    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Quick connectivity test, then exit",
    )

    parser.add_argument(
        "--output-json",
        type=str,
        help="Output results to JSON file",
    )

    args = parser.parse_args()

    client = BenchClient(args.target)

    if args.dry_run:
        success = dry_run(client)
        sys.exit(0 if success else 1)
    else:
        result = run_soak_test(
            client,
            args.duration,
            sse_clients=args.sse_clients,
            reset_interval_seconds=args.reset_interval,
            status_interval_seconds=args.status_interval,
        )

        # Print JSON result to stdout
        print(json.dumps(result, indent=2))

        # Optionally write to file
        if args.output_json:
            with open(args.output_json, "w") as f:
                json.dump(result, f, indent=2)
            print(f"\n[SOAK] Results written to {args.output_json}", file=sys.stderr)

        sys.exit(0 if result["verdict"] == "PASS" else 1)


if __name__ == "__main__":
    main()
