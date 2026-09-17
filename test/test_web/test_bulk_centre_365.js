// =============================================================================
// test/test_web/test_bulk_centre_365.js
//
// Back to centre (#318, #365): one press on the output-first table, and the
// droid paces the sweep itself.
//
// What this page is responsible for is small and exact, which is the point.
// It must send ONE request and no more - a page that sent one per Output, or
// that spaced them itself, would be holding a pace a hand-edited or imported
// client could walk around, which is the shape ADR 0043 and ADR 0049 refused.
// It must count the rows with nothing to centre off the signal the row already
// carries. And it must stop showing commanded positions as current once the
// estop has ended the run.
//
// The pace, the order and the Cadence Floor are the controller's and are not
// asserted here: they are proven natively, in
// test/test_native/test_sequence_bulk_centre.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { bootParts, freshOutputs, withParts, output, sleep } from "./helpers/parts_surface.js";

// ---------------------------------------------------------------------------

test("one press sends one request, whatever the droid has on it", async () => {
  const env = await bootParts();

  env.pressCentre();
  await sleep(20);

  assert.deepEqual(
    env.centreRequests().map((post) => post.form),
    [{}],
    "the press is the whole request: no arm, no width, no pace - all of that is the controller's",
  );
  assert.equal(
    env.posts.filter((post) => post.path === "/api/servo").length,
    0,
    "nothing is sent down the per-output route: a bulk centre is not five presses",
  );
});

test("the line counts the outputs going back and the lights skipped", async () => {
  // Two rows driving lights only, three that move. The light rows are told by
  // the same signal the calibration dial's own acts are told by.
  const outputs = withParts({
    "ledc:0": ["doorFL"],
    "ledc:1": ["magicPanel"],
    "ledc:3": ["psiFront"],
    "ledc:4": ["utilUp"],
  });
  const env = await bootParts({ outputs });

  env.pressCentre();
  await sleep(20);

  assert.equal(
    env.centreSaid(),
    "3 outputs are going back to centre, one at a time. 2 skipped — a light has no centre.",
  );
  assert.match(env.centreSaidLevel(), /success/);
});

test("with nothing to skip the line says only what is going back", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["doorFL"] }) });

  env.pressCentre();
  await sleep(20);

  assert.equal(env.centreSaid(), "5 outputs are going back to centre, one at a time.");
});

test("a single output reads as one, not as one outputs", async () => {
  const env = await bootParts({ outputs: [output("ledc:0", "ARM1", { commandedUs: 1500, targetUs: 1500 })] });

  env.pressCentre();
  await sleep(20);

  assert.equal(env.centreSaid(), "1 output is going back to centre, one at a time.");
});

test("a request the droid did not take says so, and claims nothing moved", async () => {
  const env = await bootParts();
  env.centreFails = new Error("Device unavailable");

  env.pressCentre();
  await sleep(20);

  assert.equal(env.centreSaid(), "Nothing is going back to centre: Device unavailable");
  assert.match(env.centreSaidLevel(), /error/);
});

test("the act is refused while the estop is latched, and live once it is clear", async () => {
  const env = await bootParts({ estop: true });
  await sleep(20);

  assert.equal(env.centreButton().disabled, true);
  assert.equal(env.centreButton().getAttribute("aria-disabled"), "true");

  env.pushStatus({ estop: false });
  await sleep(20);

  assert.equal(env.centreButton().disabled, false);
  assert.equal(env.centreButton().getAttribute("aria-disabled"), "false");
});

test("a press on the refused button asks the droid for nothing", async () => {
  const env = await bootParts({ estop: true });
  await sleep(20);

  env.pressCentre();
  await sleep(20);

  assert.equal(env.centreRequests().length, 0);
});

test("the estop ends the run, and no row goes on showing a commanded position as current", async () => {
  const env = await bootParts();
  env.pressCentre();
  await sleep(20);
  // The controller got two Outputs to their centres before the estop.
  env.centred("ledc:0");
  env.centred("ledc:1");
  await env.frame();
  assert.match(env.text("ledc:0", "outputs-us"), /µs/, "a centred Output reads its width before the estop");

  env.pushStatus({ estop: true });
  await sleep(20);

  // Every enabled Output has been released (ADR 0043), so none of their marks
  // is current - not the two the sweep reached, and not the ones it never did.
  ["ledc:0", "ledc:1", "ledc:3", "ledc:4", "ledc:5"].forEach((address) => {
    assert.equal(
      env.text(address, "outputs-us"),
      "Stopped — finding out where it is",
      `${address} must stop reading as a live commanded position`,
    );
    assert.equal(env.cell(address, "outputs-bar").className.includes("is-stale"), true);
  });
  assert.match(env.centreSaid(), /^The estop let go of every output\./);
  assert.match(env.centreSaidLevel(), /error/);
});

test("the droid's next answer is current again, and the marks come back", async () => {
  const env = await bootParts();
  env.pressCentre();
  await sleep(20);
  env.pushStatus({ estop: true });
  await sleep(20);
  assert.equal(env.text("ledc:0", "outputs-us"), "Stopped — finding out where it is");

  // The estop released every Output, and the droid now says so.
  env.outputs.forEach((row) => {
    row.commandedUs = null;
    row.targetUs = null;
    row.limp = "estop";
  });
  await env.frame();

  assert.equal(env.text("ledc:0", "outputs-us"), "— off");
  assert.equal(env.text("ledc:0", "outputs-release"), "Limp - the estop let go");
  assert.equal(env.cell("ledc:0", "outputs-bar").className.includes("is-stale"), false);
});

test("pressing again sends one more request and never a queue of them", async () => {
  const env = await bootParts();

  env.pressCentre();
  await sleep(20);
  env.pressCentre();
  await sleep(20);

  assert.equal(env.centreRequests().length, 2, "each press is one request; the controller decides what a second one means");
});
