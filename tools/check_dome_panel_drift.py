#!/usr/bin/env python3
"""Drift check: protoArtoo's vendored dome panel SVG vs the AstroPixelsPlus source.

protoArtoo's sequence-editor dome picker (``data/dome_panel_model.js`` ->
``DOME_PANEL_MAP_SVG``) is a *vendored copy* of the AstroPixelsPlus MK4 dome map
(``data/panels.html``). Because it is a copy, it can silently drift from the
fork's source of truth if the dome map changes there. This script compares the
two and reports drift.

Default source is the *live dome's* served panel UI, since the operator always
has the running dome but not necessarily a local AstroPixelsPlus checkout:

    tools/check_dome_panel_drift.py
    tools/check_dome_panel_drift.py --source http://astropixelsplus.local/panels.html
    tools/check_dome_panel_drift.py --source /path/to/AstroPixelsPlus/data/panels.html

Comparison:
  - Selectable ring + pie panel paths (id + ``d=`` geometry) -- the core check.
  - Fixed panels and ring callout anchors -- best-effort (ids/markup differ
    between the fork source and the protoArtoo port, so these are warnings by
    default; use --strict to make them fail too).
  - Whitespace / comma / number-formatting (minification) differences in ``d=``
    are ignored; coordinate *values* are compared (rounded to 2 dp).

Exit status:
  0  no drift (within the selected strictness)
  1  drift detected
  2  could not read/parse a side (e.g. dome unreachable)

NOTE: This live fetch is a developer/operator drift check, NOT a required build
gate. It becomes obsolete once AstroPixelsPlus exposes a stable
``GET /api/panels/model`` endpoint and the body renders from that at runtime
(see the post-v1.0.0 dome-served-panel-model issue). Until then the vendored
copy is a pragmatic MK4 shortcut, not the long-term contract.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import urllib.request

VENDORED_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data", "dome_panel_model.js",
)
DEFAULT_SOURCE = "http://astropixelsplus.local/panels.html"

# Operator-facing identity set for the movable panels (the geometry that matters).
SELECTABLE_IDS = {
    "p1", "p2", "p3", "p4", "p7", "p11", "p13",
    "pp1", "pp2", "pp3", "pp4", "pp5", "pp6",
}


def _read(src: str) -> str:
    if src.startswith(("http://", "https://")):
        with urllib.request.urlopen(src, timeout=10) as resp:
            return resp.read().decode("utf-8", "replace")
    with open(src, encoding="utf-8") as fh:
        return fh.read()


def _attr(tag: str, name: str) -> str | None:
    m = re.search(r'\b' + re.escape(name) + r'\s*=\s*"([^"]*)"', tag)
    return m.group(1) if m else None


def _extract_svg(text: str) -> str:
    """Return the dome-panel <svg>...</svg> (the one carrying panel ids)."""
    for svg in re.findall(r"<svg\b.*?</svg>", text, re.S):
        if re.search(r'\bid="pp?\d', svg):
            return svg
    return text  # vendored file is already just the SVG


def _vendored_svg() -> str:
    js = _read(VENDORED_PATH)
    m = re.search(r"DOME_PANEL_MAP_SVG\s*=\s*`(.*?)`", js, re.S)
    if not m:
        sys.exit(f"could not find DOME_PANEL_MAP_SVG in {VENDORED_PATH}")
    return m.group(1)


def _norm_d(d: str):
    """Canonicalise a path ``d`` so whitespace/comma/minification is ignored
    but coordinate values are preserved (rounded to 2 dp)."""
    toks = re.findall(r"[A-Za-z]|-?\d*\.?\d+(?:e-?\d+)?", d.replace(",", " "))
    out = []
    for t in toks:
        if t[0].isalpha():
            out.append(t)
        else:
            try:
                out.append(round(float(t), 2))
            except ValueError:
                out.append(t)
    return out


def _paths(svg: str) -> dict:
    out = {}
    for tag in re.findall(r"<path\b[^>]*>", svg):
        pid, d = _attr(tag, "id"), _attr(tag, "d")
        if pid and d:
            out[pid] = d
    return out


def _callouts(svg: str) -> dict:
    """Ring callout circles keyed by their panel target (data-target on the
    protoArtoo side, domePanelClick('Pxx') on the fork side)."""
    out = {}
    for tag in re.findall(r"<circle\b[^>]*>", svg):
        tgt = _attr(tag, "data-target")
        if not tgt:
            oc = _attr(tag, "onclick") or ""
            m = re.search(r"domePanelClick\('([^']+)'\)", oc)
            tgt = m.group(1) if m else None
        if not tgt:
            continue
        cx, cy, r = _attr(tag, "cx"), _attr(tag, "cy"), _attr(tag, "r")
        out[tgt.lower().lstrip("p")] = (cx, cy, r)
    return out


def main(argv) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default=DEFAULT_SOURCE,
                    help=f"AstroPixelsPlus panels source URL or file (default: {DEFAULT_SOURCE})")
    ap.add_argument("--strict", action="store_true",
                    help="treat fixed-panel and callout differences as drift too")
    args = ap.parse_args(argv)

    try:
        ours = _vendored_svg()
    except OSError as exc:
        print(f"ERROR: cannot read vendored model: {exc}", file=sys.stderr)
        return 2
    try:
        theirs = _extract_svg(_read(args.source))
    except Exception as exc:  # network / file errors
        print(f"ERROR: cannot read source {args.source!r}: {exc}", file=sys.stderr)
        print("       (dome may be off; pass --source <file> to compare offline)", file=sys.stderr)
        return 2

    our_paths, their_paths = _paths(ours), _paths(theirs)
    drift = []   # hard drift (core)
    warn = []    # best-effort

    # --- Core: selectable ring + pie panel geometry ---
    our_sel = {k: v for k, v in our_paths.items() if k in SELECTABLE_IDS}
    their_sel = {k: v for k, v in their_paths.items() if k in SELECTABLE_IDS}

    for pid in sorted(SELECTABLE_IDS - their_sel.keys()):
        if pid in our_sel:
            drift.append(f"selectable panel '{pid}' present in vendored but MISSING in source")
    for pid in sorted(their_sel.keys() - our_sel.keys()):
        drift.append(f"selectable panel '{pid}' present in source but EXTRA-missing in vendored")
    for pid in sorted(our_sel.keys() & their_sel.keys()):
        if _norm_d(our_sel[pid]) != _norm_d(their_sel[pid]):
            drift.append(f"selectable panel '{pid}' geometry CHANGED (d= differs)")

    # --- Best-effort: fixed panels (ids differ between port and fork; compare
    #     only those whose ids exist on both sides) ---
    our_fixed = {k: v for k, v in our_paths.items() if k not in SELECTABLE_IDS}
    their_fixed = {k: v for k, v in their_paths.items() if k not in SELECTABLE_IDS}
    for pid in sorted(our_fixed.keys() & their_fixed.keys()):
        if _norm_d(our_fixed[pid]) != _norm_d(their_fixed[pid]):
            warn.append(f"fixed/other panel '{pid}' geometry differs")
    only_ours = sorted(our_fixed.keys() - their_fixed.keys())
    only_theirs = sorted(their_fixed.keys() - our_fixed.keys())
    if only_ours:
        warn.append(f"fixed/other ids only in vendored (likely port renames): {', '.join(only_ours)}")
    if only_theirs:
        warn.append(f"fixed/other ids only in source: {', '.join(only_theirs)}")

    # --- Best-effort: ring callout anchors ---
    our_co, their_co = _callouts(ours), _callouts(theirs)
    for tgt in sorted(our_co.keys() & their_co.keys()):
        if our_co[tgt] != their_co[tgt]:
            warn.append(f"callout for target '{tgt}' anchor differs "
                        f"{our_co[tgt]} (vendored) vs {their_co[tgt]} (source)")

    # --- Report ---
    print(f"vendored: {VENDORED_PATH}")
    print(f"source  : {args.source}")
    print(f"selectable panels: {len(our_sel)} vendored / {len(their_sel)} source")
    if args.strict:
        drift += warn
        warn = []

    if warn:
        print("\nWARNINGS (best-effort; not failing -- use --strict to enforce):")
        for w in warn:
            print(f"  - {w}")
    if drift:
        print("\nDRIFT DETECTED:")
        for d in drift:
            print(f"  - {d}")
        print("\nVendored dome panel map has drifted from the AstroPixelsPlus source.")
        return 1

    print("\nOK: no drift in selectable panel geometry"
          + (" (and no best-effort warnings)" if not warn else " (see warnings above)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
