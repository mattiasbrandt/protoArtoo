// =============================================================================
// test/test_web/test_output_settings.js
//
// The Outputs' plates (#369, data/output_settings.js): which are in use and
// what each carries - a servo, or a light - set on Wiring; which servo model,
// set on Servos. One answer drawn on two surfaces, run here as the browser runs
// it - data/outputs.js holding the droid's answer (#415), the plates file
// drawing both views of it - on a real node tree, against the one fake droid
// (helpers/fake_droid.js).
//
// Three invariants earn their place:
//   - one answer, two views: what Wiring changes, Servos shows, and the save
//     carries the field the firmware named for what changed and nothing a
//     builder did not touch, so a save from one surface cannot reset what the
//     other set;
//   - each wire is its own answer (#413, ADR 0067): a droid may have several
//     lit Parts, so giving one Output a Light Type must NOT take it off
//     another, and the save must carry no droid-wide light field at all. This
//     test used to assert the opposite, because the firmware could light one
//     wire; the exclusion it asserted is now the defect;
//   - when each view's answer bites (#370): an in-use tick is read once at
//     start, so a changed one says it is waiting and one put back does not;
//     a servo type bounds the next move, so Servos never says it is waiting.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { MiniDocument } from "./helpers/mini_dom.js";
import { servoRow, describe, applyRowSave } from "./helpers/fake_droid.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");

// An Artoo PCB: two Outputs that carry only a servo, three that may carry a
// light. ARM2 and ARM3 are not wired; ARM4 carries the LED strip.
const ROWS = () => describe([
  servoRow("ledc:0", "ARM1"),
  servoRow("ledc:1", "ARM2"),
  servoRow("ledc:3", "ARM3"),
  servoRow("ledc:4", "ARM4"),
  servoRow("ledc:5", "ARM5"),
], {
  "ledc:1": { wired: false, type: "mg90s" },
  "ledc:3": { lightCapable: true, wired: false, type: "none" },
  "ledc:4": { lightCapable: true, type: "rgb" },
  "ledc:5": { lightCapable: true },
});
const CONFIG = () => ({ drive: { speedLimitMax: 300 } });

// Wiring's and Servos' hosts, each reading the droid the way the surface does
// (data/outputs.js load()) and mounting its view of the plates.
const boot = async ({ rows = ROWS(), config = CONFIG() } = {}) => {
  const document = new MiniDocument();
  for (const id of ["wiring-outputs-body", "wiring-outputs-feedback", "servo-types-body", "servo-types-feedback"]) {
    const node = document.createElement("div");
    node.id = id;
    document.body.appendChild(node);
  }
  const posts = [];
  const timers = [];
  const window = {
    document,
    PAApi: {
      messageFor: (error) => String(error?.message || error),
      get: async (path) => {
        if (path === "/api/config") return { ok: true, data: structuredClone(config) };
        assert.equal(path, "/api/servo/outputs");
        return { ok: true, data: { outputs: structuredClone(rows) } };
      },
      postJson: async (path, json) => {
        posts.push({ path, json: structuredClone(json) });
        // Answer the way the firmware does: the rows take the save, and the
        // answer is the config it now holds.
        applyRowSave(rows, json);
        return { ok: true, data: structuredClone(config) };
      },
    },
    setTimeout: (fn) => {
      timers.push(fn);
      return timers.length;
    },
    clearTimeout() {},
  };
  const context = { window, document, console, setTimeout: window.setTimeout, clearTimeout: window.clearTimeout, URLSearchParams };
  context.globalThis = context;
  for (const file of ["live_reading.js", "apply_timing.js", "outputs.js", "output_settings.js"]) {
    vm.runInNewContext(readFileSync(join(dataDir, file), "utf8"), context, { filename: file });
  }
  window.PAOutputSettings.mount("wired", {
    body: document.getElementById("wiring-outputs-body"),
    feedback: document.getElementById("wiring-outputs-feedback"),
  });
  window.PAOutputSettings.mount("type", {
    body: document.getElementById("servo-types-body"),
    feedback: document.getElementById("servo-types-feedback"),
  });
  await window.PAOutputs.load();

  const plate = (host, address) =>
    document.getElementById(host).querySelectorAll("[data-output]").find((node) => node.getAttribute("data-output") === address);
  return {
    posts,
    rows,
    // The debounce, fired by hand rather than waited for.
    flush: async () => {
      timers.splice(0).forEach((fn) => fn());
      for (let turn = 0; turn < 4; turn += 1) await new Promise((resolve) => setImmediate(resolve));
    },
    wiring: (address) => plate("wiring-outputs-body", address),
    wiringPlates: () => document.getElementById("wiring-outputs-body").querySelectorAll("[data-output]"),
    servos: (address) => plate("servo-types-body", address),
    // Wiring's in-use press is the plate's head button.
    inUse: (address) => plate("wiring-outputs-body", address).querySelector("[aria-pressed]"),
    option: (root, value) => root.querySelectorAll("[data-value]").find((node) => node.getAttribute("data-value") === value),
    // What the fake droid's row for an Output now holds.
    row: (address) => rows.find((each) => each.address === address),
  };
};

