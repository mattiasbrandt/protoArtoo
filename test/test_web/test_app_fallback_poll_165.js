// =============================================================================
// test/test_web/test_app_fallback_poll_165.js
//
// Integration test for #165: Dashboard SSE-fallback polling uses
// createBackgroundPoll with proper single-flight guarding, correct cadence,
// and proper teardown.
//
// Loads the REAL data/app.js module and verifies that:
// - refreshFromFallback returns its promise (so single-flight works)
// - Only one concurrent /api/status request is in flight at a time
// - Cadence is 3000ms
// - Interval and listener are removed on beforeunload
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// =============================================================================
// Single-flight carrying assertion: all three mutations must be KILLED
// =============================================================================

test("Dashboard fallback path: real app.js single-flight and teardown", async (t) => {
  const callLog = [];
  const statusDelay = 400; // Make /api/status slow so overlaps are detectable

  // Load the real app.js module
  const env = loadPageModule("app.js", {
    respond: (path) => {
      if (path === "/api/status") {
        // Track every /api/status call
        const id = callLog.length;
        callLog.push({ id, start: Date.now() });

        // Return a promise that resolves after the delay
        return new Promise((resolve) => {
          setTimeout(() => {
            callLog[id].end = Date.now();
            resolve({ data: { estop: false, sleepMode: false } });
          }, statusDelay);
        });
      }
      return { data: {} };
    },
  });

  // Settle initial load
  await env.settle();

  // Verify the fallback path was taken (no SSE)
  assert.ok(
    !env.window.PAStatusStream.isSupported(),
    "fallback path must be active (SSE disabled)"
  );

  // Verify interval was created
  assert.ok(
    env.intervals.length > 0,
    "fallback path must create a polling interval"
  );

  const fallbackInterval = env.intervals[env.intervals.length - 1];

  // =========================================================================
  // MUTATION 1 (m1.patch): Mutation removes `return` from refreshFromFallback
  // Without it, inFlight clears before request settles, allowing overlaps
  // Must kill this test by showing overlaps
  // =========================================================================
  // Fire interval 3 times, staggered
  // Each call takes 400ms, so if single-flight works, only 1 concurrent call
  // If single-flight is broken (no return), 3 concurrent calls
  callLog.length = 0;
  for (let i = 0; i < 3; i += 1) {
    env.fireInterval(fallbackInterval.id);
    await sleep(100);
  }

  await sleep(600); // Wait for all calls to settle

  // Count max concurrent calls
  let maxConcurrent = 0;
  for (let i = 0; i < callLog.length; i += 1) {
    let concurrent = 1;
    for (let j = 0; j < callLog.length; j += 1) {
      if (i !== j && callLog[j].start < callLog[i].end && callLog[j].start >= callLog[i].start) {
        concurrent++;
      }
    }
    maxConcurrent = Math.max(maxConcurrent, concurrent);
  }

  // m1 mutation must be KILLED: without return, maxConcurrent should be 3
  assert.equal(
    maxConcurrent,
    1,
    `single-flight broken: max concurrent was ${maxConcurrent}, expected 1. ` +
    `This kills m1.patch (removes return). Call log: ${JSON.stringify(callLog)}`
  );

  // =========================================================================
  // MUTATION 2 (c1-cadence.patch): Mutation changes cadence from 3000 to 30000
  // This test verifies the cadence was set to 3000ms
  // Must kill this test by showing cadence is NOT 30000
  // =========================================================================
  assert.equal(
    fallbackInterval.ms,
    3000,
    `cadence must be 3000ms (production value), got ${fallbackInterval.ms}. ` +
    `This kills c1-cadence.patch (changes cadence to 30000)`
  );

  // =========================================================================
  // MUTATION 3 (c2-teardown.patch): Mutation deletes beforeunload teardown
  // This test verifies the interval is cleared on beforeunload
  // Must kill this test by showing the interval is NOT cleared
  // =========================================================================
  const intervalIdBeforeTeardown = fallbackInterval.id;
  env.emit("window", "beforeunload");
  await env.settle();

  assert.ok(
    env.cleared.intervals.includes(intervalIdBeforeTeardown),
    `beforeunload must clear the fallback interval. ` +
    `This kills c2-teardown.patch (deletes the beforeunload listener)`
  );
});
