#!/usr/bin/env python3
"""Run the browser half of the protocol-check mirror.

test/fixtures/protocol_mirror.json names a sequence and the verdict both
implementations must give. This runs data/seq_protocol_check.js. The firmware
half is the native test test_protocol_mirror, which reads the same file.
Pass --native to compile and run that test too (a PlatformIO native build).

Exit 1 when a case disagrees with its expect line.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "test" / "fixtures" / "protocol_mirror.json"
JS = r"""
const fs = require("fs");
const vm = require("vm");
const fixture = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
const window = {};
vm.runInNewContext(fs.readFileSync(process.argv[2], "utf8"), { window, console });
const check = window.SeqProtocolCheck;
if (!check || typeof check.validateSequence !== "function") {
  console.error("SeqProtocolCheck.validateSequence is missing");
  process.exit(2);
}
let failed = 0;
for (const row of fixture.cases) {
  let verdict;
  try {
    verdict = !!check.validateSequence(row.seq).ok;
  } catch (error) {
    console.error(row.name + " threw: " + error);
    failed += 1;
    continue;
  }
  const expect = !!row.expect;
  if (verdict !== expect) {
    console.error(row.name + " browser=" + verdict + " expect=" + expect);
    failed += 1;
  } else {
    console.log(row.name + " " + verdict);
  }
}
process.exit(failed ? 1 : 0);
"""


def browser() -> int:
    r = subprocess.run(
        ["node", "-e", JS, str(FIXTURE), str(ROOT / "data" / "seq_protocol_check.js")],
        cwd=ROOT,
    )
    return r.returncode


def native() -> int:
    env = dict(**{k: v for k, v in __import__("os").environ.items()})
    env["PROTOCOL_MIRROR_FIXTURE"] = str(FIXTURE)
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "pio_lock.py"),
         "pio", "test", "-e", "native", "-f", "test_native/test_protocol_mirror"],
        cwd=ROOT, env=env,
    )
    return r.returncode


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--native", action="store_true", help="also run the firmware half")
    args = p.parse_args(argv)
    code = browser()
    if code != 0:
        return code
    if args.native:
        return native()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
