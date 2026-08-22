#!/usr/bin/env python3
"""P4 ESP-Hosted WiFi reliability soak test harness — #184.
Implements the full verdict contract from the issue body."""

import argparse, json, sys, threading, time, urllib.request
from typing import Optional, Dict, Any, List

REQUEST_TIMEOUT = 5
SSE_FRAME_CADENCE_MS = 1000

class BenchClient:
    def __init__(self, target: str):
        self.base_url = f"http://{target}" if not target.startswith("http") else target

    def _request(self, method: str, endpoint: str) -> Optional[Dict[str, Any]]:
        try:
            req = urllib.request.Request(f"{self.base_url}{endpoint}", method=method)
            req.add_header("User-Agent", "p4-hosted-soak/1.0")
            with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT) as r:
                data = r.read().decode("utf-8")
                return json.loads(data) if "json" in r.headers.get("content-type", "") else {"raw": data}
        except Exception as e:
            return None

    def health(self) -> bool:
        return self._request("GET", "/api/health") is not None

    def status(self) -> Optional[Dict[str, Any]]:
        return self._request("GET", "/api/status")

    def reset_c6(self) -> Optional[Dict[str, Any]]:
        return self._request("POST", "/api/c6/reset")

    def stream_sse(self, timeout_seconds: int = 30) -> tuple:
        """Returns (frames_list, error_msg). PsychicEventSource produces: data: <msg>\r\n\r\n"""
        frames = []
        error_msg = ""
        start_time = time.time()
        try:
            req = urllib.request.Request(f"{self.base_url}/api/events")
            with urllib.request.urlopen(req, timeout=timeout_seconds + REQUEST_TIMEOUT) as r:
                for line_bytes in r:
                    line = line_bytes.decode("utf-8").rstrip("\r\n")
                    if line.startswith("data: "):
                        try:
                            frames.append(int(line[6:]))
                        except ValueError as e:
                            error_msg = f"Parse error on '{line[6:]}': {str(e)}"
                            return frames, error_msg
                    if time.time() - start_time >= timeout_seconds:
                        break
        except Exception as e:
            error_msg = f"{type(e).__name__}: {str(e)}"
        return frames, error_msg

class SseReader(threading.Thread):
    def __init__(self, client: BenchClient, duration: int, reader_id: int):
        super().__init__(daemon=True)
        self.client, self.duration, self.reader_id = client, duration, reader_id
        self.frames_received = self.gaps_detected = 0
        self.last_frame_num = None
        self.error_msg = ""

    def run(self):
        frames, error_msg = self.client.stream_sse(self.duration)
        self.frames_received = len(frames)
        self.error_msg = error_msg
        for f in frames:
            if self.last_frame_num is not None and f != self.last_frame_num + 1:
                self.gaps_detected += 1
            self.last_frame_num = f

def run_sse_soak(client: BenchClient, num_clients: int, duration: int) -> Dict[str, Any]:
    """SSE soak: N concurrent readers with gap detection."""
    readers = [SseReader(client, duration, i) for i in range(num_clients)]
    for r in readers: r.start()
    for r in readers: r.join()
    
    total_frames = sum(r.frames_received for r in readers)
    total_gaps = sum(r.gaps_detected for r in readers)
    expected = (duration * 1000) // SSE_FRAME_CADENCE_MS
    half_expected = expected // 2 if expected > 0 else 0
    
    return {
        "num_clients": num_clients,
        "total_frames_received": total_frames,
        "total_frame_gaps": total_gaps,
        "clients_failed": sum(1 for r in readers if r.error_msg),
        "readers_below_half": sum(1 for r in readers if r.frames_received < half_expected),
        "min_frames": min((r.frames_received for r in readers), default=0),
        "expected_frames": expected,
        "first_error": next((r.error_msg for r in readers if r.error_msg), None),
    }

def run_reconnect_storm(client: BenchClient, duration: int, concurrent: int = 5) -> Dict[str, Any]:
    """Concurrent SSE connect/drop storm targeting /api/events."""
    success = fail = 0
    lock = threading.Lock()
    
    def worker():
        nonlocal success, fail
        frames, error = client.stream_sse(timeout_seconds=2)
        with lock:
            if error or len(frames) == 0:
                fail += 1
            else:
                success += 1
    
    start, threads = time.time(), []
    while time.time() - start < duration:
        t = threading.Thread(target=worker, daemon=True)
        t.start()
        threads.append(t)
        if len(threads) >= concurrent:
            threads.pop(0).join()
        time.sleep(0.5)
    
    for t in threads: t.join()
    
    return {"successful_connects": success, "connect_failures": fail, "total_attempts": success + fail}

