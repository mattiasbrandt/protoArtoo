// =============================================================================
// test/test_web/test_backup_restore.js
//
// Restoring a backup (data/maintenance.js): the Configuration goes back to the
// droid in one POST /api/config, in the shape GET read it - the config as the
// backup holds it, and each Output's row as `outputs` (ADR 0068).
//
// The invariant: a restore gives back every setting the backup holds. Twice a
// restore said "restored" over a droid that had lost part of it - every lit
// wire's count but one (#413), the centre, calibration and Part map (#417) -
// because each setting had its own door and the restore had to know them all.
// The droid here takes a row the way the firmware does, so a setting the
// restore leaves out stays at what the droid had and the comparison finds it.
//
// A backup made before the Outputs were read whole from their rows carries
// their settings in the config, keyed by stored id; that shape is translated
// once, at the backup seam, and the fixture is a real one (fixtures/).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { createRequire } from "node:module";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { loadPageModule } from "./helpers/page_module_env.js";
import { servoRow, outputsModule } from "./helpers/fake_droid.js";

const require = createRequire(import.meta.url);
const { createFeatureAvailability } = require("../../data/feature_availability.js");

// A backup in the shape this firmware wrote one until the Outputs became their
// rows: GET /api/config with its components{} Output entries and top-level
// arm1OpenUs .. aux3CloseUs, and GET /api/servo/outputs beside it, as
// data/maintenance.js downloads them. Every Output is set off its defaults.
const OLDER_BACKUP = JSON.parse(readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "fixtures/backup_before_rows.json"), "utf-8"));

// The same Artoo PCB as the droid restoring it: five Outputs, each row at its
// defaults, stored under the ids the firmware gives them.
const IDS = ["arm1", "arm2", "aux1", "aux2", "aux3"];
const freshDroid = () => [
  ["ledc:0", "ARM1"], ["ledc:1", "ARM2"], ["ledc:3", "ARM3"], ["ledc:4", "ARM4"], ["ledc:5", "ARM5"],
].map(([address, name], index) => servoRow(address, name, {
  id: IDS[index],
  wired: false,
  component: "none",
  ...(index >= 2 ? { lightCapable: true, ledCount: 1 } : {}),
}));

// Every setting a row holds, as the droid takes it back.
const ROW_SETTINGS = [
  "wired", "component", "ledCount", "throwMs", "accelMs", "ease", "boot",
  "openUs", "centreUs", "closeUs", "calibrated", "parts",
];

class FileReaderNow {
  readAsText(file) {
    this.onload({ target: { result: file.text } });
  }
}

// Restore `backup` onto a droid whose rows take a POST /api/config body's
// `outputs` the way the firmware does - a Part a row states comes off the row
// it was on - and whose config is the GET shape it was sent.
const restore = async (backup) => {
  let env = null;
  const rows = freshDroid();
  let config = { drive: { speedLimitMax: 100 } };
  env = loadPageModule("maintenance.js", {
    respond: (path, opts) => {
      if (opts.method === "POST" && path === "/api/config") {
        const { outputs = [], ...sent } = opts.body;
        outputs.forEach((row) => {
          (row.parts || []).forEach((part) => rows.forEach((each) => {
            each.parts = each.parts.filter((id) => id !== part);
          }));
          Object.assign(rows.find((each) => each.address === row.address), row);
        });
        config = sent;
        return { data: config };
      }
      if (path === "/api/config") return { data: config };
      if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(rows) } };
      return { data: {} };
    },
    overrides: {
      PAFeatureAvailability: createFeatureAvailability(),
      FileReader: FileReaderNow,
      PAOutputs: outputsModule(() => env.window.PAApi),
    },
  });
  await env.settle();
  env.element("backup-file-input").files = [{ text: JSON.stringify(backup) }];
  env.emitOn("backup-file-input", "change");
  env.emitOn("backup-restore-btn", "click");
  await env.settle(40);
  const posts = (path) => env.requests
    .filter((request) => request.method === "POST" && request.path === path)
    .map((request) => request.opts.body);
  return { rows, posts, receipt: env.element("backup-feedback").textContent };
};

