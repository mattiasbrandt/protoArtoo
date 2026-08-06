#!/usr/bin/env python3
"""Exercise the dashboard's control routes and read the resulting state back.

Issue #98's acceptance bar is not "the route exists". It is that the control
the operator clicks issues its request, gets an answer the async stack would
also have given, and that the state it claims to have changed actually reads
back on /api/status. This runs exactly that, once per control, and prints a
table an operator can score without reading any code.

Bench scope: no drive output, no servos, no sound module and no dome board are
attached, so a control is checked as request-resolves plus state-reads-back,
never as observed motion or sound.

Two routes are deliberately probed with input that cannot command anything --
/api/seq/test and /api/actions/test, with a name and a token that do not exist.
A 400 from them proves the route resolves and is parsing its body, which is the
distinction this ticket has to make, without starting a sequence or firing an
action on hardware nobody is watching.

Usage:
    python3 tools/dashboard_control_probe.py --host 10.0.0.22
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

TIMEOUT_S = 8.0


# Connection Admission paces accepts with a token bucket (PA_ACCEPT_BURST /
# PA_ACCEPT_PER_SECOND). One connection per request with no gap between them
# spends those tokens faster than a browser ever would, and a shed connection
# would be scored here as a dead control. Pacing the probe keeps it measuring
# the routes rather than the accept limiter.
REQUEST_GAP_S = 0.25
CONNECT_ATTEMPTS = 3


def call(host, method, path, form=None, body=None):
    """One request. Returns (status code, decoded text). 404 is a result, not an error.

    A connection that never opens is retried: that is the accept limiter, which
    is a deliberate behaviour of the server and not an answer from the route.
    An HTTP status -- any status -- is taken as the route's answer and returned
    as-is.
    """
    for attempt in range(CONNECT_ATTEMPTS):
        code, text = attempt_call(host, method, path, form, body)
        if code is not None:
            time.sleep(REQUEST_GAP_S)
            return code, text
        time.sleep(0.5 * (attempt + 1))
    return None, text


def attempt_call(host, method, path, form=None, body=None):
    url = "http://{0}{1}".format(host, path)
    data = None
    headers = {}
    if form is not None:
        data = urllib.parse.urlencode(form).encode("ascii")
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    elif body is not None:
        data = json.dumps(body).encode("ascii")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", "replace")
    except OSError as exc:
        return None, str(exc)


def status(host):
    code, text = call(host, "GET", "/api/status")
    if code != 200:
        raise RuntimeError("/api/status returned {0}".format(code))
    return json.loads(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="10.0.0.22")
    parser.add_argument("--json", help="Write the full report here")
    args = parser.parse_args()

    host = args.host
    before = status(host)
    rows = []

    def record(control, path, code, expected_codes, readback=None):
        ok = code in expected_codes and (readback is None or readback[2])
        rows.append({
            "control": control,
            "route": path,
            "code": code,
            "expected": list(expected_codes),
            "readback": None if readback is None else {
                "field": readback[0], "value": readback[1], "matched": readback[2]},
            "pass": ok,
        })
        return ok

    def settled(field, want, tries=10):
        """Poll one /api/status field, so a slow broadcast is not read as a failure."""
        value = None
        for _ in range(tries):
            value = status(host).get(field)
            if value == want:
                return field, value, True
            time.sleep(0.3)
        return field, value, False

    # --- E-Stop: the control this ticket is named for -------------------------
    code, _ = call(host, "POST", "/api/estop", form={})
    record("E-Stop engage", "/api/estop", code, (200,), settled("estop", True))

    # Latching means the state survives a page reload. A reload is a fresh GET
    # on a new connection, which is what this second read is.
    latched = status(host).get("estop") is True
    rows.append({"control": "E-Stop latch survives a reload", "route": "/api/status",
                 "code": 200, "expected": [200],
                 "readback": {"field": "estop", "value": latched, "matched": latched},
                 "pass": latched})

    code, _ = call(host, "POST", "/api/estop/clear", form={})
    record("E-Stop clear", "/api/estop/clear", code, (200,), settled("estop", False))

    # --- Mode ----------------------------------------------------------------
    code, _ = call(host, "POST", "/api/mode", form={"mode": "stationary"})
    record("Mode -> stationary", "/api/mode", code, (200,), settled("stationary", True))
    code, _ = call(host, "POST", "/api/mode", form={"mode": "driving"})
    record("Mode -> driving", "/api/mode", code, (200,), settled("stationary", False))

    # --- Mood ----------------------------------------------------------------
    original_mood = before.get("activeMood")
    target_mood = 10 if original_mood != 10 else 13
    code, _ = call(host, "POST", "/api/mood", form={"mood": str(target_mood)})
    record("Mood apply", "/api/mood", code, (200,), settled("activeMood", target_mood))

    # --- Dome ----------------------------------------------------------------
    # 503 is the honest bench answer with no dome board attached, and is not a
    # routing failure. 404 is, which is the only thing this has to separate.
    code, _ = call(host, "POST", "/api/dome/cmd", form={"cmd": ":SE00"})
    record("Dome command", "/api/dome/cmd", code, (200, 400, 503))
    code, _ = call(host, "GET", "/api/dome/layout")
    record("Dome layout", "/api/dome/layout", code, (200, 503))

    # --- Sequences -----------------------------------------------------------
    code, text = call(host, "GET", "/api/seq/list")
    record("Sequence list", "/api/seq/list", code, (200,))
    code, _ = call(host, "GET", "/api/seq/builtins")
    record("Sequence builtins", "/api/seq/builtins", code, (200,))
    code, _ = call(host, "POST", "/api/seq/stop", form={})
    record("Sequence stop", "/api/seq/stop", code, (200,))
    code, _ = call(host, "POST", "/api/seq/test",
                   body={"name": "PA_PROBE_NO_SUCH_SEQUENCE"})
    record("Sequence trigger (route only)", "/api/seq/test", code, (400, 500))

    # --- Actions -------------------------------------------------------------
    code, _ = call(host, "GET", "/api/actions")
    record("Action registry", "/api/actions", code, (200,))
    code, _ = call(host, "POST", "/api/actions/test", form={"token": "PA_PROBE_NO_SUCH_TOKEN"})
    record("Action trigger (route only)", "/api/actions/test", code, (400, 500))

    # Restore the mood the operator left the controller in.
    if original_mood is not None and original_mood != target_mood:
        call(host, "POST", "/api/mood", form={"mood": str(original_mood)})

    width = max(len(row["control"]) for row in rows) + 2
    print("{0:<{1}}{2:<26}{3:>6}  {4}".format("control", width, "route", "code", "read-back"))
    for row in rows:
        readback = row["readback"]
        detail = "-" if readback is None else "{0}={1} {2}".format(
            readback["field"], readback["value"], "ok" if readback["matched"] else "MISMATCH")
        print("{0:<{1}}{2:<26}{3:>6}  {4}{5}".format(
            row["control"], width, row["route"], row["code"], detail,
            "" if row["pass"] else "   <-- FAIL"))

    failed = [row for row in rows if not row["pass"]]
    print("\n{0} of {1} controls resolved and read back".format(len(rows) - len(failed), len(rows)))

    if args.json:
        with open(args.json, "w") as handle:
            json.dump({"host": host, "controls": rows}, handle, indent=2)
        print("report: {0}".format(args.json))

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
