// =============================================================================
// test/test_web/test_servo_calibration_test_card.js
//
// Servos (data/servo.js): what its drive controls send, run as the browser runs
// it - the shared outputs module (data/output_settings.js), Wiring's mount of it
// (data/wiring_outputs.js) and the Servos module - on a real node tree.
//
// Four invariants earn their place:
//   - Test Open and Test Close drive to the end the droid RECORDED, read from
//     GET /api/config, and never to a number a box on this page holds (#400).
//   - The test controls write no configuration: an end is set on Parts.
//   - A press names the Output by the word the firmware gave for it, exactly:
//     the board's own label, a space included (GPIO 5 on the FireBeetle 2),
//     which is what POST /api/servo takes (ADR 0033 Amendment 2026-09-19). A
//     page that derived the word from an id, or folded it, sends a word the
//     firmware refuses.
//   - The page draws the Outputs the firmware reported and no others, and an
//     Output given the LED strip stops offering servo moves at once - a press
//     there would send a servo command down an LED strip's data line.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { MiniDocument } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");

// GET /api/config as a FireBeetle 2 answers it: every Output with its board's
// label, address and save fields, and the recorded ends under the field names
// /api/config speaks. Ends deliberately unlike the firmware defaults
// (2000/1000) and unlike each other, so a test cannot pass on a fallback or on
// another Output's number.
const CONFIG = () => ({
  arm1OpenUs: 1850, arm1CloseUs: 1120,
  arm2OpenUs: 1840, arm2CloseUs: 1130,
  aux1OpenUs: 1830, aux1CloseUs: 1140,
  aux2OpenUs: 1820, aux2CloseUs: 1150,
  aux3OpenUs: 1810, aux3CloseUs: 1160,
  components: {
    arm1: { label: "GPIO 49", address: "ledc:0", enabledField: "enableArm1", typeField: "arm1Type", enabled: true, type: "mg996r" },
    arm2: { label: "GPIO 50", address: "ledc:1", enabledField: "enableArm2", typeField: "arm2Type", enabled: false, type: "mg996r" },
    aux1: { label: "GPIO 4", address: "ledc:3", ledStripPin: 1, enabledField: "enableAux1", typeField: "aux1Type", enabled: true, type: "mg996r" },
    aux2: { label: "GPIO 5", address: "ledc:4", ledStripPin: 2, enabledField: "enableAux2", typeField: "aux2Type", enabled: true, type: "mg90s" },
    aux3: { label: "GPIO 51", address: "ledc:5", ledStripPin: 3, enabledField: "enableAux3", typeField: "aux3Type", enabled: true, type: "mg996r" },
    domeEsc: { enabled: false, label: "GPIO 48" },
  },
  aux_led_pin: 0,
});

const boot = async (config = CONFIG()) => {
  const document = new MiniDocument();
  for (const id of [
    "wiring-outputs-body", "wiring-outputs-feedback", "servo-types-body", "servo-types-feedback",
    "output-controls", "output-controls-summary", "output-feedback",
    "servo-test-card", "servo-test-rows", "calib-feedback",
  ]) {
    const node = document.createElement("div");
    node.id = id;
    document.body.appendChild(node);
  }
  const requests = [];
  const timers = [];
  const window = {
    document,
    PAApi: {
      messageFor: (error) => String(error?.message || error),
      get: async (path) => {
        requests.push({ method: "GET", path });
        return { ok: true, data: path === "/api/config" ? config : {} };
      },
      postForm: async (path, form) => {
        requests.push({ method: "POST", path, form: { ...form } });
        if (path === "/api/config") {
          for (const entry of Object.values(config.components)) {
            if (!entry.enabledField) continue;
            entry.enabled = form[entry.enabledField] === "true";
            entry.type = form[entry.typeField];
          }
          config.aux_led_pin = Number(form.aux_led_pin);
        }
        return { ok: true, data: path === "/api/config" ? config : { ok: true } };
      },
    },
    PASurface: { poll: () => ({ start() {} }) },
    setTimeout: (fn) => {
      timers.push(fn);
      return timers.length;
    },
    clearTimeout() {},
  };
  const context = { window, document, console, setTimeout: window.setTimeout, clearTimeout: window.clearTimeout };
  context.globalThis = context;
  for (const file of ["apply_timing.js", "output_settings.js", "wiring_outputs.js", "servo.js"]) {
    vm.runInNewContext(readFileSync(join(dataDir, file), "utf8"), context);
  }
  const settle = async () => {
    for (let turn = 0; turn < 6; turn += 1) await new Promise((resolve) => setImmediate(resolve));
  };
  await settle();

  const rowsIn = (host) => document.getElementById(host).querySelectorAll("[data-output]");
  const rowIn = (host, id) => rowsIn(host).find((node) => node.getAttribute("data-output") === id);
  const press = (host, id, action) =>
    rowIn(host, id).querySelectorAll("[data-action]").find((node) => node.getAttribute("data-action") === action);
  return {
    requests,
    settle,
    flush: async () => {
      timers.splice(0).forEach((fn) => fn());
      await settle();
    },
    rowsIn,
    rowIn,
    press,
    servoPosts: () => requests.filter((r) => r.method === "POST" && r.path === "/api/servo").map((r) => r.form),
    configPosts: () => requests.filter((r) => r.method === "POST" && r.path === "/api/config"),
    stripOption: (id) =>
      document.getElementById("wiring-outputs-body").querySelectorAll("[data-output]")
        .find((node) => node.getAttribute("data-output") === id)
        .querySelectorAll("[data-value]").find((node) => node.getAttribute("data-value") === "rgb"),
  };
};

