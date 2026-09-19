// =============================================================================
// test/test_web/test_outputs_table.js
//
// The output-first table (#362, ADR 0050), on Servos since #412, run for real:
// the shipped shell fetches the shipped servo.html and runs its chain against a
// fake droid (helpers/parts_surface.js) that answers GET /api/servo/outputs and
// applies a POST /api/config move the way the firmware does. A frame is the
// page's own bench feed firing; what is asserted is what a builder sees, the
// nodes it is drawn on, and what the page asked the droid for.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { bootServos, freshOutputs, withParts, sleep } from "./helpers/parts_surface.js";

// ---------------------------------------------------------------------------

test("taking a Part off another Output from Servos is asked in the part-first table's own words", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:0": ["doorFL", "doorFR"], "ledc:3": ["utilUp"] }) });
  const expected = env.window.PAParts.announcement(
    env.window.PAParts.moveFor(structuredClone(env.outputs), "doorFL", "ledc:3"),
  );

  const picker = env.pickOnOutput("ledc:3", "doorFL");
  assert.equal(env.posts.length, 0, "nothing reaches the droid before the builder answers");
  assert.equal(env.dialog.open, true);
  assert.equal(env.byId("outputs-move-title").textContent, expected.title);
  assert.equal(env.byId("outputs-move-body").textContent, expected.body);
  assert.equal(
    env.byId("outputs-move-body").textContent,
    "Left body door is on ARM1. Move it to ARM3 and unwire it from ARM1? " +
      "ARM1 keeps driving Right body door. Upper utility arm is on ARM3 too — they will move together.",
  );
  assert.equal(picker.value, "", "the picker goes back to its prompt");

  env.answerMove(true);
  await sleep(20);
  assert.deepEqual(env.posts, [
    { path: "/api/config", form: { movePart: "doorFL", movePartFrom: "ledc:0", movePartTo: "ledc:3" } },
  ]);
  assert.equal(env.text("ledc:3", "outputs-parts"), "Upper utility arm, Left body door");
  assert.equal(env.text("ledc:0", "outputs-parts"), "Right body door");
});

test("cancelling from this table sends nothing and leaves both Outputs as they were", async () => {
  const env = await bootServos({ outputs: withParts({ "ledc:0": ["doorFL"] }) });

  env.pickOnOutput("ledc:4", "doorFL");
  assert.equal(env.dialog.open, true);
  env.answerMove(false);
  await sleep(20);
  assert.equal(env.posts.length, 0);
  assert.equal(env.dialog.open, false);
  assert.equal(env.text("ledc:0", "outputs-parts"), "Left body door");
  assert.equal(env.text("ledc:4", "outputs-parts"), "– not wired –");
});

test("one read of the droid's answer a second feeds the table, and stops when Servos is left", async () => {
  const env = await bootServos();

  const before = env.gets.get("/api/servo/outputs");
  await env.frame();
  assert.equal(env.gets.get("/api/servo/outputs"), before + 1, "one request a frame");

  // The shell's status plate ticks once a second too, and it is chrome that
  // never stops, so the feed is found by what its tick asks the droid for.
  let feed = null;
  for (const timer of env.intervals.filter((each) => each.ms === 1000)) {
    const asked = env.gets.get("/api/servo/outputs");
    timer.fn();
    await sleep(20);
    if (env.gets.get("/api/servo/outputs") === asked + 1) feed = timer;
  }
  assert.ok(feed, "the bench feed asks once a second while Servos is on screen");
  env.window.PASurface.showing("dashboard");
  assert.ok(env.cleared.includes(feed.id), "and it stops asking when the operator leaves Servos");
});

test("a firmware that reports no position is not shown as an Output with no pulse", async () => {
  const outputs = freshOutputs().map(({ address, name, parts }) => ({ address, name, parts }));
  outputs[2].parts = ["utilUp"];
  const env = await bootServos({ outputs });

  assert.equal(env.text("ledc:3", "outputs-us"), "Not reported by this firmware");
  assert.equal(env.tier("switched-off"), "Wired but switched off — 0 outputs");
  assert.equal(env.tier("driving"), "Driving parts — 1 output");
});
