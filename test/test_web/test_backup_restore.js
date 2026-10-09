// =============================================================================
// test/test_web/test_backup_restore.js
//
// Backup and restore (data/maintenance.js). A backup holds three parts - the
// Sequences, the Configuration and the RC Map - and a restore replaces the
// parts ticked, never merging (ADR 0056).
//
// The invariants:
//   - a restore gives back every setting the backup holds. Twice a restore
//     said "restored" over a droid that had lost part of it - every lit wire's
//     count but one (#413), the centre, calibration and Part map (#417) -
//     because each setting had its own door and the restore had to know them
//     all. The Configuration goes back in one POST /api/config, in the shape
//     GET read it (ADR 0068).
//   - a backup holds the builder's Sequences. For its first life the file had
//     none while the page promised every setting (#294).
//   - a Sequences restore the droid refuses partway leaves the library exactly
//     as it was. The reference emptied a library, failed, and saved the empty
//     one (r2d2-astromech-simulator v1.77.0).
//   - the RC Map never goes out holding a binding the droid refuses, since one
//     such binding refuses the whole map, and a refused map is never called
//     restored.
//   - a copy of what is about to be replaced that cannot be built stops the
//     restore before anything is written.
//
// The droid here answers the way the firmware does: its Sequence store has a
// cap and refuses a new save when full, POST /api/rc/map refuses the whole map
// over one dome_seq binding to a Sequence it neither holds nor ships
// (src/web/api_rc_map_apply.cpp), and its rows take a POST /api/config body's
// `outputs` - so a setting the restore leaves out stays at what the droid had
// and the comparison finds it.
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

import { loadPageModule, partsGlobals } from "./helpers/page_module_env.js";
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
  "wired", "component", "ledCount", "throwMs", "accelMs", "ease", "release", "boot",
  "openUs", "centreUs", "closeUs", "calibrated", "parts",
];

// A Learned Sequence as GET /api/seq?name= answers it: the stored JSON v1.
const sequence = (name, steps = []) => ({ version: 1, name, toggleGroup: "none", suppressMs: 0, steps });

// A Factory Sequence the droid ships and accepts a binding to.
const FACTORY = ["DM:VADER", "DM:LEIA"];

class FileReaderNow {
  readAsText(file) {
    this.onload({ target: { result: file.text } });
  }
}

const httpError = (status, message) => Object.assign(new Error(message), { kind: "http", status });

/**
 * A droid behind the routes a backup and a restore use. `library` is its
 * Learned Sequences in list order; `refuse(seq)` returns the droid's reason
 * to refuse a save, or null; `fail(path, method)` fails a request outright.
 * `breaks(path, method)` fails a write AFTER the store changed, the two ways
 * the firmware can: "rename" is a save that removed the old file and then
 * could not rename the new one into place (src/seq_store.cpp), answering 500;
 * "no-reply" is a write that landed and whose answer never arrived.
 */
