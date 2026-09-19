// =============================================================================
// test/test_web/test_output_settings.js
//
// The arm and AUX outputs (#369, data/output_settings.js): which are in use and
// which AUX line carries the LED strip, set on Wiring; which servo each carries,
// set on Servos. One answer drawn on two surfaces, run here as the browser runs
// it - the shared module, Wiring's own mount (data/wiring_outputs.js) and a
// Servos host - on a real node tree.
//
// Two invariants earn their place:
//   - one answer, two views: what Wiring changes, Servos shows, and the save
//     carries every output's fields as Configuration's rows always did;
//   - one LED strip: the controller routes the strip down one AUX line, so
//     giving it to a second line takes it off the first, and the route sent is
//     the line that now carries it. Sending two, or none, is the defect;
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

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");

// The Outputs as GET /api/config reports them (src/web/api_config.cpp
// CONFIG_OUTPUTS): the page draws one plate per entry that carries an address
// and saves it under the fields the entry names.
const OUTPUT_FACTS = {
  arm1: { label: "ARM1", address: "ledc:0", enabledField: "enableArm1", typeField: "arm1Type" },
  arm2: { label: "ARM2", address: "ledc:1", enabledField: "enableArm2", typeField: "arm2Type" },
  aux1: { label: "ARM3", address: "ledc:3", ledStripPin: 1, enabledField: "enableAux1", typeField: "aux1Type" },
  aux2: { label: "ARM4", address: "ledc:4", ledStripPin: 2, enabledField: "enableAux2", typeField: "aux2Type" },
  aux3: { label: "ARM5", address: "ledc:5", ledStripPin: 3, enabledField: "enableAux3", typeField: "aux3Type" },
};

const CONFIG = () => ({
  components: {
    arm1: { ...OUTPUT_FACTS.arm1, enabled: true, type: "mg996r" },
    arm2: { ...OUTPUT_FACTS.arm2, enabled: false, type: "mg90s" },
    aux1: { ...OUTPUT_FACTS.aux1, enabled: false, type: "none" },
    aux2: { ...OUTPUT_FACTS.aux2, enabled: true, type: "rgb" },
    aux3: { ...OUTPUT_FACTS.aux3, enabled: true, type: "mg996r" },
  },
  aux_led_pin: 2,
});

const boot = () => {
  const document = new MiniDocument();
  for (const id of ["wiring-outputs-body", "wiring-outputs-feedback", "servo-types-body", "servo-types-feedback"]) {
    const node = document.createElement("div");
    node.id = id;
    document.body.appendChild(node);
  }
  const posts = [];
  const timers = [];
  const config = CONFIG();
  const window = {
    document,
    PAApi: {
      messageFor: (error) => String(error?.message || error),
      get: async (path) => {
        assert.equal(path, "/api/config");
        return { ok: true, data: config };
      },
      postForm: async (path, form) => {
        posts.push({ path, form: { ...form } });
        for (const [id, facts] of Object.entries(OUTPUT_FACTS)) {
          config.components[id] = { ...facts, enabled: form[facts.enabledField] === "true", type: form[facts.typeField] };
        }
        config.aux_led_pin = Number(form.aux_led_pin);
        return { ok: true, data: config };
      },
    },
    setTimeout: (fn) => {
      timers.push(fn);
      return timers.length;
    },
    clearTimeout() {},
  };
  const context = { window, document, console, setTimeout: window.setTimeout, clearTimeout: window.clearTimeout };
  context.globalThis = context;
  vm.runInNewContext(readFileSync(join(dataDir, "apply_timing.js"), "utf8"), context);
  vm.runInNewContext(readFileSync(join(dataDir, "output_settings.js"), "utf8"), context);
  vm.runInNewContext(readFileSync(join(dataDir, "wiring_outputs.js"), "utf8"), context);
  window.PAOutputSettings.mount("type", {
    body: document.getElementById("servo-types-body"),
    feedback: document.getElementById("servo-types-feedback"),
  });

  const plate = (host, output) =>
    document.getElementById(host).querySelectorAll("[data-output]").find((node) => node.getAttribute("data-output") === output);
  return {
    posts,
    settle: async () => {
      for (let turn = 0; turn < 4; turn += 1) await new Promise((resolve) => setImmediate(resolve));
    },
    // The debounce, fired by hand rather than waited for.
    flush: async () => {
      timers.splice(0).forEach((fn) => fn());
      for (let turn = 0; turn < 4; turn += 1) await new Promise((resolve) => setImmediate(resolve));
    },
    wiring: (output) => plate("wiring-outputs-body", output),
    servos: (output) => plate("servo-types-body", output),
    // Wiring's in-use press is the plate's head button.
    inUse: (output) => plate("wiring-outputs-body", output).querySelector("[aria-pressed]"),
    option: (root, value) => root.querySelectorAll("[data-value]").find((node) => node.getAttribute("data-value") === value),
  };
};

test("what Wiring marks in use, Servos shows at once, and the save carries every output", async () => {
  const env = boot();
  await env.settle();
  assert.equal(env.servos("aux1").classList.contains("is-on"), false);

  env.inUse("aux1").fire("click", {});
  assert.equal(env.inUse("aux1").getAttribute("aria-pressed"), "true", "Wiring shows the line in use");
  assert.equal(env.servos("aux1").classList.contains("is-on"), true, "and Servos shows the same answer");

  await env.flush();
  assert.equal(env.posts.length, 1);
  const form = env.posts[0].form;
  assert.equal(env.posts[0].path, "/api/config");
  assert.equal(form.enableAux1, "true");
  // The fields Configuration's rows always sent, all of them, so a save from
  // one surface cannot quietly reset what the other set.
  assert.equal(form.enableArm1, "true");
  assert.equal(form.arm2Type, "mg90s");
  assert.equal(form.aux3Type, "mg996r");
  assert.equal(form.aux_led_pin, "2", "the strip stays where it was");
});

test("giving the LED strip to a second AUX line takes it off the first, and routes it there", async () => {
  const env = boot();
  await env.settle();

  env.option(env.wiring("aux3"), "rgb").fire("click", {});
  await env.flush();

  const form = env.posts.at(-1).form;
  assert.equal(form.aux3Type, "rgb");
  assert.notEqual(form.aux2Type, "rgb", "only one line carries the strip");
  assert.equal(form.aux_led_pin, "3", "and the route names the line that now carries it");
  assert.match(env.servos("aux2").textContent, /MG996R|None/, "Servos offers AUX 2 a servo again");
});

test("an in-use tick waits for the next start until it is put back; a servo type never waits", async () => {
  const env = boot();
  await env.settle();
  const wiringLine = () => env.wiring("aux1").parentElement.parentElement.querySelector(".apply-timing");
  const servosLine = () => env.servos("aux1").parentElement.parentElement.querySelector(".apply-timing");
  assert.equal(wiringLine().dataset.pending, "false", "nothing is waiting on a fresh read");

  env.inUse("aux1").fire("click", {});
  await env.flush();
  assert.equal(wiringLine().dataset.pending, "true", "the droid still runs the outputs it started with");
  assert.equal(servosLine().dataset.pending, "false", "Servos' answer is used at once");

  env.inUse("aux1").fire("click", {});
  await env.flush();
  assert.equal(wiringLine().dataset.pending, "false", "put back, nothing is waiting");
});
