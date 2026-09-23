#!/usr/bin/env python3
"""Check that no operator surface keeps its own copy of one board's wiring.

What a board prints beside a pin is a **Board Component Label**, it lives in
`include/component_labels.inc` and nowhere else, and the running firmware hands
it to the browser: `GET /api/config` reports one per component
(`src/web/api_config.cpp`, `getComponentLabel()`), and `data/configuration.js`
drops the clause where a board declares none. A surface that writes `S1` into
its own copy therefore says it twice on the Artoo PCB and says something false
on the FireBeetle 2, where the same lane prints `GPIO 20/21`.

That is the defect this catches, and it was live when the check was written:
three `docs/action-registry.yaml` display names read `S1 - Hoverboard`,
`S2 - Sound` and `S3 - Dome Control`, generated straight into the Console's
help text.

WHICH LABELS ARE BOARD-SPECIFIC, AND WHERE THAT LIST COMES FROM. Not from a
list here - from the two manifests that already declare the components whose
naming is the board's:

- `include/board_outputs.h`, `BOARD_OUTPUTS[]` - the Outputs. "An Output is
  called by what its board prints beside its pin ... There is no protoArtoo-wide
  name for an Output" (CONTEXT.md "Output Address", ADR 0033 Amendment
  2026-09-19). For an Output the label IS the name, so copy may not carry one.
- `include/board_lanes.inc` - the Board Lanes. The lane is "an optional clause
  that drops where the board declares no Lane" (#353), so it comes from the
  answer at runtime, never from the string.

`enable_dome_esc` and `enable_rc_ch1..6` are deliberately not read. Their
labels are project vocabulary that the Artoo PCB happens to print too - a
receiver's channel 1 is channel 1 on every board - and flagging `RC CH1` would
be the cry-wolf finding that gets a check muted.

THE PIN HALF IS NOT IMPLEMENTED HERE. `tools/check_pin_drift.py` compares
`include/config.h` against `docs/pin_map.md` board by board, reads the
per-board inversion from the document's own Source of Truth Contract, and
reports rather than passes vacuously where a board declares no Lane (#367).
This check calls its `check()` and prints its findings beside its own. "Two
checkers that disagree is the exact class of defect these checks exist to
catch" - so there is one, and it is that one.

Report, never rewrite - the convention `tools/check_action_registry_drift.py`
set. Run it as `make check-board-label-drift`. Its unit tests, which drive it
against fixtures and prove it can fail, are
`test/test_tools/test_board_label_drift.py`.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_pin_drift  # noqa: E402  (after the path insert above)
from operator_copy import (  # noqa: E402
    ROOT,
    is_identifier_context,
    registry_copy,
    surface_copy,
)

LABELS = ROOT / "include" / "component_labels.inc"
OUTPUTS = ROOT / "include" / "board_outputs.h"
LANES = ROOT / "include" / "board_lanes.inc"

LABEL_ROW = re.compile(r"PA_COMPONENT_LABEL\(\s*(\w+)\s*,\s*(\w+)\s*,\s*\"([^\"]*)\"\s*\)")
OUTPUT_ROW = re.compile(r"\{\s*\"(\w+)\"\s*,\s*\"(\w+)\"\s*,")
LANE_ROW = re.compile(r"PA_BOARD_LANE\(\s*(\w+)\s*,")


def rel(path: Path) -> str:
    return check_pin_drift.rel(path)


def load_labels(path: Path, errors: list[str]) -> dict[str, dict[str, str]]:
    """component -> {board: label}, read from the one file that declares them.

    Strict in the same way `load_lanes()` in the pin check is: a line that is
    neither a comment, a directive nor a row is reported, so a malformed row
    cannot vanish from the count.
    """
    labels: dict[str, dict[str, str]] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        match = LABEL_ROW.fullmatch(line)
        if not match:
            errors.append(f"{rel(path)}:{number}: unreadable PA_COMPONENT_LABEL row: {line}")
            continue
        board, component, label = match.groups()
        labels.setdefault(component, {})[board] = label
    return labels


def board_named_components(outputs: Path, lanes: Path, errors: list[str]) -> dict[str, str]:
    """component -> why its label is the board's, from the two manifests.

    An Output's component key is `BOARD_OUTPUTS[]`'s second column; a Lane's is
    its row name with the `enable_` prefix the label inventory uses.
    """
    named: dict[str, str] = {}
    for _, component in OUTPUT_ROW.findall(outputs.read_text(encoding="utf-8")):
        named[component] = f"an Output, and an Output has no name but the board's ({rel(outputs)})"
    for raw in lanes.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue  # the header comment quotes the macro's own signature
        match = LANE_ROW.match(line)
        if match:
            named[f"enable_{match.group(1)}"] = (
                f"a Board Lane, and the lane clause comes from the running board ({rel(lanes)})"
            )
    if not named:
        errors.append(
            f"{rel(outputs)} and {rel(lanes)} declare no Output and no Lane, so this check has "
            "nothing to protect. Next move: confirm the manifests, or correct OUTPUT_ROW and "
            "LANE_ROW in tools/check_board_label_drift.py to their new shape"
        )
    return named


def board_specific_labels(
    labels: dict[str, dict[str, str]], named: dict[str, str], errors: list[str]
) -> list[tuple[str, str, str]]:
    """(label, component, why) for every label a surface may not write down.

    Longest label first, so `GPIO 49` is matched before `GPIO 4` could claim it.
    """
    found: list[tuple[str, str, str]] = []
    for component, why in sorted(named.items()):
        by_board = labels.get(component)
        if not by_board:
            errors.append(
                f"{rel(LABELS)} declares no label for {component}, which is {why}. A board with "
                "an unlabelled Output has nothing to call it by"
            )
            continue
        for label in set(by_board.values()):
            found.append((label, component, why))
    return sorted(found, key=lambda row: len(row[0]), reverse=True)


def check(labels_path: Path = LABELS, outputs: Path = OUTPUTS, lanes: Path = LANES,
          data: Path | None = None, registry: Path | None = None,
          pins: bool = True) -> tuple[list[str], int, int]:
    """(findings, labels guarded, runs of copy read). Reads; never writes."""
    findings: list[str] = []
    labels = load_labels(labels_path, findings)
    named = board_named_components(outputs, lanes, findings)
    guarded = board_specific_labels(labels, named, findings)

    copy = []
    copy.extend(surface_copy(data) if data is not None else surface_copy())
    copy.extend(registry_copy(registry) if registry is not None else registry_copy())

    for piece in copy:
        if piece.in_symbol:
            continue  # another product's own legend, not this board's (see Copy.in_symbol)
        remaining = piece.text
        for label, component, why in guarded:
            for match in re.finditer(rf"(?<![\w-]){re.escape(label)}(?![\w-])", remaining):
                if is_identifier_context(remaining, match.start(), match.end()):
                    continue
                findings.append(
                    f'{piece.where()}: "{label}" is what one board prints for {component} '
                    f"({piece.kind}). It is {why}. Next move: drop it from the string - "
                    "GET /api/config already sends the running board's label"
                )
            # A label found is not searched for again under a shorter name.
            remaining = re.sub(
                rf"(?<![\w-]){re.escape(label)}(?![\w-])", lambda m: " " * len(m.group(0)), remaining
            )

    if pins:
        report = check_pin_drift.check()
        findings.extend(report.errors)
    return findings, len(guarded), len(copy)


def main(**sources) -> int:
    """`make check-board-label-drift`. `sources` are check()'s arguments."""
    findings, guarded, read = check(**sources)
    if findings:
        print("Board label drift detected:", file=sys.stderr)
        for finding in findings:
            print(f"  - {finding}", file=sys.stderr)
        return 1
    print(
        f"Board label check passed ({guarded} board-specific labels guarded, "
        f"{read} runs of operator copy read, and tools/check_pin_drift.py clean)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
