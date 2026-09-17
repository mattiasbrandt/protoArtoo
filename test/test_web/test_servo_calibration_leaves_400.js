// =============================================================================
// test/test_web/test_servo_calibration_leaves_400.js
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
import { readFileSync, existsSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { loadPageModule } from "./helpers/page_module_env.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const servoHtml = readFileSync(join(dataDir, "servo.html"), "utf-8");

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

// Green before this slice as well as after, and deliberately so: criterion 3
// keeps every control that only drives a part, and this is the guard that says
// Test position was not carried off with the form. A mutation still kills it.
test("Test position still drives to the number in its own box", async () => {
  const env = await loadedServoPage();
  env.element("arm1-test-us").value = "1234";

  env.emitOn("arm1-test-btn", "click");
  await env.settle();

  assert.deepStrictEqual(
    servoPosts(env),
    [{ arm: "arm1", action: "position", positionUs: "1234" }],
    "Test position is a drive control and this slice does not touch it"
  );
});

test("nothing on the page listens for a typed endpoint any more", async () => {
  const env = await loadedServoPage();

  // The harness throws when nothing is listening, which is the assertion: the
  // auto-save this page used to hang on every endpoint box is gone with them.
  for (const output of ["arm1", "arm2", "aux1", "aux2", "aux3"]) {
    for (const end of ["open", "close"]) {
      assert.throws(
        () => env.emitOn(`${output}-${end}-us`, "input"),
        /registered no "input" listener/,
        `${output} ${end} is still wired to something on this page`
      );
    }
  }
  assert.deepStrictEqual(
    env.requests.filter((request) => request.method === "POST"),
    [],
    "and nothing was posted while we asked"
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
    "Servos reads /api/config and never writes it -- an end is set on Parts"
  );
});

// =============================================================================
// When the card is there at all
// =============================================================================

test("the test card appears only when there is something it could drive", async () => {
  let deliverStatus = null;
  const env = loadPageModule("servo.js", {
    respond: () => ({ data: CONFIG }),
    overrides: {
      PAStatusStream: {
        isSupported: () => true,
        subscribe: (handler) => { deliverStatus = handler; return () => {}; },
        getLastStatus: () => null,
      },
    },
  });

  const hiddenStates = [];
  env.element("servo-test-card").classList.toggle = (className, on) => {
    if (className === "hidden") hiddenStates.push(on);
  };

  deliverStatus("status", {});
  deliverStatus("status", { arm1: { detail: "open" } });

  assert.deepStrictEqual(
    hiddenStates,
    [true, false],
    "nothing enabled hides the card; an enabled arm shows it"
  );
});

// =============================================================================
// What the page no longer carries, and where it sends a builder instead
// =============================================================================

test("the ten typed endpoint inputs and the card that held them are gone from the markup", () => {
  for (const output of ["arm1", "arm2", "aux1", "aux2", "aux3"]) {
    for (const end of ["Open", "Close"]) {
      assert.ok(
        !servoHtml.includes(`name="${output}${end}Us"`),
        `data/servo.html still carries a typed ${output}${end}Us input`
      );
      assert.ok(
        !servoHtml.includes(`id="${output}-${end.toLowerCase()}-us"`),
        `data/servo.html still carries the ${output} ${end.toLowerCase()} endpoint box`
      );
    }
  }
  assert.ok(!servoHtml.includes("servo-calib-card"), "the calibration card id survives");
  assert.ok(!servoHtml.includes("calib-form"), "the calibration form survives");
});

test("every test control the slice kept is still on the page", () => {
  for (const output of ["arm1", "arm2", "aux1", "aux2", "aux3"]) {
    assert.ok(servoHtml.includes(`id="${output}-test-us"`), `${output} lost its Test position box`);
    for (const suffix of ["test-btn", "open-test-btn", "close-test-btn"]) {
      assert.ok(servoHtml.includes(`id="${output}-${suffix}"`), `${output} lost its ${suffix}`);
    }
  }
});

test("the forwarding sentence sends a builder to Parts, and the address it names exists", () => {
  const forwarder = servoHtml
    .split("\n")
    .filter((line) => line.includes('href="/parts.html"'));

  assert.strictEqual(forwarder.length, 1, "exactly one forwarding link to Parts");
  assert.ok(
    servoHtml.includes("Open and close ends are set in"),
    "the sentence has to say what moved, not only link somewhere"
  );
  assert.ok(forwarder[0].includes(">Parts</a>"), "the link is labelled with the destination");
  assert.ok(
    existsSync(join(dataDir, "parts.html")),
    "data/parts.html must exist: #298 measured what a forwarding address that " +
      "names a destination which is not there costs a builder"
  );
});
