#!/usr/bin/env python3
"""Check that the committed motion planner outputs still match their declaration.

docs/servo-motion.yaml generates include/servo_motion_model.h, which ServoTask
plans every move with, and data/servo_motion.js, which the Rehearsal times a move
with (#439). The one failure the generator cannot catch is nobody running it:
an edited declaration, or an edited header constant it reads, with stale outputs
beside it is a browser timing moves by physics the droid no longer runs.

    python3 tools/check_servo_motion_drift.py

REPORT, NEVER REWRITE, the convention tools/check_droid_parts_drift.py follows:
it renders both outputs with THE REAL GENERATOR, in memory, byte-compares them
against the committed files, and writes nothing. A check that regenerated in
place would repair the drift it was meant to report, and pass the second time
on evidence it manufactured itself.

A green run says the committed outputs are what today's generator makes of
today's declaration and headers. It says nothing about whether the model is
right: that is test_native/test_servo_motion_ramp's, which runs the generated
planner, and test/test_tools/test_servo_motion_drift.py's check that the two
outputs agree move for move.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_servo_motion as gen


def check(root=None, declaration_path=None):
    """Every disagreement, as a list of lines. Empty is a pass."""
    root = Path(root or gen.ROOT)
    problems = []
    try:
        expected = gen.render(declaration_path or root / gen.DECLARATION_NAME, root)
    except gen.ModelError as error:
        return [f"{gen.DECLARATION_NAME} cannot be generated: {p}" for p in error.problems]
    for name, text in expected.items():
        path = root / name
        if not path.exists():
            problems.append(f"{name} is missing; run {gen.GENERATOR_NAME}")
            continue
        committed = path.read_text(encoding="utf-8")
        if gen.STAMP not in committed:
            problems.append(f"{name} has lost its '{gen.STAMP}' stamp")
        if committed != text:
            problems.append(f"{name} is stale against {gen.DECLARATION_NAME}; "
                            f"run {gen.GENERATOR_NAME}")
    return problems


def main() -> int:
    problems = check()
    if problems:
        print("Servo motion model drift:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    print("Servo motion model: include/servo_motion_model.h and data/servo_motion.js "
          "match docs/servo-motion.yaml")
    return 0


if __name__ == "__main__":
    sys.exit(main())
