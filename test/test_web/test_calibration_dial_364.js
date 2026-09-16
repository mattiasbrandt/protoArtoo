// =============================================================================
// test/test_web/test_calibration_dial_364.js
//
// The calibration dial (#291, #364, ADR 0064): a builder drives the part until
// it looks right, presses a button, and that becomes the end.
//
// The shipped shell mounts the shipped Parts surface against a fake droid. What
// is asserted is what the page asked the droid for, in what order, and what the
// builder saw -- never a flag the code under test reports on itself. The two
// firmware bounds are NOT asserted here: they are the controller's, they exist
// only on hardware, and #355 carries them. What is asserted here is everything
// this page is responsible for -- taking the hold, keeping it alive, capturing,
// swapping the ends, and saying what happened when the droid let go.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { bootParts, freshOutputs, withParts, output, sleep } from "./helpers/parts_surface.js";

const NOT_WIRED = "– not wired –";

// ---------------------------------------------------------------------------

test("an Output that can be driven carries the acts; a light and an unnamed row do not", async () => {
  const outputs = withParts({ "ledc:1": ["magicPanel"] });
  outputs.push(output("pca:0", "", { commandedUs: 1500, targetUs: 1500 }));
  const env = await bootParts({ outputs });

  assert.equal(env.row("ledc:0").querySelector(".outputs-calibrate").hidden, false);
  assert.equal(env.row("ledc:0").querySelector(".outputs-off").hidden, false);
  // A light has no travel and nothing to let go of.
  assert.equal(env.row("ledc:1").querySelector(".outputs-calibrate").hidden, true);
  assert.equal(env.row("ledc:1").querySelector(".outputs-off").hidden, true);
  // An expander's row has no name the servo route takes as its arm.
  assert.equal(env.row("pca:0").querySelector(".outputs-calibrate").hidden, true);
  assert.equal(env.dialOpen(), false, "the dial is a panel below the table, closed until it is opened");
});

test("opening the dial takes the Output at the width it is already standing at, so nothing moves", async () => {
  // Standing at 1750, deliberately NOT the middle of its 1000-2000 band: a dial
  // that opened at the middle would move the part the moment it was opened, and
  // on a droid that is a panel swinging while somebody has their hands in it.
  const outputs = freshOutputs();
  outputs[0] = output("ledc:0", "ARM1", { commandedUs: 1750, targetUs: 1750 });
  const env = await bootParts({ outputs });
  env.pressCalibrate("ledc:0");
  await sleep(20);

  assert.equal(env.dialOpen(), true);
  assert.match(env.dialText("cal-title"), /Calibrating ARM1/);
  assert.deepEqual(
    env.holds().map((post) => post.form),
    [{ arm: "arm1", action: "hold", positionUs: "1750" }],
    "the first hold is at the width the droid said the Output was already holding",
  );
  assert.equal(env.dialText("cal-readout"), "1750 µs");
  assert.equal(env.slider().value, "1750");
});

test("an Output with no pulse has no position to start from, so the dial starts mid-band", async () => {
  const env = await bootParts();
  // AUX3 is switched off: commandedUs is null, and the middle of its band is
  // the only honest guess at where to begin.
  env.pressCalibrate("ledc:5");
  await sleep(20);

  assert.deepEqual(env.holds().map((post) => post.form.positionUs), ["1500"]);
});

