#!/usr/bin/env python3
"""Retime an existing Learned Sequence onto a tempo: every step to its nearest
beat, through the editor's own SeqTempo.retime, with its receipt.

    retime_sequence.py <seq.json> --analysis <analysis.json> [--out FILE]

The tempo block comes from analyze_track.py's output. The retimed copy is
written to --out (default <seq>.retimed.json); the original is left alone.
A step inside a loop body is timed from its pass and cannot sit on a beat, so
the receipt counts it among the steps that did not land.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import _common as c


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("seq", type=Path)
    p.add_argument("--analysis", type=Path, required=True)
    p.add_argument("--out", type=Path)
    a = p.parse_args(argv)
    seq = c.load_json(a.seq)
    seq["tempo"] = c.load_json(a.analysis)["tempo"]
    r = c.node("retime", payload=seq)
    if "error" in r:
        print(f"Not retimed: {r['error']}")
        return 1
    dest = a.out or a.seq.with_suffix(".retimed.json")
    dest.write_text(json.dumps(r["seq"], indent=1) + "\n")
    print(f"{r['landed']} of {r['total']} steps landed on a beat. Written: {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
