// =============================================================================
// test/test_web/test_servo_calibration_test_card.js
//
// Servos' drive controls (data/servo.js; CONTEXT.md "Servos"): on each Output's
// row, a typed width sent once, and open, close and stop. There is no separate
// test section any more - testing a servo is driving its Output (operator,
// 2026-09-19 on #412) - so this file, named for the test card it once covered,
// now holds what those controls may and may not do. Run as the browser runs
// it: the shipped shell mounts the shipped Servos surface against a fake droid
// (helpers/parts_surface.js).
//
// Five invariants earn their place:
//   - A press names the Output by the word the firmware gave for it, exactly:
//     the board's own label, a space included (GPIO 5 on the FireBeetle 2),
//     which is what POST /api/servo takes (ADR 0033 Amendment 2026-09-19). A
//     page that derived the word from an id, or folded it, sends a word the
//     firmware refuses. The typed width goes out as typed.
//   - A width past what a servo takes goes out too: the droid holds the range
//     and refuses it, and the page keeps no copy (ADR 0068).
//   - Driving writes no configuration: an end is recorded only by the dial.
//   - The drive acts appear only where the firmware's answer says a servo is
//     there: an Output carrying the LED strip, or not wired, offers none -
//     a press there would send a servo command down a strip's data line.
//   - The rows are the Outputs the firmware reported, and no others.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { bootServos, output, sleep } from "./helpers/parts_surface.js";
import { wiredOutputs } from "./helpers/fake_droid.js";

// A FireBeetle 2: its labels carry a space. A Part on every Output but
// GPIO 50, which is free.
const FIREBEETLE = () => [
  output("ledc:0", "GPIO 49", { commandedUs: 1500, targetUs: 1500, parts: ["utilUp"] }),
  output("ledc:1", "GPIO 50", { commandedUs: 1500, targetUs: 1500 }),
  output("ledc:3", "GPIO 4", { commandedUs: 1500, targetUs: 1500, parts: ["dataPanel"] }),
  output("ledc:4", "GPIO 5", { commandedUs: 1500, targetUs: 1500, parts: ["doorFL"] }),
];

// What the rows hold, as the firmware reports them: GPIO 4 carries the LED
// strip and GPIO 50, with no Part on it, is not wired.
const SAY = {
  "ledc:1": { wired: false },
  "ledc:3": { lightCapable: true, type: "rgb" },
  "ledc:4": { lightCapable: true, type: "mg90s" },
};

const drive = (env, address, action) =>
  env.row(address).querySelectorAll(".outputs-go").find((node) => node.dataset.action === action);
const servoPosts = (env) => env.posts.filter((post) => post.path === "/api/servo").map((post) => post.form);

test("a typed width and open go out under the board's own word, space and all, and write no config", async () => {
  const env = await bootServos({ outputs: FIREBEETLE(), say: SAY });
  await sleep(20);

  env.row("ledc:4").querySelector(".outputs-width").value = "1720";
  env.region().fire("click", { target: drive(env, "ledc:4", "position") });
  env.region().fire("click", { target: drive(env, "ledc:0", "open") });
  await sleep(20);

  assert.deepStrictEqual(servoPosts(env), [
    { arm: "GPIO 5", action: "position", positionUs: "1720" },
    { arm: "GPIO 49", action: "open" },
  ]);
  assert.deepStrictEqual(
    env.posts.filter((post) => post.path === "/api/config"),
    [],
    "driving records nothing: an end is set with the dial",
  );
});

// The droid rules on the width (ADR 0068, amended 2026-09-26): the page keeps
// no copy of its range, so a width past it goes out as typed and the droid's
// refusal is what the builder reads.
test("a width the droid will not take still goes out as typed", async () => {
  const env = await bootServos({ outputs: FIREBEETLE(), say: SAY });
  await sleep(20);

  env.row("ledc:0").querySelector(".outputs-width").value = "4000";
  env.region().fire("click", { target: drive(env, "ledc:0", "position") });
  await sleep(20);

  assert.deepStrictEqual(servoPosts(env), [{ arm: "GPIO 49", action: "position", positionUs: "4000" }]);
});

test("an Output carrying the LED strip offers no drive at all", async () => {
  const env = await bootServos({ outputs: FIREBEETLE(), say: SAY });
  await sleep(20);
  const offered = (address) => env.cell(address, "outputs-drive-acts").hidden === false;

  assert.equal(offered("ledc:0"), true, "a wired servo offers its drive acts");
  assert.equal(offered("ledc:3"), false, "the LED strip's Output offers none");
  assert.match(env.text("ledc:3", "outputs-drive-note"), /light/, "and says it is a light, not a servo");
});

// Servos lists the servos a builder actually has (operator, 2026-09-29 on
// #411: "why is the servos page hardcoded to list out these when I have no
// parts defined with wiring?!"; CONTEXT.md "Servos"): only the Outputs the
// firmware reports with a Part on them, in its order, each named by its Part
// and the pin the board prints. An Output with no Part is not a row, whatever
// its wired tick says, and nothing here comes from a list of this page's own.
// A mixed set, so an expander's channel with a Part is a row like any other.
test("Servos lists only the Outputs with a Part, named by the Part and the pin", async () => {
  const outputs = [...FIREBEETLE(), output("pca:0", "", { parts: ["doorFR"] }), output("pca:1", "")];
  const env = await bootServos({ outputs, say: { ...SAY, "ledc:1": { wired: true } } });

  assert.deepStrictEqual(
    env.rows().map((node) => node.dataset.output),
    ["ledc:0", "ledc:3", "ledc:4", "pca:0"],
  );
  assert.deepStrictEqual(
    env.rows().map((node) => `${node.querySelector(".parts-name").textContent} | ${node.querySelector(".outputs-address").textContent}`),
    ["Upper utility arm | GPIO 49", "Data Panel | GPIO 4", "Left body door | GPIO 5", "Right body door | pca:0"],
  );
});