// =============================================================================
// What the drive controls send
// =============================================================================

test("Test Open drives to that Output's recorded open end, named by the board's own word", async () => {
  const env = await boot();

  env.press("servo-test-rows", "aux2", "test-open").fire("click", {});
  await env.settle();

  assert.deepStrictEqual(
    env.servoPosts(),
    [{ arm: "GPIO 5", action: "position", positionUs: "1820" }],
    "the end is the one recorded for this Output, and the word is its label as the firmware gave it"
  );
});

test("Test Close drives to the recorded close end, and Open sends the label as given", async () => {
  const env = await boot();

  env.press("servo-test-rows", "arm1", "test-close").fire("click", {});
  env.press("output-controls", "aux3", "open").fire("click", {});
  await env.settle();

  assert.deepStrictEqual(env.servoPosts(), [
    { arm: "GPIO 49", action: "position", positionUs: "1120" },
    { arm: "GPIO 51", action: "open" },
  ]);
});

test("pressing every drive and test control writes no configuration at all", async () => {
  const env = await boot();

  let presses = 0;
  for (const host of ["output-controls", "servo-test-rows"]) {
    for (const row of env.rowsIn(host)) {
      for (const control of row.querySelectorAll("[data-action]")) {
        control.fire("click", {});
        presses += 1;
      }
    }
  }
  await env.settle();

  assert.ok(presses > 0, "there were controls to press");
  assert.strictEqual(env.servoPosts().length, presses, "every control sends one servo command");
  assert.deepStrictEqual(env.configPosts(), [], "the controls write no configuration -- an end is set on Parts");
});

// =============================================================================
// Which Outputs the page draws
// =============================================================================

test("the page draws the Outputs the firmware reported, and only those", async () => {
  const config = CONFIG();
  // A board with three Outputs: the page has no list of its own to fall back on.
  delete config.components.aux2;
  delete config.components.aux3;
  const env = await boot(config);

  const drawn = env.rowsIn("output-controls").map((node) => node.getAttribute("data-output"));
  assert.deepStrictEqual(drawn, ["arm1", "aux1"], "the wired Outputs it reported, in its order");
});

test("an Output given the LED strip stops offering servo moves the moment the answer changes", async () => {
  const env = await boot();
  assert.ok(env.press("output-controls", "aux2", "open"), "GPIO 5 starts as a servo");

  env.stripOption("aux2").fire("click", {});

  assert.equal(env.rowIn("output-controls", "aux2").querySelectorAll("[data-action]").length, 0,
    "no servo control is left on the LED strip's Output");
  assert.equal(env.rowIn("servo-test-rows", "aux2"), undefined, "and it has no test row");
  assert.ok(env.press("output-controls", "aux1", "open"), "the other Outputs keep theirs");
});
