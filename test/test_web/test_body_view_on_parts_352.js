// =============================================================================
// test/test_web/test_body_view_on_parts_352.js
//
// The body view where it actually runs: mounted on the shipped Parts surface,
// inside the shipped Operator Shell, against a fake droid (#352, ADR 0063).
//
// The renderer's own contract is held by test_body_view_352.js. What these hold
// is the thing that only exists once a caller is wired up: that the picture is
// painted from the SAME answer the two tables are painted from, that a click
// sends nothing, that a press sends exactly one command and no width, and that
// every refusal names the builder's next move rather than stopping at no.
// =============================================================================
import test from "node:test";
import assert from "node:assert";

import { bootParts, freshOutputs, withParts, output, sleep } from "./helpers/parts_surface.js";

const marker = (env, id) =>
  env.document.querySelectorAll("[data-marker]").find((node) => node.dataset.marker === id);
const panelTitle = (env) => env.document.querySelector(".bodyview-panel-title").textContent;
const panelWhy = (env) => env.document.querySelector(".bodyview-panel-why").textContent;
const actButton = (env, id) =>
  env.document.querySelectorAll("[data-act]").find((node) => node.dataset.act === id);
const pressAct = (env, id) =>
  env.document.querySelector(".bodyview-panel-acts").fire("click", { target: actButton(env, id) });
const pick = (env, markerId) =>
  env.document.querySelector(".bodyview-svg").fire("click", { target: marker(env, markerId) });
const travels = (env) =>
  env.posts.filter((post) => post.path === "/api/servo" && post.form.action === "travel");
const facts = (env) =>
  env.document.querySelectorAll(".bodyview-panel-facts")[0].childNodes.map((node) => node.textContent);

// ARM1 measured against its linkage and driving the left body door, part way
// through opening: the state a builder is most likely to be watching.
const measuredArm1 = () =>
  withParts({ "ledc:0": ["doorFL"] }, [
    output("ledc:0", "ARM1", {
      commandedUs: 1600,
      targetUs: 2000,
      openUs: 2000,
      closeUs: 1000,
      centreUs: 1500,
      calibrated: true,
    }),
    output("ledc:1", "ARM2", { commandedUs: 1500, targetUs: 1500 }),
    output("ledc:3", "AUX1", { commandedUs: 1500, targetUs: 1500 }),
  ]);

test("the picture draws the same droid the tables below it do", async () => {
  const env = await bootParts({ outputs: measuredArm1() });

  const door = marker(env, "doorFL");
  assert.ok(door, "the left body door is on the picture");
  assert.ok(door.classList.contains("is-driven"), "an output drives it");
  assert.ok(door.classList.contains("has-position"), "and it has a position to draw");
  // Part way through a move, commanded - never a reading taken from the servo.
  assert.match(door.getAttribute("aria-label"), /part way through its travel/);
  assert.match(door.getAttribute("aria-label"), /ARM1/);

  // The same one answer the output-first row was painted from.
  assert.match(env.text("ledc:0", "outputs-us"), /1600/);

  const summary = env.document.getElementById("bodyview-summary").textContent;
  assert.match(summary, /parts on the picture/);
  assert.match(summary, /1 with an output driving it/);
});

test("a part nothing drives makes no claim about where it is", async () => {
  const env = await bootParts({ outputs: measuredArm1() });

  const drawer = marker(env, "drawer");
  assert.ok(drawer.classList.contains("is-undriven"));
  assert.equal(drawer.classList.contains("has-position"), false);
  assert.match(drawer.getAttribute("aria-label"), /nothing drives it yet/);
});

test("a click only selects: the panel fills and the droid is asked for nothing", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const before = env.posts.length;

  pick(env, "doorFL");

  assert.equal(env.posts.length, before, "picking a part sends no request at all");
  assert.match(panelTitle(env), /Left body door/);
  assert.ok(marker(env, "doorFL").classList.contains("is-selected"));
  assert.deepEqual(
    facts(env).filter((text) => text.includes("ARM1")),
    ["ARM1"],
    "the panel says what drives it"
  );
});

test("move it sends one command naming the output, and no width", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  pick(env, "doorFL");

  assert.equal(actButton(env, "move").disabled, false);
  assert.match(panelWhy(env), /out to its open end, across to its close end and back/);

  pressAct(env, "move");
  await sleep(20);

  assert.equal(travels(env).length, 1, "one press, one command - the droid runs the out-and-back");
  assert.deepEqual(travels(env)[0].form, { arm: "arm1", action: "travel" });
  assert.equal("positionUs" in travels(env)[0].form, false, "no width travels with it");
  assert.match(env.feedback(), /running through its travel and back/);
});

