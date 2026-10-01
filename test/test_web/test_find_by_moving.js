// =============================================================================
// test/test_web/test_find_by_moving.js
//
// Find by Moving (ADR 0050, #363), run from a Part's row on Wiring since #411
// (data/find_by_moving.js). The shipped catalog, data/outputs.js,
// data/parts_mapping.js and the run module are loaded together, as a browser
// loads them; the droid, the surface's poll and the Live Reading are the only
// stand-ins. What is asserted is what the page asked the droid for, in what
// order, what the builder read, and what the estop did to both -- never a
// flag the code under test reports on itself. The row's act that starts a run
// is test_parts_table.js's.
//
// A run steps through the FREE Outputs: no Part on them, no light on their
// wire. They have no pulse - nothing drives an Output with no Part - and the
// firmware takes each one for the run when its nudge arrives (#411), so none
// of these droids gives a free Output a pulse before its nudge.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument } from "./helpers/mini_dom.js";
import { servoRow as output, withParts, describe } from "./helpers/fake_droid.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

// What a run that ends without an Output says of its Part.
const STILL_OFF = "Rear-left body door is still not on any output\\.";
const settle = async () => {
  for (let turn = 0; turn < 6; turn += 1) await new Promise((resolve) => setImmediate(resolve));
};

// ARM1 and ARM2 carry the utility arms; ARM3, ARM4 and ARM5 are free and limp,
// as a droid with two Parts wired comes up.
const twoArmsOn = () => withParts({ "ledc:0": ["utilUp"], "ledc:1": ["utilLo"] }).map((row) =>
  row.parts.length ? { ...row } : { ...row, wired: false, commandedUs: null, targetUs: null });

const boot = async ({ outputs = twoArmsOn(), estop = "clear" } = {}) => {
  const document = new MiniDocument();
  const panel = document.createElement("div");
  document.body.appendChild(panel);
  const env = { outputs, posts: [], said: [], moved: [], nudgeFails: null, nudgeLands: null };

  let reading = { estopLatched: estop === "latched", moveActsLive: estop === "clear" };
  const readers = [];
  let feed = null;
  let unmount = null;

  const window = {
    document,
    PALiveReading: {
      WAITING: "Waiting",
      UNKNOWN: "Unknown",
      slotText: (word) => (word === "Waiting" ? "" : word),
      subscribe: (fn) => {
        readers.push(fn);
        fn(reading);
      },
    },
    PASurface: {
      // The run's once-a-second read, handed to a test to fire as the frame.
      poll: (fn) => {
        feed = { fn, running: false };
        return {
          start: () => {
            feed.running = true;
          },
          stop: () => {
            feed.running = false;
          },
        };
      },
      holdUnmount: (decide) => {
        unmount = decide;
      },
    },
    PAApi: {
      get: async (path) => {
        assert.equal(path, "/api/servo/outputs");
        return { data: { outputs: structuredClone(env.outputs) } };
      },
      postForm: async (path, form) => {
        env.posts.push({ path, form: { ...form } });
        // A nudge still on its way: the droid has not answered it yet.
        if (form.action === "nudge" && env.nudgeLands) await env.nudgeLands;
        if (form.action === "nudge" && env.nudgeFails) throw env.nudgeFails;
        return { ok: true, data: { ok: true } };
      },
      messageFor: (error) => error.message,
    },
  };
  const context = { window, document, console, URLSearchParams, structuredClone, setTimeout, clearTimeout };
  context.globalThis = context;
  for (const file of ["droid_parts.js", "droid_part_kind.js", "outputs.js", "parts_mapping.js", "find_by_moving.js"]) {
    vm.runInNewContext(readData(file), context, { filename: file });
  }
  await window.PAOutputs.refresh();

  const finder = window.PAFindByMoving.runner({
    panel,
    say: (text, level) => env.said.push({ text, level }),
    move: (partId, address) => env.moved.push({ partId, address }),
    pending: () => null,
    surface: "Wiring",
  });

  env.find = (partId) => finder.start(partId);
  env.running = () => finder.running();
  env.nudges = () => env.posts.filter((post) => post.form.action === "nudge").map((post) => post.form.arm);
  env.releases = () => env.posts.filter((post) => post.form.action === "release").map((post) => post.form.arm);
  env.feedback = () => env.said.at(-1)?.text ?? "";
  env.runText = () => panel.querySelector(".parts-find-text")?.textContent ?? null;
  env.feedRunning = () => feed !== null && feed.running;
  // The droid's answer, as the run's own read brings it.
  env.frame = async () => {
    await feed.fn();
    await settle();
  };
  const row = (address) => env.outputs.find((each) => each.address === address);
  // The droid took the Output for the run: its pulse is on.
  env.taken = (address) => Object.assign(row(address), { commandedUs: 1500, targetUs: 1500, limp: "off" });
  // The droid says the nudge on that Output has ended.
  env.endNudge = (address) => {
    row(address).nudgesDone += 1;
  };
  env.wentLimp = (address, why) => Object.assign(row(address), { commandedUs: null, targetUs: null, limp: why });
  env.pushReading = (next) => {
    reading = next;
    readers.forEach((fn) => fn(reading));
  };
  env.leave = () => unmount?.();
  env.pressThatOne = () => panel.querySelector(".parts-find-that").fire("click", {});
  env.pressStop = () => panel.querySelector(".parts-find-stop").fire("click", {});
  return env;
};

