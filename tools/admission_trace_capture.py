#!/usr/bin/env python3
"""Capture and read the controller's admission decision trace.

The controller records every Connection Admission and request admission
decision into a small ring (include/web_admission_trace.h) and serves it at
GET /api/admission/trace. This tool arms the ring, runs whatever produces the
load, reads the ring back, and reduces it to the numbers a floor decision turns
on.

What it answers, and why each number is here:

  Band histogram
      Where the reading each decision used actually landed, relative to the two
      floors. The Busy Recovery Page can only be served in [connection floor,
      request floor); everything below is refused before a request exists.
      Whether that band is where the readings land is the whole ticket.

  Staleness
      Both layers share one cached heap sample. Every refusal is reported with
      the age of the reading it used and with a reading taken fresh immediately
      after, so a refusal taken on a heap state that had already recovered is
      counted rather than assumed either way.

  Regime
      Refusals split by layer. Connection-layer refusals mean the navigation
      died before HTTP; request-layer refusals mean the page shed its own
      assets. Both regimes have to be costed, because a change that fixes one
      can worsen the other.

Usage:

  # Arm the ring, then take a profile around whatever you do in between.
  tools/admission_trace_capture.py --host 10.0.0.22 clear
  tools/admission_trace_capture.py --host 10.0.0.22 read --out run-1.json

  # Or in one shot, with the load produced by an existing harness.
  tools/admission_trace_capture.py --host 10.0.0.22 run --out run-1.json -- \\
      node tools/webload_browser_capture.js --url http://10.0.0.22/index.html \\
           --commit <sha> --out tasks/evidence/webload/run-1/browser

Rows carry no path -- the ring is packed to eight bytes because it competes for
DRAM with the heap it measures. What each request was fetching is recovered by
position from the harness's own network log, which already records it.
"""

import argparse
import json
import subprocess
import sys
import urllib.error
import urllib.request

TRACE_PATH = "/api/admission/trace"
DEFAULT_TIMEOUT_S = 10


def fetch(host, clear=False, timeout=DEFAULT_TIMEOUT_S):
    url = "http://{}{}{}".format(host, TRACE_PATH, "?clear=1" if clear else "")
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            raise SystemExit(
                "{} answered 404. This firmware was not built with "
                "PA_ADMISSION_TRACE=1, so it cannot answer this question -- do "
                "not read that as an empty profile.".format(url)
            )
        raise SystemExit("{} failed: HTTP {}".format(url, exc.code))
    except urllib.error.URLError as exc:
        raise SystemExit("{} unreachable: {}".format(url, exc.reason))

    try:
        return json.loads(body)
    except json.JSONDecodeError as exc:
        raise SystemExit("{} returned unparseable JSON: {}".format(url, exc))


def rows_as_dicts(doc):
    fields = doc["fields"]
    return [dict(zip(fields, row)) for row in doc["rows"]]


