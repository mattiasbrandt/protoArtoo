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

const respond = (path) => {
  if (path === "/api/config") return { data: { rc: { inputMode: "single_sbus", sbus: { recvCh2: false } }, components: {} } };
  if (path === "/api/rc/map") {
    return {
      data: {
        mode: "single_sbus",
        map: [
          { source: "sbus1", channel: 1, action: "drive_speed" },
          { source: "sbus1", channel: 6, action: "aux1_toggle" },
        ],
      },
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
  if (path === "/api/rc/map" && map) return { data: map };
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
      if (path === "/api/rc/map") return { data: stored };
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
  sources: {},
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

const loadAxes = async ({ diag = axesDiag(), onPost = () => ({ ok: true }) } = {}) => {
  const env = loadPageModule("rc.js", {
    respond: (path, opts) => {
      if (path === "/api/rc/map" && opts.method === "POST") return { data: onPost(JSON.parse(opts.body.plain)) };
      if (path === "/api/rc/map") return { data: AXES_MAP };
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

test("the reverse switch posts the axis's other direction", async () => {
  const posted = [];
  const env = await loadAxes({ onPost: (body) => { posted.push(body); return { ok: true }; } });
  await clickAxis(env, { axis: "drive_steer" }, "data-axis-reverse");
  assert.deepEqual(posted[0]?.calibration, { drive_steer: { reverse: false } }, "steer was reversed, so it goes back");
});

test("a capture out of order is not sent, and a refusal from the droid is shown on the axis", async () => {
  const posted = [];
  const env = await loadAxes({
    onPost: (body) => {
      posted.push(body);
      throw new ApiError("calibration out of range", { kind: "http", status: 400 });
    },
  });
  // CH1 reads 150, below CENTER 992: it cannot be MAX.
  await clickAxis(env, { axis: "drive_speed", axisSet: "max" }, "data-axis-set");
  assert.equal(posted.length, 0, "the droid is never asked to store MAX below CENTER");
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_speed"), /Not saved: MAX must read above CENTER\./);

  await clickAxis(env, { axis: "drive_speed", axisSet: "min" }, "data-axis-set");
  assert.equal(posted.length, 1);
  assert.match(tileOf(env.element("rc-axes").innerHTML, "drive_speed"), /Not saved: that reading is out of range\./);
});
