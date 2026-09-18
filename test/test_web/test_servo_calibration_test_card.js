// =============================================================================
// test/test_web/test_servo_calibration_test_card.js
//
// C1f (#400): the typed calibration form left Servos, and what only DRIVES a
// part stayed. Two claims, and they need two different kinds of evidence.
//
// The behavioural half runs the shipped data/servo.js: Test Open and Test Close
// used to send whatever number the box beside them was holding, and now send the
// end the droid has RECORDED, read from GET /api/config. That is a real change
// in what the droid does when a builder presses the button, so it is driven
// through the module and asserted on the request that reached the controller.
//
// The markup half asserts on the text of data/servo.html. test_web/README.md
// warns that asserting source text proves characters rather than behaviour --
// true, and it is why the tests above exist. But "the ten typed inputs are gone
// from the page" is a claim ABOUT the shipped markup, the permissive DOM stub
// answers for any id whether the page carries it or not, and #298 measured what
// a forwarding address that names a destination which is not there costs. So the
// file itself is the only witness, and these three tests say so at the site.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// Ends deliberately unlike the firmware defaults (2000/1000) and unlike each
// other, so a test cannot pass on a fallback or on the wrong channel's number.
const CONFIG = {
  arm1OpenUs: 1850, arm1CloseUs: 1120,
  arm2OpenUs: 1840, arm2CloseUs: 1130,
  aux1OpenUs: 1830, aux1CloseUs: 1140,
  aux2OpenUs: 1820, aux2CloseUs: 1150,
  aux3OpenUs: 1810, aux3CloseUs: 1160,
  components: {
    aux1: { enabled: true, type: "mg996r" },
    aux2: { enabled: true, type: "mg90s" },
    aux3: { enabled: true, type: "mg996r" },
  },
};

// The page loaded and its section run, which is the state a builder presses a
// test button in.
const loadedServoPage = async () => {
  const env = loadPageModule("servo.js", { respond: () => ({ data: CONFIG }) });
  await env.runSection("servo-calibration");
  await env.settle();
  return env;
};

// The module builds its request bodies inside the vm, so they carry the vm
// realm's Object.prototype and deepStrictEqual would reject them on that alone.
// Copied into this realm, so an assertion fails for what the droid was told and
// never for which realm built the object.
// The ids the removed form's ten boxes had. The permissive DOM stub answers for
// an id whether the page carries it or not, so writing to one is how a test asks
// the question that actually separates this slice from what came before: the old
// page drove Test Open to whatever its box was holding, and this one drives to
// the end the droid recorded, which nothing on this page can move. A decoy here
// is therefore not a fixture -- it is the discriminator.
const typeIntoRemovedBox = (env, id, value) => {
  env.element(id).value = value;
};

const servoPosts = (env) =>
  env.requests
    .filter((request) => request.method === "POST" && request.path === "/api/servo")
    .map((request) => ({ ...request.opts.body }));

// =============================================================================
// What the drive-only controls send
// =============================================================================

test("Test Open drives to the recorded open end, not to a number typed on this page", async () => {
  const env = await loadedServoPage();
  typeIntoRemovedBox(env, "arm1-open-us", "2500");

  env.emitOn("arm1-open-test-btn", "click");
  await env.settle();

  assert.deepStrictEqual(
    servoPosts(env),
    [{ arm: "arm1", action: "position", positionUs: "1850" }],
    "Test Open must drive to arm1OpenUs as the droid recorded it"
  );
});

test("Test Close drives to the recorded close end", async () => {
  const env = await loadedServoPage();
  typeIntoRemovedBox(env, "arm1-close-us", "500");

  env.emitOn("arm1-close-test-btn", "click");
  await env.settle();

  assert.deepStrictEqual(
    servoPosts(env),
    [{ arm: "arm1", action: "position", positionUs: "1120" }],
    "Test Close must drive to arm1CloseUs as the droid recorded it"
  );
});

test("an AUX Test Open drives to that AUX's own recorded end", async () => {
  const env = await loadedServoPage();
  typeIntoRemovedBox(env, "aux2-open-us", "2500");

  env.emitOn("aux2-open-test-btn", "click");
  await env.settle();

  assert.deepStrictEqual(
    servoPosts(env),
    [{ arm: "aux2", action: "position", positionUs: "1820" }],
    "each output's Test Open must read its own end, not another channel's"
  );
});

test("pressing every test control writes no configuration at all", async () => {
  const env = await loadedServoPage();

  for (const output of ["arm1", "arm2", "aux1", "aux2", "aux3"]) {
    for (const suffix of ["test-btn", "open-test-btn", "close-test-btn"]) {
      env.emitOn(`${output}-${suffix}`, "click");
    }
  }
  await env.settle();

  assert.strictEqual(servoPosts(env).length, 15, "all fifteen test controls are wired");
  assert.deepStrictEqual(
    env.requests.filter((request) => request.method === "POST" && request.path === "/api/config"),
    [],
    "the test controls write no configuration -- an end is set on Parts"
  );
});

// =============================================================================
// The servo on each output, set on this page (#369)
// =============================================================================

// The servo type moved here from Configuration, drawn by data/output_settings.js
// and shared with Wiring's in-use ticks. When that answer changes, this page's
// own AUX rows follow it at once: a line just given the LED strip has no
// position, so it must stop offering Open and Close - a press there would send
// a servo command down an LED strip's data line.
test("an AUX line given the LED strip stops offering servo moves the moment the answer changes", async () => {
  let listener = null;
  const PAOutputSettings = { mount() {}, onChange: (fn) => { listener = fn; } };
  const env = loadPageModule("servo.js", { respond: () => ({ data: CONFIG }), overrides: { PAOutputSettings } });
  await env.runSection("servo-calibration");
  await env.settle();
  assert.ok(listener, "Servos follows the shared outputs answer");
  assert.match(env.element("aux-controls-container").innerHTML, /data-arm="aux2"/, "AUX 2 starts as a servo");

  listener({
    arm1: { enabled: true, type: "mg996r" },
    arm2: { enabled: false, type: "mg996r" },
    aux1: { enabled: true, type: "mg996r" },
    aux2: { enabled: true, type: "rgb" },
    aux3: { enabled: true, type: "mg996r" },
  });
  const rows = env.element("aux-controls-container").innerHTML;
  assert.doesNotMatch(rows, /data-arm="aux2"/, "no servo control is left on the LED strip line");
  assert.match(rows, /data-arm="aux1"/, "and the other lines keep theirs");
});

// =============================================================================
// When the card is there at all
// =============================================================================

// =============================================================================
// What the page no longer carries, and where it sends a builder instead
// =============================================================================

