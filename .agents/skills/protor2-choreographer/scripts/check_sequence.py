#!/usr/bin/env python3
"""Check a drafted Learned Sequence the way the droid's editor does: Protocol
Check (the save gate) and the Rehearsal (what will not happen as written),
both run from data/ under Node.

    check_sequence.py <seq.json> [--resolve]

--resolve rewrites the file with every beat-placed step's `t` worked out from
its beat, as the editor does on save. Exit 1 when Protocol Check refuses it.
The Rehearsal runs without a droid, so rules that need the droid's Outputs or
config stay silent and its gaps say so.
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
    p.add_argument("--resolve", action="store_true")
    a = p.parse_args(argv)
    seq = c.load_json(a.seq)
    if a.resolve:
        seq = c.node("resolve", payload=seq)
        a.seq.write_text(json.dumps(seq, indent=1) + "\n")
        print(f"Resolved beats to t in {a.seq}")
    r = c.node("check", payload=seq)
    pc = r["protocolCheck"]
    if pc.get("ok"):
        print("Protocol Check: accepted")
    else:
        print(f"Protocol Check: REFUSED at {pc.get('field', '?')}: {pc.get('error')}")
    reh = r["rehearsal"]
    for f in reh["findings"]:
        where = f" (step {f['step']})" if "step" in f else ""
        print(f"  {f['level']:7} {f['code']}{where}: {f['msg']} Fix: {f['fix']}")
    if not reh["findings"]:
        print("  Rehearsal: no findings")
    for g in reh["gaps"]:
        print(f"  not checked: {g['n']} x {g['code']}")
    fig = reh["figures"]
    print(f"  {fig['steps']} steps of {fig['maxSteps']}, {fig['bytes']} bytes, about {fig['durationMs']} ms")
    return 0 if pc.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
