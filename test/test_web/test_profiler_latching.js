// =============================================================================
// Feature Availability on Maintenance (issue #186).
//
// Maintenance learns compile-time availability from the identity manifest.
// It must never probe /api/profiler to discover absence. These tests execute
// the shipped resolver (data/feature_availability.js), handed to the shipped
// data/maintenance.js the way the page loads it first, and the surface's
// renderers and polling transition (#404).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { createRequire } from "node:module";

import { loadPageModule } from "./helpers/page_module_env.js";

const require = createRequire(import.meta.url);
const { createFeatureAvailability } = require("../../data/feature_availability.js");

const PROFILER_PATH = "/api/profiler";
const CONFIG = {
  components: {
    arm1: { enabled: true },
    arm2: { enabled: false },
  },
  system: {},
};
const PROFILER_SAMPLE = {
  heapFree: 10000,
  heapMin: 5000,
  heapLargest: 8000,
  fragRatio: 0.2,
  allocBlocks: 2,
  freeBlocks: 1,
  failedAllocs: 0,
  taskStacks: [],
  snapshots: [],
};

const identity = ({ nativeWifi = true, hostedWifi = false, profiler = false } = {}) => ({
  droidName: "artoo",
  mdnsUseName: true,
  board: nativeWifi ? "artoo_esp32" : "firebeetle2",
  board_capabilities: {
    PA_CAP_NATIVE_WIFI: nativeWifi,
    PA_CAP_HOSTED_WIFI: hostedWifi,
  },
  build_flags: {
    PA_HEAP_PROFILE: profiler,
    PA_HEAP_TRACING: false,
    PA_ADMISSION_TRACE: false,
  },
});

const loadMaintenance = async ({ profilerAnswer = PROFILER_SAMPLE } = {}) => {
  const availability = createFeatureAvailability();
  const env = loadPageModule("maintenance.js", {
    respond: (path) => {
      if (path === "/api/config") return CONFIG;
      if (path === "/api/status") return {};
      if (path === PROFILER_PATH) return typeof profilerAnswer === "function"
        ? profilerAnswer()
        : profilerAnswer;
      return {};
    },
    overrides: { PAFeatureAvailability: availability },
  });
  env.element("profiler-card").dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.settle();
  return {
    ...env,
    profilerRequests: () => env.requests.filter((request) => request.path === PROFILER_PATH),
    publishIdentity: async (payload) => {
      availability.setIdentity(payload);
      await env.settle();
    },
    loseIdentity: async (reason) => {
      availability.setIdentityError(reason);
      await env.settle();
    },
  };
};

test("the shipped resolver distinguishes all four final feature states", async () => {
  const env = await loadMaintenance();
  const availability = env.window.PAFeatureAvailability;

  assert.equal(availability.resolve({ enabled: true }).state, "on");
  assert.equal(availability.resolve({ enabled: false }).state, "off");
  assert.equal(availability.resolve({ buildFlag: "PA_HEAP_PROFILE" }).state, "checking");

  availability.setIdentity(identity({ nativeWifi: false, hostedWifi: true, profiler: false }));
  assert.equal(
    availability.resolve({ boardCapability: "PA_CAP_NATIVE_WIFI", buildFlag: "PA_HEAP_PROFILE" }).state,
    "not-on-this-board",
    "board topology must explain absence before the per-image build choice",
  );
  assert.equal(
    availability.resolve({ boardCapability: "PA_CAP_HOSTED_WIFI", buildFlag: "PA_HEAP_PROFILE" }).state,
    "not-in-this-build",
  );
});

test("the profiler stays visible and says Not included without probing its endpoint", async () => {
  const env = await loadMaintenance();
  await env.publishIdentity(identity({ profiler: false }));

  assert.equal(env.profilerRequests().length, 0);
  assert.equal(env.element("profiler-card").hidden, false);
  assert.equal(env.element("profiler-card").dataset.featureState, "not-in-this-build");
  assert.equal(env.element("profiler-availability-status").textContent, "Not included");
  assert.equal(
    env.element("profiler-availability-reason").textContent,
    "Memory Profiler is included only in troubleshooting firmware.",
  );
  // Status lamp renders as indicator, not switch affordance
  assert.ok(env.element("profiler-availability-lamp"), "lamp indicator should exist");
  assert.equal(
    env.element("profiler-availability-lamp").className,
    "feature-availability-lamp-indicator feature-state-not-in-this-build",
  );
});

test("identity loading and failure never start profiler traffic", async () => {
  const env = await loadMaintenance();

  assert.equal(env.element("profiler-card").dataset.featureState, "checking");
  assert.equal(env.profilerRequests().length, 0);

  await env.loseIdentity("no-response");

  assert.equal(env.element("profiler-card").dataset.featureState, "identity-unavailable");
  assert.equal(env.element("profiler-availability-status").textContent, "Availability unknown");
  assert.equal(env.profilerRequests().length, 0);
});

test("the profiler starts polling only after the manifest reports it present", async () => {
  const env = await loadMaintenance();
  await env.publishIdentity(identity({ profiler: false }));
  assert.equal(env.profilerRequests().length, 0);

  await env.publishIdentity(identity({ profiler: true }));

  assert.equal(env.profilerRequests().length, 1, "the first reading should start immediately once present");
  assert.equal(env.element("profiler-card").dataset.featureState, "included");
  assert.equal(env.element("profiler-availability-status").textContent, "Included");
  assert.ok(env.intervals.some((interval) => interval.ms === 5000));
});

test("transient profiler errors do not change compile-time availability", async () => {
  const env = await loadMaintenance({ profilerAnswer: () => { throw new Error("controller busy"); } });
  await env.publishIdentity(identity({ profiler: true }));

  // Compile-time availability is immutable - should stay "included" despite errors
  assert.equal(env.element("profiler-card").dataset.featureState, "included", "state unchanged by transient errors");
  const profilerIntervals = env.intervals.filter((interval) => interval.ms === 5000);
  assert.equal(profilerIntervals.length, 2, "status fallback plus profiler cadence");
  profilerIntervals.forEach((interval) => interval.fn());
  await env.settle();

  assert.equal(env.profilerRequests().length, 2);
  assert.equal(env.element("profiler-card").dataset.featureState, "included", "compile-time state is immutable");
});

