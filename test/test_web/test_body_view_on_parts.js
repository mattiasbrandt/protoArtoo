// =============================================================================
// test/test_web/test_body_view_on_parts.js
//
// The body view where it actually runs: mounted on the shipped Parts surface,
// inside the shipped Operator Shell, against a fake droid (#352, ADR 0063).
//
// The renderer's own contract is held by test_body_view.js. What these hold
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

test("give it an Output routes to the row's own picker and writes nothing", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const before = env.posts.length;

  pick(env, "drawer");
  pressAct(env, "wire");

  assert.equal(env.posts.length, before, "a route is not a write");
  assert.strictEqual(env.document.activeElement, env.partRow("drawer").querySelector("select"));
  assert.match(env.feedback(), /Choose the output that moves Drawer/);
});

