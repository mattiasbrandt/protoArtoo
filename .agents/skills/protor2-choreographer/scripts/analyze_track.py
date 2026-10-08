#!/usr/bin/env python3
"""Analyse a music track for a Learned Sequence: the editor's own tempo reading
with its levels, a downbeat and a beat reference from Beat This! when it is
installed, and how far a constant grid holds over the chosen window.

    analyze_track.py <track> [--window START END] [--bpm BPM] [--downbeat-ms MS] [--out FILE]

First run: no --bpm. Read the levels, let the builder pick one, then run again
with --bpm (and --downbeat-ms if they moved beat 1) to get the tempo block and
the grid-holding figure for that choice. Bar 1 starts on the downbeat, with up
to three pickup beats before it (barPhase), so intro steps have beats too. The JSON written to --out (default
tasks/choreo/analysis/<track>.json) carries the `tempo` block ready to paste.
"""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

import _common as c

HOLD_MS = 70  # a beat further off the music than this is heard as off (MIREX tolerance)


def beat_this(track: Path):
    """Beats and downbeats in ms from Beat This!, or None with the reason."""
    try:
        from beat_this.inference import File2Beats
    except ImportError as error:
        return None, f"Beat This! is not installed ({error}); no downbeat proposal and no grid-holding figure"
    beats, downs = File2Beats(checkpoint_path="final0", device="cpu", dbn=False)(str(track))
    return ([float(b) * 1000 for b in beats], [float(d) * 1000 for d in downs]), None


def fit_bpm(beats_ms: list[float]):
    """Least-squares tempo through a run of beats (needs at least 4)."""
    if len(beats_ms) < 4:
        return None
    n = len(beats_ms)
    mk = (n - 1) / 2
    mt = sum(beats_ms) / n
    slope = sum((k - mk) * (t - mt) for k, t in enumerate(beats_ms)) / sum((k - mk) ** 2 for k in range(n))
    return 60000 / slope


def interp(xs: list[float], ys: list[float], x: float) -> float:
    """Piecewise-linear y(x) over increasing xs, extrapolated at the ends."""
    if x <= xs[0]:
        i = 0
    elif x >= xs[-1]:
        i = len(xs) - 2
    else:
        lo, hi = 0, len(xs) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if xs[mid] <= x:
                lo = mid
            else:
                hi = mid
        i = lo
    return ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / (xs[i + 1] - xs[i])


