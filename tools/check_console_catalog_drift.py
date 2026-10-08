#!/usr/bin/env python3
"""Check that the committed Operation Catalog still matches the registry it came from.

docs/action-registry.yaml generates three committed files through
tools/generate_console_catalog.py - include/console_catalog.h,
src/console/console_catalog.cpp and data/console_help.txt - and the one failure
the generator cannot catch is nobody running it. A registry row edited with the
catalog left stale is a Console that answers for yesterday's registry, and a
hand-edited catalog is a Console that answers for no registry at all (#474).

    python3 tools/check_console_catalog_drift.py     # or: make check-console-catalog-drift

REPORT, NEVER REWRITE - the convention tools/check_action_registry_drift.py
set. It runs THE REAL GENERATOR, whose render functions build each file as a
string and write nothing, and byte-compares the result against the committed
files: a check that rebuilds what it tests in place can never fail twice. The
same shape as tools/check_droid_parts_drift.py, without that tool's write
interception, because this generator keeps its writes in main().

A row the generator refuses - a `console: excluded:` reason outside its fixed
list, or a `console: page:` that is not a page in SURFACES (data/shell.js) - is
reported here too, as the finding it is.

The slice gate carries this through tools/check_action_registry_drift.py, the
way that check carries check_wiring_cards_drift.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_console_catalog as gen  # noqa: E402  (after the path insert above)


def rel(path: Path) -> str:
    return path.relative_to(gen.REPO_ROOT).as_posix()


def check(errors: list[str]) -> int:
    """Append every finding to `errors`; return how many files were compared."""
    try:
        outputs = gen.generate()
    except gen.CatalogError as error:
        errors.extend(f"{gen.GENERATOR_NAME} refuses {rel(gen.REGISTRY_PATH)}: {problem}"
                      for problem in error.problems)
        return 0

    for path, text in outputs.items():
        name = rel(path)
        if not path.exists():
            errors.append(f"{name} is missing; run {gen.GENERATOR_NAME}")
            continue
        # Bytes, so a re-encoding or a line ending changed under an editor is
        # drift rather than something a text read would normalise away.
        committed = path.read_bytes()
        built = text.encode("utf-8")
        if committed != built:
            errors.append(
                f"{name} is not what {gen.GENERATOR_NAME} produces from "
                f"{rel(gen.REGISTRY_PATH)} today ({len(committed)} bytes committed, "
                f"{len(built)} generated); regenerate it, never edit it by hand"
            )
    return len(outputs)


def main() -> int:
    errors: list[str] = []
    count = check(errors)
    if errors:
        print("Console catalog drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"Console catalog drift check passed ({count} committed files byte-identical "
          "to a fresh run).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