const makeDroid = ({
  board = "artoo_esp32",
  cap = 5,
  library = [],
  rcMap = { mode: "dual_sbus", map: [] },
  refuse = () => null,
  fail = () => false,
  breaks = () => null,
  soundOff = false,
} = {}) => {
  const { DroidParts } = partsGlobals();
  const modelled = new Set(DroidParts.parts.map((part) => part.id));
  const droid = {
    DroidParts,
    rows: freshDroid(),
    config: { drive: { speedLimitMax: 100 } },
    store: new Map(library.map((seq) => [seq.name, structuredClone(seq)])),
    rcMap: structuredClone(rcMap),
  };
  droid.respond = (path, opts) => {
    const method = opts.method;
    if (fail(path, method)) throw httpError(0, `no answer from ${path}`);
    if (path === "/api/identity") {
      return { data: { board, learned_sequence_cap: cap, learned_sequence_max_bytes: 12288 } };
    }
    if (path === "/api/seq/list") return { data: [...droid.store.keys()].map((name) => ({ name, valid: true })) };
    if (path === "/api/seq/builtins") return { data: FACTORY.map((name) => ({ name })) };
    if (path.startsWith("/api/seq?name=")) {
      const name = decodeURIComponent(path.slice("/api/seq?name=".length));
      if (!droid.store.has(name)) throw httpError(404, "not found");
      if (method === "DELETE") {
        droid.store.delete(name);
        if (breaks(path, method) === "no-reply") throw httpError(0, `no answer from ${path}`);
        const danglingBindings = droid.rcMap.map
          .filter((entry) => entry.action === "dome_seq" && entry.payload === name)
          .map((entry) => ({ source: entry.source, channel: entry.channel }));
        return { data: { ok: true, ...(danglingBindings.length ? { danglingBindings } : {}) } };
      }
      return { data: structuredClone(droid.store.get(name)) };
    }
    if (path === "/api/seq" && method === "POST") {
      const seq = opts.body;
      const reason = refuse(seq);
      if (reason) throw httpError(400, reason);
      if (!droid.store.has(seq.name) && droid.store.size >= cap) throw httpError(400, `store full (${cap} sequences max)`);
      const broken = breaks(path, method);
      if (broken === "rename") {
        droid.store.delete(seq.name);
        throw httpError(500, "rename failed");
      }
      droid.store.set(seq.name, structuredClone(seq));
      if (broken === "no-reply") throw httpError(0, `no answer from ${path}`);
      return { data: { ok: true } };
    }
    if (path === "/api/rc/map" && method === "POST") {
      const body = JSON.parse(opts.body.plain);
      body.map.forEach((entry) => {
        if (entry.action === "dome_seq" && !FACTORY.includes(entry.payload) && !droid.store.has(entry.payload)) {
          throw httpError(400, "invalid dome sequence payload (expected DM:NAME)");
        }
        if (entry.action === "dome_marcduino" && String(entry.payload).startsWith(":SM")) {
          throw httpError(400, ":SM is diagnostic only and cannot be saved as an RC binding");
        }
        // A droid of one SBUS receiver reads SBUS1 only, and says which entry
        // it refused and why, as data (ADR 0070).
        if (droid.rcMap.mode === "single_sbus" && entry.source === "sbus2") {
          throw Object.assign(httpError(400, "the RC Receiver type does not read this source"), {
            field: "map.source", reason: "out-of-range", accepts: "sbus1",
            entry: { source: entry.source, channel: entry.channel, action: entry.action },
          });
        }
      });
      droid.rcMap = { ...droid.rcMap, map: body.map };
      return { data: { ok: true } };
    }
    if (path === "/api/rc/map") return { data: structuredClone(droid.rcMap) };
    if (path === "/api/audio" && method === "GET") return { data: { output: soundOff ? "off" : "on" } };
    if (path === "/api/audio" && soundOff) throw httpError(409, "Sound is off.");
    if (method === "POST" && path === "/api/config") {
      if ((opts.body.droidBuild?.fitted || []).some((id) => !modelled.has(id))) {
        throw Object.assign(httpError(400, "fittedParts names a Part this build does not model"), {
          field: "fittedParts", reason: "out-of-range", accepts: null,
        });
      }
      const { outputs = [], ...sent } = opts.body;
      outputs.forEach((row) => {
        (row.parts || []).forEach((part) => droid.rows.forEach((each) => {
          each.parts = each.parts.filter((id) => id !== part);
        }));
        Object.assign(droid.rows.find((each) => each.address === row.address), row);
      });
      droid.config = sent;
      return { data: droid.config };
    }
    if (path === "/api/config") return { data: droid.config };
    if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(droid.rows) } };
    return { data: {} };
  };
  return droid;
};

// The page on `droid`, with the browser's file download caught: each file the
// page saves lands in `saved`, parsed.
const openPage = (droid) => {
  let env = null;
  const saved = [];
  class Blob {
    constructor(parts) {
      this.text = parts.join("");
    }
  }
  const URL = {
    createObjectURL: (blob) => {
      saved.push(JSON.parse(blob.text));
      return "blob:backup";
    },
    revokeObjectURL: () => {},
  };
  env = loadPageModule("maintenance.js", {
    respond: droid.respond,
    chain: ["seq_rehearsal.js"],
    overrides: {
      PAFeatureAvailability: createFeatureAvailability(),
      FileReader: FileReaderNow,
      DroidParts: droid.DroidParts,
      PAOutputs: outputsModule(() => env.window.PAApi, { DroidParts: droid.DroidParts }),
      Blob,
      URL,
    },
  });
  const writes = () => env.requests.filter((request) => request.method !== "GET");
  const posts = (path) => env.requests
    .filter((request) => request.method === "POST" && request.path === path)
    .map((request) => request.opts.body);
  const receipt = () => env.element("backup-feedback").textContent;
  // Waits for the page to settle after an act that makes several requests in
  // turn: until `done()` holds, bounded so a hang fails rather than stalls.
  const until = async (done) => {
    for (let i = 0; i < 200 && !done(); i += 1) await env.settle(5);
    assert.ok(done(), `the page did not settle: ${receipt()}`);
  };
  return { env, saved, writes, posts, receipt, until };
};