test("a part ganged to another says so before it moves them both", async () => {
  const outputs = measuredArm1();
  outputs.find((each) => each.address === "ledc:0").parts = ["doorFL", "doorFR"];
  const env = await bootParts({ outputs });

  pick(env, "doorFL");
  pressAct(env, "move");
  await sleep(20);

  assert.match(env.feedback(), /Right body door/, "the part sharing the lead is named");
});

test("move it is refused under a latched estop, and says which no it is", async () => {
  const env = await bootParts({ outputs: measuredArm1(), estop: true });
  pick(env, "doorFL");

  assert.equal(actButton(env, "move").disabled, true);
  assert.equal(actButton(env, "move").getAttribute("aria-disabled"), "true");
  assert.match(panelWhy(env), /estop is latched/);

  pressAct(env, "move");
  await sleep(20);
  assert.equal(travels(env).length, 0);

  // Cleared, and the act comes back without the builder picking again.
  env.pushStatus({ estop: false });
  await sleep(20);
  assert.equal(actButton(env, "move").disabled, false);
  assert.match(panelWhy(env), /out to its open end/);
});

test("every refusal names the builder's next move rather than stopping at no", async () => {
  const unwired = withParts({}, [
    output("ledc:0", "ARM1", { commandedUs: 1500, targetUs: 1500 }),
  ]);
  const env = await bootParts({ outputs: unwired });

  pick(env, "doorFL");
  assert.equal(actButton(env, "move").disabled, true);
  assert.match(panelWhy(env), /give it an output first/i);

  // Wired, but nobody has measured its ends: a different no with a different
  // move, and the one the calibration dial below answers.
  env.outputs[0].parts = ["doorFL"];
  await env.frame();
  assert.equal(actButton(env, "move").disabled, true);
  assert.match(panelWhy(env), /ends are not measured yet/);
  assert.match(panelWhy(env), /Calibrate/);
  assert.ok(marker(env, "doorFL").classList.contains("is-unmeasured"));
  assert.equal(marker(env, "doorFL").classList.contains("has-position"), false);
});

test("give it an Output routes to the row's own picker and writes nothing", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const before = env.posts.length;

  pick(env, "drawer");
  pressAct(env, "wire");

  assert.equal(env.posts.length, before, "a route is not a write");
  assert.strictEqual(env.document.activeElement, env.partRow("drawer").querySelector("select"));
  assert.match(env.feedback(), /Choose the output that moves Drawer/);
});

test("add it to the build is a named act, and the droid keeps the answer", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  // The device holds a build with nothing fitted, so every part is one the
  // builder has not said they have yet.
  pick(env, "drawer");
  assert.equal(actButton(env, "fit").disabled, false);
  assert.ok(facts(env).includes("not yet"));

  pressAct(env, "fit");
  await sleep(20);

  const fits = env.posts.filter((post) => post.path === "/api/config" && "fittedParts" in post.form);
  assert.equal(fits.length, 1);
  assert.deepEqual(fits[0].form.fittedParts.split(","), ["drawer"]);
  assert.match(env.feedback(), /is on your droid now/);
  // And the act refuses itself afterwards: there is nothing left to add.
  assert.equal(actButton(env, "fit").disabled, true);
  assert.ok(facts(env).includes("yes"));
});

test("a holoprojector is one marker, and the panel offers the Parts it stands for", async () => {
  const env = await bootParts({ outputs: measuredArm1() });

  assert.equal(marker(env, "hp1Pan"), undefined, "an axis has no marker of its own");
  pick(env, "holoprojectors:1");

  assert.match(panelTitle(env), /Holoprojector 1 pan/);
  assert.match(panelTitle(env), /Holoprojector 1 tilt/);
  const terms = facts(env);
  assert.ok(terms.some((text) => text.includes("HP1-1")), "each Part gets its own line");
  assert.ok(terms.some((text) => text.includes("HP1-2")));
});

test("the selection survives the bench feed, once a second, forever", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  pick(env, "doorFL");
  const cell = marker(env, "doorFL");

  for (let tick = 0; tick < 4; tick += 1) {
    env.outputs[0].commandedUs = 1600 + tick * 100;
    await env.frame();
  }

  assert.ok(marker(env, "doorFL").classList.contains("is-selected"));
  assert.strictEqual(marker(env, "doorFL"), cell, "repainted in place, never rebuilt");
  assert.match(panelTitle(env), /Left body door/, "and the panel is still the one the builder opened");
});

test("the spare slots are not on the picture, because the droid cannot place them", async () => {
  const env = await bootParts({ outputs: freshOutputs() });

  assert.equal(marker(env, "other1"), undefined);
  // They are still a row in the table below, which is where a builder meets
  // them - not silently gone from the surface altogether.
  assert.ok(env.partRow("other1"), "other1 has a row");
});
