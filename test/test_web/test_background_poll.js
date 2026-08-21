// =============================================================================
// test/test_web/test_background_poll.js
//
// Integration tests for Background Poll: the bootstrap-owned cadence and retry
// interface that pages use to drive conditional polling, retry ladders, and
// visibility-aware work. Tests load the real PART 1 of page_bootstrap.js and
// drive the real createBackgroundPoll, asserting on the actual timers it creates.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapFile = readFileSync(join(__dirname, "../../data/page_bootstrap.js"), "utf-8");

// Extract PART 1 (reducer) only. PART 2 and 3 are not needed for these tests.
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Builds a context with mocked timers and document visibility state.
const makeEnv = () => {
  const env = {
    intervals: [],
    timeouts: [],
    cleared: { intervals: [], timeouts: [] },
    listeners: { document: [] },
  };

  const addTimer = (list, fn, ms) => {
    const id = list.length + 1;
    list.push({ id, fn, ms });
    return id;
  };

  const documentMock = {
    visibilityState: "visible",
    hidden: false,
    addEventListener: (type, handler) => {
      if (type === "visibilitychange") {
        env.listeners.document.push({ type, handler });
      }
    },
    removeEventListener: (type, handler) => {
      if (type === "visibilitychange") {
        const idx = env.listeners.document.findIndex((l) => l.handler === handler);
        if (idx >= 0) env.listeners.document.splice(idx, 1);
      }
    },
  };

  const context = {
    window: {
      PageBootstrap: undefined, // will be set by the script
      setInterval: (fn, ms) => addTimer(env.intervals, fn, ms),
      clearInterval: (id) => env.cleared.intervals.push(id),
      setTimeout: (fn, ms) => addTimer(env.timeouts, fn, ms),
      clearTimeout: (id) => env.cleared.timeouts.push(id),
    },
    document: documentMock,
    console: { log: () => {}, warn: () => {}, error: () => {} },
  };
  context.globalThis = context;
  context.window.PageBootstrap = context.window.PageBootstrap || {};

  vm.runInNewContext(part1Src, context);

  return {
    ...env,
    window: context.window,
    document: documentMock,
    fireInterval: (id) => env.intervals.find((t) => t.id === id)?.fn(),
    fireTimeout: (id) => env.timeouts.find((t) => t.id === id)?.fn(),
    fireAllTimeouts: async () => {
      let fired = 0;
      while (env.timeouts.length > fired) {
        const next = env.timeouts[fired];
        fired += 1;
        next.fn();
        await sleep(0);
      }
      return fired;
    },
    emit: (type, event = {}) => {
      env.listeners.document.forEach(({ handler }) => handler(event));
    },
  };
};

// =============================================================================
// Cadence: setInterval-driven polling
// =============================================================================

test("BackgroundPoll sets up a setInterval for cadence", async (t) => {
  const env = makeEnv();
  const poll = env.window.PageBootstrap.createBackgroundPoll(() => Promise.resolve(true), {
    cadenceMs: 5000,
  });
  poll.start();

  assert.equal(env.intervals.length, 1, "must create one interval");
  assert.equal(env.intervals[0].ms, 5000, "interval must be 5000 ms");
});

test("BackgroundPoll clears the interval on stop()", async (t) => {
  const env = makeEnv();
  const poll = env.window.PageBootstrap.createBackgroundPoll(() => Promise.resolve(true), {
    cadenceMs: 5000,
  });
  poll.start();
  const intervalId = env.intervals[0].id;

  poll.stop();

  assert.ok(env.cleared.intervals.includes(intervalId), "stop() must clear the interval");
});

// =============================================================================
// Retry backoff: setTimeout-driven ladder
// =============================================================================

test("BackgroundPoll retries with exponential backoff on falsy result", async (t) => {
  const env = makeEnv();
  let callCount = 0;
  const attempt = async () => {
    callCount += 1;
    return false; // always fail, trigger retry
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    retry: { baseMs: 500, factor: 2, maxAttempts: 3 },
    runOnStart: true,
  });
  poll.start();

  await sleep(10); // Wait for runOnStart promise to settle
  assert.equal(callCount, 1, "runOnStart must call attempt immediately");
  assert.equal(env.timeouts.length, 1, "failed attempt must arm first retry");
  assert.equal(env.timeouts[0].ms, 500, "first retry must be baseMs");

  env.fireTimeout(env.timeouts[0].id);
  await sleep(10);
  assert.equal(callCount, 2, "fired timeout must call attempt again");
  assert.equal(env.timeouts.length, 2, "second retry must be armed");
  assert.equal(env.timeouts[1].ms, 1000, "second retry must be baseMs * factor");

  env.fireTimeout(env.timeouts[1].id);
  await sleep(10);
  assert.equal(callCount, 3, "second retry must call attempt");
  assert.equal(env.timeouts.length, 2, "third attempt must not arm retry (maxAttempts cap)");
});

test("BackgroundPoll stops retrying on successful attempt", async (t) => {
  const env = makeEnv();
  let callCount = 0;
  const attempt = async () => {
    callCount += 1;
    return callCount > 1; // fail once, then succeed
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    retry: { baseMs: 500, factor: 2, maxAttempts: 3 },
    runOnStart: true,
  });
  poll.start();

  await sleep(10); // Wait for runOnStart
  assert.equal(callCount, 1, "runOnStart must call");
  assert.equal(env.timeouts.length, 1, "failed first attempt must arm retry");

  env.fireTimeout(env.timeouts[0].id);
  await sleep(10);
  assert.equal(callCount, 2, "fired retry must call attempt");
  assert.equal(env.timeouts.length, 1, "successful attempt must not arm another retry");
});

