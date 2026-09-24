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
    env.window.PAParts.moveFor(env.window.PAOutputs.list(), "doorFL", "ledc:3"),
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

// How an Output moves is set on its row (ADR 0052, #414), and an Output nobody
// has measured moves by none of it: its first move is a jump whatever its
// profile says. So its row can send no shape and no time - not from a control
// it hides, and not from an event that reaches the table anyway - while a
// measured Output's row saves under the field the firmware named for it.
test("an Output nobody has measured cannot send a shape, and a measured one saves it under the firmware's field", async () => {
  const outputs = freshOutputs();
  outputs[0].calibrated = true;
  const env = await bootServos({ outputs });
  const entry = (address) => Object.values(env.components).find((each) => each.address === address);
  const pickEase = (address, ease) =>
    env.region().fire("click", { target: env.row(address).querySelector(`[data-ease="${ease}"]`) });
  const typeThrow = (address, ms) => {
    const box = env.row(address).querySelector(".outputs-throw");
    box.value = String(ms);
    env.region().fire("change", { target: box });
  };

  pickEase("ledc:1", "overshoot");
  typeThrow("ledc:1", 800);
  await sleep(20);
  assert.equal(env.moves().length, 0, "the unmeasured Output asked the droid for nothing");

  pickEase("ledc:0", "overshoot");
  await sleep(20);
  typeThrow("ledc:0", 800);
  await sleep(20);
  assert.deepEqual(env.moves().map((post) => post.form), [
    { [entry("ledc:0").easeField]: "overshoot" },
    { [entry("ledc:0").throwField]: "800" },
  ]);
});

// What an Output does at power-up is set on the same row (ADR 0052, #414), and
// unlike how it moves it is NOT fenced by calibration: the two are separate
// decisions, and calibrating never changes it. So an unmeasured Output's row
// saves it, under the field the firmware named - never one the page made up.
test("what an Output does at power-up saves under the firmware's field, measured or not", async () => {
  const env = await bootServos({ outputs: freshOutputs() });
  const entry = (address) => Object.values(env.components).find((each) => each.address === address);

  env.region().fire("click", { target: env.row("ledc:1").querySelector('[data-boot="home-hold"]') });
  await sleep(20);

  assert.deepEqual(env.moves().map((post) => post.form), [{ [entry("ledc:1").bootField]: "home-hold" }]);
});

// An upgrade can narrow an Output's recorded ends into what its part takes, and
// the firmware alone knows which rows it did that to (#417): it reports the
// pair it narrowed from on that row, and stops once the builder saves the
// Output. The row says so only while the droid reports it, with the droid's
// numbers - the page never decides a narrowing of its own, from the band, the
// component or which Output it is.
test("a row shows narrowed ends only while the droid reports them, with the droid's numbers", async () => {
  const outputs = freshOutputs();
  outputs[1].narrowedFrom = { openUs: 2200, closeUs: 2100 };
  const env = await bootServos({ outputs });
  const note = (address) => env.cell(address, "outputs-narrowed");

  assert.equal(note("ledc:1").hidden, false, "the reported row says it was narrowed");
  assert.match(note("ledc:1").textContent, /2200\D+2100/, "with the pair the droid reported");
  for (const other of outputs.filter((each) => each.address !== "ledc:1")) {
    assert.equal(note(other.address).hidden, true, `${other.address} was not reported narrowed`);
  }

  env.outputs[1].narrowedFrom = null; // the builder saved that Output
  await env.frame();
  assert.equal(note("ledc:1").hidden, true, "the note goes when the droid stops reporting it");
});