test("what Wiring marks in use, Servos shows at once, and the save carries only what changed", async () => {
  const env = await boot();
  assert.equal(env.servos("ledc:3").classList.contains("is-on"), false);

  env.inUse("ledc:3").fire("click", {});
  assert.equal(env.inUse("ledc:3").getAttribute("aria-pressed"), "true", "Wiring shows the line in use");
  assert.equal(env.servos("ledc:3").classList.contains("is-on"), true, "and Servos shows the same answer");

  await env.flush();
  assert.equal(env.posts.length, 1);
  assert.equal(env.posts[0].path, "/api/config");
  assert.deepEqual(env.posts[0].json, { outputs: [{ address: "ledc:3", wired: true }] },
    "the one setting the builder changed, on its Output's row, and nothing else");
  assert.equal(env.servos("ledc:3").classList.contains("is-on"), true, "the droid's answer keeps it in use");
});

// A droid may have several lit body Parts, each on its own wire (ADR 0067).
// Until #413 the controller could light exactly one, and this module took a
// Light Type off every other Output the moment one was given a light. That
// exclusion would now silently unwire a builder's second strip, so its absence
// is the invariant - and the save carries no droid-wide light field to
// disagree with the types either.
test("a second wire can carry a light without taking it off the first", async () => {
  const env = await boot();
  assert.equal(env.option(env.wiring("ledc:4"), "rgb").classList.contains("active"), true);

  env.option(env.wiring("ledc:5"), "rgb").fire("click", {});
  await env.flush();

  assert.deepEqual(env.posts.at(-1).json, { outputs: [{ address: "ledc:5", component: "rgb" }] },
    "the wire the builder just gave a light carries one, and no other wire is sent anything");
  assert.equal(env.row("ledc:4").component, "rgb", "and the one that already did still does");
  assert.equal(env.option(env.wiring("ledc:4"), "rgb").classList.contains("active"), true);
});

test("an in-use tick waits for the next start until it is put back; a servo type never waits", async () => {
  const env = await boot();
  const wiringLine = () => env.wiring("ledc:3").parentElement.parentElement.querySelector(".apply-timing");
  const servosLine = () => env.servos("ledc:3").parentElement.parentElement.querySelector(".apply-timing");
  assert.equal(wiringLine().dataset.pending, "false", "nothing is waiting on a fresh read");

  env.inUse("ledc:3").fire("click", {});
  await env.flush();
  assert.equal(wiringLine().dataset.pending, "true", "the droid still runs the outputs it started with");
  // Servos' answer is used at once, and an immediate answer says nothing at
  // all (operator, 2026-09-19 on #412).
  assert.equal(servosLine().textContent, "", "Servos' answer is used at once, so it carries no line");

  env.inUse("ledc:3").fire("click", {});
  await env.flush();
  assert.equal(wiringLine().dataset.pending, "false", "put back, nothing is waiting");
});