// =============================================================================
// Visibility: pause on hidden, re-arm on return
// =============================================================================

test("BackgroundPoll skips cadence ticks while hidden", async (t) => {
  const env = makeEnv();
  let callCount = 0;
  const attempt = async () => {
    callCount += 1;
    return true;
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    cadenceMs: 5000,
  });
  poll.start();

  // Make hidden and fire the interval
  env.document.visibilityState = "hidden";
  env.document.hidden = true;
  env.emit("visibilitychange");
  env.fireInterval(env.intervals[0].id);
  await sleep(10);

  assert.equal(callCount, 0, "hidden tab must skip the tick");
});

test("BackgroundPoll refreshes on tab return when refreshOnReturn is true", async (t) => {
  const env = makeEnv();
  let callCount = 0;
  const attempt = async () => {
    callCount += 1;
    return true;
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    cadenceMs: 5000,
    refreshOnReturn: true,
  });
  poll.start();
  assert.equal(callCount, 0, "setup must not call attempt yet");

  // Hide and then restore
  env.document.visibilityState = "hidden";
  env.document.hidden = true;
  env.emit("visibilitychange");
  await sleep(10);
  assert.equal(callCount, 0, "hide must not call");

  env.document.visibilityState = "visible";
  env.document.hidden = false;
  env.emit("visibilitychange");
  await sleep(10);

  assert.equal(callCount, 1, "returning to visible with refreshOnReturn must call attempt");
});

// =============================================================================
// skipWhen: conditional skip without stopping
// =============================================================================

test("BackgroundPoll respects skipWhen predicate", async (t) => {
  const env = makeEnv();
  let callCount = 0;
  let shouldSkip = true;

  const attempt = async () => {
    callCount += 1;
    return true;
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    cadenceMs: 5000,
    skipWhen: () => shouldSkip,
  });
  poll.start();

  // First tick: skip
  env.fireInterval(env.intervals[0].id);
  await sleep(10);
  assert.equal(callCount, 0, "skipWhen=true must skip the tick");

  // Second tick: run
  shouldSkip = false;
  env.fireInterval(env.intervals[0].id);
  await sleep(10);
  assert.equal(callCount, 1, "skipWhen=false must run the tick");
});

// =============================================================================
// Single-flight: no overlapping attempts
// =============================================================================

test("BackgroundPoll never runs overlapping attempts", async (t) => {
  const env = makeEnv();
  let concurrent = 0;
  let maxConcurrent = 0;

  const attempt = async () => {
    concurrent += 1;
    maxConcurrent = Math.max(maxConcurrent, concurrent);
    await sleep(5);
    concurrent -= 1;
    return true;
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    cadenceMs: 1,
  });
  poll.start();

  // Fire many ticks rapidly
  for (let i = 0; i < 5; i += 1) {
    env.fireInterval(env.intervals[0].id);
  }
  await sleep(50);

  assert.equal(maxConcurrent, 1, "must never run concurrent attempts");
});

// =============================================================================
// Lifecycle: start, cancelRetry, stop
// =============================================================================

test("BackgroundPoll runs immediately on runOnStart", async (t) => {
  const env = makeEnv();
  let callCount = 0;
  const attempt = async () => {
    callCount += 1;
    return true;
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    runOnStart: true,
  });
  assert.equal(callCount, 0, "setup must not call");

  poll.start();
  await sleep(10); // Wait for async attempt
  assert.equal(callCount, 1, "start() with runOnStart must call attempt immediately");
});

test("cancelRetry clears pending retry timeout", async (t) => {
  const env = makeEnv();
  const attempt = async () => false; // always fail

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    retry: { baseMs: 500, factor: 2, maxAttempts: 3 },
    runOnStart: true,
  });
  poll.start();

  await sleep(10); // Wait for attempt to settle and arm retry
  assert.equal(env.timeouts.length, 1, "failed attempt must arm retry");
  const timeoutId = env.timeouts[0].id;

  poll.cancelRetry();

  assert.ok(env.cleared.timeouts.includes(timeoutId), "cancelRetry must clear the timeout");
});

test("stop() clears all timers and listeners", async (t) => {
  const env = makeEnv();
  const attempt = async () => false;

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    cadenceMs: 5000,
    refreshOnReturn: true,
    retry: { baseMs: 500, factor: 2, maxAttempts: 3 },
    runOnStart: true,
  });
  poll.start();

  await sleep(10); // Wait for attempt to settle and arm retry
  const intervalId = env.intervals[0]?.id;
  const timeoutId = env.timeouts[0]?.id;
  const listenerCount = env.listeners.document.length;

  poll.stop();

  assert.ok(env.cleared.intervals.includes(intervalId), "stop() must clear interval");
  assert.ok(env.cleared.timeouts.includes(timeoutId), "stop() must clear retry timeout");
  assert.equal(env.listeners.document.length, 0, "stop() must remove visibility listener");
});

// =============================================================================
// No cadence: retry-only poll
// =============================================================================

test("BackgroundPoll works without cadence (retry only)", async (t) => {
  const env = makeEnv();
  let callCount = 0;
  const attempt = async () => {
    callCount += 1;
    return callCount > 1; // fail once
  };

  const poll = env.window.PageBootstrap.createBackgroundPoll(attempt, {
    retry: { baseMs: 500, factor: 2, maxAttempts: 2 },
    runOnStart: true,
  });
  poll.start();

  await sleep(10); // Wait for attempt to settle
  assert.equal(env.intervals.length, 0, "no cadenceMs means no interval");
  assert.equal(callCount, 1, "runOnStart must run");
  assert.equal(env.timeouts.length, 1, "failed attempt must arm retry");
});
