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
// "none picked" in the meantime (the #360 false-state class).
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
  for (const file of ["apply_timing.js", "product_art.js", "component_picker.js"]) {
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
    assert.match(said, /Reading it from the droid/);
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
