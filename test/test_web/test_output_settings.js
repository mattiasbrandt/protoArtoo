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
//     the line that now carries it. Sending two, or none, is the defect.
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

const CONFIG = () => ({
  components: {
    arm1: { enabled: true, type: "mg996r" },
    arm2: { enabled: false, type: "mg90s" },
    aux1: { enabled: false, type: "none" },
    aux2: { enabled: true, type: "rgb" },
    aux3: { enabled: true, type: "mg996r" },
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
        for (const id of ["arm1", "arm2", "aux1", "aux2", "aux3"]) {
          const upper = id.charAt(0).toUpperCase() + id.slice(1);
          config.components[id] = { enabled: form[`enable${upper}`] === "true", type: form[`${id}Type`] };
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