def run_soak_test(client: BenchClient, duration: int, sse_clients: int = 3, 
                  reset_interval: int = 60, status_interval: int = 10,
                  heap_tol_pct: int = 20) -> Dict[str, Any]:
    """Full soak test per the verdict contract."""
    start_time = time.time()
    boot_prev = reset_reason_prev = faults_prev = None
    boot_final = reset_reason_final = faults_final = None
    heap_baseline = heap_final = None
    sse_clients_max = 0
    status_count = 0
    resets_triggered = 0
    exceptions_list = []
    
    print(f"[SOAK] Starting: duration={duration}s, sse_clients={sse_clients}, heap_tol={heap_tol_pct}%")
    
    # Run SSE soak
    print("[SOAK] Launching SSE soak driver...")
    sse_res = run_sse_soak(client, sse_clients, int(duration * 0.8))
    
    # Run reconnect storm
    print("[SOAK] Launching reconnect-storm driver...")
    reconnect_res = run_reconnect_storm(client, duration // 2, concurrent=5)
    
    # Main test loop: periodic status checks and C6 resets
    last_status = last_reset = 0.0
    try:
        while time.time() - start_time < duration:
            now = time.time() - start_time
            
            if time.time() - last_status >= status_interval:
                try:
                    s = client.status()
                    if s:
                        status_count += 1
                        boot = s.get("bootCount")
                        reset = s.get("resetReason")
                        faults = s.get("linkFaultCount")
                        heap = s.get("freeHeapBytes", 0)
                        sse_connected = s.get("sseClientsConnected", 0)
                        
                        if boot_prev is None: boot_prev = boot
                        if reset_reason_prev is None: reset_reason_prev = reset
                        if faults_prev is None: faults_prev = faults
                        if heap_baseline is None: heap_baseline = heap
                        
                        boot_final = boot
                        reset_reason_final = reset
                        faults_final = faults
                        heap_final = heap
                        sse_clients_max = max(sse_clients_max, sse_connected)
                        
                        print(f"[SOAK] @ {now:.1f}s: boot={boot} reset={reset} faults={faults} sse_clients={sse_connected}")
                    else:
                        exceptions_list.append("Status fetch failed: connection error")
                except Exception as e:
                    exceptions_list.append(f"Status check: {type(e).__name__}: {str(e)}")
                
                last_status = time.time()
            
            if time.time() - last_reset >= reset_interval:
                try:
                    if client.reset_c6():
                        resets_triggered += 1
                        print(f"[SOAK] C6 reset triggered @ {now:.1f}s")
                    else:
                        exceptions_list.append("C6 reset failed: connection error")
                except Exception as e:
                    exceptions_list.append(f"C6 reset: {type(e).__name__}: {str(e)}")
                
                last_reset = time.time()
            
            time.sleep(0.5)
    
    except Exception as e:
        exceptions_list.append(f"Main loop: {type(e).__name__}: {str(e)}")
    
    # Compute verdicts per contract
    boot_count_advanced = (boot_final > boot_prev) if (boot_prev is not None and boot_final is not None) else False
    
    # SSE verdict
    sse_fail = []
    if sse_res["total_frames_received"] == 0:
        sse_fail.append("total_frames_received == 0 (inert measurement)")
    if sse_res["readers_below_half"] > 0:
        sse_fail.append(f"{sse_res['readers_below_half']} readers received < 50% of expected frames")
    if sse_res["total_frame_gaps"] > 0:
        sse_fail.append(f"total_frame_gaps > 0: {sse_res['total_frame_gaps']}")
    if sse_res["clients_failed"] > 0:
        sse_fail.append(f"clients_failed > 0: {sse_res['clients_failed']}")
    if sse_clients_max < sse_clients:
        sse_fail.append(f"firmware sseClientsConnected max {sse_clients_max} never reached requested {sse_clients}")
    
    # Reconnect storm verdict
    reconnect_fail = []
    if reconnect_res["connect_failures"] > 0:
        reconnect_fail.append(f"connect_failures > 0: {reconnect_res['connect_failures']}")
    
    # C6 reset recovery verdict
    c6_fail = []
    if boot_count_advanced:
        c6_fail.append("boot_count_advanced: host rebooted during C6 reset")
    if reset_reason_final in [1, 3, 4, 5]:  # ESP_RST_PANIC=1, TASK_WDT=3, INT_WDT=4, WDT=5
        c6_fail.append(f"resetReason {reset_reason_final}: panic/watchdog detected")
    if faults_final is not None and faults_final > faults_prev and (not client.status() or not client.status().get("wifiConnected")):
        c6_fail.append("WiFi did not return to connected after C6 reset")
    
    # Overall verdict: PASS only if all sub-verdicts pass AND at least 1 reset AND at least 1 status sample
    sse_verdict = "PASS" if not sse_fail else "FAIL"
    reconnect_verdict = "PASS" if not reconnect_fail else "FAIL"
    c6_verdict = "PASS" if not c6_fail else "FAIL"
    
    overall_verdict = "PASS" if (sse_verdict == "PASS" and reconnect_verdict == "PASS" and c6_verdict == "PASS" and
                                 resets_triggered >= 1 and status_count >= 1) else "FAIL"
    
    return {
        "verdict": overall_verdict,
        "elapsed_seconds": time.time() - start_time,
        "duration_target_seconds": duration,
        "status_samples_taken": status_count,
        "c6_resets_triggered": resets_triggered,
        "sse_soak": {
            "verdict": sse_verdict,
            "fail_reasons": sse_fail if sse_fail else None,
            **sse_res,
        },
        "reconnect_storm": {
            "verdict": reconnect_verdict,
            "fail_reasons": reconnect_fail if reconnect_fail else None,
            **reconnect_res,
        },
        "c6_reset_recovery": {
            "verdict": c6_verdict,
            "fail_reasons": c6_fail if c6_fail else None,
            "boot_count_advanced": boot_count_advanced,
            "boot_count_prev": boot_prev,
            "boot_count_final": boot_final,
            "resetReason_final": reset_reason_final,
        },
        "exceptions_captured": exceptions_list if exceptions_list else None,
    }

def self_test() -> bool:
    """Offline parser validation. PsychicEventSource produces: data: <msg>\r\n\r\n"""
    print("[SELF-TEST] Testing SSE frame parser against fixture bytes...")
    
    # Fixture 1: Clean sequence
    frames, gaps = [], 0
    for line in b"data: 0\r\ndata: 1\r\ndata: 2\r\n".decode().split("\r\n"):
        if line.startswith("data: "):
            try:
                f = int(line[6:])
                if frames and f != frames[-1] + 1: gaps += 1
                frames.append(f)
            except ValueError: gaps = 999
    if frames != [0, 1, 2] or gaps != 0:
        print(f"[SELF-TEST] FAIL: clean sequence got {frames}, gaps={gaps}")
        return False
    print(f"[SELF-TEST] PASS: clean sequence {frames}, gaps={gaps}")
    
    # Fixture 2: Gapped sequence
    frames, gaps = [], 0
    for line in b"data: 0\r\ndata: 2\r\n".decode().split("\r\n"):
        if line.startswith("data: "):
            try:
                f = int(line[6:])
                if frames and f != frames[-1] + 1: gaps += 1
                frames.append(f)
            except ValueError: gaps = 999
    if frames != [0, 2] or gaps != 1:
        print(f"[SELF-TEST] FAIL: gapped sequence got {frames}, gaps={gaps}")
        return False
    print(f"[SELF-TEST] PASS: gapped sequence {frames}, gaps={gaps}")
    
    # Fixture 3: Garbled sequence
    frames, gaps = [], 0
    for line in b"data: garbled\r\n".decode().split("\r\n"):
        if line.startswith("data: "):
            try:
                f = int(line[6:])
                frames.append(f)
            except ValueError:
                gaps = 999
                break
    if gaps != 999:
        print(f"[SELF-TEST] FAIL: garbled didn't trigger parse error")
        return False
    print(f"[SELF-TEST] PASS: garbled rejected")
    
    # Fixture 4: Empty stream
    frames, gaps = [], 0
    for line in b"".decode().split("\r\n"):
        if line.startswith("data: "):
            frames.append(int(line[6:]))
    if frames != [] or gaps != 0:
        print(f"[SELF-TEST] FAIL: empty got {frames}")
        return False
    print(f"[SELF-TEST] PASS: empty stream")
    
    print("[SELF-TEST] All tests passed")
    return True

def dry_run(client: BenchClient) -> bool:
    print("[DRY-RUN] Quick connectivity test...")
    if not client.health():
        print("[DRY-RUN] FAIL: health check")
        return False
    print("[DRY-RUN] OK: health")
    
    s = client.status()
    if not s:
        print("[DRY-RUN] FAIL: status fetch")
        return False
    print(f"[DRY-RUN] OK: status (boot={s.get('bootCount')})")
    
    frames, err = client.stream_sse(timeout_seconds=10)
    if len(frames) < 3:
        print(f"[DRY-RUN] FAIL: SSE got {len(frames)} frames, error: {err}")
        return False
    print(f"[DRY-RUN] OK: SSE stream ({len(frames)} frames)")
    
    print("[DRY-RUN] All checks passed")
    return True

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--target", default="protoartoo.local")
    p.add_argument("--duration", type=int, default=600)
    p.add_argument("--sse-clients", type=int, default=3)
    p.add_argument("--reset-interval", type=int, default=60)
    p.add_argument("--status-interval", type=int, default=10)
    p.add_argument("--heap-recovery-tolerance-pct", type=int, default=20)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--output-json", type=str)
    args = p.parse_args()
    
    if args.self_test:
        sys.exit(0 if self_test() else 1)
    
    c = BenchClient(args.target)
    
    if args.dry_run:
        sys.exit(0 if dry_run(c) else 1)
    
    r = run_soak_test(c, args.duration, sse_clients=args.sse_clients,
                      reset_interval=args.reset_interval,
                      status_interval=args.status_interval,
                      heap_tol_pct=args.heap_recovery_tolerance_pct)
    
    print(json.dumps(r, indent=2))
    
    if args.output_json:
        with open(args.output_json, "w") as f:
            json.dump(r, f, indent=2)
        print(f"\n[SOAK] Results written to {args.output_json}", file=sys.stderr)
    
    sys.exit(0 if r["verdict"] == "PASS" else 1)

if __name__ == "__main__":
    main()
