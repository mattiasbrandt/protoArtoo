// =============================================================================
// test/test_web/test_live_reading.js
//
// The Live Reading (data/live_reading.js, CONTEXT.md "Live Reading"): the one
// place that decides what the droid has reported, for the shell and every
// surface (#419).
//
// Runs the shipped status_stream.js and live_reading.js together, on the
// bootstrap's real background poll (data/page_bootstrap.js PART 1). Timers are
// recorded, not run, so a test fires the poll's tick itself and nothing here
// can pass by timing out.
// =============================================================================
import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { statusFrame } from "./helpers/fake_droid.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const readData = (name) => readFileSync(join(__dirname, "../../data", name), "utf-8");
const bootstrapFile = readData("page_bootstrap.js");
const part1Src = bootstrapFile.substring(
  bootstrapFile.indexOf("(() => {"),
  bootstrapFile.indexOf("// =========================== PART 2"),
);

// `stream` gives the context an EventSource, so the reading takes the stream;
// without one it runs its fallback poll. `answer()` is GET /api/status.
const boot = ({ stream = false, answer = () => statusFrame() } = {}) => {
  const intervals = [];
  const cleared = [];
  const reads = [];
  const sources = [];
  const document = { visibilityState: "visible", listeners: [], addEventListener(type, fn) { this.listeners.push({ type, fn }); }, removeEventListener() {} };
  const window = {
    PAAssetsReady: true,
    PAApi: {
      get: async (path) => {
        reads.push(path);
        return { data: await answer() };
      },
    },
    setInterval: (fn, ms) => intervals.push({ id: intervals.length + 1, fn, ms }),
    clearInterval: (id) => cleared.push(id),
    setTimeout: () => 0,
    clearTimeout: () => {},
    addEventListener() {},
  };
  const context = { window, document, console: { warn() {}, error() {}, log() {} }, Math, JSON, Date, Object, Set, Promise, Error };
  if (stream) {
    context.EventSource = class {
      constructor() {
        this.handlers = {};
        sources.push(this);
      }
      addEventListener(type, fn) {
        this.handlers[type] = fn;
      }
      close() {}
    };
  }
  context.globalThis = context;
  vm.createContext(context);
  vm.runInContext(part1Src, context);
  vm.runInContext(readData("status_stream.js"), context);
  vm.runInContext(readData("live_reading.js"), context);
  const live = window.PALiveReading;
  live.start();

  const seen = [];
  return {
    live,
    reads,
    intervals,
    cleared,
    document,
    seen,
    listen: () => live.subscribe((reading) => seen.push(reading)),
    push: (frame) => window.PAStatusStream.seed(frame),
    dropStream: () => sources[sources.length - 1].onerror(),
    settle: () => new Promise((resolve) => setImmediate(resolve)),
  };
};

const withoutField = (field, changes) => {
  const frame = statusFrame(changes);
  delete frame[field];
  return frame;
};

test("a frame missing a core field is ignored, and the last reading stands, estop included", () => {
  const env = boot({ stream: true });
  env.push(statusFrame({ estop: true, speedLimitMax: 600 }));
  assert.equal(env.live.current().estop, "latched");

  // Each of the six alone: a frame without it is not one a reading is made of.
  for (const field of ["estop", "sbusHwFailsafe", "sbusSignalLost", "webDriveExpired", "webControlEnabled", "sleepMode"]) {
    env.push(withoutField(field, { estop: false, speedLimitMax: 350 }));
    const reading = env.live.current();
    assert.equal(reading.estop, "latched", `a frame without ${field} does not release a latch`);
    assert.equal(reading.status.speedLimitMax, 600, `a frame without ${field} is not read at all`);
    assert.equal(reading.notHearing, "frame", "and it is told apart from a lost link");
  }

  // And never read as clear when it is the first thing that arrives.
  const fresh = boot({ stream: true });
  fresh.push(withoutField("estop"));
  assert.equal(fresh.live.current().estop, "finding-out");
  assert.equal(fresh.live.current().moveActsLive, false);
});

