"""The motion planner is one declaration with two outputs, and they agree (#439).

docs/servo-motion.yaml generates the planner ServoTask moves every Output with
(include/servo_motion_model.h) and the one the Rehearsal times a move with
(data/servo_motion.js). Three things make that true rather than hoped for:

  the committed outputs are what the generator makes today, which the drift
  check reports and never repairs, so a corrupted output fails twice;

  the two outputs plan the same move the same way, down to the millisecond the
  C++ rounds to. That is asserted by compiling the generated header with the
  native test flags and running the generated module in node, over the same
  moves, and comparing every field - not by a hand-written model of either.
"""

import contextlib
import io
import json
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import check_servo_motion_drift as check  # noqa: E402
import generate_servo_motion as gen  # noqa: E402

INPUTS = (gen.DECLARATION_NAME, "include/servo_output_row.h", "include/sequence_bulk_centre.h")


def scratch_tree(stack):
    """The declaration, the headers it reads and its outputs, in a temp dir."""
    root = Path(stack.enter_context(tempfile.TemporaryDirectory()))
    for name in INPUTS + (gen.FIRMWARE_NAME, gen.BROWSER_NAME):
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / name, root / name)
    return root


class TheCommittedTree(unittest.TestCase):
    def test_the_committed_outputs_match_the_declaration_they_came_from(self):
        self.assertEqual(check.check(), [])

    def test_the_check_exits_zero_on_the_committed_tree(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(check.main(), 0)


class Staleness(unittest.TestCase):
    def test_an_edited_declaration_with_stale_outputs_fails(self):
        with contextlib.ExitStack() as stack:
            root = scratch_tree(stack)
            declaration = root / gen.DECLARATION_NAME
            text = declaration.read_text(encoding="utf-8")
            declaration.write_text(text.replace("distance / 12", "distance / 10"), encoding="utf-8")
            problems = check.check(root)
        self.assertEqual(len(problems), 2, problems)
        self.assertTrue(all("is stale" in p for p in problems), problems)

    def test_a_header_constant_that_moved_makes_the_browser_output_stale(self):
        with contextlib.ExitStack() as stack:
            root = scratch_tree(stack)
            header = root / "include/sequence_bulk_centre.h"
            text = header.read_text(encoding="utf-8")
            header.write_text(text.replace("SEQ_CADENCE_FLOOR_MS = 450;",
                                           "SEQ_CADENCE_FLOOR_MS = 500;"), encoding="utf-8")
            problems = check.check(root)
        self.assertEqual(problems, [f"{gen.BROWSER_NAME} is stale against "
                                    f"{gen.DECLARATION_NAME}; run {gen.GENERATOR_NAME}"])

    def test_a_corrupted_output_fails_and_fails_again(self):
        with contextlib.ExitStack() as stack:
            root = scratch_tree(stack)
            output = root / gen.BROWSER_NAME
            output.write_text(output.read_text(encoding="utf-8").replace(
                "trunc(distance / 12)", "trunc(distance / 10)"), encoding="utf-8")
            before = output.read_bytes()
            first, second = check.check(root), check.check(root)
            self.assertEqual(output.read_bytes(), before, "the check rewrote what it checks")
        self.assertEqual(first, second)
        self.assertEqual(len(first), 1, first)

    def test_an_implicit_conversion_in_the_declaration_is_refused(self):
        with contextlib.ExitStack() as stack:
            root = scratch_tree(stack)
            declaration = root / gen.DECLARATION_NAME
            text = declaration.read_text(encoding="utf-8")
            declaration.write_text(text.replace("fullThrowMs: f32 = f32(profile.throwMs)",
                                                "fullThrowMs: f32 = profile.throwMs"),
                                   encoding="utf-8")
            problems = check.check(root)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("write the cast", problems[0])


# -----------------------------------------------------------------------------
# The two outputs, move for move.
# -----------------------------------------------------------------------------
CPP_DRIVER = r"""
#include <cstdio>
#include "servo_motion_model.h"

int main() {
    unsigned lo, hi, thr, acc, ease, cal, from, to;
    while (scanf("%u %u %u %u %u %u %u %u", &lo, &hi, &thr, &acc, &ease, &cal, &from, &to) == 8) {
        ServoMotionProfile p = {};
        p.loUs = (uint16_t)lo; p.hiUs = (uint16_t)hi; p.throwMs = (uint16_t)thr;
        p.accelMs = (uint16_t)acc; p.easing = (ServoEasing)ease; p.calibrated = cal != 0;
        const ServoMotionRamp r = servoMotionPlan((uint16_t)from, (uint16_t)to, p, 0);
        printf("%u %u %u %u %u %u\n", (unsigned)r.toUs, (unsigned)r.settleUs,
               (unsigned)r.durationMs, (unsigned)r.rampMs, (unsigned)r.softStart,
               (unsigned)servoMotionArrivalMs((uint16_t)from, (uint16_t)to, p));
    }
    return 0;
}
"""

JS_DRIVER = r"""
const fs = require("node:fs");
const vm = require("node:vm");
const sandbox = { window: {} };
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(process.argv[2], "utf8"), sandbox);
const M = sandbox.window.ServoMotion;
const out = [];
for (const line of fs.readFileSync(0, "utf8").trim().split("\n")) {
  const [lo, hi, thr, acc, ease, cal, from, to] = line.split(" ").map(Number);
  const p = { loUs: lo, hiUs: hi, throwMs: thr, accelMs: acc, easing: ease, calibrated: cal !== 0 };
  const r = M.servoMotionPlan(from, to, p, 0);
  out.push([r.toUs, r.settleUs, r.durationMs, r.rampMs, r.softStart ? 1 : 0,
            M.servoMotionArrivalMs(from, to, p)].join(" "));
}
process.stdout.write(out.join("\n") + "\n");
"""


def native_build_flags():
    """[env:native] build_flags, read from platformio.ini rather than copied."""
    text = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    section = re.split(r"^\[env:native\]\s*$", text, flags=re.M)[1]
    section = re.split(r"^\[", section, flags=re.M)[0]
    match = re.search(r"^build_flags\s*=\s*(.+)$", section, re.M)
    return match.group(1).split()


def moves():
    """Edge cases a builder can reach, then a seeded spread across the ranges."""
    cases = []
    for ease in (0, 1, 2):
        for cal in (0, 1):
            for lo, hi in ((1000, 2000), (1000, 1000), (500, 2500), (1400, 1410)):
                for thr, acc in ((900, 225), (20, 1), (10000, 10000), (1000, 250), (21, 0),
                                 (19, 5), (600, 400)):
                    for frm, to in ((lo, hi), (hi, lo), (lo, lo), (lo, (lo + hi) // 2),
                                    ((lo + hi) // 2, hi - 1), (hi - 50, hi + 100), (lo + 1, lo)):
                        cases.append((lo, hi, thr, acc, ease, cal, max(frm, 0), max(to, 0)))
    # Three moves where planning in double instead of single precision rounds
    # to a different millisecond (found by searching; 1 in ~10^5 of the spread
    # below). Without them the comparison could pass on a browser module that
    # forgot the firmware computes in float.
    cases += [(1862, 1958, 4443, 4737, 2, 1, 1874, 1930),
              (1953, 2133, 5466, 1281, 2, 1, 1448, 906),
              (2278, 2490, 4327, 1185, 0, 1, 1035, 1406)]
    rng = random.Random(439)
    for _ in range(6000):
        lo = rng.randint(500, 2400)
        hi = rng.randint(lo, 2500)
        cases.append((lo, hi, rng.randint(20, 10000), rng.randint(1, 10000), rng.randint(0, 2),
                      rng.randint(0, 1), rng.randint(500, 2500), rng.randint(500, 2500)))
    return cases


@unittest.skipUnless(shutil.which("g++") and shutil.which("node"),
                     "needs g++ and node to run both generated outputs")
class TheTwoOutputsAgree(unittest.TestCase):
    def test_every_move_plans_and_arrives_the_same_in_cpp_and_javascript(self):
        cases = moves()
        stdin = "".join(" ".join(str(v) for v in case) + "\n" for case in cases)
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / "driver.cpp", Path(tmp) / "driver"
            source.write_text(CPP_DRIVER, encoding="utf-8")
            subprocess.run(["g++", *native_build_flags(), "-I", str(ROOT / "include"),
                            str(source), "-o", str(binary)], check=True, cwd=ROOT, timeout=120)
            cpp = subprocess.run([str(binary)], input=stdin, capture_output=True, text=True,
                                 check=True, timeout=60).stdout.splitlines()
            script = Path(tmp) / "driver.cjs"
            script.write_text(JS_DRIVER, encoding="utf-8")
            js = subprocess.run(["node", str(script), str(ROOT / gen.BROWSER_NAME)], input=stdin,
                                capture_output=True, text=True, check=True,
                                timeout=60).stdout.splitlines()
        self.assertEqual(len(cpp), len(cases))
        self.assertEqual(len(js), len(cases))
        disagree = [(case, c, j) for case, c, j in zip(cases, cpp, js) if c != j]
        self.assertEqual(disagree[:5], [], f"{len(disagree)} of {len(cases)} moves disagree")
        # Not a vacuous agreement: the spread has to reach every branch.
        arrivals = [int(line.split()[5]) for line in cpp]
        settles = sum(1 for line in cpp if line.split()[0] != line.split()[1])
        self.assertGreater(sum(1 for a in arrivals if a == 0), 100, "no snaps in the spread")
        self.assertGreater(sum(1 for a in arrivals if a > 0), 1000, "no ramps in the spread")
        self.assertGreater(settles, 100, "no overshoots in the spread")


if __name__ == "__main__":
    unittest.main()