// ---------------------------------------------------------------------------

test("a run nudges each free output in turn, limp or not, and steps on only when the droid says the nudge ended", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();

  assert.deepEqual(env.nudges(), ["ARM3"], "ARM1 and ARM2 carry Parts, so ARM3 is the first free Output");
  assert.equal(env.feedRunning(), true, "the run reads the droid while it goes");
  assert.match(env.runText(), /Nudging ARM3 \(1 of 3\)/);
  assert.match(env.runText(), /press That one when Rear-left body door moves/);

  // The droid has not taken ARM3 yet: it still reads limp, and that is not a
  // fault. Nothing more is sent until its nudge has ended.
  await env.frame();
  env.taken("ledc:3");
  await env.frame();
  assert.notEqual(env.running(), null, "a free Output read limp before it was taken does not end the run");
  assert.deepEqual(env.nudges(), ["ARM3"]);

  env.endNudge("ledc:3");
  await env.frame();
  assert.deepEqual(env.nudges(), ["ARM3", "ARM4"]);
  assert.match(env.runText(), /Nudging ARM4 \(2 of 3\)/);

  // A count that moved on some other Output is not this nudge ending.
  env.endNudge("ledc:3");
  await env.frame();
  assert.deepEqual(env.nudges(), ["ARM3", "ARM4"]);
  assert.equal(env.moved.length, 0, "nothing is wired: the builder has not said which one");
});

// Never a light's wire, and never an Output a Part is on: those move through
// their Part (#411). An expander's channel has no name the servo route takes,
// so it is never a candidate either.
test("a run never nudges a light's wire, an Output with a Part, or one the route cannot name", async () => {
  const outputs = describe(twoArmsOn(), { "ledc:4": { lightCapable: true, type: "rgb" } });
  outputs.push(output("pca:0", "", { commandedUs: 1500, targetUs: 1500 }));
  const env = await boot({ outputs });
  env.find("doorRL");
  await settle();
  env.endNudge("ledc:3");
  await env.frame();
  env.endNudge("ledc:5");
  await env.frame();

  assert.deepEqual(env.nudges(), ["ARM3", "ARM5"]);
  assert.equal(env.running(), null, "two free Outputs, two nudges");
  assert.match(env.feedback(), new RegExp(`None of the 2 free outputs moved Rear-left body door in one pass\\. ${STILL_OFF}`));
});

// A builder who presses Stop expects stillness now, not when the firmware's
// expiry lets go, so the run lets go of the Output under the nudge itself.
test("Stop lets go of the output under the nudge and sends nothing further", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  env.taken("ledc:3");
  await env.frame();

  env.pressStop();
  await settle();
  assert.equal(env.running(), null);
  assert.equal(env.feedRunning(), false, "the run's reads stop with it");
  assert.match(env.feedback(), new RegExp(`Stopped\\. ${STILL_OFF}`));
  assert.deepEqual(env.releases(), ["ARM3"]);
  assert.deepEqual(env.nudges(), ["ARM3"], "no next nudge is asked for");
});

// The pulse under the nudge is the run's, whatever landed on the Output since.
// If a Part lands on it while the run goes - from another tab, or the Console -
// Stop still lets go of it: the Part's Output waits for the droid's next start
// to be driven, so nothing but the run's hold was on it (#411 slice 4).
test("Stop lets go of the run's output even when a Part has landed on it meanwhile", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  env.taken("ledc:3");
  env.outputs.find((each) => each.address === "ledc:3").parts = ["doorFL"];
  await env.frame();

  env.pressStop();
  await settle();
  assert.equal(env.running(), null);
  assert.deepEqual(env.releases(), ["ARM3"]);
});

// Stop always wins over a nudge still on its way. The droid takes a free
// Output when the nudge reaches it and lets go in the order the two arrive, so
// a release that overtook the nudge would let go of nothing, and the nudge
// would then pulse after Stop. The release goes only once the nudge is answered.
test("Stop pressed while a nudge is on its way sends the release behind it", async () => {
  const env = await boot();
  let land;
  env.nudgeLands = new Promise((resolve) => {
    land = resolve;
  });
  env.find("doorRL");
  await settle();

  env.pressStop();
  await settle();
  assert.equal(env.running(), null);
  assert.deepEqual(env.releases(), [], "nothing overtakes the nudge");
  land();
  await settle();
  assert.deepEqual(
    env.posts.map((post) => `${post.form.action} ${post.form.arm}`),
    ["nudge ARM3", "release ARM3"]
  );
});

