// =============================================================================
// test/test_web/test_rc_page.js
//
// The RC page (data/rc.js) and the names it shows for what a channel does.
//
// An action about one Output - the toggles - is named by what the running
// board prints beside that Output ("GPIO 4 Toggle" on the FireBeetle 2), and
// only the firmware knows that, so the page's built-in fallback list carries
// no Output toggle at all (ADR 0033 Amendment 2026-09-19;
// tools/check_action_registry_drift.py holds the fallback to it). The
// invariant: a binding to one is named from GET /api/actions once that answer
// arrives, whichever of the page's reads finished first. Before the fix a map
// read that landed first left the binding showing its bare token for good.
//
// The radio and receiver cards say "not known yet" until the droid has
// answered both reads they come from - the lineup and the config - and never
// "none picked" in the meantime (the #360 false-state class); nor once it has
// answered that no radio is fitted (#369).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "node:fs";

import { loadPageModule, ApiError } from "./helpers/page_module_env.js";

// The shipped Component Picker, whose lineup read has not answered: its GET
// never settles, the way a slow controller leaves it for one load cycle.
const pickerAwaitingLineup = () => {
  const context = {
    window: { PAApi: { get: () => new Promise(() => {}) } },
    document: { getElementById: () => null, createElement: () => ({ setAttribute() {}, appendChild() {} }) },
    console,
  };
  context.globalThis = context;
  for (const file of ["apply_timing.js", "component_picker.js"]) {
    vm.runInNewContext(readFileSync(new URL(`../../data/${file}`, import.meta.url), "utf8"), context, { filename: file });
  }
  return context.window.ComponentPicker;
};

// The shipped Component Picker once the droid has answered its lineup read.
const pickerWithLineup = () => {
  const context = {
    window: { PAApi: { get: async () => ({ data: { categories: [], parts: [] } }) } },
    document: { getElementById: () => null, createElement: () => ({ setAttribute() {}, appendChild() {} }) },
    console,
  };
  context.globalThis = context;
  for (const file of ["apply_timing.js", "component_picker.js"]) {
    vm.runInNewContext(readFileSync(new URL(`../../data/${file}`, import.meta.url), "utf8"), context, { filename: file });
  }
  return context.window.ComponentPicker;
};

const ACTIONS = [
  { id: 1, name: "drive.action.speed", display_name: "Speed", domain: "drive", description: "", token: "drive_speed", testable: false, one_shot: false, safety_critical: false },
  { id: 6, name: "servo.action.toggle-aux1", display_name: "GPIO 4 Toggle", domain: "servo", description: "Open or close the part on GPIO 4.", token: "aux1_toggle", testable: true, one_shot: false, safety_critical: false },
];

// GET /api/rc/map's `receivers` (ADR 0070), as the droid answers them for
// each receiver type: the page offers only what these say.
const RECEIVERS_FOR = {
  dual_sbus: { read: ["sbus1", "sbus2"], drive: ["sbus1"], cues: ["sbus1", "sbus2"] },
  single_sbus: { read: ["sbus1"], drive: ["sbus1"], cues: ["sbus1"] },
  standard_pwm: { read: ["pwm"], drive: ["pwm"], cues: [] },
};
const asDroidMap = (map) => (map && !map.receivers
  ? { ...map, receivers: RECEIVERS_FOR[map.mode] || { read: [], drive: [], cues: [] } }
  : map);

const respond = (path) => {
  if (path === "/api/config") return { data: { rc: { inputMode: "single_sbus", sbus: { recvCh2: false } }, components: {} } };
  if (path === "/api/rc/map") {
    return {
      data: asDroidMap({
        mode: "single_sbus",
        map: [
          { source: "sbus1", channel: 1, action: "drive_speed" },
          { source: "sbus1", channel: 6, action: "aux1_toggle" },
        ],
      }),
    };
  }
  if (path === "/api/actions") return { data: ACTIONS };
  return { data: {} };
};

test("a binding to an Output's toggle is named by the board once the firmware's list arrives", async () => {
  const env = loadPageModule("rc.js", { respond });
  await env.settle();

  // The map lands first: the fallback cannot name the toggle, so it cannot
  // be the last word either.
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-action-targets");
  await env.settle();

  const summary = env.element("rc-summary-body").innerHTML;
  assert.match(summary, /<td>GPIO 4 Toggle<\/td>/, "the binding is named as the board names the Output");
  assert.doesNotMatch(summary, /<td>aux1_toggle<\/td>/, "and never left as its stored token");
});

test("before the droid has answered, the radio and receiver cards never say nothing is picked", async () => {
  const env = loadPageModule("rc.js", { respond, overrides: { ComponentPicker: pickerAwaitingLineup() } });
  await env.settle();

  // The config lands (the picker adopts it); the lineup has not.
  await env.runSection("rc-mode-mapping");
  await env.settle();

  for (const id of ["rc-radio-card", "rc-receiver-card"]) {
    const said = env.element(id).innerHTML;
    assert.doesNotMatch(said, /picked yet/, `${id} must not claim nothing is picked before the droid has said`);
    assert.match(said, /<p class="hint waiting"><\/p>/, `${id} shows the waiting dots`);
  }
});

