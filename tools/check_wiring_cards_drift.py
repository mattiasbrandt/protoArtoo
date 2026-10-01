#!/usr/bin/env python3
"""Check that Wiring's committed product cards still match the spec sheets.

The `wiring_card:` blocks in docs/spec-sheets/ generate two committed partials
(tools/generate_wiring_cards.py), and the one failure the generator cannot
catch is nobody running it. A sheet's card edited with the partial left stale
is a hazard corrected in the research and still wrong on the builder's screen
(#458).

    python3 tools/check_wiring_cards_drift.py     # or: make check-wiring-cards-drift

REPORT, NEVER REWRITE - the convention tools/check_action_registry_drift.py
set. It runs THE REAL GENERATOR with its writes intercepted in memory and
byte-compares the result against the committed files, the shape
tools/check_droid_parts_drift.py uses and whose interception this borrows: a
check that rebuilds what it tests in place can never fail twice.

A card the generator refuses - an unknown id, a citation of no section, an
avoided word - is reported here too, as the finding it is.

The slice gate carries this through tools/check_action_registry_drift.py, the
way that check carries check_setting_words.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_wiring_cards as gen  # noqa: E402  (after the path insert above)
from check_droid_parts_drift import Wrote, writes_captured  # noqa: E402


def check(errors: list[str]) -> int:
    """Append every finding to `errors`; return how many cards were generated."""
    outputs = (gen.CARDS_OUTPUT_PATH, gen.EMPTY_OUTPUT_PATH)
    try:
        with writes_captured({path.resolve() for path in outputs}) as captured:
            cards = gen.generate(quiet=True)
    except gen.CardError as error:
        errors.extend(error.problems)
        return 0
    except Wrote as error:
        errors.append(
            f"{gen.GENERATOR_NAME} wrote to {error}, which this check does not know "
            "about; teach it that output before trusting a green run"
        )
        return 0

    for path in outputs:
        name = gen.rel(path)
        if not path.exists():
            errors.append(f"{name} is missing; run {gen.GENERATOR_NAME}")
            continue
        # Bytes, so a re-encoding or a line ending changed under an editor is
        # drift rather than something a text read would normalise away.
        committed = path.read_bytes()
        built = captured[path.resolve()].encode("utf-8")
        if committed != built:
            errors.append(
                f"{name} is not what {gen.GENERATOR_NAME} makes of the wiring_card "
                f"blocks in {gen.rel(gen.SHEETS_DIR)}/ today ({len(committed)} bytes "
                f"committed, {len(built)} generated); regenerate it"
            )
    return len(cards)


def main() -> int:
    errors: list[str] = []
    count = check(errors)
    if errors:
        print("Wiring cards drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(
        f"Wiring cards drift check passed ({count} cards, "
        "2 committed partials byte-identical to a fresh run)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
