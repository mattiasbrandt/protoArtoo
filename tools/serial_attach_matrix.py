#!/usr/bin/env python3
"""Host-attach reset matrix for the artoo-esp32.

Results and the safe-attach rule live in docs/troubleshooting.md,
"Serial monitor caveat".

Answers one question per run, black-box: does attaching THIS host terminal to
the controller reset it? Repeat it (>= 5 trials per method) -- a single clean
trial cannot distinguish "does not reset" from "did not reset that time".

Two independent anchors per trial, because a missed serial capture looks exactly
like a clean attach:
  1. the ROM boot banner (`rst:0x..`) in the serial capture, and
  2. `resetReason` + `uptimeMs` from /api/status over HTTP, either side.
The HTTP anchor does not travel over the serial path, which is what makes a
"no banner seen" trial trustworthy.

SCOPE LIMIT: this measures host-side and board-observable BEHAVIOUR. It says
nothing about the EN or GPIO0 waveform at the chip, which needs a logic
analyser. Never quote a result here as electrical proof.

Environment:
  PA_ATTACH_PORT    serial device        (default /dev/ttyUSB0)
  PA_ATTACH_STATUS  status URL           (default http://10.0.0.22/api/status)

Usage:
  python3 tools/serial_attach_matrix.py <method> <trial> [--seconds N] [--outdir DIR]

Methods: serial_monitor_posix, serial_monitor_pyserial, pio_device_monitor,
         cat_hupcl_cleared, cat_hupcl_set, picocom

Measured baseline, unseated, 2026-08-28 (see docs/troubleshooting.md): every
method above is 0/5 except serial_monitor_pyserial, which is 7/7 and can strand
the board in the ROM download stub.
"""
import argparse, json, os, subprocess, sys, time, urllib.request

PORT = os.environ.get("PA_ATTACH_PORT", "/dev/ttyUSB0")
STATUS = os.environ.get("PA_ATTACH_STATUS", "http://10.0.0.22/api/status")


def http_anchor(retries=30, delay=2.0):
    """(resetReason, uptimeMs) or None. Retries so a rebooting board is waited out."""
    last = None
    for _ in range(retries):
        try:
            with urllib.request.urlopen(STATUS, timeout=4) as r:
                d = json.load(r)
            return d["resetReason"], d["uptimeMs"]
        except Exception as e:
            last = e
            time.sleep(delay)
    print(f"  !! HTTP anchor unreachable: {last}", file=sys.stderr)
    return None


def recover():
    """Boot the app after a method has stranded the board.

    A method that leaves DTR asserted holds GPIO0 low across the reset, so the
    chip comes up in the ROM download stub: silent on serial and absent from the
    network. Deassert DTR (GPIO0 high) and pulse RTS (EN low->high) to boot the
    application image. Recorded per trial - a method that needs this did more
    than reset the board, it stranded it.
    """
    import serial
    s = serial.Serial(PORT, 115200, timeout=0.2)
    s.dtr = False
    s.rts = True
    time.sleep(0.2)
    s.rts = False
    time.sleep(3.0)
    s.close()