// A droid with no radio fitted has answered (GLOSSARY.md "Radio Controller",
// #369): the cards say so, never that a radio is still to be picked.
test("a droid with no radio fitted is shown as Not fitted, never as nothing picked yet", async () => {
  const notFitted = (path) => (path === "/api/config"
    ? { data: { rc: { inputMode: "not_fitted", sbus: { recvCh2: false } }, components: {} } }
    : respond(path));
  const env = loadPageModule("rc.js", { respond: notFitted, overrides: { ComponentPicker: pickerWithLineup() } });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.settle();

  for (const id of ["rc-radio-card", "rc-receiver-card"]) {
    const said = env.element(id).innerHTML;
    assert.doesNotMatch(said, /picked yet/, `${id} must not ask for a pick the builder already answered`);
    assert.match(said, /Not fitted/, `${id} says no radio is fitted`);
  }
});

// The droid's verbose RC logs follow RC on and off the screen. The page asks
// for them on arrival; inside the Operator Shell a surface is left without the
// document unloading, so an off sent only from beforeunload never went, and
// the logs stayed on for the rest of the session (#355). Leaving is driven the
// way the shell drives it: the unmount question first, then the surface named.
test("the droid's verbose RC logs are on while RC is on screen, and only then", async () => {
  const env = loadPageModule("rc.js", { respond });
  await env.settle();
  const asked = () => env.requests
    .filter((request) => request.path === "/api/rc/debug")
    .map((request) => request.opts.body?.enabled);

  assert.deepEqual(asked(), [true], "RC asks for verbose logs when it opens");

  assert.equal(env.window.PASurface.unmountHeld(null), false, "leaving RC is never held");
  env.window.PASurface.showing("home");
  await env.settle();
  assert.deepEqual(asked(), [true, false], "leaving RC turns the verbose logs off");

  env.window.PASurface.showing(null);
  await env.settle();
  assert.deepEqual(asked(), [true, false, true], "and coming back to RC turns them on again");
});

// The verbose-log toggle is not a reading. A droid that refused it must not
// keep RC saying it is showing what it read before the operator left once the
// diagnostics have answered again: the note is about the values on screen
// (#360), and the toggle put none there.
test("a refused verbose-log toggle does not keep RC saying its reading is old", async () => {
  const env = loadPageModule("rc.js", {
    respond: (path) => {
      if (path === "/api/rc/debug") throw new ApiError("the droid did not take it", { kind: "http", status: 500 });
      return respond(path);
    },
  });
  await env.settle();

  env.window.PASurface.unmountHeld(null);
  env.window.PASurface.showing("home");
  await env.settle();
  assert.equal(env.window.PASurface.isStale(null), true, "RC was left, so what it shows is from before");

  env.window.PASurface.showing(null);
  await env.settle();
  const diagnostics = env.intervals.filter((timer) => timer.ms === 1000 && !env.cleared.intervals.includes(timer.id));
  assert.ok(diagnostics.length > 0, "RC polls its diagnostics again on the way back");
  diagnostics.forEach((timer) => env.fireInterval(timer.id));
  await env.settle();

  assert.equal(
    env.window.PASurface.isStale(null),
    false,
    "the diagnostics answered, and a refused verbose-log toggle is no reason to call them old",
  );
});

// ---------------------------------------------------------------------------
// #389: the RC Channels an SBUS frame carries, and which receiver they are on.
// ---------------------------------------------------------------------------

// A droid answering every RC read. `config`, `diag` and `map` replace the
// answer of that route; anything else answers as respond() above.
const rcDroid = ({ config, diag, map } = {}) => (path) => {
  if (path === "/api/config" && config) return { data: config };
  if (path === "/api/rc" && diag) return { data: diag };
  if (path === "/api/rc/map" && map) return { data: asDroidMap(map) };
  return respond(path);
};

const sixteen = (base) => Array.from({ length: 16 }, (_, i) => base + i);