def summarise(doc, out=sys.stdout):
    floors = doc["floors"]
    conn_floor = floors["connection"]
    request_floor = floors["request"]
    diag_floor = floors["requestDiagnostic"]
    layers = doc["layers"]
    outcomes = doc["outcomes"]
    navs = doc["nav"]
    rows = rows_as_dicts(doc)

    def line(text=""):
        print(text, file=out)

    line("floors        connection={} request={} diagnostic={}".format(
        conn_floor, request_floor, diag_floor))
    line("sampling      one shared sample, refreshed at most every {} ms".format(
        floors["sampleIntervalMs"]))
    line("inflight cap  {}".format(floors["maxInflightRequests"]))
    line("rows          {} held, {} recorded, {} overwritten".format(
        doc["held"], doc["total"], doc["overwritten"]))
    if doc["overwritten"]:
        line("              WARNING: the ring wrapped. This profile is the TAIL of "
             "the run,")
        line("              not the whole of it. Clear and capture a shorter window.")
    if not rows:
        line()
        line("No decisions recorded. Was the ring cleared after the load rather than "
             "before it?")
        return

    fresh_taken = any(row["fresh"] for row in rows)
    line("fresh reading {}".format(
        "taken per decision" if fresh_taken
        else "NOT taken (PA_ADMISSION_TRACE_FRESH=0): staleness is unmeasurable in "
             "this run"))
    line()

    # ---------------------------------------------------------------- outcomes
    line("Decisions by layer and outcome")
    counts = {}
    for row in rows:
        counts[(row["layer"], row["outcome"])] = counts.get(
            (row["layer"], row["outcome"]), 0) + 1
    for (layer, outcome), count in sorted(counts.items()):
        line("  {:<5} {:<9} {}".format(layers[layer], outcomes[outcome], count))
    line()

    # ------------------------------------------------------------------- curve
    blocks = [row["block"] for row in rows]
    ordered = sorted(blocks)
    line("Reading used, across every decision")
    line("  min {}  p25 {}  median {}  p75 {}  max {}".format(
        ordered[0],
        ordered[len(ordered) // 4],
        ordered[len(ordered) // 2],
        ordered[(3 * len(ordered)) // 4],
        ordered[-1]))
    line()

    # ------------------------------------------------------------------- bands
    # The band a reading lands in decides what a refusal can look like on the
    # wire, which is the question this ticket exists to settle.
    below = sum(1 for b in blocks if b < conn_floor)
    inside = sum(1 for b in blocks if conn_floor <= b < request_floor)
    above = sum(1 for b in blocks if b >= request_floor)
    total = len(blocks)
    line("Where the readings landed")
    line("  below {:<6} (connection refused pre-HTTP, browser error page)  {:>4}  {:>5.1f}%"
         .format(conn_floor, below, 100.0 * below / total))
    line("  [{}, {})  (the only band the Busy Recovery Page can fire in) {:>5}  {:>5.1f}%"
         .format(conn_floor, request_floor, inside, 100.0 * inside / total))
    line("  at or above {:<6} (served)                                  {:>6}  {:>5.1f}%"
         .format(request_floor, above, 100.0 * above / total))
    line()

    # --------------------------------------------------------------- staleness
    refusals = [row for row in rows if outcomes[row["outcome"]] != "admit"]
    if not refusals:
        line("No refusals in this profile.")
    else:
        line("Refusals: {}".format(len(refusals)))
        ages = sorted(row["ageMs"] for row in refusals)
        line("  age of the reading used: min {} ms, median {} ms, max {} ms".format(
            ages[0], ages[len(ages) // 2], ages[-1]))

        if fresh_taken:
            # The control the staleness argument needs. A refusal whose fresh
            # reading clears the same floor was taken on a heap state that had
            # already recovered -- shed by the cache, not by the heap.
            recovered = 0
            for row in refusals:
                floor = conn_floor if layers[row["layer"]] == "conn" else request_floor
                if row["fresh"] >= floor:
                    recovered += 1
            line("  taken on a reading the heap had ALREADY recovered from: {} of {} "
                 "({:.1f}%)".format(recovered, len(refusals),
                                    100.0 * recovered / len(refusals)))
            line("     -- these would have been admitted against a reading taken at "
                 "the moment of the decision.")

        navigations = [row for row in refusals if navs[row["nav"]] == "navigation"]
        assets = [row for row in refusals if navs[row["nav"]] == "asset"]
        line("  refused navigations: {} (each one is a dead tab unless the Busy "
             "Recovery Page answered it)".format(len(navigations)))
        line("  refused assets:      {} (each one is a page shedding its own "
             "resources)".format(len(assets)))
    line()
    line("Rows carry no path. Join them by position onto the capturing harness's "
         "request log.")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True,
                        help="Controller host or IP, e.g. 10.0.0.22")
    parser.add_argument("--out", help="Write the raw trace JSON here")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S,
                        help="HTTP timeout in seconds (default %(default)s)")
    parser.add_argument("action", choices=("clear", "read", "run"))
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="For 'run': -- followed by the command producing the load")
    args = parser.parse_args(argv)

    if args.action == "clear":
        fetch(args.host, clear=True, timeout=args.timeout)
        print("Ring armed. Produce the load, then read it back.")
        return 0

    if args.action == "run":
        command = [part for part in args.command if part != "--"]
        if not command:
            parser.error("run needs a command: ... run -- node tools/...")
        fetch(args.host, clear=True, timeout=args.timeout)
        # The exit code is reported, not enforced: a harness that reports a
        # failed page load is exactly the run whose profile is worth reading, so
        # its failure must not stop the readback.
        completed = subprocess.run(command)
        print("load command exited {}".format(completed.returncode))
        print()

    doc = fetch(args.host, clear=False, timeout=args.timeout)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            json.dump(doc, handle, indent=1)
        print("raw profile written to {}".format(args.out))
        print()

    summarise(doc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