// Choose `backup` as the file, untick the parts named in `untick`, press
// Restore and answer the question with `answer` ("replace", "copy" or
// "cancel"); returns once the receipt is in.
const restoreOn = async (droid, backup, { untick = [], answer = "replace" } = {}) => {
  const page = openPage(droid);
  const { env } = page;
  await env.settle();
  env.element("backup-file-input").files = [{ text: JSON.stringify(backup) }];
  env.emitOn("backup-file-input", "change");
  await page.until(() => env.element("backup-restore-btn").disabled === false);
  untick.forEach((id) => {
    env.element(`restore-chk-${id}`).checked = false;
    env.emitOn(`restore-chk-${id}`, "change");
  });
  env.emitOn("backup-restore-btn", "click");
  env.emitOn(`restore-${answer}-btn`, "click");
  await page.until(() => !["", "Restoring..."].includes(page.receipt()));
  return page;
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
  const droid = makeDroid();
  const { posts, receipt } = await restoreOn(droid, OLDER_BACKUP);

  const saves = posts("/api/config");
  assert.equal(saves.length, 1, "the Configuration goes back in one request, so it lands whole or not at all");
  assert.equal(saves[0].drive.speedLimitMax, OLDER_BACKUP.config.drive.speedLimitMax,
    "the config goes back in the shape it was read");
  droid.rows.forEach((row, index) => {
    const want = heldBy(OLDER_BACKUP, IDS[index], row.address);
    const got = Object.fromEntries(ROW_SETTINGS.filter((key) => key in want).map((key) => [key, row[key]]));
    assert.deepEqual(got, want, `${row.name} is not what the backup holds`);
  });
  assert.match(receipt(), /Configuration: restored/);
});

// A backup made since an Output was its row carries every setting on the row,
// in the shape GET /api/servo/outputs answers it, and a restore puts every one
// of them back: a setting the restore leaves off is one the droid silently
// keeps from before, which reads as restored and is not - the Output Release
// time (#443) included.
test("a backup's rows go back with every setting each row holds", async () => {
  const backup = structuredClone(OLDER_BACKUP);
  backup.servo_outputs.outputs.forEach((row, index) => {
    Object.assign(row, {
      wired: true, throwMs: 700 + index, accelMs: 120 + index, ease: "soft",
      release: 1500 + index * 250, boot: "home-hold",
    });
  });
  const { posts } = await restoreOn(makeDroid(), backup);

  const sent = posts("/api/config")[0].outputs;
  backup.servo_outputs.outputs.forEach((row) => {
    const back = sent.find((each) => each.address === row.address);
    ROW_SETTINGS.filter((key) => key in row).forEach((key) => {
      assert.deepEqual(back?.[key], row[key], `${row.name}'s ${key} did not go back as the backup holds it`);
    });
  });
});

// A backup can name an Output this droid does not have - an expander that is
// not fitted here. Its row is not sent (the droid would change nothing for it),
// and "restored" is not said over it: the receipt names what did not land
// (#417).
test("an Output the backup names and this droid lacks is not sent, and the receipt names it", async () => {
  const backup = structuredClone(OLDER_BACKUP);
  backup.servo_outputs.outputs.push({ address: "pca:3", name: "", parts: [], calibrated: true, centreUs: 1500 });
  const { posts, receipt } = await restoreOn(makeDroid(), backup);

  const sent = posts("/api/config")[0].outputs.map((row) => row.address);
  assert.ok(!sent.includes("pca:3"), "a row for an Output this droid lacks is not sent");
  assert.equal(sent.length, 5, "and every Output it has is");
  assert.match(receipt(), /Configuration: partial — pca:3 not on this droid/);
});

