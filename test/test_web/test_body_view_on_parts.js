// =============================================================================
// test/test_web/test_body_view_on_parts.js
//
// The droid picture where it actually runs: mounted on the shipped Parts
// surface, inside the shipped Operator Shell, against a fake droid (#352,
// #372, ADR 0063 as amended 2026-09-19).
//
// The renderer's own contract is held by test_body_view.js. What these hold
// is the thing that only exists once a caller is wired up: that the picture is
// painted from the SAME answer the two tables are painted from, that a click
// sends nothing, that a press sends exactly one command and no width, that the
// estop holds every move the picture can start, that a holoprojector is never
// offered one, and that every refusal names the builder's next move.
// =============================================================================
import test from "node:test";
import assert from "node:assert";

import { bootParts, withParts, output, sleep } from "./helpers/parts_surface.js";

const marker = (env, id) =>
  env.document.querySelectorAll("[data-marker]").find((node) => node.dataset.marker === id);
const panelTitle = (env) => env.document.querySelector(".bodyview-panel-title").textContent;
const panelWhy = (env) => env.document.querySelector(".bodyview-panel-why").textContent;
const actButton = (env, id) =>
  env.document.querySelectorAll("[data-act]").find((node) => node.dataset.act === id);
const pressAct = (env, id) =>
  env.document.querySelector(".bodyview-panel-acts").fire("click", { target: actButton(env, id) });
const pick = (env, markerId) =>
  env.document
    .querySelectorAll(".bv-svg")
    .find((svg) => svg.querySelectorAll("[data-marker]").some((node) => node.dataset.marker === markerId))
    .fire("click", { target: marker(env, markerId) });
const servoPosts = (env) => env.posts.filter((post) => post.path === "/api/servo");
const domePosts = (env) => env.posts.filter((post) => post.path === "/api/dome/cmd");
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

  const smallDoor = marker(env, "smallDoor");
  assert.ok(smallDoor.classList.contains("is-unassigned"));
  assert.equal(smallDoor.classList.contains("is-open"), false);
  assert.equal(smallDoor.classList.contains("is-closed"), false);
});

test("a click only selects: the panel fills and the droid is asked for nothing", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const before = env.posts.length;

  pick(env, "doorFL");
  pick(env, "dome-pp1");

  assert.equal(env.posts.length, before, "picking a part sends no request at all");
  assert.match(panelTitle(env), /Dome pie 1/);
  assert.ok(marker(env, "doorFL").classList.contains("is-selected"));
  assert.ok(marker(env, "dome-pp1").classList.contains("is-selected"));
});

test("Open it sends one command naming the output and the end, and no width", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  pick(env, "doorFL");

  assert.ok(facts(env).some((text) => text.includes("ARM1")), "the panel names the servo it is on");
  assert.equal(actButton(env, "toggle").disabled, false);

  pressAct(env, "toggle");
  await sleep(20);

  assert.equal(servoPosts(env).length, 1, "one press, one command");
  // Six tenths of the way to the open end draws Open, so the press closes it.
  assert.deepEqual(servoPosts(env)[0].form, { arm: "arm1", action: "close" });
  assert.equal("positionUs" in servoPosts(env)[0].form, false, "no width travels with it");
});

test("a part ganged to another says so when it moves them both", async () => {
  const outputs = measuredArm1();
  outputs.find((each) => each.address === "ledc:0").parts = ["doorFL", "doorFR"];
  const env = await bootParts({ outputs });

  pick(env, "doorFL");
  pressAct(env, "toggle");
  await sleep(20);

  assert.match(env.feedback(), /Right body door/, "the part sharing the lead is named");
});

