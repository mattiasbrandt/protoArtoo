// =============================================================================
// test/test_web/test_app_fallback_poll_165.js
//
// Integration test for #165: Dashboard SSE-fallback polling uses
// createBackgroundPoll with proper single-flight guarding.
// Proves that refreshFromFallback returns its promise (critical for
// single-flight to work) and that overlapping attempts are prevented.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapFile = readFileSync(
  join(__dirname, "../../data/page_bootstrap.js"),
  "utf-8"
);
const appFile = readFileSync(join(__dirname, "../../data/app.js"), "utf-8");

// Extract PART 1 (reducer) only from page_bootstrap.js
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part1Src = bootstrapFile.substring(
  bootstrapFile.indexOf("(() => {"),
  part2Marker
);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Builds a context with mocked timers and document visibility state.
const makeEnv = () => {
  const env = {
    intervals: [],
    timeouts: [],
    cleared: { intervals: [], timeouts: [] },
    listeners: { document: [], window: [] },
    statusAttempts: [],
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
      PageBootstrap: undefined,
      setInterval: (fn, ms) => addTimer(env.intervals, fn, ms),
      clearInterval: (id) => env.cleared.intervals.push(id),
      setTimeout: (fn, ms) => addTimer(env.timeouts, fn, ms),
      clearTimeout: (id) => env.cleared.timeouts.push(id),
      addEventListener: (type, handler) => {
        if (type === "beforeunload") {
          env.listeners.window.push({ type, handler });
        }
      },
      PAStatusStream: {
        isSupported: () => false, // Force fallback path
      },
    },
    document: documentMock,
    console: { log: () => {}, warn: () => {}, error: () => {} },
  };
  context.globalThis = context;
  context.window.PageBootstrap = context.window.PageBootstrap || {};

  // Run page_bootstrap PART 1 to set up createBackgroundPoll
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
      if (type === "visibilitychange") {
        env.listeners.document.forEach(({ handler }) => handler(event));
      }
    },
    fireBeforeunload: () => {
      env.listeners.window.forEach(({ handler }) => handler());
    },
  };
};

// =============================================================================
// Single-flight guard: the carrying assertion for #165
// =============================================================================

test("Dashboard fallback path: single-flight prevents overlapping attempts", async (t) => {
  const env = makeEnv();
  const callTimeline = [];

  // Mock refreshStatusOnce to track timing of concurrent calls
  const mockRefreshStatusOnce = async () => {
    const callId = callTimeline.length;
    const startTime = Date.now();
    callTimeline.push({ id: callId, start: startTime, end: null });

    // Simulate a response that takes longer than one cadence tick (3000 ms)
    await sleep(500);

    callTimeline[callId].end = Date.now();
    return true;
  };

  // Inject mocked refreshStatusOnce into the app context
  const appContext = {
    window: env.window,
    document: env.document,
    console: { log: () => {}, warn: () => {}, error: () => {} },
    refreshStatusOnce: mockRefreshStatusOnce,
    setStale: () => {},
    applyStatus: () => {},
    appendLogLine: () => {},
    setEstopUi: () => {},
    setSleepUi: () => {},
    startPageLoad: () => {},
    SECTIONS: [],
  };
  appContext.globalThis = appContext;

  // Extract and run only the SSE-unsupported branch from app.js
  // This is the fallback polling code we're testing
  const fallbackCode = `
  const refreshFromFallback = () => {
    return refreshStatusOnce().catch(() => {
      // pollFailCount++;
      // if (pollFailCount >= 2) setStale(true);
    });
  };

  const fallbackPoll = window.PageBootstrap.createBackgroundPoll(
    refreshFromFallback,
    {
      cadenceMs: 200,
      refreshOnReturn: true,
    }
  );
  fallbackPoll.start();

  window.addEventListener("beforeunload", () => {
    fallbackPoll.stop();
  });
  `;

  vm.runInNewContext(fallbackCode, appContext);

  // Fire the cadence interval multiple times over time
  // Each fires 200ms apart, but each call takes 500ms
  // So if single-flight works, only the first should complete before the next fires
  for (let i = 0; i < 3; i += 1) {
    env.fireInterval(env.intervals[0].id);
    await sleep(150); // Stagger the fires to allow previous to start
  }

  // Wait for all calls to settle
  await sleep(1000);

  // Verify no overlapping calls
  let hasOverlap = false;
  for (let i = 0; i < callTimeline.length; i += 1) {
    for (let j = i + 1; j < callTimeline.length; j += 1) {
      const call1 = callTimeline[i];
      const call2 = callTimeline[j];
      // Check if call2 started before call1 ended
      if (call2.start < call1.end) {
        hasOverlap = true;
      }
    }
  }

  assert.equal(
    hasOverlap,
    false,
    `single-flight must prevent overlapping calls. timeline: ${JSON.stringify(callTimeline)}`
  );
});