// The backup the #355 bench restored (2026-09-28): made before #409 retired
// `drawer`, onto a droid with Sound off. The droid refuses a fitted list naming
// a Part it does not model, so the retired id used to fail the whole
// Configuration - droid build, Sound pick and Guided Setup with it - and the
// volume read as a failure. The retired id is dropped and named; the volume is
// left out and said to be.
test("a backup naming a retired Part, restored with Sound off, lands the rest and names what it left out", async () => {
  const backup = structuredClone(OLDER_BACKUP);
  backup.config.droidBuild.fitted.push("drawer");
  const droid = makeDroid({ soundOff: true });
  const { posts, receipt } = await restoreOn(droid, backup);

  assert.deepEqual(droid.config.droidBuild?.fitted, ["doorFL", "utilUp", "gripArm"],
    "every fitted Part this build models lands, and the retired one does not");
  assert.deepEqual(droid.config.guidedSetup, OLDER_BACKUP.config.guidedSetup);
  assert.equal(droid.config.components?.audio?.member, OLDER_BACKUP.config.components.audio.member);
  assert.match(receipt(), /Configuration: partial — [^\n]*drawer/, `the dropped Part is not named: ${receipt()}`);
  assert.equal(posts("/api/audio").length, 0, "no volume is sent to a droid with Sound off");
  assert.match(receipt(), /Audio tracks: restored[^\n]*volume/, `the skipped volume is not said: ${receipt()}`);
});

// An older backup can carry the 5000 ms SBUS timeout a diagnostic once used.
// The droid now takes at most 1000 ms (#389) and would refuse the whole body,
// so the restore lowers it and the receipt says so.
test("a backup with an SBUS timeout above 1000 ms restores at 1000 and says so", async () => {
  const backup = structuredClone(OLDER_BACKUP);
  backup.config.rc = { ...(backup.config.rc || {}), sbusTimeoutMs: 5000 };
  const { posts, receipt } = await restoreOn(makeDroid(), backup);

  assert.equal(posts("/api/config")[0].rc.sbusTimeoutMs, 1000);
  assert.match(receipt(), /Configuration: partial — [^\n]*signal-lost timeout lowered to 1000 ms/);
});

test("a backup with an SBUS timeout within range sends it as it is", async () => {
  const backup = structuredClone(OLDER_BACKUP);
  backup.config.rc = { ...(backup.config.rc || {}), sbusTimeoutMs: 300 };
  const { posts, receipt } = await restoreOn(makeDroid(), backup);

  assert.equal(posts("/api/config")[0].rc.sbusTimeoutMs, 300);
  assert.doesNotMatch(receipt(), /signal-lost timeout/);
});

// A backup that puts one Part on two Outputs is refused by the droid as a
// conflict, and the refusal names only the row (`ledc:3.parts`). The builder
// is told which Part, by the name the catalog gives it - never its id, and
// never the row key - found from the rows the restore itself sent.
test("a backup with one Part on two Outputs says which Part, in the builder's words", async () => {
  const backup = structuredClone(OLDER_BACKUP);
  const aux1 = backup.servo_outputs.outputs.find((row) => row.address === "ledc:3");
  aux1.parts = ["doorFL"];  // doorFL is on ARM1 in the fixture too
  const droid = makeDroid();
  const partName = droid.DroidParts.parts.find((part) => part.id === "doorFL").name;
  const answer = droid.respond;
  droid.respond = (path, opts) => {
    if (opts.method === "POST" && path === "/api/config") {
      throw Object.assign(httpError(400, "ledc:3.parts names a Part another row names too"), {
        field: "ledc:3.parts", reason: "conflict", accepts: null,
      });
    }
    return answer(path, opts);
  };
  const { receipt } = await restoreOn(droid, backup);

  assert.match(receipt(), /Configuration: FAILED/);
  assert.ok(receipt().includes(partName), `the Part is not named as the builder knows it: ${receipt()}`);
  assert.ok(receipt().includes("ARM3"), `the Output is not named as the board prints it: ${receipt()}`);
  assert.doesNotMatch(receipt(), /doorFL|ledc:/, "no id and no row key reaches the builder");
});

