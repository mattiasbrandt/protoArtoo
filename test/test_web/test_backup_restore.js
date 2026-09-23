// =============================================================================
// test/test_web/test_backup_restore.js
//
// Restoring a backup (data/maintenance.js): the config section goes back to the
// droid through POST /api/config, the Outputs' own settings through
// data/outputs.js in the same request (#415).
//
// The invariant: which Outputs exist, and which config fields save each, is the
// running firmware's answer - GET /api/config's Output entries, each with its
// enabledField, typeField and, where a light may go on that wire, its
// ledCountField - and a backup is matched to it by the Output's stored id. So a
// backup made before the firmware reported those fields restores the same way,
// and the page lists no Output of its own (ADR 0033 Amendment 2026-09-19). The
// field names here follow no pattern on purpose: a page that built them from
// the id would send fields this firmware never named.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { createRequire } from "node:module";

import { loadPageModule } from "./helpers/page_module_env.js";
import { servoRow, configOutputs, outputsModule } from "./helpers/fake_droid.js";

const require = createRequire(import.meta.url);
const { createFeatureAvailability } = require("../../data/feature_availability.js");

// What the running firmware reports about its Outputs: one that carries only
// a servo, and one a light may go on, which names a third field for that
// light's own settings (ADR 0067). One that may not names none.
const OUTPUTS = configOutputs([servoRow("ledc:0", "GPIO 49"), servoRow("ledc:3", "GPIO 4")], {
  "ledc:0": { enabled: false },
  "ledc:3": { lightCapable: true, enabled: false, type: "rgb" },
});
const LIVE = { components: { ...OUTPUTS, domeEsc: { enabled: false, label: "GPIO 48" } } };
const [DOOR, STRIP] = Object.keys(OUTPUTS);
const field = (id, name) => OUTPUTS[id][name];

// A backup taken before the firmware reported any save fields: ids only, one
// of them an Output this droid does not have.
const BACKUP = {
  schema: 1,
  generated: "2026-09-01T00:00:00Z",
  config: {
    components: {
      [DOOR]: { enabled: true, type: "mg90s", ledCount: 9, throwMs: 700, accelMs: 120, ease: "overshoot", boot: "home-release" },
      [STRIP]: { enabled: true, type: "rgb", ledCount: 24 },
      aux3: { enabled: true, type: "mg996r" },
    },
    [`${DOOR}OpenUs`]: 1900,
    [`${DOOR}CloseUs`]: 1100,
  },
};

class FileReaderNow {
  readAsText(file) {
    this.onload({ target: { result: file.text } });
  }
}

test("a restore saves each Output under the fields the running firmware names for it", async () => {
  let env = null;
  env = loadPageModule("maintenance.js", {
    respond: (path) => (path === "/api/config" ? { data: LIVE } : { data: {} }),
    overrides: {
      PAFeatureAvailability: createFeatureAvailability(),
      FileReader: FileReaderNow,
      PAOutputs: outputsModule(() => env.window.PAApi),
    },
  });
  await env.settle();

  env.element("backup-file-input").files = [{ text: JSON.stringify(BACKUP) }];
  env.emitOn("backup-file-input", "change");
  env.emitOn("backup-restore-btn", "click");
  await env.settle(8);

  const saves = env.requests.filter((request) => request.method === "POST" && request.path === "/api/config");
  assert.equal(saves.length, 1, "the config section is restored in one save");
  const form = new URLSearchParams(saves[0].opts.body);
  assert.equal(form.get(field(DOOR, "enabledField")), "true", "the wired tick goes out under the firmware's field");
  assert.equal(form.get(field(DOOR, "typeField")), "mg90s", "and the servo under its");
  assert.equal(form.get(`${DOOR}OpenUs`), "1900", "the recorded ends follow the Output's id");
  assert.equal(form.get("enableArm1"), null, "no field the firmware did not name");
  assert.equal(form.get("aux3Type"), null, "and nothing for an Output this droid does not report");

  // A light's settings are one per Output since #413. The droid-wide pair this
  // replaced could carry exactly one answer, so a restore onto a droid with two
  // lit wires put one builder's LED count on both and dropped the other. Each
  // one goes back under the field ITS Output named, and an Output that cannot
  // carry a light gets none - even when the backup holds a number for it.
  assert.equal(form.get(field(STRIP, "ledCountField")), "24", "the lit Output's LED count goes back under its own field");
  assert.equal(form.get("ledCount"), null, "never under a name of the page's own making");
  assert.equal([...form.values()].filter((value) => value === "9").length, 0,
    "and an Output that cannot be lit is sent no count");

  // How each Output moves (#414) is its own setting like the rest, and a
  // restore that left it out handed a restored droid the defaults: every door
  // back to a one-second throw and no ease, silently.
  assert.equal(form.get(field(DOOR, "throwField")), "700", "time to full throw goes back under the firmware's field");
  assert.equal(form.get(field(DOOR, "accelField")), "120", "time to get up to speed under its");
  assert.equal(form.get(field(DOOR, "easeField")), "overshoot", "and the ease under its");
  assert.equal(form.get(field(DOOR, "bootField")), "home-release",
    "and what it does at power-up, or a restored droid comes up limp everywhere");
});