def grid_holds(bpm: float, phase_ms: float, ref_bpm: float, ref_beats: list[float], start_ms: float, end_ms: float):
    """How far a constant grid at `bpm` from `phase_ms` stays within HOLD_MS of
    the reference beats, read at the chosen level: the reference's own beat
    count is scaled by bpm/ref_bpm, so a half or double level is compared with
    every other or every half reference beat."""
    idx = list(range(len(ref_beats)))
    ratio = bpm / ref_bpm
    u0 = interp(ref_beats, idx, phase_ms)
    period = 60000 / bpm
    worst, holds_until, k = 0.0, None, 0
    # Past the reference's last beat there is nothing to compare with.
    end_ms = min(end_ms, ref_beats[-1])
    while True:
        t = phase_ms + k * period
        if t > end_ms:
            break
        if t >= start_ms:
            ref = interp(idx, ref_beats, u0 + k / ratio)
            off = abs(t - ref)
            worst = max(worst, off)
            if off > HOLD_MS and holds_until is None:
                holds_until = t
        k += 1
    return {"worstOffMs": round(worst), "holdsUntilMs": None if holds_until is None else round(holds_until)}


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("track", type=Path)
    p.add_argument("--window", nargs=2, type=float, metavar=("START_S", "END_S"), help="the stretch the sequence covers, seconds")
    p.add_argument("--bpm", type=float, help="the tempo level the builder picked")
    p.add_argument("--downbeat-ms", type=float, help="where beat 1 sits, if the builder moved it")
    p.add_argument("--out", type=Path)
    a = p.parse_args(argv)
    if not a.track.is_file():
        p.error(f"no such file: {a.track}")

    with tempfile.TemporaryDirectory() as tmp:
        pcm = Path(tmp) / "pcm.f32"
        c.decode(a.track, pcm)
        ours = c.node("analyze", str(pcm), str(c.SAMPLE_RATE))
    if not ours["bpm"]:
        print("No steady beat in this track: type a tempo or tap it in the editor instead.")
        return 1
    duration = ours["durationMs"]
    start_ms, end_ms = ((a.window[0] * 1000, a.window[1] * 1000) if a.window else (0.0, float(duration)))
    end_ms = min(end_ms, float(duration))

    bt, why_not = beat_this(a.track)
    ref = None
    if bt:
        beats, downs = bt
        in_window = [b for b in beats if start_ms <= b <= end_ms]
        ref_bpm = fit_bpm(in_window)
        first_down = next((d for d in downs if d >= start_ms), None)
        ref = {"bpm": None if ref_bpm is None else round(ref_bpm, 1), "downbeatMs": None if first_down is None else round(first_down)}

    levels = []
    for lv in ours["levels"]:
        near = bool(ref and ref["bpm"] and abs(lv["bpm"] - ref["bpm"]) / ref["bpm"] < 0.04)
        levels.append({**lv, "nearReference": near})

    chosen = a.bpm if a.bpm else ours["bpm"]
    period = 60000 / chosen
    # Beat 1: the builder's, else Beat This!'s downbeat, else the analyser's
    # beat 0 -- each put on the chosen grid, so beat 1 is a beat of it.
    anchor = a.downbeat_ms if a.downbeat_ms is not None else (ref["downbeatMs"] if ref and ref["downbeatMs"] is not None else ours["phaseMs"])
    k = round((anchor - ours["phaseMs"]) / period)
    downbeat = max(0.0, ours["phaseMs"] + k * period)
    # Bar 1 starts on the downbeat, and up to a bar's worth of beats before it
    # are pickups (barPhase), so a step in the intro still has a beat to sit
    # on rather than being pulled onto the downbeat.
    bar_len = 4
    pickups = min(bar_len - 1, int(downbeat // period))
    phase = round(downbeat - pickups * period)

    holds = None
    if bt and ref and ref["bpm"]:
        holds = grid_holds(chosen, downbeat, ref["bpm"], bt[0], start_ms, end_ms)

    tempo = {
        "bpm": round(chosen, 1), "phase": phase, "barLen": bar_len, "barPhase": pickups, "duration": duration,
        "hash": c.fingerprint(a.track), "source": "analysed", "confidence": ours["confidence"],
    }
    out = {
        "track": str(a.track), "windowMs": [round(start_ms), round(end_ms)],
        "analyser": {"bpm": ours["bpm"], "phaseMs": ours["phaseMs"], "confidence": ours["confidence"]},
        "levels": levels, "reference": ref, "referenceNote": why_not,
        "chosenBpm": round(chosen, 1), "downbeatMs": round(downbeat), "pickupBeats": pickups, "gridHolds": holds, "tempo": tempo,
    }
    dest = a.out or c.ROOT / "tasks" / "choreo" / "analysis" / f"{a.track.stem}.json"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(out, indent=1) + "\n")

    print(f"{a.track.name}: {duration / 1000:.1f} s, heard {ours['bpm']} BPM, confidence {ours['confidence']}")
    print("Levels (bpm, strength vs heard):")
    for lv in levels:
        mark = "  <- Beat This! agrees" if lv["nearReference"] else ""
        print(f"  {lv['num']}/{lv['den']:<2} {lv['bpm']:>6}  {lv['strength']:.2f}{mark}")
    if ref:
        print(f"Beat This!: {ref['bpm']} BPM in the window, first downbeat at {ref['downbeatMs']} ms")
    else:
        print(f"Reference: {why_not}")
    print(f"Chosen {tempo['bpm']} BPM, bar 1 at {round(downbeat)} ms, {pickups} pickup beat(s) from {phase} ms")
    if holds:
        if holds["holdsUntilMs"] is None:
            print(f"Grid holds across the window (worst {holds['worstOffMs']} ms off)")
        else:
            print(f"Grid holds until {holds['holdsUntilMs'] / 1000:.1f} s, then drifts past {HOLD_MS} ms (worst {holds['worstOffMs']} ms)")
    print(f"Written: {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
