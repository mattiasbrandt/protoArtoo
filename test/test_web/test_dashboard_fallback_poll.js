// =============================================================================
// test/test_web/test_dashboard_fallback_poll.js
//
// The Dashboard with no event stream (#165, #419).
//
// It used to run its own 3 s /api/status poll, beside Foot Drive's, Sound's and
// the shell's. Each spent one of the controller's three client slots asking
// what another had just asked. Now the Live Reading's single poll is the only
// status reader on this path, and the Dashboard paints what it hands over.
//
// Runs the REAL data/app.js with the real status stream and Live Reading
// (helpers/page_module_env.js), and asserts on requests and rendering:
// - the Dashboard installs no poll of its own
// - the shared poll keeps one /api/status in flight at a time
// - what the poll hears is what the Dashboard shows
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";
import { statusFrame } from "./helpers/fake_droid.js";

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

test("with no stream the Dashboard polls nothing itself, and the shell's one poll feeds it one read at a time", async () => {
  const callLog = [];
  const statusDelay = 400; // Slow enough that overlapping reads are measurable
  let latched = false;

  const env = loadPageModule("app.js", {
    respond: (path) => {
      if (path !== "/api/status") return { data: {} };
      const id = callLog.length;
      callLog.push({ id, start: Date.now() });
      return new Promise((resolve) => {
        setTimeout(() => {
          callLog[id].end = Date.now();
          resolve({ data: statusFrame({ estop: latched }) });
        }, statusDelay);
      });
    },
  });
  await env.settle();

  assert.ok(!env.window.PAStatusStream.isSupported(), "the fallback path is the one under test");
  assert.equal(env.intervals.length, 1, "one poll: the Live Reading's, and none of the Dashboard's own");
  const [poll] = env.intervals;

  // Three ticks while a slow read is still out: the poll must wait for its own
  // answer rather than stacking reads on the controller.
  latched = true;
  for (let i = 0; i < 3; i += 1) {
    poll.fn();
    await sleep(100);
  }
  await sleep(600);

  let maxConcurrent = 0;
  for (const call of callLog) {
    const overlapping = callLog.filter((other) => other.start < call.end && other.end > call.start).length;
    maxConcurrent = Math.max(maxConcurrent, overlapping);
  }
  assert.equal(maxConcurrent, 1, `one status read in flight at a time. Call log: ${JSON.stringify(callLog)}`);

  assert.equal(
    env.element("estop-clear").disabled,
    false,
    "the latch the poll heard is the one the Dashboard offers to release",
  );
});