test("an SBUS receiver offers all 18 RC Channels, CH17 and CH18 as on/off", async () => {
  const env = loadPageModule("rc.js", {
    respond: rcDroid({
      config: { rc: { inputMode: "dual_sbus", activeInputMode: "dual_sbus" }, components: {} },
      map: { mode: "dual_sbus", map: [{ source: "sbus1", channel: 9, action: "sleep_toggle" }], capacity: { total: 14, used: 1 } },
      diag: {
        mode: "dual_sbus",
        sources: {},
        raw: { sbus1: sixteen(1000), sbus2: sixteen(1100) },
        digital: { arm1: { activeSource: "sbus1", bindingChannel: 17, pressed: true } },
      },
    }),
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();

  const list = env.element("rc-channel-items").innerHTML;
  for (const source of ["sbus1", "sbus2"]) {
    for (let channel = 1; channel <= 18; channel += 1) {
      assert.match(list, new RegExp(`data-chkey="${source}:${channel}"`), `${source} CH ${channel} is offered`);
    }
  }
  const item = (key) => list.slice(list.indexOf(`data-chkey="${key}"`), list.indexOf("</div>\n        </div>", list.indexOf(`data-chkey="${key}"`)));
  assert.match(item("sbus1:9"), /<span class="rc-ch-raw">1008<\/span>/, "CH9 shows its raw number");
  assert.match(item("sbus1:9"), /Sleep Toggle/, "a CH7+ binding is shown on its own item");
  assert.match(item("sbus1:17"), /<span class="rc-ch-raw">On<\/span>/, "CH17 says on, from the binding that reads it");
  assert.match(item("sbus1:18"), /<span class="rc-ch-raw">—<\/span>/, "CH18 with nothing reading it is not known, never off");
});

test("a single SBUS droid offers SBUS1 alone, even with an SBUS2 reading on hand", async () => {
  const env = loadPageModule("rc.js", {
    respond: rcDroid({
      config: { rc: { inputMode: "single_sbus", activeInputMode: "single_sbus", sbus: { recvCh2: true } }, components: {} },
      map: { mode: "single_sbus", map: [], capacity: { total: 14, used: 0 } },
      // The decoy: an sbus2 array the old list drew as a second receiver.
      diag: { mode: "single_sbus", sources: {}, raw: { sbus1: sixteen(1000), sbus2: sixteen(1100) }, digital: {} },
    }),
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();

  const list = env.element("rc-channel-items").innerHTML;
  assert.match(list, /data-chkey="sbus1:18"/, "SBUS1 is offered, all 18");
  assert.doesNotMatch(list, /data-chkey="sbus2:/, "SBUS2 reads nothing on a single SBUS droid");
});

// The RC Map is kept for the saved receiver type; the droid runs the one it
// started with until a restart. The page maps for the saved one and, while the
// two differ, says so in Configuration's words (data/apply_timing.js).
test("a saved receiver type the droid does not run yet is said, and one it runs is not", async () => {
  const load = async (active) => {
    const env = loadPageModule("rc.js", {
      chain: ["apply_timing.js"],
      respond: rcDroid({
        config: { rc: { inputMode: "single_sbus", activeInputMode: active, sbus: { recvCh2: false } }, components: {} },
        map: { mode: "single_sbus", map: [], capacity: { total: 14, used: 0 } },
        diag: { mode: active, sources: {}, raw: { sbus1: sixteen(1000), sbus2: sixteen(1100) }, digital: {} },
      }),
    });
    await env.settle();
    await env.runSection("rc-mode-mapping");
    await env.runSection("rc-diagnostics");
    await env.settle();
    return env;
  };

  const waiting = await load("dual_sbus");
  assert.equal(waiting.element("rc-mode-summary").textContent, "Single SBUS saved · Dual SBUS running");
  assert.match(waiting.element("rc-mode-waiting").textContent, /^Saved\. The droid runs the old setting until you restart it\./);
  assert.doesNotMatch(waiting.element("rc-channel-items").innerHTML, /data-chkey="sbus2:/,
    "the page maps for the saved type, not the running one");

  const caughtUp = await load("single_sbus");
  assert.equal(caughtUp.element("rc-mode-summary").textContent, "Single SBUS");
  assert.equal(caughtUp.element("rc-mode-waiting").textContent, "", "nothing waits when the droid runs what is saved");
});

// GLOSSARY.md "RC Map": 11 trigger bindings. GET /api/rc/map's capacity also
// counts the three axes, which have places of their own (src/web/api_config.cpp).
test("the RC Map's trigger count is shown against 11, the axes left out", async () => {
  const map = {
    mode: "dual_sbus",
    map: [
      { source: "sbus1", channel: 1, action: "drive_speed" },
      { source: "sbus1", channel: 2, action: "drive_steer" },
      { source: "sbus1", channel: 7, action: "sleep_toggle" },
      { source: "sbus1", channel: 17, action: "sound_next" },
      { source: "rest", channel: 1, action: "sound_rand_happy", threshold: 20, quietS: 5 },
    ],
    capacity: { total: 14, used: 5 },
  };
  const env = loadPageModule("rc.js", { respond: rcDroid({ map }) });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.settle();
  assert.equal(env.element("rc-capacity").textContent, "3 of 11 used");

  // A droid that gives no total gets no count, never "of NaN".
  const silent = loadPageModule("rc.js", { respond: rcDroid({ map: { ...map, capacity: undefined } }) });
  await silent.settle();
  await silent.runSection("rc-mode-mapping");
  await silent.settle();
  assert.equal(silent.element("rc-capacity").textContent, "");
});

// POST /api/rc/map answers {"ok":true} and nothing else, so what the droid
// stored is read back and drawn - not the page's own copy of what it sent.
test("after a save the page draws the map the droid read back", async () => {
  let stored = { mode: "dual_sbus", map: [{ source: "sbus1", channel: 7, action: "sleep_toggle" }], capacity: { total: 14, used: 1 } };
  const env = loadPageModule("rc.js", {
    respond: (path, opts) => {
      if (path === "/api/rc/map" && opts.method === "POST") {
        // The droid keeps its own idea of the map: a binding the page never sent.
        stored = { ...stored, map: [{ source: "sbus2", channel: 12, action: "sound_next" }] };
        return { data: { ok: true } };
      }
      if (path === "/api/rc/map") return { data: asDroidMap(stored) };
      return respond(path);
    },
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.settle();
  env.window.PAOverlay.ask = async () => true;

  await env.emitOn("rc-reset-defaults", "click");
  await env.settle(6);

  const reads = env.requests.filter((request) => request.path === "/api/rc/map").map((request) => request.method);
  assert.deepEqual(reads, ["GET", "POST", "GET"], "the map is read back after the save");
  const summary = env.element("rc-summary-body").innerHTML;
  assert.match(summary, /SBUS#2 CH 12/, "the table shows what the droid stored");
  assert.match(env.element("rc-editor-feedback").textContent, /^Cleared all mappings\.$/);
});

test("each SBUS receiver's frame rate is shown beside its link, and only where the droid sends one", async () => {
  const env = loadPageModule("rc.js", {
    respond: rcDroid({
      diag: {
        mode: "dual_sbus",
        sources: {
          sbus1: { enabled: true, linked: true, ageMs: 9, framesPerSecond: 71, decodeFails: 3 },
          sbus2: { enabled: true, linked: false, ageMs: 900 },
          pwm: { enabled: false },
        },
        raw: {},
        digital: {},
      },
    }),
  });
  await env.settle();
  await env.runSection("rc-diagnostics");
  await env.settle();
  const html = env.element("rc-preview-source-health").innerHTML;
  assert.match(html, /<span>SBUS1<\/span>\s*<span class="indicator-text">linked · 71 frames\/s · 9ms old<\/span>/);
  assert.match(html, /<span>SBUS2<\/span>\s*<span class="indicator-text">waiting · 900ms old<\/span>/, "no rate sent, none made up");
});

// ---------------------------------------------------------------------------
// #389: the three axes - the boot hold, a stick resting past an end, and the
// ends and direction set from the stick (POST /api/rc/map `calibration`).
// ---------------------------------------------------------------------------

const AXES_MAP = {
  mode: "dual_sbus",
  map: [
    { source: "sbus1", channel: 1, action: "drive_speed" },
    { source: "sbus1", channel: 2, action: "drive_steer" },
    { source: "sbus1", channel: 7, action: "sleep_toggle" },
  ],
  capacity: { total: 14, used: 3 },
};
const axesDiag = (overrides = {}) => ({
  mode: "dual_sbus",
  sources: {
    sbus1: { enabled: true, linked: true, ageMs: 12, lostFrames: 0, failsafe: false },
    sbus2: { enabled: true, linked: true, ageMs: 12, lostFrames: 0, failsafe: false },
  },
  driveAwaitingCentre: false,
  // CH1 rests at 150, below its MIN of 172: a HotRC trigger at its end.
  raw: { sbus1: [150, 1700, ...sixteen(1000).slice(2)] },
  digital: {},
  mappingProfile: {
    version: 1,
    channels: {
      driveSpeed: { source: "sbus1", channel: 1, min: 172, center: 992, max: 1811, deadband: 0, reverse: false },
      driveSteer: { source: "sbus1", channel: 2, min: 172, center: 992, max: 1811, deadband: 0, reverse: true },
      domeSpeed: { source: "none", channel: 0, min: 0, center: 0, max: 0, deadband: 0, reverse: false },
    },
  },
  ...overrides,
});

const loadAxes = async ({ diag = axesDiag(), onPost = () => ({ ok: true }), map = AXES_MAP } = {}) => {
  const env = loadPageModule("rc.js", {
    respond: (path, opts) => {
      if (path === "/api/rc/map" && opts.method === "POST") return { data: onPost(JSON.parse(opts.body.plain)) };
      if (path === "/api/rc/map") return { data: asDroidMap(map) };
      if (path === "/api/rc") return { data: diag };
      return respond(path);
    },
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();
  return env;
};

// A click on a tile's control, as the one delegated listener on #rc-axes hears it.
const clickAxis = async (env, dataset, attribute) => {
  await env.emitOn("rc-axes", "click", {
    target: { closest: (selector) => (selector === `[${attribute}]` ? { disabled: false, dataset } : null) },
  });
  await env.settle(6);
};

const tileOf = (html, token) => {
  const start = html.indexOf(`data-axis="${token}"`);
  const next = html.indexOf('<div class="rc-axis"', start + 1);
  return html.slice(start, next < 0 ? undefined : next);
};

test("the drive cards say the boot hold while the droid holds drive, and not after", async () => {
  const held = await loadAxes({ diag: axesDiag({ driveAwaitingCentre: true }) });
  assert.equal(held.element("rc-drive-hold").textContent, "Center both drive sticks to drive.");
  const free = await loadAxes({ diag: axesDiag({ driveAwaitingCentre: false }) });
  assert.equal(free.element("rc-drive-hold").textContent, "");
});

test("a stick read past an end says so on its own axis, and one inside its ends says nothing", async () => {
  const env = await loadAxes();
  const html = env.element("rc-axes").innerHTML;
  assert.match(tileOf(html, "drive_speed"), /<p class="rc-axis-warn" role="status">Past MIN, so it reads as full travel\.<\/p>/);
  assert.match(tileOf(html, "drive_steer"), /<p class="rc-axis-warn" role="status"><\/p>/, "1700 sits inside 172..1811");
  assert.match(tileOf(html, "drive_speed"), /MIN 172 · CENTER 992 · MAX 1811/, "the ends the droid holds are shown");
  assert.match(tileOf(html, "dome_speed"), /Not mapped\./);
  assert.equal(env.element("rc-axes-summary").textContent, "2 of 3 mapped");
});

test("an end set from the stick posts the map unchanged and that axis's end, then reads the ends back", async () => {
  const posted = [];
  const env = await loadAxes({ onPost: (body) => { posted.push(body); return { ok: true }; } });
  await clickAxis(env, { axis: "drive_steer", axisSet: "max" }, "data-axis-set");

  assert.equal(posted.length, 1);
  assert.deepEqual(posted[0].calibration, { drive_steer: { max: 1700 } }, "the live reading of CH2 becomes MAX");
  assert.deepEqual(posted[0].map.map((entry) => `${entry.source}:${entry.channel}:${entry.action}`),
    ["sbus1:1:drive_speed", "sbus1:2:drive_steer", "sbus1:7:sleep_toggle"], "the map goes back as the droid holds it");
  const after = env.requests.map((request) => `${request.method} ${request.path}`);
  assert.equal(after.at(-1), "GET /api/rc", "the ends are read back from the droid");
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_steer"), /Saved MAX 1700\./);
});

// A binding the droid says it does not read (a save would refuse it, ADR 0070)
// is left out of the map the page posts, or the droid refuses the whole map
// over it - a single SBUS droid still holding the factory dome on SBUS2 could
// otherwise never save (#486 review).
test("a binding the droid does not read is left out of what the page posts, and said", async () => {
  const posted = [];
  const map = {
    ...AXES_MAP,
    map: [...AXES_MAP.map, {
      source: "sbus2", channel: 1, action: "dome_speed", read: false, field: "map.source", reason: "out-of-range", accepts: "sbus1",
    }],
  };
  const env = await loadAxes({ map, onPost: (body) => { posted.push(body); return { ok: true }; } });
  assert.match(env.element("rc-editor-feedback").textContent, /^Not read by the droid: SBUS#2 CH 1 \(RC Receiver must be SBUS1\)\. Apply drops it\.$/);
  await clickAxis(env, { axis: "drive_steer", axisSet: "max" }, "data-axis-set");

  assert.equal(posted.length, 1);
  assert.deepEqual(posted[0].map.map((entry) => `${entry.source}:${entry.channel}:${entry.action}`),
    ["sbus1:1:drive_speed", "sbus1:2:drive_steer", "sbus1:7:sleep_toggle"]);
});

// A cached or failsafe number is not where the stick is: with the receiver
// unheard or in failsafe there is nothing to set an end from (#483 review).
test("an end is not set from a receiver that is unheard or in failsafe", async () => {
  for (const sbus1 of [
    { enabled: true, linked: false, ageMs: 900, lostFrames: 0, failsafe: false },
    { enabled: true, linked: true, ageMs: 12, lostFrames: 0, failsafe: true },
  ]) {
    const posted = [];
    const diag = axesDiag();
    diag.sources.sbus1 = sbus1;
    const env = await loadAxes({ diag, onPost: (body) => { posted.push(body); return { ok: true }; } });
    const tile = tileOf(env.element("rc-axes").innerHTML, "drive_steer");
    assert.match(tile, /<span class="rc-axis-raw cal-readout">—<\/span>/, "no live reading is shown");
    assert.match(tile, /data-axis-set="max" disabled/, "Set is not offered");
    await clickAxis(env, { axis: "drive_steer", axisSet: "max" }, "data-axis-set");
    assert.equal(posted.length, 0, "nothing is sent");
  }
});

// A drive stored on SBUS2 before #483 does not move the droid: GET
// /api/rc/map marks it not read, with the refusal a save would give it, and
// its tile says so in words instead of offering ends to set (ADR 0070).
test("a drive axis stored on SBUS2 says it is not read", async () => {
  const unread = { read: false, field: "map.source", reason: "out-of-range", accepts: "sbus1" };
  const env = loadPageModule("rc.js", {
    respond: (path, opts) => {
      if (path === "/api/rc/map" && opts.method !== "POST") {
        return { data: asDroidMap({ ...AXES_MAP, map: [
          { source: "sbus2", channel: 1, action: "drive_speed", ...unread },
          { source: "sbus2", channel: 2, action: "drive_steer", ...unread },
        ] }) };
      }
      if (path === "/api/rc") return { data: axesDiag() };
      return respond(path);
    },
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();
  const tile = tileOf(env.element("rc-axes").innerHTML, "drive_speed");
  assert.match(tile, /Not read: RC Receiver must be SBUS1\. Map it again\./);
  assert.doesNotMatch(tile, /data-axis-set/, "no ends are offered");
});

// An axis unread for its ends alone (a dead zone that swallows a side, stored
// before the rule) offers a reset: the map goes back with the axis in it and
// without its ends, the droid starts it from the defaults (ADR 0070), and the
// tile is drawn as one the droid reads.
test("an axis unread for its ends is mended by a reset", async () => {
  const unread = { read: false, field: "calibration.deadband", reason: "conflict" };
  const posted = [];
  let mended = false;
  const env = loadPageModule("rc.js", {
    respond: (path, opts) => {
      if (path === "/api/rc/map" && opts.method === "POST") {
        posted.push(JSON.parse(opts.body.plain));
        mended = true;
        return { data: { ok: true } };
      }
      if (path === "/api/rc/map") {
        return { data: asDroidMap({ ...AXES_MAP, map: [
          { source: "sbus1", channel: 1, action: "drive_speed", ...(mended ? {} : unread) },
          { source: "sbus1", channel: 2, action: "drive_steer" },
        ] }) };
      }
      if (path === "/api/rc") return { data: axesDiag() };
      return respond(path);
    },
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();
  let tile = tileOf(env.element("rc-axes").innerHTML, "drive_speed");
  assert.match(tile, /Not read: Dead zone is wider than CENTER sits from an end\./);
  assert.doesNotMatch(tile, /Map it again/);
  assert.match(tile, /data-axis-reset>Reset ends<\/button>/);

  await clickAxis(env, { axis: "drive_speed" }, "data-axis-reset");
  assert.equal(posted.length, 1);
  assert.equal(posted[0].calibration, undefined, "no ends are posted: the droid takes its defaults");
  assert.deepEqual(posted[0].map.find((entry) => entry.action === "drive_speed"),
    { source: "sbus1", channel: 1, action: "drive_speed" });
  tile = tileOf(env.element("rc-axes").innerHTML, "drive_speed");
  assert.match(tile, /data-axis-set="min"/, "read again, its ends are offered");
  assert.match(tile, /Saved the default ends\./);
});

// A Set posts the whole map beside its calibration: pressed while a map save is
// still going, it would post the map that save replaces and undo it. It waits
// for the save and its read-back (#483 review).
test("an end is not set while a map save is still going", async () => {
  let release;
  const posted = [];
  const env = loadPageModule("rc.js", {
    respond: (path, opts) => {
      if (path === "/api/rc/map" && opts.method === "POST") {
        const body = JSON.parse(opts.body.plain);
        posted.push(body);
        if (!body.calibration) return new Promise((resolve) => { release = () => resolve({ data: { ok: true } }); });
        return { data: { ok: true } };
      }
      if (path === "/api/rc/map") return { data: asDroidMap(AXES_MAP) };
      if (path === "/api/rc") return { data: axesDiag() };
      return respond(path);
    },
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();
  env.window.PAOverlay.ask = async () => true;

  env.emitOn("rc-reset-defaults", "click");
  await env.settle(4);
  assert.equal(posted.length, 1, "the clear is in flight");
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_steer"), /data-axis-set="max" disabled/);
  await clickAxis(env, { axis: "drive_steer", axisSet: "max" }, "data-axis-set");
  assert.equal(posted.length, 1, "no calibration is posted while the clear is in flight");

  release();
  await env.settle(6);
  await clickAxis(env, { axis: "drive_steer", axisSet: "max" }, "data-axis-set");
  assert.equal(posted.filter((body) => body.calibration).length, 1, "after the read-back a Set goes");
});

test("the reverse switch posts the axis's other direction", async () => {
  const posted = [];
  const env = await loadAxes({ onPost: (body) => { posted.push(body); return { ok: true }; } });
  await clickAxis(env, { axis: "drive_steer" }, "data-axis-reverse");
  assert.deepEqual(posted[0]?.calibration, { drive_steer: { reverse: false } }, "steer was reversed, so it goes back");
});

// The droid rules on the order of the ends, and words its refusal: the page
// keeps no copy of the rule (ADR 0068, ADR 0070).
test("a capture out of order goes to the droid, and its refusal is shown on the axis in words", async () => {
  const posted = [];
  const env = await loadAxes({
    onPost: (body) => {
      posted.push(body);
      throw body.calibration.drive_speed.max !== undefined
        ? new ApiError("calibration needs min < center < max", {
          kind: "http", status: 400, field: "calibration.center", reason: "conflict",
        })
        : new ApiError("calibration out of range", {
          kind: "http", status: 400, field: "calibration.min", reason: "out-of-range", accepts: "0..2047",
        });
    },
  });
  // CH1 reads 150, below CENTER 992: as MAX the droid refuses it.
  await clickAxis(env, { axis: "drive_speed", axisSet: "max" }, "data-axis-set");
  assert.equal(posted.length, 1, "the droid is asked, and rules");
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_speed"), /Not saved: CENTER must sit between MIN and MAX\./);

  await clickAxis(env, { axis: "drive_speed", axisSet: "min" }, "data-axis-set");
  assert.equal(posted.length, 2);
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_speed"), /Not saved: MIN must be 0 to 2047\./);
});

// What the page offers on a radio RC Channel comes from the droid's own
// answers (ADR 0070): GET /api/rc/map's receivers and GET /api/actions'
// rc_input. The rule is the marked pure block in data/rc.js, run here as
// shipped (the editor's picker is drawn with innerHTML, which this harness
// cannot click into).
const loadOfferedOnRadio = () => {
  const source = readFileSync(new URL("../../data/rc.js", import.meta.url), "utf8");
  const begin = source.indexOf("// ==== WHAT THE DROID OFFERS (#486) BEGIN ====");
  const end = source.indexOf("// ==== WHAT THE DROID OFFERS (#486) END ====");
  assert.ok(begin >= 0 && end > begin, "data/rc.js carries the WHAT THE DROID OFFERS block");
  const context = { module: { exports: null } };
  vm.runInNewContext(`${source.slice(begin, end)}\nmodule.exports = offeredOnRadio;`, context);
  return context.module.exports;
};

test("a drive axis is offered only on a receiver the drive may use, the dome on any it reads", () => {
  const offeredOnRadio = loadOfferedOnRadio();
  const dual = RECEIVERS_FOR.dual_sbus;
  const speed = { token: "drive_speed", rcInput: "stick" };
  const dome = { token: "dome_speed", rcInput: "stick" };
  assert.equal(offeredOnRadio(speed, "sbus2", false, dual), false, "the drive reads SBUS1");
  assert.equal(offeredOnRadio(speed, "sbus1", false, dual), true);
  assert.equal(offeredOnRadio(dome, "sbus2", false, dual), true, "the dome may read SBUS2");
  assert.equal(offeredOnRadio(speed, "sbus1", true, dual), false, "a stick never sits on CH17/CH18");
  assert.equal(offeredOnRadio(speed, "sbus1", false, null), false, "nothing before the droid has answered");
});

test("a cue is offered only on a receiver that carries one: none on PWM", () => {
  const offeredOnRadio = loadOfferedOnRadio();
  const sleep = { token: "sleep_toggle", rcInput: "switch" };
  const puppet = { token: "puppet_part", rcInput: "stick" };
  assert.equal(offeredOnRadio(sleep, "pwm", false, RECEIVERS_FOR.standard_pwm), false);
  assert.equal(offeredOnRadio(puppet, "pwm", false, RECEIVERS_FOR.standard_pwm), false);
  assert.equal(offeredOnRadio({ token: "drive_speed", rcInput: "stick" }, "pwm", false, RECEIVERS_FOR.standard_pwm), true);
  assert.equal(offeredOnRadio(sleep, "sbus1", true, RECEIVERS_FOR.dual_sbus), true, "a switch may sit on CH17");
  assert.equal(offeredOnRadio(puppet, "sbus2", true, RECEIVERS_FOR.dual_sbus), false, "a puppet string needs a stick");
});

test("the droid's refusal of a drive on SBUS2 is shown in the builder's words", async () => {
  const env = await loadAxes({
    onPost: () => {
      throw new ApiError("drive reads SBUS1, the drive receiver", {
        kind: "http", status: 400, field: "map.source", reason: "out-of-range", accepts: "sbus1",
      });
    },
  });
  await clickAxis(env, { axis: "drive_steer", axisSet: "max" }, "data-axis-set");
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_steer"), /Not saved: RC Receiver must be SBUS1\./);
});

test("the droid's refusal of a split drive is shown in the builder's words", async () => {
  const env = await loadAxes({
    onPost: () => {
      throw new ApiError("drive speed and steer must be on the same receiver", {
        kind: "http", status: 400, field: "map.source", reason: "conflict",
      });
    },
  });
  await clickAxis(env, { axis: "drive_steer", axisSet: "max" }, "data-axis-set");
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_steer"), /Not saved: RC Receiver must be the one Speed reads\./);
});

// A CENTER that leaves one side shorter than the axis's dead zone would move
// nothing on that side; POST /api/rc/map refuses it ("calibration leaves no
// travel past the deadband") and the page words the refusal from its field.
// The page keeps no copy of the rule (ADR 0068, ADR 0070).
test("a CENTER too close to an end goes to the droid, and its refusal reads in words", async () => {
  const diag = axesDiag();
  diag.mappingProfile.channels.driveSteer.deadband = 200;
  const posted = [];
  const env = await loadAxes({
    diag,
    onPost: (body) => {
      posted.push(body);
      throw new ApiError("calibration leaves no travel past the deadband", {
        kind: "http", status: 400, field: "calibration.deadband", reason: "conflict",
      });
    },
  });
  // CH2 reads 1700: as CENTER it leaves 111 above it, inside a dead zone of 200.
  await clickAxis(env, { axis: "drive_steer", axisSet: "center" }, "data-axis-set");
  assert.equal(posted.length, 1, "the droid is asked, and rules");
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_steer"), /Not saved: Dead zone is wider than CENTER sits from an end\./);
});

// GET /api/rc rawDigital: [CH17, CH18] per receiver, whatever binds them.
// The decoy is `digital`, which says the opposite: rawDigital is the one read.
test("CH17 and CH18 read from rawDigital, in the grid and in the Live column", async () => {
  const env = loadPageModule("rc.js", {
    respond: rcDroid({
      config: { rc: { inputMode: "dual_sbus", activeInputMode: "dual_sbus" }, components: {} },
      map: { mode: "dual_sbus", map: [{ source: "sbus1", channel: 18, action: "op_mode" }], capacity: { total: 14, used: 1 } },
      diag: {
        mode: "dual_sbus",
        sources: {},
        raw: { sbus1: sixteen(1000) },
        rawDigital: { sbus1: [false, true] },
        digital: { arm1: { activeSource: "sbus1", bindingChannel: 17, pressed: true } },
      },
    }),
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();

  const list = env.element("rc-channel-items").innerHTML;
  const raw = (key) => {
    const at = list.indexOf(`data-chkey="${key}"`);
    return list.slice(at, list.indexOf("</span>", list.indexOf('class="rc-ch-raw"', at)) + 7);
  };
  assert.match(raw("sbus1:17"), /<span class="rc-ch-raw">Off<\/span>/, "rawDigital says off, whatever digital says");
  assert.match(raw("sbus1:18"), /<span class="rc-ch-raw">On<\/span>/, "CH18 bound in no named slot still reads");
  assert.match(env.element("rc-summary-body").innerHTML, /SBUS#1 CH 18<\/td>\s*<td><span class="rc-trigger-state"><span class="indicator ok"[^>]*><\/span>Pressed/,
    "the Live column reads the switch on");
});

// A bound switch reads pressed when the droid says so (GET /api/rc
// `pressed`, by that binding's own ends and dead zone, ADR 0070), never by a
// distance from centre the page picks. The decoys: a stick far from centre the
// droid reads released, and one near it the droid reads pressed.
test("the Live column says pressed when the droid reads the switch pressed", async () => {
  const load = async (raw5, pressed) => {
    const env = loadPageModule("rc.js", {
      respond: rcDroid({
        config: { rc: { inputMode: "dual_sbus", activeInputMode: "dual_sbus" }, components: {} },
        map: { mode: "dual_sbus", map: [{ source: "sbus1", channel: 5, action: "op_mode" }], capacity: { total: 14, used: 1 } },
        diag: {
          mode: "dual_sbus",
          sources: {},
          raw: { sbus1: Object.assign(sixteen(1000), { 4: raw5 }) },
          rawDigital: { sbus1: [false, false] },
          digital: {},
          pressed: pressed === undefined ? {} : { "sbus1:5": pressed },
        },
      }),
    });
    await env.settle();
    await env.runSection("rc-mode-mapping");
    await env.runSection("rc-diagnostics");
    await env.settle();
    return env.element("rc-summary-body").innerHTML;
  };
  const row = /SBUS#1 CH 5<\/td>\s*<td><span class="rc-trigger-state"><span class="indicator( ok)?"[^>]*><\/span>(Pressed|Released)/;
  assert.equal(row.exec(await load(1700, false))?.[2], "Released", "far from centre, but the droid reads it released");
  assert.equal(row.exec(await load(1000, true))?.[2], "Pressed", "near centre, but the droid reads it pressed");
  // Nothing said - a lost receiver, older firmware - is not known, never released.
  assert.match(await load(1700, undefined), /SBUS#1 CH 5<\/td>\s*<td><span class="rc-trigger-state"><span class="indicator"[^>]*><\/span>—/);
});

test("Detect lands on CH17 when that switch flips", async () => {
  const reading = (ch17) => ({
    mode: "dual_sbus",
    sources: { sbus1: { enabled: true, linked: true, ageMs: 5 } },
    raw: { sbus1: sixteen(1000) },
    rawDigital: { sbus1: [ch17, false] },
    digital: {},
  });
  let diag = reading(false);
  const env = loadPageModule("rc.js", {
    respond: (path) => (path === "/api/rc" ? { data: diag } : rcDroid({
      config: { rc: { inputMode: "dual_sbus", activeInputMode: "dual_sbus" }, components: {} },
      map: { mode: "dual_sbus", map: [], capacity: { total: 14, used: 0 } },
    })(path)),
  });
  await env.settle();
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-diagnostics");
  await env.settle();

  await env.emitOn("rc-learn-btn", "click");
  assert.match(env.element("rc-learn-status").textContent, /^Listening\./, "nothing has moved yet");

  diag = reading(true);
  const poll = env.intervals.filter((timer) => timer.ms === 1000 && !env.cleared.intervals.includes(timer.id));
  assert.ok(poll.length > 0, "the page polls its readings without a stream");
  poll.forEach((timer) => env.fireInterval(timer.id));
  await env.settle();
  assert.equal(env.element("rc-learn-status").textContent, "Detected SBUS#1 CH 17: not mapped yet.");
});
