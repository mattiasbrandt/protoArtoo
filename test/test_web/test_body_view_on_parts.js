// =============================================================================
// test/test_web/test_body_view_on_parts.js
//
// The droid picture where it actually runs: mounted on the shipped Parts
// surface, inside the shipped Operator Shell, against a fake droid (#352,
// #372, ADR 0063 as amended 2026-09-19).
//
// The renderer's own contract is held by test_body_view.js. What these hold
// is the thing that only exists once a caller is wired up: that the picture is
// painted from the SAME answer the Unused list and Servos are painted from,
// that "Give it an output" and each Unused row reach the one picker on Wiring
// (#411), that a click
// sends nothing, that a press sends exactly one command and no width, that the
// estop holds every move the picture can start, that a holoprojector is never
// offered one, and that every refusal names the builder's next move.
// =============================================================================
import test from "node:test";
import assert from "node:assert";

import { bootParts, bootServos, withParts, output, sleep } from "./helpers/parts_surface.js";

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
    output("ledc:3", "ARM3", { commandedUs: 1500, targetUs: 1500 }),
  ]);

test("a part nothing drives makes no claim about where it is", async () => {
  const env = await bootParts({ outputs: measuredArm1() });

  const smallDoor = marker(env, "smallDoor");
  assert.ok(smallDoor.classList.contains("is-unassigned"));
  assert.equal(smallDoor.classList.contains("is-open"), false);
  assert.equal(smallDoor.classList.contains("is-closed"), false);
});

// The dome reports nothing back, so a dome piece this page has not told
// anything has no known position. It drew Closed until #417 - a position nobody
// said. Once told, it draws what it was told, which is all anybody knows.
test("a dome piece nobody has told draws no position, and one told draws what it was told", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const pie = () => marker(env, "dome-pp1");

  assert.ok(pie().classList.contains("is-unknown"), "an untold dome piece draws no state");
  assert.equal(pie().classList.contains("is-closed"), false, "and not Closed");

  pick(env, "dome-pp1");
  pressAct(env, "toggle");
  await sleep(20);
  assert.equal(domePosts(env).length, 1);
  assert.ok(pie().classList.contains("is-open"), "told to open, it draws Open");
});

// One Output, one answer (#421): a gauge and a dial disagreeing about the same
// servo is the worst outcome a two-surface mapping can have. Until #421 Parts
// showed a limp Output as a bare Limp while Servos named why, from its own
// table.
test("a limp Part says why in the same words Servos uses for its Output", async () => {
  const limp = () => measuredArm1().map((row) =>
    (row.address === "ledc:0" ? { ...row, commandedUs: null, targetUs: null, limp: "expiry" } : row));
  const servos = await bootServos({ outputs: limp() });
  const said = servos.text("ledc:0", "outputs-release");
  assert.ok(said.length > 0, "Servos says why the Output is limp");

  const env = await bootParts({ outputs: limp() });
  pick(env, "doorFL");
  assert.ok(marker(env, "doorFL").classList.contains("is-limp"), "the picture draws it limp");
  assert.ok(facts(env).some((text) => text.includes(said)),
    `the panel says what Servos says ("${said}"): ${facts(env).join(" | ")}`);
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
  assert.deepEqual(servoPosts(env)[0].form, { arm: "ARM1", action: "close" });
  assert.equal("positionUs" in servoPosts(env)[0].form, false, "no width travels with it");
});