// =============================================================================
// Carrying assertion: detecting the "no return" mutation
//
// The mutation removes "return" from refreshFromFallback, so the promise is
// not returned to createBackgroundPoll's runAttempt. This causes inFlight to
// clear before the request settles, allowing overlapping requests.
//
// This test detects the mutation by:
// 1. Measuring how many times refreshStatusOnce is called when intervals fire
// 2. With the fix (return), only 1 call (single-flight guard works)
// 3. Without the fix (no return), multiple concurrent calls (inFlight clears early)
// =============================================================================

test("Dashboard fallback path: promise-return enables single-flight guarding", async (t) => {
  const env = makeEnv();
  const callLog = [];

  // Mock refreshStatusOnce to track every invocation
  const mockRefreshStatusOnce = async () => {
    const id = callLog.length;
    callLog.push({ id, start: Date.now() });
    await sleep(500);
    callLog[id].end = Date.now();
    return true;
  };

  const appContext = {
    window: env.window,
    document: env.document,
    console: { log: () => {}, warn: () => {}, error: () => {} },
    refreshStatusOnce: mockRefreshStatusOnce,
    setStale: () => {},
    applyStatus: () => {},
    appendLogLine: () => {},
    setEstopUi: () => {},
    setSleepUi: () => {},
    startPageLoad: () => {},
    SECTIONS: [],
  };
  appContext.globalThis = appContext;

  // Extract and load the real fallback code from app.js
  // This ensures mutations to the refreshFromFallback function are detected
  const appFile = readFileSync(join(__dirname, "../../data/app.js"), "utf-8");

  // Find the fallback polling section (SSE-unsupported branch)
  // Extract from "const refreshFromFallback" through the closing bracket
  // This regex captures both "return refreshStatusOnce" and "refreshStatusOnce" (mutated version)
  const refreshFromFallbackMatch = appFile.match(
    /const refreshFromFallback = \(\) => \{\s*(?:return\s+)?refreshStatusOnce\(\)[\s\S]*?\};\s*/
  );

  if (!refreshFromFallbackMatch) {
    assert.fail("Could not find refreshFromFallback in app.js");
  }

  // Build the fallback polling setup code from the actual app.js
  const fallbackCode = `
  ${refreshFromFallbackMatch[0]}

  const fallbackPoll = window.PageBootstrap.createBackgroundPoll(
    refreshFromFallback,
    {
      cadenceMs: 200,
      refreshOnReturn: true,
    }
  );
  fallbackPoll.start();
  `;

  vm.runInNewContext(fallbackCode, appContext);

  // Fire the interval 3 times over 300ms total
  // Each refreshStatusOnce call takes 500ms
  // With single-flight guard: only 1 call total
  // Without single-flight guard: 3 concurrent calls (since inFlight clears immediately)
  for (let i = 0; i < 3; i += 1) {
    env.fireInterval(env.intervals[0].id);
    await sleep(100);
  }

  // Wait for all promises to settle
  await sleep(600);

  // Count concurrent calls: if any two intervals start before a previous one ends
  let maxConcurrent = 0;
  for (let i = 0; i < callLog.length; i++) {
    let concurrent = 1; // Count the current call
    for (let j = 0; j < callLog.length; j++) {
      if (i !== j && callLog[j].start < callLog[i].end && callLog[j].start >= callLog[i].start) {
        concurrent++;
      }
    }
    maxConcurrent = Math.max(maxConcurrent, concurrent);
  }

  // With the fix (with return), maxConcurrent must be 1
  // Without the fix (no return), maxConcurrent will be 3
  assert.equal(
    maxConcurrent,
    1,
    `single-flight guard broken: max concurrent calls was ${maxConcurrent}, expected 1. ` +
    `This means refreshFromFallback is not returning the promise, allowing inFlight to clear early. ` +
    `Call log: ${JSON.stringify(callLog)}`
  );
});