// "That one" is the row's own move, and the run lets go of the Output first:
// once the Part is on it, it is the Part's.
test("That one lets go of the output and puts the Part on it", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  env.pressThatOne();
  await settle();
  assert.deepEqual(env.releases(), ["ARM3"]);
  assert.deepEqual(env.moved, [{ partId: "doorRL", address: "ledc:3" }]);
});

test("with no free servo output it says so and sends nothing", async () => {
  const env = await boot({
    outputs: withParts({ "ledc:0": ["utilUp"], "ledc:1": ["utilLo"], "ledc:3": ["doorFL"], "ledc:4": ["doorFR"], "ledc:5": ["smallDoor"] }),
  });
  env.find("doorRL");
  await settle();
  assert.deepEqual(env.nudges(), []);
  assert.equal(env.running(), null);
  assert.match(env.feedback(), /Nothing to nudge\. There is no free servo output/);
});

test("a firmware that does not say when a nudge has ended is refused rather than waited on", async () => {
  const env = await boot({ outputs: twoArmsOn().map(({ nudgesDone, ...rest }) => rest) });
  env.find("doorRL");
  await settle();
  assert.deepEqual(env.nudges(), []);
  assert.match(env.feedback(), /does not say when a nudge has ended/);
});

// The firmware has already let go of every Output a run drove (ADR 0043), so a
// run the estop ends sends nothing more - no release, no next nudge.
test("the estop ends a run at once, and a latched estop starts none", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  env.pushReading({ estopLatched: true, moveActsLive: false });
  assert.equal(env.running(), null, "the run ended on the reading");
  assert.match(env.feedback(), new RegExp(`The estop stopped the run\\. ${STILL_OFF}`));
  assert.deepEqual(env.releases(), [], "the estop has already let go");

  env.find("doorRR");
  await settle();
  assert.deepEqual(env.nudges(), ["ARM3"], "nothing is nudged while the estop is latched");
});

// The droid holds a free Output for the run until a few seconds after its
// nudge (the dial's expiry, ADR 0064), then lets it go on its own. That is the
// run moving on, not a fault: a read that finds the Output limp AFTER its count
// went up steps the run on to the next Output rather than ending it.
test("an output the droid lets go after its nudge ended steps the run on", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  env.taken("ledc:3");
  await env.frame();
  env.endNudge("ledc:3");
  env.wentLimp("ledc:3", "off");
  await env.frame();
  assert.notEqual(env.running(), null, "the run goes on");
  assert.deepEqual(env.nudges(), ["ARM3", "ARM4"]);
});

// Pulses off, a dial's bound, anything that takes a pulse off a pin: an Output
// that goes limp AFTER the droid took it cannot twitch, so the run ends rather
// than waiting on a nudge that is not moving anything.
test("an output that goes limp under its nudge ends the run", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  env.taken("ledc:3");
  await env.frame();
  env.wentLimp("ledc:3", "pulses-off");
  await env.frame();
  assert.equal(env.running(), null);
  assert.match(env.feedback(), /ARM3 is limp, so the run stopped/);
  assert.deepEqual(env.nudges(), ["ARM3"]);
});

test("a nudge the droid refuses ends the run and says why", async () => {
  const env = await boot();
  env.nudgeFails = new Error("Servo command queue full");
  env.find("doorRL");
  await settle();
  assert.equal(env.running(), null);
  assert.match(env.feedback(), new RegExp(`did not reach the droid: Servo command queue full\\. ${STILL_OFF}`));
});

test("one run at a time: a second start is answered on the page, not sent to the droid", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  env.find("doorRR");
  await settle();
  assert.deepEqual(env.nudges(), ["ARM3"]);
  assert.match(env.feedback(), /One run at a time: Rear-left body door is being found/);
});

// Leaving Wiring ends a run: its reads stop with the surface (#360), so nothing
// could step it on, and a nudge on the way back would be motion nobody pressed
// for. The Output under the nudge is let go as Stop lets it go.
test("leaving Wiring ends the run and lets go of the output under the nudge", async () => {
  const env = await boot();
  env.find("doorRL");
  await settle();
  assert.equal(env.leave(), false, "leaving is never held");
  await settle();
  assert.equal(env.running(), null);
  assert.equal(env.feedRunning(), false);
  assert.deepEqual(env.releases(), ["ARM3"]);
  assert.match(env.feedback(), /The run stopped when you left Wiring/);
});
