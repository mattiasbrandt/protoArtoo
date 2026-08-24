// =============================================================================
// Feature Availability on Setup (issue #186).
//
// The setup page learns compile-time availability from the identity manifest.
// It must never probe /api/profiler to discover absence. These tests execute
// the shipped setup.js resolver, renderers, and polling transition.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "node:fs";

import { loadPageModule } from "./helpers/page_module_env.js";

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

// Interpret enough of shipped HTML's structure and visibility rules to observe
// what a maker can read, rather than asserting that copy bytes exist somewhere
// in the source. Kept test-local because no production module needs an HTML host.
const visibleDescriptionIn = (html, ancestorId) => {
  const stack = [];
  let foundAncestor = false;
  let foundDescription = false;
  let text = "";
  const nonRenderedTags = new Set(["script", "style", "template"]);
  const voidTags = new Set([
    "area", "base", "br", "col", "embed", "hr", "img",
    "input", "link", "meta", "source", "track", "wbr",
  ]);
  const tokens = html.match(/<!--[\s\S]*?-->|<![^>]*>|<[^>]+>|[^<]+/g) || [];

  for (const token of tokens) {
    if (token.startsWith("<!--") || token.startsWith("<!")) continue;
    if (!token.startsWith("<")) {
      const current = stack.at(-1);
      if (current?.inDescription && !current.hidden) text += ` ${token}`;
      continue;
    }

    if (token.startsWith("</")) {
      const closingTag = token.match(/^<\/\s*([\w-]+)/)?.[1]?.toLowerCase();
      while (stack.length) {
        if (stack.pop().tag === closingTag) break;
      }
      continue;
    }

    const tag = token.match(/^<\s*([\w-]+)/)?.[1]?.toLowerCase() || "";
    const attributes = new Map();
    for (const match of token.matchAll(/([\w-]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g)) {
      attributes.set(match[1].toLowerCase(), match[2] ?? match[3] ?? match[4] ?? "");
    }

    const parent = stack.at(-1);
    const inAncestor = Boolean(parent?.inAncestor || attributes.get("id") === ancestorId);
    const classes = (attributes.get("class") || "").split(/\s+/);
    const startsDescription = inAncestor && classes.includes("desc") && !foundDescription;
    const inDescription = Boolean(parent?.inDescription || startsDescription);
    const style = (attributes.get("style") || "").replace(/\s/g, "").toLowerCase();
    const hidden = Boolean(
      parent?.hidden
      || nonRenderedTags.has(tag)
      || attributes.has("hidden")
      || attributes.get("aria-hidden") === "true"
      || style.includes("display:none")
      || style.includes("visibility:hidden")
    );

    foundAncestor ||= attributes.get("id") === ancestorId;
    foundDescription ||= startsDescription;
    if (!token.endsWith("/>") && !voidTags.has(tag)) {
      stack.push({ tag, hidden, inAncestor, inDescription });
    }
  }

  assert.equal(foundAncestor, true, `missing rendered ancestor #${ancestorId}`);
  assert.equal(foundDescription, true, `missing rendered description in #${ancestorId}`);
  return text.replace(/\s+/g, " ").trim();
};

const loadSetupPage = async ({ profilerAnswer = PROFILER_SAMPLE } = {}) => {
  const env = loadPageModule("setup.js", {
    respond: (path) => {
      if (path === "/api/config") return CONFIG;
      if (path === "/api/status") return {};
      if (path === PROFILER_PATH) return typeof profilerAnswer === "function"
        ? profilerAnswer()
        : profilerAnswer;
      return {};
    },
  });
  env.element("profiler-card").dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.settle();
  return {
    ...env,
    profilerRequests: () => env.requests.filter((request) => request.path === PROFILER_PATH),
    publishIdentity: async (payload) => {
      env.emit("window", "pa:identity-available", { detail: payload });
      await env.settle();
    },
  };
};

test("the shipped resolver distinguishes all four final feature states", async () => {
  const env = await loadSetupPage();
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

test("component rows render present toggles as On and Off", async () => {
  const env = await loadSetupPage();

  assert.equal(env.element("status-arm1").textContent, "On");
  assert.equal(env.element("status-arm2").textContent, "Off");
  assert.equal(env.element("enable-arm1").disabled, false);
  assert.equal(env.element("enable-arm2").disabled, false);
});

test("the profiler stays visible and says Not in this build without probing its endpoint", async () => {
  const env = await loadSetupPage();
  await env.publishIdentity(identity({ profiler: false }));

  assert.equal(env.profilerRequests().length, 0);
  assert.equal(env.element("profiler-card").hidden, false);
  assert.equal(env.element("profiler-card").dataset.featureState, "not-in-this-build");
  assert.equal(env.element("profiler-availability-status").textContent, "Not in this build");
  assert.equal(
    env.element("profiler-availability-reason").textContent,
    "This controller was loaded without Memory Profiler.",
  );
});

test("identity loading and failure never start profiler traffic", async () => {
  const env = await loadSetupPage();

  assert.equal(env.element("profiler-card").dataset.featureState, "checking");
  assert.equal(env.profilerRequests().length, 0);

  env.emit("window", "pa:identity-unavailable", { detail: { error: new Error("offline") } });
  await env.settle();

  assert.equal(env.element("profiler-card").dataset.featureState, "identity-unavailable");
  assert.equal(env.element("profiler-availability-status").textContent, "Availability unknown");
  assert.equal(env.profilerRequests().length, 0);
});

test("the profiler starts polling only after the manifest reports it present", async () => {
  const env = await loadSetupPage();
  await env.publishIdentity(identity({ profiler: false }));
  assert.equal(env.profilerRequests().length, 0);

  await env.publishIdentity(identity({ profiler: true }));

  assert.equal(env.profilerRequests().length, 1, "the first reading should start immediately once present");
  assert.equal(env.element("profiler-card").dataset.featureState, "on");
  assert.equal(env.element("profiler-availability-status").textContent, "On");
  assert.ok(env.intervals.some((interval) => interval.ms === 5000));
});

test("transient profiler errors do not change compile-time availability", async () => {
  const env = await loadSetupPage({ profilerAnswer: () => { throw new Error("controller busy"); } });
  await env.publishIdentity(identity({ profiler: true }));

  const profilerIntervals = env.intervals.filter((interval) => interval.ms === 5000);
  assert.equal(profilerIntervals.length, 2, "status fallback plus profiler cadence");
  profilerIntervals.forEach((interval) => interval.fn());
  await env.settle();

  assert.equal(env.profilerRequests().length, 2);
  assert.equal(env.element("profiler-card").dataset.featureState, "on");
});

test("a board-gated component renders Not on this board and remains visible", async () => {
  const env = await loadSetupPage();
  const arm1 = env.element("enable-arm1");
  arm1.dataset.boardCapability = "PA_CAP_HOSTED_WIFI";
  await env.publishIdentity(identity({ hostedWifi: false }));

  assert.equal(arm1.disabled, true);
  assert.equal(env.element("status-arm1").textContent, "Not on this board");
});

test("the setup markup declares every component row plus the profiler in the registry grain", () => {
  const html = readFileSync("data/setup.html", "utf8");
  const entries = [...html.matchAll(/data-feature-entry="([^"]+)"/g)].map((match) => match[1]);

  assert.equal(entries.length, 16);
  assert.equal(new Set(entries).size, 16);
  assert.ok(entries.includes("system.api.get-profiler"));
});

test("the rendered profiler card explains its purpose in maker language", () => {
  const renderedDescription = visibleDescriptionIn(
    readFileSync("data/setup.html", "utf8"),
    "profiler-card",
  );

  assert.equal(
    renderedDescription,
    "Shows where controller memory is being used and how much room remains.",
  );
});

test("shell publishes the once-per-page identity response for feature consumers", async () => {
  const payload = identity({ profiler: true });
  const env = loadPageModule("shell.js", {
    respond: (path) => path === "/api/identity" ? payload : {},
  });

  await env.runSection("shell-identity");

  assert.deepEqual(env.window.PAIdentity, payload);
  assert.deepEqual(env.pathsRequested(), ["/api/identity"]);
});
