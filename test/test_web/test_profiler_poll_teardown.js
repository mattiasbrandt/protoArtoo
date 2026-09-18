// =============================================================================
// Feature Availability profiler poll ownership (issue #186).
//
// The identity manifest owns whether the poll exists. Moving from a profiler
// image to an unavailable/error state must tear down cadence and return-to-tab
// work; no endpoint status is used as a capability probe.
//
// The profiler is Maintenance's (#404), and the manifest reaches it through the
// shipped Feature Availability module the surface loads first. That module is
// handed in for real, the way the Dashboard's tests take health_signals.js, and
// the manifest is published on it; the event feed in front of it is proven with
// the shell in test_feature_availability_inert_control.js.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { createRequire } from "node:module";

import { loadPageModule } from "./helpers/page_module_env.js";

const require = createRequire(import.meta.url);
const { createFeatureAvailability } = require("../../data/feature_availability.js");

const PROFILER_PATH = "/api/profiler";
const profilerIdentity = (enabled) => ({
  board: "artoo_esp32",
  board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
  build_flags: {
    PA_HEAP_PROFILE: enabled,
    PA_HEAP_TRACING: false,
    PA_ADMISSION_TRACE: false,
  },
});

const load = async () => {
  const availability = createFeatureAvailability();
  const env = loadPageModule("maintenance.js", {
    respond: (path) => path === PROFILER_PATH
      ? { heapFree: 1, heapMin: 1, heapLargest: 1, fragRatio: 0, taskStacks: [], snapshots: [] }
      : {},
    overrides: { PAFeatureAvailability: availability },
  });
  env.element("profiler-card").dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.settle();
  const publish = async (payload) => {
    availability.setIdentity(payload);
    await env.settle();
  };
  const lose = async () => {
    availability.setIdentityError("no-response");
    await env.settle();
  };
  return { env, publish, lose };
};

test("an unavailable manifest installs no profiler cadence or visibility work", async () => {
  const { env, publish } = await load();
  await publish(profilerIdentity(false));

  assert.equal(env.requests.filter((request) => request.path === PROFILER_PATH).length, 0);
  assert.equal(env.intervals.filter((interval) => interval.ms === 5000).length, 1,
    "only the Maintenance status fallback poll should exist");
});

test("losing the identity manifest stops a running profiler cadence", async () => {
  const { env, publish, lose } = await load();
  await publish(profilerIdentity(true));
  const profilerIntervals = env.intervals.filter((interval) => interval.ms === 5000);
  assert.equal(profilerIntervals.length, 2, "status fallback plus profiler cadence");

  await lose();

  assert.ok(
    profilerIntervals.some((interval) => env.cleared.intervals.includes(interval.id)),
    "the profiler-owned interval must be cleared when availability becomes unknown",
  );
});

test("changing to a non-profiler manifest stops polling without probing", async () => {
  const { env, publish } = await load();
  await publish(profilerIdentity(true));
  const before = env.requests.filter((request) => request.path === PROFILER_PATH).length;
  const profilerIntervals = env.intervals.filter((interval) => interval.ms === 5000);

  await publish(profilerIdentity(false));

  assert.equal(env.requests.filter((request) => request.path === PROFILER_PATH).length, before);
  assert.ok(profilerIntervals.some((interval) => env.cleared.intervals.includes(interval.id)));
  assert.equal(env.element("profiler-card").dataset.featureState, "not-in-this-build");
});

test("returning to the tab refreshes a profiler that the manifest says is present", async () => {
  const { env, publish } = await load();
  await publish(profilerIdentity(true));
  const before = env.requests.filter((request) => request.path === PROFILER_PATH).length;

  env.document.visibilityState = "visible";
  env.emit("document", "visibilitychange");
  await env.settle();

  assert.equal(env.requests.filter((request) => request.path === PROFILER_PATH).length, before + 1);
});
