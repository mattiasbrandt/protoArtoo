// =============================================================================
// test/test_web/test_apply_timing.js
//
// When an answer takes effect (#370, data/apply_timing.js): each guided Setup
// step and each Configuration row says so beside its question, from one
// structured field. Configuration as the browser runs it
// (helpers/configuration_surface.js).
//
// Five invariants earn their place, each a rule the slice exists to hold:
//   - a step that does not state its timing is not drawn. A default here is
//     the blanket "applies straight away" promise this field replaced, which
//     was false for at least three steps;
//   - putting a staged change back clears what the step says is waiting - the
//     restart warning's own shipped rule (test_staged_rc_settings.js, WARNING
//     #3), now carried by the line beside the question;
//   - amber means the builder must restart. A change that only waits for the
//     next start is never amber (#327's Status Color rule);
//   - a Radio Controller member pick changes nothing on the controller, so its
//     card never reads as waiting on a restart, while a Sound member does;
//   - an answer the droid uses at once says nothing about when: only one that
//     waits for a start or a restart carries a line (operator, 2026-09-19 on
//     #412);
//   - what is waiting is read against what the droid says it started with,
//     never against the page's own first read, so a page opened between a
//     save and a restart still says so - the defect #371 found.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import fs from "fs";
import { syncBuiltinESMExports } from "module";

import { configured, ready } from "./helpers/configuration_surface.js";

const timingLine = (env, step) =>
  env.parsed.querySelector(`[data-setup-step="${step}"]`).querySelector("[data-apply-timing]");

const variant = (env, mode) =>
  env.host("radio_controller").querySelector(`[data-variant="${mode}"]`);

test("a guided step that does not say when it takes effect is not drawn", async () => {
  // The fault is injected into the shipped setup.js as it is read, so the
  // guard under test is the production one: Foot Drive loses its timing.
  const read = fs.readFileSync;
  const errors = [];
  const consoleError = console.error;
  fs.readFileSync = (path, ...rest) => {
    const text = read(path, ...rest);
    if (!String(path).endsWith("setup.js")) return text;
    const stripped = text.replace(
      /(key: "drive",[\s\S]*?)applies: TIMING\.AT_REBOOT,\n/,
      "$1",
    );
    assert.notEqual(stripped, text, "the fault must actually land in setup.js");
    return stripped;
  };
  syncBuiltinESMExports();
  console.error = (...args) => errors.push(args.join(" "));
  let env;
  try {
    env = await ready();
  } finally {
    fs.readFileSync = read;
    syncBuiltinESMExports();
    console.error = consoleError;
  }

  const rail = env.parsed.getElementById("wizard-rail");
  assert.equal(rail.querySelector('[data-step="drive"]'), null, "the step is not on the rail");
  assert.ok(rail.querySelector('[data-step="sound"]'), "and the steps that state it still are");
  assert.equal(
    timingLine(env, "drive").classList.contains("hidden"),
    true,
    "nothing is said beside the question on its behalf",
  );
});

test("putting a staged receiver change back clears the restart the step asked for", async () => {
  const env = await ready();
  const line = timingLine(env, "rc");
  assert.equal(line.dataset.pending, "false", "nothing is waiting on a fresh page");

  variant(env, "single_sbus").fire("click", {});
  await env.settle();
  assert.equal(line.dataset.pending, "true", "a receiver change waits for a restart");
  assert.ok(line.classList.contains("note-act"), "which the builder must perform: amber");

  variant(env, "dual_sbus").fire("click", {});
  await env.settle();
  assert.equal(line.dataset.pending, "false", "put back, nothing is waiting");
  assert.equal(line.classList.contains("note-act"), false, "and the amber is gone");
});

test("a change that only waits for the next start is never amber", async () => {
  const env = await ready();
  const line = timingLine(env, "sound");

  env.press(env.plate("sound", "mp3_trigger")).fire("click", {});
  await env.settle();

  assert.equal(line.dataset.pending, "true", "the droid still runs the module it started with");
  assert.equal(line.classList.contains("note-act"), false, "nothing for the builder to do: not amber");
  assert.equal(line.querySelector("a"), null, "and no route to a restart");
});

test("a Sound member waits for the next start; a Radio Controller member never waits", async () => {
  const env = await ready();

  env.press(env.plate("sound", "mp3_trigger")).fire("click", {});
  await env.settle();
  assert.equal(env.plate("sound", "mp3_trigger").dataset.state, "chosen-waiting");

  env.press(env.plate("radio_controller", "rc_radio")).fire("click", {});
  await env.settle();
  assert.equal(
    env.plate("radio_controller", "rc_radio").dataset.state,
    "chosen",
    "the radio drives nothing on the controller, so a pick of it is never pending",
  );
});

test("an answer the droid uses at once carries no timing line at all", async () => {
  const env = await ready();
  const line = timingLine(env, "name");

  assert.equal(line.textContent, "", "an immediate answer says nothing about when");
  assert.equal(line.classList.contains("hidden"), true, "and draws no empty note");
  assert.notEqual(timingLine(env, "sound").textContent, "", "while one that waits for a start still says so");
});

test("a page opened between a save and a restart still says the change is waiting", async () => {
  // Saved: Foot Drive fitted and two SBUS receivers. Started with: neither.
  const config = configured();
  config.activeToggles = Object.keys(config.components).filter(
    (id) => id !== "drive" && config.components[id].enabled === true,
  );
  config.rc.activeInputMode = "standard_pwm";
  const env = await ready({ config });

  assert.equal(timingLine(env, "drive").dataset.pending, "true", "the droid still runs without its feet");
  const rc = timingLine(env, "rc");
  assert.equal(rc.dataset.pending, "true", "and still reads the receiver it started with");
  assert.ok(rc.classList.contains("note-act"), "which the builder must restart for: amber");
});
