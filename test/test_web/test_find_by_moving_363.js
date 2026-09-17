// =============================================================================
// test/test_web/test_find_by_moving_363.js
//
// Find by Moving (ADR 0050, #363): a builder who cannot remember which output
// the rear-left door is on presses the button on that door's unwired row and
// watches the droid. The shipped shell mounts the shipped Parts surface
// against a fake droid; what is asserted is what the page asked the droid for,
// in what order, what the builder saw on the row, and what the estop did to
// both -- never a flag the code under test reports on itself.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { bootParts, freshOutputs, withParts, output, sleep } from "./helpers/parts_surface.js";

const NOT_WIRED = "– not wired –";
const readCss = () => readFileSync(join(dirname(fileURLToPath(import.meta.url)), "../../data/style.css"), "utf-8");

// ---------------------------------------------------------------------------

test("an unwired row carries the button, a wired row does not, and it waits for the droid's first status", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["utilUp"] }) });

  assert.equal(env.findButton("doorRL").hidden, false, "a Part nothing drives can be found by moving");
  assert.equal(env.findButton("utilUp").hidden, true, "a Part on an Output has nothing to find");
  assert.equal(env.findButton("doorRL").disabled, false, "the droid has said the estop is clear, so the button is live");
  assert.equal(env.findButton("doorRL").getAttribute("aria-disabled"), "false");
  assert.match(env.findButton("doorRL").className, /\bbtn\b/, "a refused press has to land where the shell's notice looks");
  assert.match(
    env.document.getElementById("parts-table").parentNode.textContent,
    /Find by moving/,
    "the entrance says what the button does before the commitment",
  );
});

test("pressing it nudges the first spare output, one at a time, and steps on only when the droid says the nudge has ended", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["utilUp"] }) });
  env.pressFind("doorRL");
  await sleep(20);

  assert.deepEqual(env.nudges(), [{ path: "/api/servo", form: { arm: "arm2", action: "nudge" } }],
    "ARM1 drives a Part and AUX3 has no pulse, so ARM2 is the first spare Output");
  assert.equal(env.findButton("doorRL").hidden, true, "the run takes the button's place");
  assert.ok(env.runPanel(), "the run is on the row");
  assert.equal(env.runPanel().closest("[data-part]").dataset.part, "doorRL");
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
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm2", "aux1"]);
  assert.match(env.runText(), /Nudging AUX1 \(2 of 3\)/);

  // A count that moved on some other Output is not this nudge ending.
  env.endNudge("ledc:1");
  await env.frame();
  assert.equal(env.nudges().length, 2);

  env.endNudge("ledc:3");
  await env.frame();
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm2", "aux1", "aux2"]);
  assert.equal(env.moves().length, 0, "nothing has been wired: the builder has not said which one");
});

test("That one records the assignment with the picker's own move, and the run ends", async () => {
  const env = await bootParts();
  env.pressFind("doorRL");
  await sleep(20);
  env.endNudge("ledc:0");
  await env.frame();
  assert.match(env.runText(), /Nudging ARM2 \(2 of 4\)/);

  env.pressThatOne();
  await sleep(30);
  assert.deepEqual(env.moves(), [
    { path: "/api/config", form: { movePart: "doorRL", movePartFrom: "none", movePartTo: "ledc:1" } },
  ], "the Output being nudged when the builder pressed, off nothing, with no question");
  assert.equal(env.runPanel(), null, "the run is over");
  assert.equal(env.text("ledc:1", "outputs-parts"), "Rear-left body door");
  assert.equal(env.partRow("doorRL").querySelector("select").value, "ledc:1");
  assert.equal(env.findButton("doorRL").hidden, true, "a wired Part has nothing left to find");
  assert.match(env.feedback(), /Rear-left body door is on ARM2\./);

  env.endNudge("ledc:1");
  await env.frame();
  assert.equal(env.nudges().length, 2, "nothing more is sent after the run ended");
});