test("the dial opens at the row's own component band and says which band and why", async () => {
  const outputs = freshOutputs();
  outputs[3] = output("ledc:4", "AUX2", {
    commandedUs: 1500, targetUs: 1500, component: "mg90s", bandLoUs: 500, bandHiUs: 2500,
  });
  const env = await bootParts({ outputs });

  env.pressCalibrate("ledc:0");
  await sleep(20);
  assert.match(env.dialText("cal-band"), /1000-2000 µs/);
  assert.match(env.dialText("cal-band"), /what an MG996R takes/);
  assert.match(env.dialText("cal-band"), /Record the part as an MG90S for the full 500-2500/);
  assert.equal(env.slider().min, "1000");
  assert.equal(env.slider().max, "2000");

  env.pressDial("cal-done");
  await sleep(20);
  env.pressCalibrate("ledc:4");
  await sleep(20);
  // On a part recorded as taking it, that same rule opens the full range #291
  // asked for - and stops offering an unlock it already has.
  assert.match(env.dialText("cal-band"), /500-2500 µs/);
  assert.match(env.dialText("cal-band"), /what an MG90S takes/);
  assert.doesNotMatch(env.dialText("cal-band"), /Record the part/);
  assert.equal(env.slider().min, "500");
  assert.equal(env.slider().max, "2500");
});

test("driving the dial sends the width, coalesced, and never one request an event", async () => {
  const env = await bootParts();
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
  const env = await bootParts();
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

test("Set MIN, Set CENTER and Set MAX capture where the dial stands, with no typing", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);

  env.drive(1150);
  await sleep(80);
  env.pressDial("cal-set");  // Set MIN
  await sleep(40);
  assert.deepEqual(env.captures().at(-1).form, {
    captureOutput: "ledc:0", captureEnd: "close", captureUs: "1150",
  });
  assert.match(env.dialNote(), /MIN is 1150 µs/);

  env.drive(1850);
  await sleep(80);
  env.dial().fire("click", { target: env.dial().querySelectorAll(".cal-set")[2] });  // Set MAX
  await sleep(40);
  assert.equal(env.captures().at(-1).form.captureEnd, "open");
  assert.equal(env.captures().at(-1).form.captureUs, "1850");

  env.drive(1400);
  await sleep(80);
  env.dial().fire("click", { target: env.dial().querySelectorAll(".cal-set")[1] });  // Set CENTER
  await sleep(40);
  assert.equal(env.captures().at(-1).form.captureEnd, "centre");
  assert.match(env.dialText("cal-ends"), /MIN 1150 µs · CENTER 1400 µs · MAX 1850 µs/);
});

test("a captured end that swallows the centre says the centre moved, rather than refusing", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);

  // The row opens at MIN 1000 / CENTER 1500 / MAX 2000. Capturing MIN above the
  // centre leaves the centre outside the travel the builder just described.
  env.drive(1600);
  await sleep(80);
  env.pressDial("cal-set");
  await sleep(40);

  assert.equal(env.captures().length, 1, "the capture was made, not refused");
  assert.match(env.dialNote(), /MIN is 1600 µs/);
  assert.match(env.dialNote(), /Centre moved to 1600 µs — it was outside the travel you just captured/);
  assert.match(env.dialText("cal-ends"), /CENTER 1600 µs/);
});

test("reverse swaps the two ends, stores no flag, and pressing it again is a real undo", async () => {
  const env = await bootParts({
    outputs: [
      output("ledc:0", "ARM1", { commandedUs: 1500, targetUs: 1500, openUs: 1900, closeUs: 1100, calibrated: true }),
      ...freshOutputs().slice(1),
    ],
  });
  env.pressCalibrate("ledc:0");
  await sleep(20);
  assert.match(env.dialText("cal-ends"), /MIN 1100 µs · CENTER 1500 µs · MAX 1900 µs/);

  env.pressDial("cal-reverse");
  await sleep(40);
  assert.deepEqual(env.reverses().at(-1).form, { reverseOutput: "ledc:0" },
    "no width travels with a reverse: the droid swaps what the row holds");
  assert.match(env.dialNote(), /Ends swapped — MIN is now 1900 µs/);
  assert.match(env.dialText("cal-ends"), /MIN 1900 µs · CENTER 1500 µs · MAX 1100 µs/);

  env.pressDial("cal-reverse");
  await sleep(40);
  assert.match(env.dialText("cal-ends"), /MIN 1100 µs · CENTER 1500 µs · MAX 1900 µs/,
    "unticking it is a real undo, because the state IS the pair");
});