// "restored" is a claim that every part of the section landed. Before #417 it
// was said over a file with no centre, calibration or Part map at all, and over
// an audio restore that had quietly retried a refused CHIRP slot as a plain
// track - which cleared the slot's bank - and dropped every category bank.
test("a restore never says restored over something that did not land, and names it", async () => {
  const { servo_outputs: _rows, ...withoutRows } = OLDER_BACKUP;
  const droid = makeDroid();
  const answer = droid.respond;
  droid.respond = (path, opts) => {
    if (opts.method === "POST" && typeof opts.body === "object" && new URLSearchParams(opts.body).has("bank")) {
      throw new Error("refused");
    }
    return answer(path, opts);
  };
  const { env, receipt } = await restoreOn(droid, {
    ...withoutRows,
    audio_tracks: {
      doodoo: 3,
      snd_cat_gen_lo: 1,
      snd_cat_gen_hi: 20,
      chirp_bindings: { doodoo: { bank: 1, page: "A", index: 3 } },
      chirp_category_bindings: { snd_cat_gen_lo: { bank: 2, page: "B" } },
    },
  });
  const posts = (path) => env.requests
    .filter((request) => request.method === "POST" && request.path === path)
    .map((request) => Object.fromEntries(new URLSearchParams(request.opts.body)));

  assert.match(receipt(), /Configuration: partial — no centre, calibration or Part map in this file/);
  assert.match(receipt(), /Audio tracks: partial — 2 failed \(doodoo, snd_cat_gen_lo\)/);
  assert.doesNotMatch(receipt(), /(Configuration|Audio tracks): restored/);
  assert.deepEqual(posts("/api/audio/tracks"), [{ key: "doodoo", track: "3", bank: "1", page: "A" }],
    "a refused banked slot is not sent again as a plain track, and no category key goes this way");
  assert.deepEqual(posts("/api/audio/category-range"),
    [{ lo_key: "snd_cat_gen_lo", hi_key: "snd_cat_gen_hi", lo: "1", hi: "20", bank: "2", page: "B" }],
    "a category goes back as its pair, with its bank and page");
});

test("a backup holds every Learned Sequence as the droid stores it, in its list order, and the board that wrote it", async () => {
  const library = [sequence("DM:WAVE", [{ type: "dome", cmd: ":OP01", ms: 0 }]), sequence("DM:NOD")];
  const page = openPage(makeDroid({ board: "firebeetle2", cap: 10, library }));
  await page.env.settle();
  page.env.emitOn("backup-download-btn", "click");
  await page.until(() => page.saved.length > 0 || /No backup/.test(page.receipt()));

  const [file] = page.saved;
  assert.deepEqual(file.sequences, library);
  assert.equal(file.board, "firebeetle2");
  assert.ok(file.config && file.servo_outputs && file.rc_map && file.audio_tracks && file.audio_mood_map,
    "and every part it carried before");
});

test("a Sequence the droid refuses partway through leaves the droid's library exactly as it was", async () => {
  const library = [sequence("DM:MINE"), sequence("DM:KEEP", [{ type: "dome", cmd: ":OP01", ms: 0 }])];
  const droid = makeDroid({
    library,
    refuse: (seq) => (seq.name === "DM:BIG" ? "payload too large" : null),
  });
  const backup = {
    schema: 2,
    board: "artoo_esp32",
    sequences: [sequence("DM:KEEP"), sequence("DM:NEW"), sequence("DM:BIG")],
  };
  const { receipt } = await restoreOn(droid, backup);

  assert.deepEqual([...droid.store.values()].sort((a, b) => a.name.localeCompare(b.name)),
    library.slice().sort((a, b) => a.name.localeCompare(b.name)),
    "the droid holds the Sequences it had, each as it was");
  assert.match(receipt(), /Sequences: FAILED — the droid refused DM:BIG: payload too large\. The Sequences the droid had are put back\./);
});

// The droid changes its store before it answers. A save that fails at the
// rename has already removed the Sequence it was replacing, and a delete whose
// answer is lost has still deleted. Recording a name only on a good answer
// left both out of the put-back, and the receipt said the library was back
// (Codex review of #448 slice 1).
const byName = (seqs) => seqs.slice().sort((a, b) => a.name.localeCompare(b.name));
const heldAfter = (droid) => byName([...droid.store.values()]);

