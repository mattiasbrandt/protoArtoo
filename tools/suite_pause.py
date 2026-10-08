#!/usr/bin/env python3
"""Per-slice product suites are paused through 2026-10-31 (#464).

The operator is checking whether a second agent's read of the diff carries
the quality these runs were being repeated to buy. The tests stay in the
tree. CI still runs the native suite and the web suite on a pull request
into main when its files reach them (.github/workflows/verification.yml
classifies the pull request's whole range; the epic's closure pull request
reaches both). This pause covers local
`make test`, `make test-web`, the native, web, and mutation stages of
slice_verify, and a direct `tools/mutation_verify.py` run.

On 2026-11-01 the skip ends by itself. PROTOR2_SUITES=1 runs them now.

`--check` exits 0 when the pause is active and 1 when the suites should run,
so the Makefile can fall through.
"""

from __future__ import annotations

import os
import sys
from datetime import date

# First day the suites run again. The pause covers 2026-10-02 through 2026-10-31.
SUITES_RESUME_ON = date(2026, 11, 1)

BANNER = (
    "SUITES PAUSED until 2026-11-01 (#464). "
    "Native tests, web tests, and mutation checks are not run per slice. "
    "A pull request into main still runs the native and web suites in CI "
    "when its files reach them. "
    "PROTOR2_SUITES=1 runs them now."
)


def paused(today: date | None = None) -> bool:
    if os.environ.get("PROTOR2_SUITES") == "1":
        return False
    if today is None:
        today = date.today()
    return today < SUITES_RESUME_ON


def main(argv: list[str]) -> int:
    active = paused()
    if "--check" in argv:
        if active:
            print(BANNER)
        return 0 if active else 1
    print(BANNER if active else "suites are not paused")
    return 0 if active else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
