#!/usr/bin/env python3
"""Per-archive (and optional per-object) image delta between two maps.

usage: armdiff.py <control.map> <arm.map> [top] [object-filter]
Uses mapsize.py's parser (same directory).
"""
import sys, subprocess, os, re, collections
here = os.path.dirname(os.path.abspath(__file__))
ctl, arm = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
filt = sys.argv[4] if len(sys.argv) > 4 else None

def table(mp, mode, filt=None):
    args = [sys.executable, os.path.join(here, 'mapsize.py'), mp, mode, '100000']
    if filt:
        args.append(filt)
    out = subprocess.run(args, capture_output=True, text=True).stdout.splitlines()
    res = {}
    for l in out[1:]:
        if l.startswith('-') or 'TOTAL' in l:
            continue
        parts = l.split()
        if len(parts) < 8:
            continue
        key = ' '.join(parts[7:])
        res[key] = tuple(int(x) for x in parts[:7])  # image, text, ro, iram, ddata, dbss, rtc
    return res

def show(mode, filt=None):
    a, b = table(ctl, mode, filt), table(arm, mode, filt)
    keys = set(a) | set(b)
    rows = []
    for k in keys:
        x = a.get(k, (0,)*7); y = b.get(k, (0,)*7)
        d = tuple(y[i]-x[i] for i in range(7))
        if any(d):
            rows.append((d, k))
    rows.sort(key=lambda r: r[0][0])
    print(f"{'d.image':>8} {'d.text':>8} {'d.ro':>7} {'d.iram':>7} {'d.data':>7} {'d.bss':>7}  {mode}")
    for d, k in rows[:top]:
        print(f"{d[0]:>8} {d[1]:>8} {d[2]:>7} {d[3]:>7} {d[4]:>7} {d[5]:>7}  {k}")
    if len(rows) > top:
        rest = [r for r in rows[top:]]
        s = [sum(r[0][i] for r in rest) for i in range(7)]
        print(f"{s[0]:>8} {s[1]:>8} {s[2]:>7} {s[3]:>7} {s[4]:>7} {s[5]:>7}  (+{len(rest)} more)")
    tot = [sum(r[0][i] for r in rows) for i in range(7)]
    print(f"{tot[0]:>8} {tot[1]:>8} {tot[2]:>7} {tot[3]:>7} {tot[4]:>7} {tot[5]:>7}  TOTAL delta (map-attributed)")

show('archive')
if filt:
    print()
    show('object', filt)
