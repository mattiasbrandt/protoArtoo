#!/usr/bin/env python3
"""
P4 ESP-Hosted WiFi reliability soak test harness — #184

Drives the bench firmware via HTTP endpoints to exercise SDIO WiFi stability.
Tests:
  - Periodic status checks (link fault tracking)
  - C6 co-processor reset cycles
  - Graceful timeout and recovery handling

Typical invocation:
  python3 tools/p4_hosted_soak.py --target 192.168.1.100 --duration 3600

The firmware must be running p4_hosted_bench.cpp on a P4+C6 board over SDIO.
"""

import argparse
import json
import sys
import time
from typing import Optional, Dict, Any
import urllib.request
import urllib.error

# HTTP timeout for individual requests (seconds)
REQUEST_TIMEOUT = 5


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

    def _request(self, method: str, endpoint: str) -> Dict[str, Any]:
        """
        Make an HTTP request to the bench firmware.

        Args:
            method: HTTP method ("GET" or "POST")
            endpoint: API endpoint path (e.g., "/api/status")

        Returns:
            Parsed JSON response, or None on error

        Raises:
            urllib.error.URLError: if the request fails
            json.JSONDecodeError: if the response is not valid JSON
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
        except (urllib.error.URLError, urllib.error.HTTPError) as e:
            raise urllib.error.URLError(f"Request failed to {url}: {e}")

    def health(self) -> bool:
        """Check firmware liveness."""
        try:
            response = self._request("GET", "/api/health")
            return response is not None
        except Exception:
            return False

    def status(self) -> Optional[Dict[str, Any]]:
        """Get bench status (metrics, WiFi state, link faults)."""
        try:
            return self._request("GET", "/api/status")
        except Exception as e:
            print(f"Error fetching status: {e}", file=sys.stderr)
            return None

    def reset_c6(self) -> Optional[Dict[str, Any]]:
        """Trigger C6 co-processor reset."""
        try:
            return self._request("POST", "/api/c6/reset")
        except Exception as e:
            print(f"Error triggering C6 reset: {e}", file=sys.stderr)
            return None


def run_soak_test(
    client: BenchClient,
    duration_seconds: int,
    reset_interval_seconds: int = 60,
    status_interval_seconds: int = 10,
) -> bool:
    """
    Run the soak test against the bench firmware.

    Periodically:
      - Check firmware health
      - Fetch status metrics
      - Trigger C6 reset (at longer interval)
      - Track link fault count changes

    Args:
        client: Initialized BenchClient
        duration_seconds: Total test duration (0 = run indefinitely until ^C)
        reset_interval_seconds: Interval between C6 reset triggers
        status_interval_seconds: Interval between status checks

    Returns:
        True if all checks passed, False if critical error occurred
    """
    print(f"[SOAK] Starting bench test against {client.base_url}")
    print(f"[SOAK] Duration: {duration_seconds}s, Reset interval: {reset_interval_seconds}s")

    start_time = time.time()
    last_status_check = 0
    last_reset = 0
    last_link_fault_count = None
    test_passed = True

    try:
        while True:
            elapsed = time.time() - start_time
            now = time.time()

            # Check if duration exceeded
            if duration_seconds > 0 and elapsed >= duration_seconds:
                print(f"[SOAK] Duration {duration_seconds}s exceeded. Test complete.")
                break

            # Periodic health check
            if not client.health():
                print(f"[SOAK] Health check FAILED at {elapsed:.1f}s")
                test_passed = False
                # Don't exit immediately; allow the test to continue and possibly recover
            else:
                print(f"[SOAK] Health OK at {elapsed:.1f}s", file=sys.stderr)

            # Periodic status check
            if now - last_status_check >= status_interval_seconds:
                status = client.status()
                if status:
                    link_faults = status.get("linkFaultCount", -1)
                    wifi_connected = status.get("wifiConnected", False)
                    uptime = status.get("uptimeMs", 0) / 1000.0
                    free_heap = status.get("freeHeapBytes", -1)

                    print(f"[SOAK] Status at {elapsed:.1f}s: "
                          f"uptime={uptime:.1f}s, "
                          f"wifi={'CONN' if wifi_connected else 'DISC'}, "
                          f"linkFaults={link_faults}, "
                          f"heap={free_heap} bytes")

                    # Track link fault escalation
                    if last_link_fault_count is not None:
                        if link_faults > last_link_fault_count:
                            print(f"[SOAK] Link fault detected: "
                                  f"{last_link_fault_count} -> {link_faults}")
                    last_link_fault_count = link_faults
                else:
                    print(f"[SOAK] Status check FAILED at {elapsed:.1f}s", file=sys.stderr)
                    test_passed = False

                last_status_check = now

            # Periodic C6 reset
            if now - last_reset >= reset_interval_seconds:
                reset_response = client.reset_c6()
                if reset_response:
                    print(f"[SOAK] C6 reset triggered at {elapsed:.1f}s")
                else:
                    print(f"[SOAK] C6 reset FAILED at {elapsed:.1f}s", file=sys.stderr)
                    test_passed = False

                last_reset = now

            # Small sleep to avoid busy-spinning
            time.sleep(1)

    except KeyboardInterrupt:
        print("\n[SOAK] Test interrupted by user.")
    except Exception as e:
        print(f"[SOAK] Unexpected error: {e}", file=sys.stderr)
        test_passed = False

    return test_passed


def dry_run(client: BenchClient) -> bool:
    """
    Perform a quick dry-run to verify the bench firmware is reachable.

    Makes a single health check and status fetch without looping.
    """
    print(f"[DRY-RUN] Testing connectivity to {client.base_url}")

    # Health check
    if not client.health():
        print("[DRY-RUN] Health check failed. Is the firmware running?")
        return False

    print("[DRY-RUN] Health check OK")

    # Status check
    status = client.status()
    if not status:
        print("[DRY-RUN] Status fetch failed.")
        return False

    print(f"[DRY-RUN] Status OK:")
    print(f"  - Boot count: {status.get('bootCount', 'unknown')}")
    print(f"  - Uptime: {status.get('uptimeMs', 0) / 1000.0:.1f}s")
    print(f"  - WiFi connected: {status.get('wifiConnected', False)}")
    print(f"  - Link faults: {status.get('linkFaultCount', 'unknown')}")
    print(f"  - Free heap: {status.get('freeHeapBytes', 'unknown')} bytes")

    print("[DRY-RUN] Firmware is reachable and responding. Ready for full test.")
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
        help="Test duration in seconds (default: 3600s = 1 hour). Use 0 to run indefinitely.",
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
        help="Quick connectivity test (single health + status check), then exit",
    )

    args = parser.parse_args()

    client = BenchClient(args.target)

    if args.dry_run:
        # Dry-run mode: single health + status check
        success = dry_run(client)
        sys.exit(0 if success else 1)
    else:
        # Full soak test
        success = run_soak_test(
            client,
            args.duration,
            reset_interval_seconds=args.reset_interval,
            status_interval_seconds=args.status_interval,
        )
        sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