test("the estop holds every move the picture can start, body and dome, and says which no it is", async () => {
  const env = await bootParts({ outputs: measuredArm1(), estop: true });

  ["doorFL", "dome-pp1"].forEach((id) => {
    pick(env, id);
    assert.equal(actButton(env, "toggle").disabled, true, `${id} is held`);
    assert.equal(actButton(env, "toggle").getAttribute("aria-disabled"), "true");
    assert.match(panelWhy(env), /Estop latched/);
    pressAct(env, "toggle");
  });
  await sleep(20);
  assert.equal(servoPosts(env).length, 0, "no servo command under a latched estop");
  assert.equal(domePosts(env).length, 0, "no dome command under a latched estop");

  // Cleared, and the act comes back without the builder picking again.
  env.pushStatus({ estop: false });
  await sleep(20);
  assert.equal(actButton(env, "toggle").disabled, false);
  pressAct(env, "toggle");
  await sleep(20);
  assert.deepEqual(domePosts(env).map((post) => post.form.cmd), [":OPP1"]);
});

test("a holoprojector is offered no Open at all, only its facts", async () => {
  const env = await bootParts({ outputs: measuredArm1() });

  pick(env, "dome-hp3");

  assert.equal(actButton(env, "toggle").hidden, true, "a pan and tilt device never opens");
  pressAct(env, "toggle");
  await sleep(20);
  assert.equal(domePosts(env).length, 0);
});

test("Give it an output routes to the row's own picker and writes nothing", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const before = env.posts.length;

  pick(env, "smallDoor");
  pressAct(env, "wire");

  assert.equal(env.posts.length, before, "a route is not a write");
  assert.strictEqual(env.document.activeElement, env.partRow("smallDoor").querySelector("select"));
  assert.match(env.feedback(), /Choose the output that moves Small long door/);
});

// What the droid holds as its Droid Build, changed the way a builder elsewhere
// would change it, and read back through the page's own seam.
const holdFitted = async (env, fitted) => {
  env.droidBuild.fitted = fitted;
  await env.window.DroidBuild.load({ refresh: true });
  await sleep(20);
};
const configPosts = (env) => env.posts.filter((post) => post.path === "/api/config");

test("Drop takes a Part off through the Droid Build alone, and leaves its Output mapped", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  pick(env, "doorFL");
  assert.equal(actButton(env, "fit").textContent, "Drop from build");

  pressAct(env, "fit");
  await sleep(40);

  const writes = env.posts.filter((post) => post.path !== "/api/servo/outputs");
  assert.equal(writes.length, 1, "one write, and nothing but the build");
  assert.equal(writes[0].path, "/api/config");
  assert.deepEqual(Object.keys(writes[0].form), ["fittedParts"]);
  assert.equal(env.droidBuild.fitted.includes("doorFL"), false);
  assert.deepEqual(env.outputs.find((each) => each.address === "ledc:0").parts, ["doorFL"], "the Output keeps the Part");
  assert.ok(marker(env, "doorFL").classList.contains("is-unfitted"));
  assert.match(env.feedback(), /Still mapped to ARM1/);
  assert.equal(actButton(env, "wire").hidden, false, "the way to change the Output is offered");
});

test("Drop takes a Common Addition off as the group it was fitted as", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  await holdFitted(env, env.droidBuild.fitted.concat(["gripArm", "gripClaw"]));

  pick(env, "gripArm");
  pressAct(env, "fit");
  await sleep(40);

  assert.equal(configPosts(env).length, 1);
  assert.equal(env.droidBuild.fitted.includes("gripArm"), false);
  assert.equal(env.droidBuild.fitted.includes("gripClaw"), false, "an arm does not leave its claw behind");
});

test("a build change the droid refuses is not left on the picture", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  await holdFitted(env, env.droidBuild.fitted.filter((id) => id !== "doorFL"));
  assert.ok(marker(env, "doorFL").classList.contains("is-unfitted"));

  env.configFails = new Error("refused");
  pick(env, "doorFL");
  pressAct(env, "fit");
  await sleep(40);

  assert.ok(marker(env, "doorFL").classList.contains("is-unfitted"), "the picture shows what the droid holds");
  assert.match(env.feedback(), /nothing was added/);
});
