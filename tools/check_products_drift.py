#!/usr/bin/env python3
"""Check that the committed Component Registry manifest is what docs/products.yaml generates.

docs/products.yaml generates include/component_registry.inc
(tools/generate_component_registry.py), and the one failure the generator
cannot catch is nobody running it - or somebody editing the manifest by hand,
which the next regeneration then silently reverts.

    python3 tools/check_products_drift.py     # or: make check-products-drift

REPORT, NEVER REWRITE - the convention tools/check_action_registry_drift.py
set. It runs THE REAL GENERATOR with its writes intercepted in memory and
byte-compares the result against the committed file, the shape
tools/check_droid_parts_drift.py uses and whose interception this borrows.

A YAML the generator refuses is reported here too, as the finding it is.

Wiring's product cards are generated from the same file and held by
tools/check_wiring_cards_drift.py. What the manifest MEANS - every capability
has a consumer, a gate agrees with its `included` - is still
tools/check_component_registry_drift.py's, which reads the compiled manifest.

The slice gate carries this through tools/check_action_registry_drift.py, the
way that check carries check_wiring_cards_drift.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_component_registry as gen  # noqa: E402  (after the path insert above)
from check_droid_parts_drift import Wrote, writes_captured  # noqa: E402


def check(errors: list[str]) -> None:
    """Append every finding to `errors`."""
    output = gen.OUTPUT_PATH
    try:
        with writes_captured({output.resolve()}) as captured:
            gen.generate(quiet=True)
    except gen.RegistryError as error:
        errors.extend(error.problems)
        return
    except Wrote as error:
        errors.append(
            f"{gen.GENERATOR_NAME} wrote to {error}, which this check does not know "
            "about; teach it that output before trusting a green run"
        )
        return

    name = gen.rel(output)
    if not output.exists():
        errors.append(f"{name} is missing; run {gen.GENERATOR_NAME}")
        return
    # Bytes, so a re-encoding or a line ending changed under an editor is
    # drift rather than something a text read would normalise away.
    committed = output.read_bytes()
    built = captured[output.resolve()].encode("utf-8")
    if committed != built:
        errors.append(
            f"{name} is not what {gen.GENERATOR_NAME} makes of {gen.rel(gen.PRODUCTS_PATH)} "
            f"today ({len(committed)} bytes committed, {len(built)} generated); edit the "
            "YAML, not the manifest, and regenerate it"
        )


def main() -> int:
    errors: list[str] = []
    check(errors)
    if errors:
        print("Component Registry drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(
        f"Products drift check passed ({gen.rel(gen.OUTPUT_PATH)} byte-identical "
        "to a fresh run)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