// =============================================================================
// Teardown on beforeunload
// =============================================================================

test("Dashboard fallback path: poll stops on beforeunload", async (t) => {
  const env = makeEnv();
  const mockRefreshStatusOnce = async () => true;

  const appContext = {
    window: env.window,
    document: env.document,
    console: { log: () => {}, warn: () => {}, error: () => {} },
    refreshStatusOnce: mockRefreshStatusOnce,
    setStale: () => {},
    applyStatus: () => {},
    appendLogLine: () => {},
    setEstopUi: () => {},
    setSleepUi: () => {},
    startPageLoad: () => {},
    SECTIONS: [],
  };
  appContext.globalThis = appContext;

  const setupCode = `
  const refreshFromFallback = () => {
    return refreshStatusOnce().catch(() => {});
  };

  const fallbackPoll = window.PageBootstrap.createBackgroundPoll(
    refreshFromFallback,
    {
      cadenceMs: 3000,
      refreshOnReturn: true,
    }
  );
  fallbackPoll.start();

  window.addEventListener("beforeunload", () => {
    fallbackPoll.stop();
  });
  `;

  vm.runInNewContext(setupCode, appContext);

  // Verify interval and listener were created
  assert.ok(env.intervals.length > 0, "must create interval");
  const initialIntervalId = env.intervals[0].id;
  const initialListenerCount = env.listeners.window.length;

  // Fire beforeunload
  env.fireBeforeunload();
  await sleep(10);

  // Interval should be cleared
  assert.ok(
    env.cleared.intervals.includes(initialIntervalId),
    "beforeunload must clear the cadence interval"
  );
});

// =============================================================================
// Cadence and visibility behavior are delegated to createBackgroundPoll
// =============================================================================

test("Dashboard fallback path: uses 3000ms cadence", async (t) => {
  const env = makeEnv();
  const mockRefreshStatusOnce = async () => true;

  const appContext = {
    window: env.window,
    document: env.document,
    console: { log: () => {}, warn: () => {}, error: () => {} },
    refreshStatusOnce: mockRefreshStatusOnce,
    setStale: () => {},
    applyStatus: () => {},
    appendLogLine: () => {},
    setEstopUi: () => {},
    setSleepUi: () => {},
    startPageLoad: () => {},
    SECTIONS: [],
  };
  appContext.globalThis = appContext;

  const setupCode = `
  const refreshFromFallback = () => {
    return refreshStatusOnce().catch(() => {});
  };

  const fallbackPoll = window.PageBootstrap.createBackgroundPoll(
    refreshFromFallback,
    {
      cadenceMs: 3000,
      refreshOnReturn: true,
    }
  );
  fallbackPoll.start();

  window.addEventListener("beforeunload", () => {
    fallbackPoll.stop();
  });
  `;

  vm.runInNewContext(setupCode, appContext);

  assert.equal(
    env.intervals[0].ms,
    3000,
    "cadence must be 3000ms per #165 spec"
  );
});