test("test sweep is unavailable with no recorded ends and says why, and sweeps them once there are", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);

  assert.equal(env.dialButton("cal-sweep").disabled, true, "nowhere sane to sweep between yet");
  assert.equal(env.dialButton("cal-sweep").getAttribute("aria-disabled"), "true");
  assert.equal(env.dialButton("cal-useends").disabled, true, "and no ends to narrow the dial to");
  assert.match(env.dialText("cal-ends"), /No ends recorded yet/);

  // Record two ends, and both become available.
  env.drive(1200);
  await sleep(80);
  env.pressDial("cal-set");
  await sleep(40);
  env.drive(1800);
  await sleep(80);
  env.dial().fire("click", { target: env.dial().querySelectorAll(".cal-set")[2] });
  await sleep(40);

  assert.equal(env.dialButton("cal-sweep").disabled, false);

  // Park the dial somewhere that is neither end, so "and back" is a width the
  // sweep could only reach by returning to where it started.
  env.drive(1450);
  await sleep(120);
  const before = env.holds().length;
  env.pressDial("cal-sweep");
  await sleep(3200);
  // The first three are the sweep's legs; the keepalive resumes after it, so
  // anything past them is not the sweep and is not what this asserts.
  const swept = env.holds().slice(before, before + 3).map((post) => post.form.positionUs);
  assert.deepEqual(swept, ["1200", "1800", "1450"],
    "it visits the recorded ends and comes back to where the dial was standing");
  assert.match(env.dialNote(), /Swept 1200 µs to 1800 µs and back/);
  assert.equal(env.dialText("cal-readout"), "1450 µs");
});

test("safe range narrows a wide row and is inert on a narrow one, saying so", async () => {
  const outputs = freshOutputs();
  outputs[3] = output("ledc:4", "AUX2", {
    commandedUs: 1500, targetUs: 1500, component: "mg90s", bandLoUs: 500, bandHiUs: 2500,
  });
  const env = await bootParts({ outputs });

  env.pressCalibrate("ledc:4");
  await sleep(20);
  assert.equal(env.slider().max, "2500");
  env.pressDial("cal-safe");
  await sleep(20);
  assert.equal(env.slider().min, "1000", "narrowed to the cautious band");
  assert.equal(env.slider().max, "2000");

  env.pressDial("cal-done");
  await sleep(20);
  env.pressCalibrate("ledc:0");
  await sleep(20);
  assert.match(env.dialButton("cal-safe").textContent, /already/);
  env.pressDial("cal-safe");
  await sleep(20);
  assert.match(env.dialNote(), /already on the cautious range, so safe range has nothing to narrow/);
  assert.equal(env.slider().min, "1000", "and nothing moved");
});

test("pulses off from a row makes the Output limp at once and says which way it is limp", async () => {
  const env = await bootParts();
  env.pressPulsesOff("ledc:0");
  await sleep(40);

  assert.deepEqual(env.releases().map((post) => post.form), [{ arm: "arm1", action: "release" }]);
  assert.match(env.feedback(), /ARM1 is limp — nothing is driving it, so it will sit wherever it is/);
  await env.frame();
  assert.equal(env.text("ledc:0", "outputs-release"), "Limp - pulses off");
  assert.equal(env.text("ledc:0", "outputs-us"), "— off");
});

test("an Output the dial is holding says so, and each way of going limp reads differently", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);
  await env.frame();
  assert.equal(env.text("ledc:0", "outputs-release"), "The dial is holding it");

  env.wentLimp("ledc:0", "ceiling");
  await env.frame();
  assert.equal(env.text("ledc:0", "outputs-release"), "Went limp - ten minutes is the most a dial holds");
  assert.match(env.dialNote(), /ten minutes is the most a dial holds/);
  assert.match(env.dialNote(), /Press take it again to hold it once more/);

  env.wentLimp("ledc:1", "expiry");
  await env.frame();
  assert.equal(env.text("ledc:1", "outputs-release"), "Went limp - the dial stopped asking");
  env.wentLimp("ledc:3", "estop");
  await env.frame();
  assert.equal(env.text("ledc:3", "outputs-release"), "Limp - the estop let go");
});

