// =============================================================================
// test/test_web/test_calibration_dial.js
//
// The calibration dial (#291, #364, ADR 0064): a builder drives the part until
// it looks right, presses a button, and that becomes the end.
//
// The shipped shell mounts the shipped Servos surface (#412) against a fake droid. What
// is asserted is what the page asked the droid for, in what order, and what the
// builder saw -- never a flag the code under test reports on itself. The two
// firmware bounds are NOT asserted here: they are the controller's, they exist
// only on hardware, and #355 carries them. What is asserted here is everything
// this page is responsible for -- taking the hold, keeping it alive, capturing,
// swapping the ends, and saying what happened when the droid let go.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { bootServos, freshOutputs, withParts, output, sleep } from "./helpers/parts_surface.js";

const NOT_WIRED = "– not wired –";

// ---------------------------------------------------------------------------

test("opening the dial takes the Output at the width it is already standing at, so nothing moves", async () => {
  // Standing at 1750, deliberately NOT the middle of its 1000-2000 band: a dial
  // that opened at the middle would move the part the moment it was opened, and
  // on a droid that is a panel swinging while somebody has their hands in it.
  const outputs = freshOutputs();
  outputs[0] = output("ledc:0", "ARM1", { commandedUs: 1750, targetUs: 1750 });
  const env = await bootServos({ outputs });
  env.pressCalibrate("ledc:0");
  await sleep(20);

  assert.equal(env.dialOpen(), true);
  assert.match(env.dialText("cal-title"), /Calibrating ARM1/);
  assert.deepEqual(
    env.holds().map((post) => post.form),
    [{ arm: "ARM1", action: "hold", positionUs: "1750" }],
    "the first hold is at the width the droid said the Output was already holding",
  );
  assert.equal(env.dialText("cal-readout"), "1750 µs");
  assert.equal(env.slider().value, "1750");
});

test("driving the dial sends the width, coalesced, and never one request an event", async () => {
  const env = await bootServos();
  env.pressCalibrate("ledc:0");
  await sleep(20);
  const before = env.holds().length;

  // A drag: many input events in a few milliseconds.
  env.drive(1520);
  env.drive(1540);
  env.drive(1560);
  assert.equal(env.dialText("cal-readout"), "1560 µs", "the readout follows the dial at once");
  assert.equal(env.holds().length, before, "nothing is sent while the drag is still moving");

  await sleep(120);
  const sent = env.holds().slice(before);
  assert.equal(sent.length, 1, "one hold for the drag, not one per event");
  assert.equal(sent[0].form.positionUs, "1560");
});

test("the hold is kept alive while the dial is open, and stops when it is closed", async () => {
  const env = await bootServos();
  env.pressCalibrate("ledc:0");
  await sleep(20);

  const keepalive = env.intervals.filter((each) => each.ms === 1000).at(-1);
  assert.ok(keepalive, "a keepalive runs while the dial is open");
  const before = env.holds().length;
  keepalive.fn();
  await sleep(20);
  assert.equal(env.holds().length, before + 1, "a tick refreshes the hold at the current width");

  env.pressDial("cal-done");
  await sleep(20);
  assert.ok(env.cleared.includes(keepalive.id), "closing the dial stops asking");
  assert.equal(env.releases().length, 1, "and lets go of the Output rather than waiting for the expiry");
  assert.equal(env.dialOpen(), false);
});

// The word a move is sent with is the Output's label exactly as the firmware
// gave it - the board's own word, which is what POST /api/servo takes (ADR 0033
// Amendment 2026-09-19). A FireBeetle 2 prints GPIO 49, space included; a page
// that folded or rewrote the label into an id of its own would send a word that
// board's firmware refuses.
test("a move names its Output by the board's label as the droid gave it, space and all", async () => {
  const outputs = freshOutputs();
  outputs[0] = output("ledc:0", "GPIO 49", { commandedUs: 1600, targetUs: 1600 });
  const env = await bootServos({ outputs });
  env.pressCalibrate("ledc:0");
  await sleep(20);
  env.pressPulsesOff("ledc:0");
  await sleep(40);

  assert.deepEqual(env.holds()[0].form, { arm: "GPIO 49", action: "hold", positionUs: "1600" });
  assert.deepEqual(env.releases().map((post) => post.form.arm), ["GPIO 49"]);
});

test("pulses off from a row makes the Output limp at once and says which way it is limp", async () => {
  const env = await bootServos();
  env.pressPulsesOff("ledc:0");
  await sleep(40);

  assert.deepEqual(env.releases().map((post) => post.form), [{ arm: "ARM1", action: "release" }]);
  assert.match(env.feedback(), /ARM1 is limp — nothing is driving it, so it will sit wherever it is/);
  await env.frame();
  assert.equal(env.text("ledc:0", "outputs-release"), "Limp - pulses off");
  assert.equal(env.text("ledc:0", "outputs-us"), "— off");
});

