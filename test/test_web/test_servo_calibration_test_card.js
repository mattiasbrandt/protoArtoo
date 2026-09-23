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
// Four invariants earn their place:
//   - A press names the Output by the word the firmware gave for it, exactly:
//     the board's own label, a space included (GPIO 5 on the FireBeetle 2),
//     which is what POST /api/servo takes (ADR 0033 Amendment 2026-09-19). A
//     page that derived the word from an id, or folded it, sends a word the
//     firmware refuses. The typed width goes out as typed.
//   - Driving writes no configuration: an end is recorded only by the dial.
//   - The drive acts appear only where the firmware's answer says a servo is
//     there: an Output carrying the LED strip, or not wired, offers none -
//     a press there would send a servo command down a strip's data line.
//   - The rows are the Outputs the firmware reported, and no others.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { bootServos, output, sleep } from "./helpers/parts_surface.js";
import { configOutputs } from "./helpers/fake_droid.js";

// A FireBeetle 2: its labels carry a space.
const FIREBEETLE = () => [
  output("ledc:0", "GPIO 49", { commandedUs: 1500, targetUs: 1500 }),
  output("ledc:1", "GPIO 50", { commandedUs: 1500, targetUs: 1500 }),
  output("ledc:3", "GPIO 4", { commandedUs: 1500, targetUs: 1500 }),
  output("ledc:4", "GPIO 5", { commandedUs: 1500, targetUs: 1500 }),
];

// GET /api/config's Output entries, as the firmware reports them: GPIO 4
// carries the LED strip and GPIO 50 is not wired.
const COMPONENTS = configOutputs(FIREBEETLE(), {
  "ledc:1": { enabled: false },
  "ledc:3": { lightCapable: true, type: "rgb" },
  "ledc:4": { lightCapable: true, type: "mg90s" },
});

const drive = (env, address, action) =>
  env.row(address).querySelectorAll(".outputs-go").find((node) => node.dataset.action === action);
const servoPosts = (env) => env.posts.filter((post) => post.path === "/api/servo").map((post) => post.form);

test("a typed width and open go out under the board's own word, space and all, and write no config", async () => {
  const env = await bootServos({ outputs: FIREBEETLE(), components: COMPONENTS });
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

test("an Output carrying the LED strip, or not wired, offers no drive at all", async () => {
  const env = await bootServos({ outputs: FIREBEETLE(), components: COMPONENTS });
  await sleep(20);
  const offered = (address) => env.cell(address, "outputs-drive-acts").hidden === false;

  assert.equal(offered("ledc:0"), true, "a wired servo offers its drive acts");
  assert.equal(offered("ledc:3"), false, "the LED strip's Output offers none");
  assert.equal(offered("ledc:1"), false, "an Output not wired offers none");
  assert.match(env.text("ledc:3", "outputs-drive-note"), /LED strip/);
});

// The browser knows no Output (operator, 2026-09-19 on #411): a board that
// reports three draws three rows, in its order, and no fourth from a list of
// this page's own.
test("the page draws the Outputs the firmware reported, and only those", async () => {
  const env = await bootServos({ outputs: FIREBEETLE().slice(0, 3), components: COMPONENTS });

  assert.deepStrictEqual(
    env.rows().map((node) => node.dataset.output),
    ["ledc:0", "ledc:1", "ledc:3"],
  );
  assert.deepStrictEqual(
    env.rows().map((node) => node.querySelector(".parts-name").textContent),
    ["GPIO 49", "GPIO 50", "GPIO 4"],
  );
});