def run_method(method, capture_path, seconds):
    """Attach with `method`, hold the port `seconds`, close. Serial -> capture_path."""
    repo = os.environ.get("PA_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    cap = open(capture_path, "wb")
    pre = post = None
    try:
        if method == "serial_monitor_posix":
            cmd = [sys.executable, f"{repo}/tools/serial_monitor.py",
                   "--port", PORT, "--baud", "115200", "--duration", str(seconds)]
            subprocess.run(cmd, stdout=cap, stderr=subprocess.DEVNULL,
                           timeout=seconds + 25)
        elif method == "serial_monitor_pyserial":
            cmd = [sys.executable, f"{repo}/tools/serial_monitor.py",
                   "--port", PORT, "--baud", "115200", "--duration", str(seconds),
                   "--pyserial"]
            subprocess.run(cmd, stdout=cap, stderr=subprocess.DEVNULL,
                           timeout=seconds + 25)
        elif method == "pio_device_monitor":
            p = subprocess.Popen(["pio", "device", "monitor", "-p", PORT,
                                  "-b", "115200", "--raw"],
                                 stdout=cap, stderr=subprocess.DEVNULL,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
            time.sleep(seconds)
            p.terminate()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill(); p.wait(timeout=5)
        elif method == "cat_hupcl_cleared":
            subprocess.run(["stty", "-F", PORT, "115200", "-hupcl", "raw", "-echo"],
                           check=True)
            p = subprocess.Popen(["cat", PORT], stdout=cap,
                                 stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                                 start_new_session=True)
            time.sleep(seconds)
            p.terminate()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill(); p.wait(timeout=5)
        elif method == "cat_hupcl_set":
            # The discriminating arm. #233 recorded 1 reset in 5 HUPCL-set cycles
            # against 0 in 5 cleared, as a starting observation with no mechanism.
            # HUPCL makes the kernel drop DTR/RTS on last close, which is exactly
            # the post-close line transition the auto-reset pair responds to.
            subprocess.run(["stty", "-F", PORT, "115200", "hupcl", "raw", "-echo"],
                           check=True)
            p = subprocess.Popen(["cat", PORT], stdout=cap,
                                 stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                                 start_new_session=True)
            time.sleep(seconds)
            p.terminate()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill(); p.wait(timeout=5)
        elif method == "picocom":
            p = subprocess.Popen(["picocom", "-b", "115200", "-q", "--imap", "",
                                  "--omap", "", PORT],
                                 stdout=cap, stderr=subprocess.DEVNULL,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
            time.sleep(seconds)
            p.terminate()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill(); p.wait(timeout=5)
        else:
            raise SystemExit(f"unknown method {method}")
    finally:
        cap.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("method")
    ap.add_argument("trial", type=int)
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--outdir", default=".")
    a = ap.parse_args()

    cap = os.path.join(a.outdir, f"{a.method}_t{a.trial}.bin")

    pre_recovery = False
    pre = http_anchor(retries=8, delay=2.0)
    if pre is None:
        # A previous trial left the board stranded. Recover to a known state so
        # THIS trial measures its own method, then note that it was needed.
        recover()
        pre_recovery = True
        pre = http_anchor(retries=20, delay=3.0)
    if pre is None:
        print(json.dumps({"method": a.method, "trial": a.trial,
                          "result": "ABORTED_NO_PRE_ANCHOR_EVEN_AFTER_RECOVERY"}))
        return 2

    run_method(a.method, cap, a.seconds)

    stranded = False
    post = http_anchor(retries=8, delay=2.0)
    if post is None:
        # The method took the board off the network and it did not come back on
        # its own. That is a reset AND a strand - the strongest row in this matrix.
        stranded = True
        recover()
        post = http_anchor(retries=20, delay=3.0)
    if post is None:
        print(json.dumps({"method": a.method, "trial": a.trial,
                          "result": "ABORTED_NO_POST_ANCHOR_EVEN_AFTER_RECOVERY",
                          "pre": pre, "stranded": True}))
        return 2

    raw = open(cap, "rb").read()
    banner = b"rst:0x" in raw
    # uptime must advance by roughly the attach duration; a DECREASE is a reboot.
    uptime_went_back = post[1] < pre[1]
    # A strand implies a reset even if the post-anchor uptime now reflects the
    # recovery boot rather than the method's own.
    reset = banner or uptime_went_back or stranded

    print(json.dumps({
        "method": a.method, "trial": a.trial,
        "pre_resetReason": pre[0], "pre_uptimeMs": pre[1],
        "post_resetReason": post[0], "post_uptimeMs": post[1],
        "delta_uptimeMs": post[1] - pre[1],
        "rom_banner_in_capture": banner,
        "uptime_went_backwards": uptime_went_back,
        "capture_bytes": len(raw),
        "stranded_off_network": stranded,
        "needed_recovery_before_trial": pre_recovery,
        "reset": reset,
    }))
    return 0


if __name__ == "__main__":
    sys.exit(main())
