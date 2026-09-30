// =============================================================================
// test/test_web/test_find_by_moving.js
//
// Find by Moving (ADR 0050, #363; on Servos since #412): a builder who cannot
// remember which output the rear-left door is on picks that door and presses
// Find by moving, and watches the droid. The shipped shell mounts the shipped
// Servos surface against a fake droid; what is asserted is what the page asked
// the droid for, in what order, what the builder saw, and what the estop did
// to both -- never a flag the code under test reports on itself.
//
// Servos lists only the Outputs with a Part (#411), so every droid here has
// one - on ARM5, which has no pulse and is never a spare Output to nudge - and
// the spare Outputs a run steps through have no row of their own.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { bootServos, withParts, output, sleep } from "./helpers/parts_surface.js";

const NOT_WIRED = "– not wired –";

// ARM1..ARM4 spare, ARM5 carrying a Part and no pulse: the spare set is the
// four with a pulse, as on a droid with nothing else wired.
const oneRow = () => withParts({ "ledc:5": ["smallDoor"] });

// ---------------------------------------------------------------------------

test("pressing it nudges the first spare output, one at a time, and steps on only when the droid says the nudge has ended", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);

  assert.deepEqual(env.nudges(), [{ path: "/api/servo", form: { arm: "ARM2", action: "nudge" } }],
    "ARM1 drives a Part and ARM5 has no pulse, so ARM2 is the first spare Output");
  assert.equal(env.findButton().hidden, true, "the run takes the button's place");
  assert.ok(env.runPanel(), "the run is up, in the button's place");
  assert.match(env.runText(), /Nudging ARM2 \(1 of 3\)/);
  assert.match(env.runText(), /press That one when Rear-left body door moves/);

  // Frames arrive while the nudge is still going: nothing more is sent.
  await env.frame();
  await env.frame();
  assert.equal(env.nudges().length, 1, "the droid has not said ARM2's nudge ended, so no second nudge");

  // The droid says ARM2's nudge has ended, and only then the next spare
  // Output is nudged.
  env.endNudge("ledc:1");
  await env.frame();
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM2", "ARM3"]);
  assert.match(env.runText(), /Nudging ARM3 \(2 of 3\)/);

  // A count that moved on some other Output is not this nudge ending.
  env.endNudge("ledc:1");
  await env.frame();
  assert.equal(env.nudges().length, 2);

  env.endNudge("ledc:3");
  await env.frame();
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM2", "ARM3", "ARM4"]);
  assert.equal(env.moves().length, 0, "nothing has been wired: the builder has not said which one");
});

test("Stop sends nothing further, and the Part stays not wired", async () => {
  const env = await bootServos({ outputs: oneRow() });
  env.pressFind("doorRL");
  await sleep(20);
  assert.equal(env.nudges().length, 1);

  env.pressStop();
  assert.equal(env.runPanel(), null);
  assert.equal(env.findButton().hidden, false, "the button is back");
  assert.match(env.feedback(), /Stopped\. Rear-left body door stays – not wired –\./);

  env.endNudge("ledc:0");
  await env.frame();
  assert.equal(env.nudges().length, 1, "the nudge in flight finishes on its own; no next one is asked for");
  assert.equal(env.moves().length, 0);
  assert.ok(env.outputs.every((each) => !each.parts.includes("doorRL")), "the Part is on no Output");
});

test("a pass through every spare output with no press ends with the Part still not wired", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:0": ["utilUp"], "ledc:3": ["utilDown"] }) });
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM2"]);
  env.endNudge("ledc:1");
  await env.frame();
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM2", "ARM4"]);
  env.endNudge("ledc:4");
  await env.frame();

  assert.equal(env.nudges().length, 2, "two spare Outputs, two nudges, never ARM5 which has no pulse");
  assert.equal(env.runPanel(), null);
  assert.match(env.feedback(), /None of the 2 spare outputs moved Rear-left body door in one pass, so it stays – not wired –/);
  assert.equal(env.moves().length, 0);
  assert.ok(env.outputs.every((each) => !each.parts.includes("doorRL")), "the Part is on no Output");
});

test("with nothing spare to nudge it says so and sends nothing", async () => {
  const outputs = withParts({ "ledc:0": ["utilUp"], "ledc:1": ["utilDown"], "ledc:3": ["doorFL"], "ledc:4": ["doorFR"] });
  const env = await bootServos({ outputs });
  env.pressFind("doorRL");
  await sleep(20);

  assert.equal(env.nudges().length, 0);
  assert.equal(env.runPanel(), null);
  assert.match(env.feedback(), /Nothing to nudge/);
});

test("a firmware that does not say when a nudge has ended is refused rather than waited on", async () => {
  const outputs = oneRow().map(({ nudgesDone, ...rest }) => rest);
  const env = await bootServos({ outputs });
  env.pressFind("doorRL");
  await sleep(20);

  assert.equal(env.nudges().length, 0);
  assert.match(env.feedback(), /does not say when a nudge has ended/);
});

