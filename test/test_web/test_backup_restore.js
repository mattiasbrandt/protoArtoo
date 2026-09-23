// =============================================================================
// test/test_web/test_backup_restore.js
//
// Restoring a backup (data/maintenance.js): the config section goes back to the
// droid through POST /api/config.
//
// The invariant: which Outputs exist, and which config fields save each, is the
// running firmware's answer - GET /api/config's Output entries, each with its
// enabledField, typeField and, where a light may go on that wire, its
// ledCountField - and a backup is matched to it by the Output's stored id. So a backup made before the firmware reported those fields
// restores the same way, and the page lists no Output of its own (ADR 0033
// Amendment 2026-09-19). The field names here follow no pattern on purpose: a
// page that built them from the id would send fields this firmware never named.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { createRequire } from "node:module";

import { loadPageModule } from "./helpers/page_module_env.js";

const require = createRequire(import.meta.url);
const { createFeatureAvailability } = require("../../data/feature_availability.js");

// What the running firmware reports about its Outputs.
const LIVE = {
  components: {
    arm1: { enabled: false, label: "GPIO 49", address: "ledc:0", enabledField: "wiredA", typeField: "servoA", type: "mg996r" },
    // An Output a light may go on names a third field, for that light's own
    // settings (ADR 0067). One that may not names none.
    aux1: { enabled: false, label: "GPIO 4", address: "ledc:3", enabledField: "wiredB", typeField: "servoB", ledCountField: "ledsB", type: "rgb", ledCount: 1 },
    domeEsc: { enabled: false, label: "GPIO 48" },
  },
};

// A backup taken before the firmware reported any save fields: ids only.
const BACKUP = {
  schema: 1,
  generated: "2026-09-01T00:00:00Z",
  config: {
    components: {
      arm1: { enabled: true, type: "mg90s", ledCount: 9 },
      aux1: { enabled: true, type: "rgb", ledCount: 24 },
      aux3: { enabled: true, type: "mg996r" },
    },
    arm1OpenUs: 1900,
    arm1CloseUs: 1100,
  },
};

class FileReaderNow {
  readAsText(file) {
    this.onload({ target: { result: file.text } });
  }
}

test("a restore saves each Output under the fields the running firmware names for it", async () => {
  const env = loadPageModule("maintenance.js", {
    respond: (path) => (path === "/api/config" ? { data: LIVE } : { data: {} }),
    overrides: { PAFeatureAvailability: createFeatureAvailability(), FileReader: FileReaderNow },
  });
  await env.settle();

  env.element("backup-file-input").files = [{ text: JSON.stringify(BACKUP) }];
  env.emitOn("backup-file-input", "change");
  env.emitOn("backup-restore-btn", "click");
  await env.settle(8);

  const saves = env.requests.filter((request) => request.method === "POST" && request.path === "/api/config");
  assert.equal(saves.length, 1, "the config section is restored in one save");
  const form = new URLSearchParams(saves[0].opts.body);
  assert.equal(form.get("wiredA"), "true", "the wired tick goes out under the firmware's field");
  assert.equal(form.get("servoA"), "mg90s", "and the servo under its");
  assert.equal(form.get("arm1OpenUs"), "1900", "the recorded ends follow the Output's id");
  assert.equal(form.get("enableArm1"), null, "no field the firmware did not name");
  assert.equal(form.get("aux3Type"), null, "and nothing for an Output this droid does not report");

  // A light's settings are one per Output since #413. The droid-wide pair this
  // replaced could carry exactly one answer, so a restore onto a droid with two
  // lit wires put one builder's LED count on both and dropped the other. Each
  // one goes back under the field ITS Output named, and an Output that cannot
  // carry a light gets none - even when the backup holds a number for it.
  assert.equal(form.get("ledsB"), "24", "the lit Output's LED count goes back under its own field");
  assert.equal(form.get("ledCount"), null, "never under a name of the page's own making");
  assert.equal(form.get("ledsA"), null, "and an Output that cannot be lit is sent no count");
});