// The bound that would not exist if this page kept asking. A firmware release
// drops the hold, so the very next hold command takes the Output afresh and
// starts the ceiling over -- a page that kept its keepalive running would hold
// a servo for as long as the tab was open, which is the one thing ADR 0064 says
// a page must not be able to do.
test("once the droid has let go, the page stops asking and waits to be told to resume", async () => {
  const env = await bootServos();
  env.pressCalibrate("ledc:0");
  await sleep(20);
  const keepalive = env.intervals.filter((each) => each.ms === 1000).at(-1);

  // Still held: a tick refreshes it, which is what keeps the short expiry away.
  const held = env.holds().length;
  keepalive.fn();
  await sleep(20);
  assert.equal(env.holds().length, held + 1);

  // The ten minutes ran out.
  env.wentLimp("ledc:0", "ceiling");
  await env.frame();

  const after = env.holds().length;
  keepalive.fn();
  keepalive.fn();
  await sleep(40);
  assert.equal(env.holds().length, after, "the page does not take the Output back on its own");
  assert.equal(env.text("ledc:0", "outputs-release"), "Went limp - ten minutes is the most a dial holds");
});

test("pulses off during a Find by Moving run ends the run at once and says which happened", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM2"], "ARM2 is the first spare Output");
  assert.ok(env.runPanel(), "the run is on the row");

  env.pressPulsesOff("ledc:1");
  await sleep(40);

  assert.equal(env.runPanel(), null, "the run ended at once, on the press, not on the next answer");
  assert.match(env.feedback(), /ARM2 is limp, and that stopped the run finding Rear-left body door/);
  assert.match(env.feedback(), new RegExp(`Rear-left body door stays ${NOT_WIRED}`));
  // The droid has answered since, so the row shows what it said rather than the
  // mark the run was watching: no pulse, and why there is none. (The held-back
  // mark covers only the window before that answer arrives, which is the estop
  // case test_find_by_moving.js pins.)
  await env.frame();
  assert.equal(env.text("ledc:1", "outputs-us"), "— off");
  assert.equal(env.text("ledc:1", "outputs-release"), "Limp - pulses off");
  assert.equal(env.cell("ledc:1", "outputs-bar").classList.contains("is-off"), true);

  // The nudge that was in flight ends on its own; nothing steps on from it.
  env.endNudge("ledc:1");
  await env.frame();
  assert.equal(env.nudges().length, 1, "no second Output is nudged");
});

test("an Output going limp under a run ends the run rather than stepping on to the next", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["ARM2"]);

  // Anything that takes the pulse off counts the nudge as ended, so without the
  // limp check the count going up would read as "try the next one".
  env.wentLimp("ledc:1", "estop");
  await env.frame();

  assert.equal(env.nudges().length, 1, "the run did not step on");
  assert.equal(env.runPanel(), null);
  assert.match(env.feedback(), /ARM2 is limp, so the run stopped/);
});

test("a run in progress refuses to open a dial, so one thing moves the droid at a time", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);

  env.pressCalibrate("ledc:3");
  await sleep(20);
  assert.equal(env.dialOpen(), false);
  assert.equal(env.holds().length, 0, "nothing was asked of the droid");
  assert.match(env.feedback(), /One at a time: Rear-left body door is being found/);
});

test("while the estop is latched every act is refused, and a press sends nothing", async () => {
  const env = await bootServos({ estop: true });

  assert.equal(env.row("ledc:0").querySelector(".outputs-calibrate").disabled, true);
  assert.equal(env.row("ledc:0").querySelector(".outputs-calibrate").getAttribute("aria-disabled"), "true");
  assert.equal(env.row("ledc:0").querySelector(".outputs-off").disabled, true);

  env.pressCalibrate("ledc:0");
  env.pressPulsesOff("ledc:0");
  await sleep(20);
  assert.equal(env.holds().length, 0);
  assert.equal(env.releases().length, 0);
  assert.equal(env.dialOpen(), false);

  env.pushStatus({ estop: false });
  assert.equal(env.row("ledc:0").querySelector(".outputs-calibrate").disabled, false, "released, the acts are live");
});

test("an estop while the dial is open says the droid let go, and the dial stays open to resume from", async () => {
  const env = await bootServos();
  env.pressCalibrate("ledc:0");
  await sleep(20);

  env.pushStatus({ estop: true });
  assert.match(env.dialNote(), /The estop let go of every output/);
  assert.equal(env.dialOpen(), true, "the panel stays where the builder is looking");
  assert.equal(env.slider().disabled, true, "and everything on it is refused");
});

test("leaving Servos lets go of the Output rather than leaving it driven", async () => {
  const env = await bootServos();
  env.pressCalibrate("ledc:0");
  await sleep(20);
  assert.equal(env.releases().length, 0);

  env.navigate("#home");
  await sleep(180);

  assert.equal(env.releases().length, 1, "the hold ends when the page that took it goes away");
  assert.equal(env.releases()[0].form.arm, "ARM1");
});

test("the dial closes itself if its Output leaves the droid's answer", async () => {
  const env = await bootServos();
  env.pressCalibrate("ledc:4");
  await sleep(20);
  assert.equal(env.dialOpen(), true);

  // A reboot into a different firmware: ARM4 is not there any more.
  env.outputs = env.outputs.filter((each) => each.address !== "ledc:4");
  await env.frame();

  assert.equal(env.dialOpen(), false);
  assert.equal(env.releases().length, 0, "and nothing is asked of an Output that is gone");
});