test("a save that fails after the droid removed the old Sequence still puts it back", async () => {
  const mine = sequence("DM:MINE", [{ type: "dome", cmd: ":OP01", ms: 0 }]);
  let broken = false;
  const droid = makeDroid({
    library: [mine],
    breaks: (path, method) => {
      if (method !== "POST" || broken) return null;
      broken = true;
      return "rename";
    },
  });
  const backup = { schema: 2, board: "artoo_esp32", sequences: [sequence("DM:MINE")] };
  const { receipt } = await restoreOn(droid, backup);

  assert.deepEqual(heldAfter(droid), [mine], "the droid holds the Sequence it had, as it was");
  assert.match(receipt(), /Sequences: FAILED — the droid refused DM:MINE: rename failed\. The Sequences the droid had are put back\./);
});

test("a delete whose answer is lost still puts the Sequence back", async () => {
  const library = [sequence("DM:GONE", [{ type: "dome", cmd: ":OP02", ms: 0 }]), sequence("DM:KEEP")];
  const droid = makeDroid({
    library,
    breaks: (path, method) => (method === "DELETE" && path.includes("GONE") ? "no-reply" : null),
  });
  const backup = { schema: 2, board: "artoo_esp32", sequences: [sequence("DM:KEEP"), sequence("DM:NEW")] };
  const { receipt } = await restoreOn(droid, backup);

  assert.deepEqual(heldAfter(droid), byName(library),
    "the droid holds the Sequences it had, each as it was");
  assert.match(receipt(), /Sequences: FAILED — the droid refused DM:GONE: no answer[^\n]*The Sequences the droid had are put back\./);
});