test("while the estop is latched the button is refused, disabled and aria-disabled, and a press sends nothing", async () => {
  const env = await bootServos({ outputs: oneRow(), estop: true });
  const button = env.findButton();
  assert.equal(button.disabled, true);
  assert.equal(button.getAttribute("aria-disabled"), "true");

  // A browser never delivers a click to a disabled button; the shell's notice
  // fires on the pointerdown instead. This is the handler's own guard should
  // one arrive: nothing is asked of the droid, and no run starts.
  env.pressFind("doorRL");
  await sleep(20);
  assert.equal(env.nudges().length, 0);
  assert.equal(env.runPanel(), null);

  env.pushStatus({ estop: false });
  assert.equal(button.disabled, false, "released, the button is live again");
  assert.equal(button.getAttribute("aria-disabled"), "false");
  env.pushStatus({ estop: true });
  assert.equal(button.disabled, true);
  assert.equal(env.nudges().length, 0);
});

// The nudged Output is a spare one, with no row here, so what the estop does
// to the table is seen on the rows that are listed: every Output's mark is
// held back, since the estop edge RELEASES every enabled Output and commands
// no position (C1d, #364, 075cf487; ADR 0043) - a row left reading its last
// commanded width would be claiming the droid holds a part it has just let go
// of (corrected on #365).
test("an estop mid-run ends the run at once and stops showing any output's last mark as current", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:4": ["doorRR"] }) });
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM1"]);
  assert.equal(env.text("ledc:4", "outputs-us"), "1500 µs");

  env.pushStatus({ estop: true });
  assert.equal(env.runPanel(), null, "the run ended on the frame");
  assert.match(env.feedback(), /The estop stopped the run\. Rear-left body door stays – not wired –\./);
  assert.equal(env.cell("ledc:4", "outputs-bar").classList.contains("is-stale"), true, "the estop let go of every Output");
  assert.doesNotMatch(env.text("ledc:4", "outputs-us"), /µs/, "no width is read as current");

  // The firmware ended the nudge where it was and says so on the next answer;
  // that answer is current and repaints the rows.
  env.endNudge("ledc:0");
  await env.frame();
  assert.equal(env.cell("ledc:4", "outputs-bar").classList.contains("is-stale"), false);
  assert.equal(env.text("ledc:4", "outputs-us"), "1500 µs");
  assert.equal(env.nudges().length, 1, "the count went up, but there is no run to step on");
  assert.equal(env.findButton().disabled, true, "and the button stays refused while the estop is latched");
});

test("a nudge the droid refuses ends the run and says why, and the Part stays not wired", async () => {
  const env = await bootServos({ outputs: oneRow() });
  env.nudgeFails = new Error("Servo command queue full");
  env.pressFind("doorRL");
  await sleep(30);

  assert.equal(env.runPanel(), null);
  assert.match(env.feedback(), /did not reach the droid: Servo command queue full\. Rear-left body door stays – not wired –/);
  env.endNudge("ledc:0");
  await env.frame();
  assert.equal(env.nudges().length, 1);
});

test("one run at a time: a second press is answered on the page, not sent to the droid", async () => {
  const env = await bootServos({ outputs: oneRow() });
  env.pressFind("doorRL");
  await sleep(20);
  env.pressFind("doorRR");
  await sleep(20);

  assert.equal(env.nudges().length, 1);
  assert.match(env.feedback(), /One run at a time: Rear-left body door is being found/);
  assert.match(env.runText(), /Rear-left body door moves/, "the run in progress is still the first one");
});

test("leaving Servos ends the run, and coming back sends nothing the builder did not press for", async () => {
  const env = await bootServos({ outputs: oneRow() });
  env.pressFind("doorRL");
  await sleep(20);
  assert.equal(env.nudges().length, 1);

  env.navigate("#home");
  await sleep(180);
  assert.equal(env.document.body.dataset.page, "home", "the Dashboard is the mounted surface");
  env.endNudge("ledc:0");

  env.navigate("#servo");
  await sleep(180);
  await env.frame();
  assert.equal(env.nudges().length, 1, "the run ended when Servos was left; the count going up starts nothing");
  assert.equal(env.runPanel(), null);
  assert.equal(env.findButton().hidden, false);
  assert.match(env.feedback(), /The run stopped when you left Servos/);
});

// An Output an expander would add: no name the servo route takes, so never a
// candidate even with a pulse on it.
test("an Output without a name the servo route takes is not nudged", async () => {
  const outputs = oneRow();
  outputs.push(output("pca:0", "", { commandedUs: 1500, targetUs: 1500 }));
  const env = await bootServos({ outputs });
  env.pressFind("doorRL");
  await sleep(20);
  for (const address of ["ledc:0", "ledc:1", "ledc:3", "ledc:4"]) {
    env.endNudge(address);
    await env.frame();
  }
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM1", "ARM2", "ARM3", "ARM4"]);
  assert.equal(env.runPanel(), null);
});