test("Stop sends nothing further, and the Part stays not wired", async () => {
  const env = await bootParts();
  env.pressFind("doorRL");
  await sleep(20);
  assert.equal(env.nudges().length, 1);

  env.pressStop();
  assert.equal(env.runPanel(), null);
  assert.equal(env.findButton("doorRL").hidden, false, "the button is back");
  assert.match(env.feedback(), /Stopped\. Rear-left body door stays – not wired –\./);

  env.endNudge("ledc:0");
  await env.frame();
  assert.equal(env.nudges().length, 1, "the nudge in flight finishes on its own; no next one is asked for");
  assert.equal(env.moves().length, 0);
  assert.equal(env.text("ledc:0", "outputs-parts"), NOT_WIRED);
});

test("a pass through every spare output with no press ends with the Part still not wired", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["utilUp"], "ledc:3": ["utilDown"] }) });
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm2"]);
  env.endNudge("ledc:1");
  await env.frame();
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm2", "aux2"]);
  env.endNudge("ledc:4");
  await env.frame();

  assert.equal(env.nudges().length, 2, "two spare Outputs, two nudges, never AUX3 which has no pulse");
  assert.equal(env.runPanel(), null);
  assert.match(env.feedback(), /None of the 2 spare outputs moved Rear-left body door in one pass, so it stays – not wired –/);
  assert.equal(env.moves().length, 0);
  assert.equal(env.partRow("doorRL").querySelector("select").value, "none");
});

test("with nothing spare to nudge it says so and sends nothing", async () => {
  const outputs = withParts({ "ledc:0": ["utilUp"], "ledc:1": ["utilDown"], "ledc:3": ["doorFL"], "ledc:4": ["doorFR"] });
  const env = await bootParts({ outputs });
  env.pressFind("doorRL");
  await sleep(20);

  assert.equal(env.nudges().length, 0);
  assert.equal(env.runPanel(), null);
  assert.match(env.feedback(), /Nothing to nudge/);
});

test("a firmware that does not say when a nudge has ended is refused rather than waited on", async () => {
  const outputs = freshOutputs().map(({ nudgesDone, ...rest }) => rest);
  const env = await bootParts({ outputs });
  env.pressFind("doorRL");
  await sleep(20);

  assert.equal(env.nudges().length, 0);
  assert.match(env.feedback(), /does not say when a nudge has ended/);
});

