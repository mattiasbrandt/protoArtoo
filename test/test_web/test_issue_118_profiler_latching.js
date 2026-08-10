// =============================================================================
// test/test_web/test_issue_118_profiler_latching.js
//
// Heap profiler polling on the setup page (issue #118): a build without the
// profiler (PA_HEAP_PROFILE=0) answers 404 or 501, and the page must stop
// asking. A 503 or a dropped connection is transient - ADR 0016 - and must not
// disable the panel for the rest of the session.
//
// These tests poll the real refreshProfiler in data/setup.js through the timer
// and visibility paths that call it. The previous version defined its own
// twenty-line "simulate the refreshProfiler logic" copy and asserted against
// that, so the latch could have been inverted in the shipped file without any
// test noticing. Issue #146.
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

const HEAP_SAMPLE = { heapFree: 10000, heapMin: 5000, fragRatio: 0.25 };

// Brings the setup page up with a scripted profiler endpoint. The module polls
// once at load; `poll()` drives each subsequent attempt through the 5 s
// interval the page installed.
const loadSetupPage = async (answer) => {
  const env = loadPageModule("setup.js", {
    fetchImpl: (url) => {
      if (String(url).startsWith(PROFILER_PATH)) return answer();
      return response(200, {});
    },
  });
  await env.settle();

  const profilerPolls = () => env.fetches.filter((f) => String(f.url).startsWith(PROFILER_PATH)).length;
  // The page installs more than one 5 s interval. Firing all of them is what a
  // 5 s tick does in the browser, and avoids guessing which one is the
  // profiler's.
  const ticking = env.intervals.filter((i) => i.ms === 5000);
  assert.ok(ticking.length > 0, "the setup page must install a 5 s poll");

  return {
    ...env,
    profilerPolls,
    poll: async () => {
      ticking.forEach((interval) => interval.fn());
      await env.settle();
    },
  };
};

test("The setup page polls the profiler as soon as it loads", async (t) => {
  const env = await loadSetupPage(() => response(200, HEAP_SAMPLE));

  assert.equal(env.profilerPolls(), 1, "the panel must populate without waiting for the first interval");
});

test("A 404 stops the page asking for a profiler this build does not have", async (t) => {
  const env = await loadSetupPage(() => response(404));
  assert.equal(env.profilerPolls(), 1);

  await env.poll();
  await env.poll();

  assert.equal(
    env.profilerPolls(),
    1,
    "404 means the feature is absent from this build; polling it every 5 s forever is pure noise"
  );
});

test("A 501 stops the page asking, the same as a 404", async (t) => {
  const env = await loadSetupPage(() => response(501));
  assert.equal(env.profilerPolls(), 1);

  await env.poll();

  assert.equal(env.profilerPolls(), 1, "501 is the other permanently-absent answer");
});

test("A 503 does not latch - admission control is transient", async (t) => {
  const env = await loadSetupPage(() => response(503));
  assert.equal(env.profilerPolls(), 1);

  await env.poll();

  assert.equal(
    env.profilerPolls(),
    2,
    "a busy controller must not cost the operator the profiler for the rest of the session"
  );
});

test("A dropped connection does not latch", async (t) => {
  const env = await loadSetupPage(() => Promise.reject(new Error("Simulated network failure")));
  assert.equal(env.profilerPolls(), 1);

  await env.poll();

  assert.equal(env.profilerPolls(), 2, "a transient network failure must leave the poll running");
});

test("A 500 does not latch", async (t) => {
  const env = await loadSetupPage(() => response(500));

  await env.poll();

  assert.equal(
    env.profilerPolls(),
    2,
    "only 404 and 501 mean absent; every other failure is worth retrying"
  );
});

test("Polling continues after a success, and after a success that carried no data", async (t) => {
  const env = await loadSetupPage(() => response(200, {}));

  await env.poll();
  await env.poll();

  assert.equal(
    env.profilerPolls(),
    3,
    "a thin payload is still a working endpoint - it must not be mistaken for an absent one"
  );
});

test("Latching survives the visibility path, not just the interval", async (t) => {
  const env = await loadSetupPage(() => response(404));

  env.document.visibilityState = "visible";
  env.emit("document", "visibilitychange");
  await env.settle();

  assert.equal(
    env.profilerPolls(),
    1,
    "returning to the tab must not reopen a poll the page has already latched off"
  );
});

test("Returning to the tab repolls while the profiler is still available", async (t) => {
  const env = await loadSetupPage(() => response(200, HEAP_SAMPLE));

  env.document.visibilityState = "visible";
  env.emit("document", "visibilitychange");
  await env.settle();

  assert.equal(env.profilerPolls(), 2, "a returning operator must see current heap numbers");
});