test("a part ganged to another says so when it moves them both", async () => {
  const outputs = measuredArm1();
  outputs.find((each) => each.address === "ledc:0").parts = ["doorFL", "doorFR"];
  const env = await bootParts({ outputs });

  pick(env, "doorFL");
  pressAct(env, "toggle");
  await sleep(20);

  assert.match(env.feedback(), /Right body door/, "the part sharing the wire is named");
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

// An Output the droid does not drive since it started (#364) - wired after
// boot - is refused by POST /api/servo whatever is pressed. The picture decides
// that before anything is sent, in the route's own words, and sends nothing.
test("a Part on an Output nothing drives is offered no Open, and says what drives it", async () => {
  const outputs = measuredArm1();
  Object.assign(outputs.find((each) => each.address === "ledc:0"), { driven: false, activeWired: false });
  const env = await bootParts({ outputs });

  pick(env, "doorFL");
  assert.equal(actButton(env, "toggle").disabled, true, "the toggle is refused");
  assert.equal(panelWhy(env), "Restart the droid to drive ARM1.");
  pressAct(env, "toggle");
  await sleep(20);
  assert.equal(servoPosts(env).length, 0, "and nothing reaches the droid");
});

test("a holoprojector is offered no Open at all, only its facts", async () => {
  const env = await bootParts({ outputs: measuredArm1() });

  pick(env, "dome-hp3");

  assert.equal(actButton(env, "toggle").hidden, true, "a pan and tilt device never opens");
  pressAct(env, "toggle");
  await sleep(20);
  assert.equal(domePosts(env).length, 0);
});

// The one picker is Wiring's (operator, 2026-09-28 on #411). A Part asked
// for from Parts arrives with the cursor in its own row there, and the route
// itself asks the droid to change nothing.
const landsOnPicker = async (env, partId) => {
  const deadline = Date.now() + 3000;
  const select = () => env.partsRegion()?.querySelectorAll("[data-part]").find((row) => row.dataset.part === partId)?.querySelector("select");
  while (env.document.activeElement !== select() || !select()) {
    if (Date.now() > deadline) break;
    await sleep(5);
  }
  assert.equal(env.window.location.hash, "#wiring", "the route goes to Wiring");
  assert.ok(select(), `Wiring's picker has a row for ${partId}`);
  assert.strictEqual(env.document.activeElement, select(), "the cursor is in that Part's own row");
};

test("Give it an output routes to the Part's row in the picker on Wiring and writes nothing", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const before = env.posts.length;

  pick(env, "smallDoor");
  pressAct(env, "wire");
  await landsOnPicker(env, "smallDoor");

  assert.equal(env.posts.length, before, "a route is not a write");
  assert.match(env.wiringFeedback(), /Choose the output that moves Small long door/);
});

// ---------------------------------------------------------------------------
// Unused: the Parts no Output claims (moved here from Wiring, #411)
// ---------------------------------------------------------------------------

// Whether an Output is wired is a fact about the droid's wiring, and whether a
// Part is on one is the mapping's: a Part on an Output nobody has marked wired
// is still claimed. The Availability Reason rides each row.
test("a part on an output is not Unused, whether or not the output is wired", async () => {
  const outputs = withParts({ "ledc:0": ["doorFL"], "ledc:1": ["utilUp"] }, [
    output("ledc:0", "ARM1", { commandedUs: 1500, targetUs: 1500 }),
    output("ledc:1", "ARM2", { commandedUs: 1500, targetUs: 1500 }),
  ]);
  const env = await bootParts({ outputs, say: { "ledc:1": { wired: false } } });
  const unused = env.unusedRows().map((row) => row.dataset.part);

  assert.ok(!unused.includes("doorFL"), "a part on a wired output is claimed");
  assert.ok(!unused.includes("utilUp"), "and so is one on an output not wired");
  const row = env.unusedRows().find((each) => each.dataset.part === "smallDoor");
  assert.ok(row, "a part on no output is Unused");
  assert.equal(row.dataset.tier, "part-not-assigned", "with the reason the droid reports for it");
});

// Each row acts rather than pointing, and it is the picture's act: the same
// route, to the same row of the same picker.
test("an Unused row's act routes to the Part's row in the picker on Wiring and writes nothing", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const before = env.posts.length;

  env.pressUnused("smallDoor");
  await landsOnPicker(env, "smallDoor");

  assert.equal(env.posts.length, before, "a route is not a write");
  assert.match(env.wiringFeedback(), /Choose the output that moves Small long door/);
});

// What the droid holds as its Droid Build, changed the way a builder elsewhere
// would change it, and read back through the page's own seam.
const holdFitted = async (env, fitted) => {
  env.droidBuild.fitted = fitted;
  await env.window.DroidBuild.load({ refresh: true });
  await sleep(20);
};
const configPosts = (env) => env.posts.filter((post) => post.path === "/api/config");

// Parts draws the built-in dome by the same rule as the Dashboard's dome card:
// MK4.1's dome is drawn as the MK4 Complex dome its seeds copy. Each page once
// carried its own copy of that rule, and on the #355 bench an MK4.1 dome had
// no dome pieces on either (#409). A dome the drawing is not of still has none.
test("a dome drawn as the built-in design keeps its dome pieces, and another dome does not", async () => {
  const env = await bootParts({ outputs: measuredArm1() });
  const domeDrawn = () =>
    !env.document.querySelector(".bv-dome-pieces").classList.contains("is-absent") &&
    env.document.querySelector(".bv-dome-note").hidden;
  const holdDome = async (design, variant) => {
    env.droidBuild.domeDesign = design;
    env.droidBuild.domeVariant = variant;
    await env.window.DroidBuild.load({ refresh: true });
    await sleep(20);
  };

  await holdDome("mk41", "");
  assert.equal(domeDrawn(), true, "an MK4.1 dome is drawn as MK4 Complex");

  await holdDome("own", "");
  assert.equal(domeDrawn(), false, "a dome the drawing is not of is not drawn as theirs");
});

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