test("while the estop is latched the button is refused, disabled and aria-disabled, and a press sends nothing", async () => {
  const env = await bootParts({ estop: true });
  const button = env.findButton("doorRL");
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

test("an estop mid-run ends the run at once and stops showing the nudged output's last mark as current", async () => {
  const env = await bootParts();
  env.pressFind("doorRL");
  await sleep(20);
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm1"]);
  // The droid's last answer had ARM1 part way out.
  env.outputs[0].commandedUs = 1600;
  env.outputs[0].targetUs = 1400;
  await env.frame();
  assert.equal(env.text("ledc:0", "outputs-us"), "1600 → 1400 µs");

  env.pushStatus({ estop: true });
  assert.equal(env.runPanel(), null, "the run ended on the frame");
  assert.match(env.feedback(), /The estop stopped the run\. Rear-left body door stays – not wired –\./);
  assert.equal(env.cell("ledc:0", "outputs-bar").classList.contains("is-stale"), true, "the last commanded mark is held back");
  assert.match(env.text("ledc:0", "outputs-us"), /Stopped — finding out/);
  // Every OTHER Output's mark is held back too, and that changed under this
  // test. When it was written the estop stopped the nudge and nothing else, so
  // only the nudged row's mark went stale. C1d (#364, 075cf487) made the estop
  // edge RELEASE every enabled Output and command no position (ADR 0043), so
  // none of them is being driven and none of their marks is current any more -
  // a row left reading its last commanded width would be claiming the droid is
  // holding a part it has just let go of (corrected on #365).
  assert.equal(env.cell("ledc:1", "outputs-bar").classList.contains("is-stale"), true, "the estop let go of every Output");
  assert.match(env.text("ledc:1", "outputs-us"), /Stopped — finding out/);

  // The firmware ended the move where it was and says so on the next answer;
  // that answer is current and repaints the row.
  env.outputs[0].commandedUs = 1560;
  env.outputs[0].targetUs = 1560;
  env.endNudge("ledc:0");
  await env.frame();
  assert.equal(env.cell("ledc:0", "outputs-bar").classList.contains("is-stale"), false);
  assert.equal(env.text("ledc:0", "outputs-us"), "1560 µs");
  assert.equal(env.nudges().length, 1, "the count went up, but there is no run to step on");
  assert.equal(env.findButton("doorRL").disabled, true, "and the button stays refused while the estop is latched");
});

test("a nudge the droid refuses ends the run and says why, and the Part stays not wired", async () => {
  const env = await bootParts();
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
  const env = await bootParts();
  env.pressFind("doorRL");
  await sleep(20);
  env.pressFind("doorRR");
  await sleep(20);

  assert.equal(env.nudges().length, 1);
  assert.match(env.feedback(), /One run at a time: Rear-left body door is being found/);
  assert.equal(env.runPanel().closest("[data-part]").dataset.part, "doorRL");
});

test("leaving Parts ends the run, and coming back sends nothing the builder did not press for", async () => {
  const env = await bootParts();
  env.pressFind("doorRL");
  await sleep(20);
  assert.equal(env.nudges().length, 1);

  env.navigate("#home");
  await sleep(180);
  assert.equal(env.document.body.dataset.page, "home", "the Dashboard is the mounted surface");
  env.endNudge("ledc:0");

  env.navigate("#parts");
  await sleep(180);
  await env.frame();
  assert.equal(env.nudges().length, 1, "the run ended when Parts was left; the count going up starts nothing");
  assert.equal(env.runPanel(), null);
  assert.equal(env.findButton("doorRL").hidden, false);
  assert.match(env.feedback(), /The run stopped when you left Parts/);
});

test("a frame repaints values on the nodes that are there; the button and the run are never rebuilt under the pointer", async () => {
  const env = await bootParts();
  const button = env.findButton("doorRL");
  env.pressFind("doorRL");
  await sleep(20);
  const panel = env.runPanel();
  env.outputs[1].commandedUs = 1750;
  await env.frame();
  assert.equal(env.findButton("doorRL"), button);
  assert.equal(env.runPanel(), panel);
  assert.equal(env.cell("ledc:1", "outputs-now").style.width, "75.0%");
});

test("the position bar no longer clips a mark at either end", () => {
  // The tick is 2 px centred on its mark; with the bar clipping its own
  // padding box, a mark at 0% or 100% showed half a tick (#362's inherited
  // find). The marks are bounded by the band already, so nothing else can
  // leave the bar, and the bar does not clip.
  const css = readCss();
  const bar = /\.outputs-bar\s*\{([^}]*)\}/.exec(css);
  assert.ok(bar, "the bar's rule is still there");
  assert.doesNotMatch(bar[1], /overflow\s*:\s*(hidden|clip)/);
  assert.match(bar[1], /position:\s*relative/);
});

// An Output an expander would add: no name the servo route takes, so never a
// candidate even with a pulse on it.
test("an Output without a name the servo route takes is not nudged", async () => {
  const outputs = freshOutputs();
  outputs.push(output("pca:0", "", { commandedUs: 1500, targetUs: 1500 }));
  const env = await bootParts({ outputs });
  env.pressFind("doorRL");
  await sleep(20);
  for (const address of ["ledc:0", "ledc:1", "ledc:3", "ledc:4"]) {
    env.endNudge(address);
    await env.frame();
  }
  assert.deepEqual(env.nudges().map((post) => post.form.arm), ["arm1", "arm2", "aux1", "aux2"]);
  assert.equal(env.runPanel(), null);
});