test("before the first good frame every field is Finding out, and nothing may move", () => {
  const env = boot({ stream: true });
  const reading = env.live.current();

  assert.equal(reading.status, null);
  assert.equal(reading.estop, "finding-out");
  assert.equal(reading.moveActsLive, false);
  assert.equal(reading.estopLatched, false, "and no latch is offered for release either");
  assert.equal(reading.word("estop"), env.live.FINDING_OUT);
  assert.equal(reading.word("firmwareVersion"), env.live.FINDING_OUT);
});

test("a field the frames that arrive do not carry is Unknown, and a carried one has no word", () => {
  const env = boot({ stream: true });
  env.push(statusFrame({ firmwareVersion: "1.2.3" }));
  const reading = env.live.current();

  assert.equal(reading.word("heapLargestBlock"), env.live.UNKNOWN);
  assert.equal(reading.answer("heapLargestBlock"), "unknown");
  assert.equal(reading.word("firmwareVersion"), null);
  assert.equal(reading.answer("firmwareVersion"), "heard");
});

test("losing contact turns the estop back to Finding out and keeps every other field", () => {
  const env = boot({ stream: true });
  env.listen();
  env.push(statusFrame({ estop: false, speedLimitMax: 600 }));
  assert.equal(env.live.current().moveActsLive, true);

  env.dropStream();
  let reading = env.live.current();
  assert.equal(reading.estop, "finding-out", "nobody knows the estop is still clear");
  assert.equal(reading.moveActsLive, false);
  assert.equal(reading.status.speedLimitMax, 600, "the other fields keep their last value");
  assert.equal(reading.notHearing, "link");
  assert.equal(env.seen.at(-1), reading, "and every listener was told");

  // A bad frame is the droid answering, but not with a reading: the estop is
  // still not known.
  env.push(withoutField("sleepMode"));
  assert.equal(env.live.current().estop, "finding-out");

  // Only a frame the droid has just sent brings it back.
  env.push(statusFrame({ estop: false }));
  reading = env.live.current();
  assert.equal(reading.estop, "clear");
  assert.equal(reading.notHearing, null);
});

test("with no stream, one poll runs while anybody listens, skips a hidden tab, and stops when nobody does", async () => {
  const env = boot({ stream: false });
  assert.equal(env.intervals.length, 0, "nobody is listening yet, so nothing asks");

  const first = env.listen();
  const second = env.live.subscribe(() => {});
  assert.equal(env.intervals.length, 1, "two listeners, one poll");
  const [poll] = env.intervals;

  poll.fn();
  await env.settle();
  assert.deepEqual(env.reads, ["/api/status"], "a tick asks the droid");
  assert.equal(env.live.current().estop, "clear", "and its answer is the reading");

  env.document.visibilityState = "hidden";
  poll.fn();
  await env.settle();
  assert.equal(env.reads.length, 1, "a hidden tab is not asked for");
  env.document.visibilityState = "visible";

  first();
  assert.deepEqual(env.cleared, [], "one listener left, so it keeps asking");
  second();
  assert.deepEqual(env.cleared, [poll.id], "nobody listening, so it stops");

  env.live.subscribe(() => {});
  assert.equal(env.intervals.length, 2, "and starts again when somebody does");
});

test("a refused poll is a lost link, and the droid's next answer is contact again", async () => {
  let answering = true;
  const env = boot({
    stream: false,
    answer: () => {
      if (!answering) throw new Error("no response from the droid");
      return statusFrame();
    },
  });
  env.listen();
  const [poll] = env.intervals;
  poll.fn();
  await env.settle();
  assert.equal(env.live.current().moveActsLive, true);

  answering = false;
  poll.fn();
  await env.settle();
  assert.equal(env.live.current().estop, "finding-out");
  assert.equal(env.live.current().notHearing, "link");

  answering = true;
  poll.fn();
  await env.settle();
  assert.equal(env.live.current().estop, "clear");
});
