// =============================================================================
// test/test_web/test_console_help_txt_223.js
//
// data/console_help.txt is generated from docs/action-registry.yaml by
// tools/generate_console_catalog.py and read at runtime by the Console
// module's help path (both adapters, ADR 0036) - it is the "web production"
// data/ artifact this ticket's registry corrections regenerate (#223).
//
// Six status entries carried wrong or placeholder executor names before this
// ticket (docs/action-registry.yaml's commit history has the detail): the
// literal string "Core 0" for sound.status.current, and buildStatusJson - the
// giant /api/status builder, not the real per-endpoint core - for
// system.status.health, system.status.wifi, dome.status.current and
// dome.status.serial-link. `help <operation>` renders this file's executor
// field verbatim on both adapters, so a silent regression here would put a
// wrong or non-existent symbol back in front of every operator who asks.
//
// This does not re-implement the generator's escaping (backslash/pipe/newline)
// - none of the six lines under test contain characters that need it, so a
// plain split on "|" is a faithful read of the real file, not a parser stand-in.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const helpTxtPath = join(__dirname, "../../data/console_help.txt");

// name -> executor, read fresh on every test run (never cached at import
// time) so a stale regenerate is caught the same as a wrong one.
const readExecutors = () => {
  const lines = readFileSync(helpTxtPath, "utf-8").split("\n").filter(Boolean);
  const executors = new Map();
  for (const line of lines) {
    const parts = line.split("|");
    const [name, , , executor] = parts;
    executors.set(name, executor);
  }
  return executors;
};

const EXPECTED_EXECUTORS = {
  "system.status.health": "captureHealthSnapshot",
  "system.status.wifi": "captureWifiStatusSnapshot",
  "dome.status.current": "captureDomeStatusSnapshot",
  "sound.status.current": "captureAudioStatusSnapshot",
  "dome.status.serial-link": "captureDomeSerialLinkSnapshot",
  "rc.status.snapshot": "captureRcDiagnosticsSnapshot",
};

test("console help text names the real capture function for every status query #223 corrected", () => {
  const executors = readExecutors();
  for (const [name, expected] of Object.entries(EXPECTED_EXECUTORS)) {
    assert.ok(executors.has(name), `${name} missing from data/console_help.txt`);
    assert.strictEqual(
      executors.get(name),
      expected,
      `${name}'s help executor should be ${expected}`
    );
  }
});

test("console help text no longer carries the pre-#223 placeholder/wrong executors", () => {
  const executors = readExecutors();
  // The exact defects this ticket fixed: a non-symbol placeholder, and the
  // monolithic /api/status builder standing in for five different real cores.
  assert.notStrictEqual(
    executors.get("sound.status.current"),
    "Core 0",
    'sound.status.current must not carry the placeholder executor "Core 0"'
  );
  for (const name of [
    "system.status.health",
    "system.status.wifi",
    "dome.status.current",
    "dome.status.serial-link",
  ]) {
    assert.notStrictEqual(
      executors.get(name),
      "buildStatusJson",
      `${name} must not be attributed to buildStatusJson, the /api/status builder`
    );
  }
});
