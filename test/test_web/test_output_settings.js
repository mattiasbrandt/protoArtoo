// =============================================================================
// test/test_web/test_output_settings.js
//
// Servos' servo picks (#369, #399, data/output_settings.js): which servo model
// each Output carries, picked on the Output's own row, run here as the browser
// runs it - data/outputs.js holding the droid's answer (#415) - on a real node
// tree, against the one fake droid (helpers/fake_droid.js).
//
// Wiring's Output plates, the other view this file drew, are gone: Wiring is
// part-first, and what is on a wire is picked on a Part's row there (#411).
// Their invariants - each wire is its own answer, and a wire change waits for
// the next start - moved with them to test_parts_table.js.
//
// The one invariant here: a pick saves the field the firmware named for what
// changed and nothing a builder did not touch, so a save cannot reset what
// another surface set; and a servo type bounds the next move, so it never
// says it is waiting (#370).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { MiniDocument } from "./helpers/mini_dom.js";
import { servoRow, describe, applyRowSave } from "./helpers/fake_droid.js";
import { shippedWords } from "./helpers/shipped_words.cjs";

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
// (data/outputs.js load()) and mounting its view: Wiring's plates in one body,
// Servos' picks each in the slot its host keeps on that Output's row.
const boot = async ({ rows = ROWS(), config = CONFIG() } = {}) => {
  const document = new MiniDocument();
  for (const id of ["servo-types-body", "servo-types-timing", "servo-types-feedback"]) {
    const node = document.createElement("div");
    node.id = id;
    document.body.appendChild(node);
  }
  const posts = [];
  const timers = [];
  const window = {
    document,
    PAApi: {
      // The shipped words table's lookups (helpers/shipped_words.cjs).
      ...shippedWords(),
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
  // Servos' rows, as data/servo.js keeps them: one slot per Output.
  const slots = new Map();
  const slot = (address) => {
    if (!slots.has(address)) {
      const node = document.createElement("div");
      node.setAttribute("data-output", address);
      document.getElementById("servo-types-body").appendChild(node);
      slots.set(address, node);
    }
    return slots.get(address);
  };
  window.PAOutputSettings.mount("type", {
    slot,
    timing: document.getElementById("servo-types-timing"),
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
    servos: (address) => plate("servo-types-body", address),
    servosLine: () => document.getElementById("servo-types-timing"),
    option: (root, value) => root.querySelectorAll("[data-value]").find((node) => node.getAttribute("data-value") === value),
    // What the fake droid's row for an Output now holds.
    row: (address) => rows.find((each) => each.address === address),
  };
};

test("a servo picked on Servos is drawn at once, saves only its own field, and never waits", async () => {
  const env = await boot();
  assert.equal(env.servos("ledc:0").classList.contains("is-on"), true, "a wired Output's row reads on");
  assert.equal(env.servos("ledc:1").classList.contains("is-on"), false, "and one not wired reads off");

  env.option(env.servos("ledc:0"), "mg90s").fire("click", {});
  assert.equal(env.option(env.servos("ledc:0"), "mg90s").classList.contains("active"), true, "drawn before the droid answers");

  await env.flush();
  assert.equal(env.posts.length, 1);
  assert.equal(env.posts[0].path, "/api/config");
  assert.deepEqual(env.posts[0].json, { outputs: [{ address: "ledc:0", component: "mg90s" }] },
    "the one setting the builder changed, on its Output's row, and nothing else");
  assert.equal(env.row("ledc:0").component, "mg90s");
  // A servo type bounds the very next move, and an immediate answer says
  // nothing at all (operator, 2026-09-19 on #412).
  assert.equal(env.servosLine().textContent, "", "Servos' answer is used at once, so it carries no line");
});