// What the backup says each Output holds, read the way a builder would read
// it: the config's entry for the Output's id, its recorded ends, and its row.
const heldBy = (backup, id, address) => {
  const entry = backup.config.components[id];
  const row = backup.servo_outputs.outputs.find((each) => each.address === address);
  return {
    wired: entry.enabled,
    component: entry.type,
    ...(entry.ledCount !== undefined ? { ledCount: entry.ledCount } : {}),
    throwMs: entry.throwMs,
    accelMs: entry.accelMs,
    ease: entry.ease,
    boot: entry.boot,
    openUs: backup.config[`${id}OpenUs`],
    centreUs: row.centreUs,
    closeUs: backup.config[`${id}CloseUs`],
    calibrated: row.calibrated,
    parts: row.parts,
  };
};

test("a backup made before the Outputs were their rows restores every row setting, in one request", async () => {
  const { rows, posts, receipt } = await restore(OLDER_BACKUP);

  const saves = posts("/api/config");
  assert.equal(saves.length, 1, "the Configuration goes back in one request, so it lands whole or not at all");
  assert.equal(saves[0].drive.speedLimitMax, OLDER_BACKUP.config.drive.speedLimitMax,
    "the config goes back in the shape it was read");
  rows.forEach((row, index) => {
    const want = heldBy(OLDER_BACKUP, IDS[index], row.address);
    const got = Object.fromEntries(ROW_SETTINGS.filter((key) => key in want).map((key) => [key, row[key]]));
    assert.deepEqual(got, want, `${row.name} is not what the backup holds`);
  });
  assert.match(receipt, /Core config: restored/);
});

// "restored" is a claim that every part of the section landed. Before #417 it
// was said over a file with no centre, calibration or Part map at all, and over
// an audio restore that had quietly retried a refused CHIRP slot as a plain
// track - which cleared the slot's bank - and dropped every category bank.
test("a restore never says restored over something that did not land, and names it", async () => {
  const { servo_outputs: _rows, ...withoutRows } = OLDER_BACKUP;
  let env = null;
  const rows = freshDroid();
  env = loadPageModule("maintenance.js", {
    respond: (path, opts) => {
      if (opts.method === "POST" && new URLSearchParams(opts.body).has("bank")) throw new Error("refused");
      if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(rows) } };
      return { data: {} };
    },
    overrides: {
      PAFeatureAvailability: createFeatureAvailability(),
      FileReader: FileReaderNow,
      PAOutputs: outputsModule(() => env.window.PAApi),
    },
  });
  await env.settle();
  env.element("backup-file-input").files = [{
    text: JSON.stringify({
      ...withoutRows,
      audio_tracks: {
        doodoo: 3,
        snd_cat_gen_lo: 1,
        snd_cat_gen_hi: 20,
        chirp_bindings: { doodoo: { bank: 1, page: "A", index: 3 } },
        chirp_category_bindings: { snd_cat_gen_lo: { bank: 2, page: "B" } },
      },
    }),
  }];
  env.emitOn("backup-file-input", "change");
  env.emitOn("backup-restore-btn", "click");
  await env.settle(40);
  const posts = (path) => env.requests
    .filter((request) => request.method === "POST" && request.path === path)
    .map((request) => Object.fromEntries(new URLSearchParams(request.opts.body)));
  const receipt = env.element("backup-feedback").textContent;

  assert.match(receipt, /Core config: partial — no centre, calibration or Part map in this file/);
  assert.match(receipt, /Audio tracks: partial — 2 failed \(doodoo, snd_cat_gen_lo\)/);
  assert.doesNotMatch(receipt, /(Core config|Audio tracks): restored/);
  assert.deepEqual(posts("/api/audio/tracks"), [{ key: "doodoo", track: "3", bank: "1", page: "A" }],
    "a refused banked slot is not sent again as a plain track, and no category key goes this way");
  assert.deepEqual(posts("/api/audio/category-range"),
    [{ lo_key: "snd_cat_gen_lo", hi_key: "snd_cat_gen_hi", lo: "1", hi: "20", bank: "2", page: "B" }],
    "a category goes back as its pair, with its bank and page");
});