test("the RC Map never goes out with a binding the droid would refuse, and a refused map is never called restored", async () => {
  // Six Sequences onto a droid that stores five: the sixth is left out, and
  // the RC Map's binding to it with it.
  const names = ["DM:A", "DM:B", "DM:C", "DM:D", "DM:E", "DM:SIXTH"];
  const rcMap = {
    mode: "dual_sbus",
    map: [
      { source: "sbus1", channel: 5, action: "dome_seq", payload: "DM:SIXTH" },
      { source: "sbus1", channel: 6, action: "dome_seq", payload: "DM:VADER" },
      { source: "sbus1", channel: 7, action: "dome_seq", payload: "DM:A" },
    ],
  };
  const droid = makeDroid({ cap: 5 });
  const backup = { schema: 2, board: "artoo_esp32", sequences: names.map((name) => sequence(name)), rc_map: rcMap };
  const { receipt } = await restoreOn(droid, backup);

  assert.deepEqual(droid.rcMap.map, rcMap.map.slice(1), "the map landed without the binding whose Sequence was left out");
  assert.match(receipt(), /SBUS#1 CH 5: DM:SIXTH is not on this droid/);

  const refusing = makeDroid();
  const bad = { schema: 2, board: "artoo_esp32", rc_map: { map: [{ source: "sbus1", channel: 4, action: "dome_marcduino", payload: ":SM01" }] } };
  const refused = await restoreOn(refusing, bad);
  assert.match(refused.receipt(), /RC Map: FAILED — :SM is diagnostic only/);
  assert.doesNotMatch(refused.receipt(), /RC Map: (restored|partial)/);
});

// A binding the droid that made the backup did not read ("read": false, ADR
// 0070) is one a save refuses: sent back, it would refuse the whole map.
test("a binding the backed-up droid did not read is left out of the restored RC Map, and named", async () => {
  const rcMap = {
    mode: "single_sbus",
    map: [
      { source: "sbus1", channel: 1, action: "drive_speed" },
      { source: "sbus2", channel: 1, action: "dome_speed", read: false },
    ],
  };
  const droid = makeDroid();
  const { receipt } = await restoreOn(droid, { schema: 2, board: "artoo_esp32", rc_map: rcMap });

  assert.deepEqual(droid.rcMap.map, rcMap.map.slice(0, 1));
  assert.match(receipt(), /RC Map: partial — 1 left out/);
  assert.match(receipt(), /SBUS#2 CH 1: not read by the droid it came from/);
});

// A backup from a droid of two SBUS receivers onto one of one: the droid
// refuses the SBUS2 binding by name, so the restore leaves that one out, says
// why in words, and lands the rest (ADR 0070).
test("a binding this droid refuses by name is left out of the restored RC Map, and the rest lands", async () => {
  const rcMap = {
    mode: "dual_sbus",
    map: [
      { source: "sbus1", channel: 1, action: "drive_speed" },
      { source: "sbus2", channel: 6, action: "sound_next" },
      { source: "sbus1", channel: 7, action: "sleep_toggle" },
    ],
  };
  const droid = makeDroid({ rcMap: { mode: "single_sbus", map: [] } });
  const { receipt } = await restoreOn(droid, { schema: 2, board: "artoo_esp32", rc_map: rcMap });

  assert.deepEqual(droid.rcMap.map, [rcMap.map[0], rcMap.map[2]]);
  assert.match(receipt(), /RC Map: partial — 1 left out/);
  assert.match(receipt(), /SBUS#2 CH 6: RC Receiver must be SBUS1/);
});

// A file is offered only in the shape Download backup writes. Any object with
// a schema used to pass: an RC Map alone emptied the droid's, and a schema 2
// file without its board was taken as this droid's own and wrote the whole
// Configuration, pins included (Codex review of #448 slice 1).
test("a file not in the shape Download backup writes is refused, and nothing is offered", async () => {
  const boardless = { ...OLDER_BACKUP, schema: 2 };
  for (const file of [boardless, { schema: 1, rc_map: { map: [] } }]) {
    const page = openPage(makeDroid());
    await page.env.settle();
    page.env.element("backup-file-input").files = [{ text: JSON.stringify(file) }];
    page.env.emitOn("backup-file-input", "change");
    await page.env.settle();

    assert.match(page.receipt(), /^Not a protoR2 backup: /, `${JSON.stringify(file).slice(0, 60)} was offered`);
    assert.equal(page.env.element("restore-sections").hidden, true, "no part is offered");
  }
});

// Save a copy first reads the droid for seconds before the first write, and
// the chooser stayed usable: a file chosen meanwhile became the one written,
// not the one the builder confirmed (Codex review of #448 slice 1).
test("a file chosen while the copy is read is not the one written", async () => {
  const confirmed = { schema: 2, board: "artoo_esp32", sequences: [sequence("DM:CONFIRMED")] };
  const other = { schema: 2, board: "artoo_esp32", sequences: [sequence("DM:OTHER")] };
  const droid = makeDroid({ library: [sequence("DM:MINE")] });
  let page = null;
  let identityReads = 0;
  let lockedWhileCopying = null;
  const answer = droid.respond;
  droid.respond = (path, opts) => {
    // The first identity read is the droid being asked about the chosen file;
    // the second is the copy's.
    if (path === "/api/identity" && ++identityReads === 2) {
      lockedWhileCopying = page.env.element("backup-file-input").disabled;
      page.env.element("backup-file-input").files = [{ text: JSON.stringify(other) }];
      page.env.emitOn("backup-file-input", "change");
    }
    return answer(path, opts);
  };
  page = openPage(droid);
  const { env } = page;
  await env.settle();
  env.element("backup-file-input").files = [{ text: JSON.stringify(confirmed) }];
  env.emitOn("backup-file-input", "change");
  await page.until(() => env.element("backup-restore-btn").disabled === false);
  env.emitOn("backup-restore-btn", "click");
  env.emitOn("restore-copy-btn", "click");
  await page.until(() => !["", "Restoring..."].includes(page.receipt()));

  assert.deepEqual([...droid.store.keys()], ["DM:CONFIRMED"], "the droid holds what the confirmed file holds");
  assert.equal(lockedWhileCopying, true, "the chooser is locked while the restore runs");
  assert.equal(env.element("backup-file-input").disabled, false, "and unlocked once the receipt is in");
});

test("a copy of what is about to be replaced that cannot be built replaces nothing", async () => {
  const droid = makeDroid({
    library: [sequence("DM:MINE")],
    fail: (path, method) => path === "/api/rc/map" && method === "GET",
  });
  const backup = { schema: 2, board: "artoo_esp32", sequences: [sequence("DM:OTHER")], rc_map: { map: [] } };
  const { writes, saved, receipt } = await restoreOn(droid, backup, { answer: "copy" });

  assert.deepEqual(writes(), [], "nothing was sent to the droid");
  assert.equal(saved.length, 0, "and no copy was saved");
  assert.deepEqual([...droid.store.keys()], ["DM:MINE"]);
  assert.match(receipt(), /No copy saved: the droid did not send rc_map\. Nothing was replaced\./);
});
