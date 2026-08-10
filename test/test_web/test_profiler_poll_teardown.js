// =============================================================================
// test/test_web/test_profiler_poll_teardown.js
//
// Heap profiler Background Poll teardown (issue #152): when the profiler
// endpoint returns 404/501, the card must stop polling AND clean up the
// interval and visibility listener. The frozen test (test_issue_118_*.js)
// only measures silence (no more requests); this test measures cleanup.
// A card that mutes requests while leaving a timer ticking forever defeats
// the epic (#150, user story 6) that exists to remove idle-tab load.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

const PROFILER_PATH = "/api/profiler";

const response = (status, body = {}) => ({
  ok: status >= 200 && status < 300,
  status,
  json: async () => body,
});

const loadSetupPage = async (answer) => {
  const env = loadPageModule("setup.js", {
    fetchImpl: (url) => {
      if (String(url).startsWith(PROFILER_PATH)) return answer();
      return response(200, {});
    },
  });
  await env.settle();

  const profilerPolls = () =>
    env.fetches.filter((f) => String(f.url).startsWith(PROFILER_PATH)).length;
  const ticking = env.intervals.filter((i) => i.ms === 5000);

  return {
    ...env,
    profilerPolls,
    ticking,
  };
};

test("404 clears the 5s interval (teardown, not just silence)", async (t) => {
  const env = await loadSetupPage(() => response(404));

  // Initial poll on page load
  assert.equal(env.profilerPolls(), 1, "initial poll on load");

  // Capture which 5s intervals exist before calling the attempt
  const tickingIdsBefore = new Set(env.ticking.map((i) => i.id));
  assert.ok(tickingIdsBefore.size > 0, "setup page must install a 5s interval");

  // Trigger one more poll attempt (simulates 5s tick)
  env.ticking.forEach((interval) => interval.fn());
  await env.settle();

  // At least one of the 5s intervals must have been cleared by poll.stop()
  const clearedTickingIntervals = env.ticking.filter((i) => env.cleared.intervals.includes(i.id));
  assert.ok(
    clearedTickingIntervals.length > 0,
    "poll.stop() must clearInterval the profiler's 5s cadence, not just mute it"
  );
});

test("501 clears the 5s interval (same as 404)", async (t) => {
  const env = await loadSetupPage(() => response(501));

  const tickingIdsBefore = new Set(env.ticking.map((i) => i.id));
  assert.ok(tickingIdsBefore.size > 0, "setup page must install a 5s interval");

  env.ticking.forEach((interval) => interval.fn());
  await env.settle();

  const clearedTickingIntervals = env.ticking.filter((i) => env.cleared.intervals.includes(i.id));
  assert.ok(
    clearedTickingIntervals.length > 0,
    "501 must trigger the same teardown as 404"
  );
});

test("404 removes the visibility listener (no poll on tab return)", async (t) => {
  const env = await loadSetupPage(() => response(404));

  // Initial fetch count
  assert.equal(env.profilerPolls(), 1);

  // Simulate visibility change to trigger the listener (if it were still attached)
  env.document.visibilityState = "visible";
  env.emit("document", "visibilitychange");
  await env.settle();

  // A properly cleaned-up card should NOT have made another request
  // because the listener was removed by poll.stop()
  assert.equal(
    env.profilerPolls(),
    1,
    "poll.stop() must removeEventListener to prevent visibility-triggered polls"
  );
});
