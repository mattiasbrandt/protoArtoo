// =============================================================================
// test/test_web/test_estop_gates_move_acts.js
//
// A move act is live only on a heard, clear estop (CONTEXT.md "Live Reading").
//
// Servos and Parts each used to decide the latch for themselves with
// `payload.estop === true`, so a status frame that did not carry the estop at
// all read as "clear" and switched their move acts on; and neither heard the
// shell losing contact with the droid, so the acts stayed on through it (#419).
// Firmware still refuses motion under a latch, so neither reached the drive
// path - both were the page showing something untrue.
//
// Driven through the shipped Operator Shell, the shipped status stream and the
// shipped surface, against a fake droid (helpers/parts_surface.js).
// =============================================================================
import test from "node:test";
import assert from "node:assert";

import { bootParts, bootServos, statusFrame, withParts, output, sleep } from "./helpers/parts_surface.js";

// A frame with the other five core fields and no estop.
const frameWithoutEstop = () => {
  const frame = statusFrame();
  delete frame.estop;
  return frame;
};

const servoActs = (env) => [env.centreButton(), env.findButton()];

const assertOff = (buttons, why) =>
  buttons.forEach((button) => {
    assert.equal(button.disabled, true, why);
    assert.equal(button.getAttribute("aria-disabled"), "true", why);
  });

const assertOn = (buttons, why) =>
  buttons.forEach((button) => {
    assert.equal(button.disabled, false, why);
  });

// ARM1 measured and driving the left body door, so the picture has a body
// Part with an Open it that can go live.
const measuredArm1 = () =>
  withParts({ "ledc:0": ["doorFL"] }, [
    output("ledc:0", "ARM1", { commandedUs: 1600, targetUs: 2000, openUs: 2000, closeUs: 1000, centreUs: 1500, calibrated: true }),
    output("ledc:1", "ARM2", { commandedUs: 1500, targetUs: 1500 }),
  ]);

// Picks the door on the picture; a second pick of the same marker would put it
// down again, so each test picks once and reads the act as often as it likes.
const pickDoor = (env) => {
  const marker = env.document.querySelectorAll("[data-marker]").find((node) => node.dataset.marker === "doorFL");
  env.document
    .querySelectorAll(".bv-svg")
    .find((svg) => svg.querySelectorAll("[data-marker]").some((node) => node === marker))
    .fire("click", { target: marker });
};
const openIt = (env) => env.document.querySelectorAll("[data-act]").find((node) => node.dataset.act === "toggle");

test("Servos: a frame that does not carry the estop leaves every move act off", async () => {
  const env = await bootServos({ frame: frameWithoutEstop() });
  await sleep(20);

  assertOff(servoActs(env), "a frame that says nothing about the estop has not said it is clear");
});

test("Parts: a frame that does not carry the estop leaves Open it off", async () => {
  const env = await bootParts({ outputs: measuredArm1(), frame: frameWithoutEstop() });
  await sleep(20);
  pickDoor(env);

  assertOff([openIt(env)], "a frame that says nothing about the estop has not said it is clear");
});

test("Servos: losing contact with the droid turns every move act off until it is heard again", async () => {
  const env = await bootServos();
  await sleep(20);
  assertOn(servoActs(env), "a heard, clear estop lets the acts go");

  env.loseStream();
  await sleep(20);
  assertOff(servoActs(env), "nobody knows the estop is still clear once contact is lost");

  env.pushStatus({ estop: false });
  await sleep(20);
  assertOn(servoActs(env), "a fresh frame is contact again");
});

test("Parts: losing contact with the droid turns Open it off until it is heard again", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  await sleep(20);
  pickDoor(env);
  assertOn([openIt(env)], "a heard, clear estop lets Open it go");

  env.loseStream();
  await sleep(20);
  assertOff([openIt(env)], "nobody knows the estop is still clear once contact is lost");
});