test("one press takes the Output back after it has gone limp", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);
  env.wentLimp("ledc:0", "ceiling");
  await env.frame();

  const resume = env.dialButton("cal-resume");
  assert.equal(resume.hidden, false, "the way back is offered where the builder is looking");
  const before = env.holds().length;
  env.pressDial("cal-resume");
  await sleep(40);
  assert.equal(env.holds().length, before + 1, "one press re-takes it, which restarts both bounds");
  assert.match(env.dialNote(), /Holding it again/);
});

test("pulses off during a Find by Moving run ends the run at once and says which happened", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm2"], "ARM2 is the first spare Output");
  assert.ok(env.runPanel(), "the run is on the row");

  env.pressPulsesOff("ledc:1");
  await sleep(40);

  assert.equal(env.runPanel(), null, "the run ended at once, on the press, not on the next answer");
  assert.match(env.feedback(), /ARM2 is limp, and that stopped the run finding Rear-left body door/);
  assert.match(env.feedback(), new RegExp(`Rear-left body door stays ${NOT_WIRED}`));
  // The droid has answered since, so the row shows what it said rather than the
  // mark the run was watching: no pulse, and why there is none. (The held-back
  // mark covers only the window before that answer arrives, which is the estop
  // case test_find_by_moving_363.js pins.)
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
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm2"]);

  // Anything that takes the pulse off counts the nudge as ended, so without the
  // limp check the count going up would read as "try the next one".
  env.wentLimp("ledc:1", "estop");
  await env.frame();

  assert.equal(env.nudges().length, 1, "the run did not step on");
  assert.equal(env.runPanel(), null);
  assert.match(env.feedback(), /ARM2 is limp, so the run stopped/);
});

test("a run in progress refuses to open a dial, so one thing moves the droid at a time", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);

  env.pressCalibrate("ledc:3");
  await sleep(20);
  assert.equal(env.dialOpen(), false);
  assert.equal(env.holds().length, 0, "nothing was asked of the droid");
  assert.match(env.feedback(), /One at a time: Rear-left body door is being found/);
});

test("while the estop is latched every act is refused, and a press sends nothing", async () => {
  const env = await bootParts({ estop: true });

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
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);

  env.pushStatus({ estop: true });
  assert.match(env.dialNote(), /The estop let go of every output/);
  assert.equal(env.dialOpen(), true, "the panel stays where the builder is looking");
  assert.equal(env.slider().disabled, true, "and everything on it is refused");
});

test("a frame repaints the dial in place; the slider is never rebuilt under the pointer", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);
  const panel = env.dial();
  const slider = env.slider();

  // The builder has hold of the dial and the droid answers meanwhile.
  env.document.activeElement = slider;
  env.drive(1700);
  env.outputs[0].commandedUs = 1500;
  await env.frame();

  assert.equal(env.dial(), panel, "the panel is the same node");
  assert.equal(env.slider(), slider, "and so is the dial itself");
  assert.equal(env.slider().value, "1700", "a repaint leaves the control being held alone");
  env.document.activeElement = null;
});

test("leaving Parts lets go of the Output rather than leaving it driven", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:0");
  await sleep(20);
  assert.equal(env.releases().length, 0);

  env.navigate("#home");
  await sleep(180);

  assert.equal(env.releases().length, 1, "the hold ends when the page that took it goes away");
  assert.equal(env.releases()[0].form.arm, "arm1");
});

test("the dial closes itself if its Output leaves the droid's answer", async () => {
  const env = await bootParts();
  env.pressCalibrate("ledc:4");
  await sleep(20);
  assert.equal(env.dialOpen(), true);

  // A reboot into a different firmware: AUX2 is not there any more.
  env.outputs = env.outputs.filter((each) => each.address !== "ledc:4");
  await env.frame();

  assert.equal(env.dialOpen(), false);
  assert.equal(env.releases().length, 0, "and nothing is asked of an Output that is gone");
});
