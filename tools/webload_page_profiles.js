#!/usr/bin/env node
"use strict";

// Page profiles for the webload collectors (GitHub issue #94).
//
// Both collectors used to hardcode index.html's script chain, its four API
// calls and its DOM readiness gate, and to reject any --url that did not end
// in /index.html. ADR 0019 makes wifi.html the tracer page, so the ADR 0017
// envelope has to pass on a page whose load looks nothing like the dashboard's.
//
// The three things that must agree about a page -- which resources it fetches,
// which APIs it calls, and what "this page is up" means -- travel together here
// as one named profile. Adding pages 3..10 during the ADR 0019 rollout is an
// entry in PAGE_PROFILES below, not a change to either collector.
//
// The domProbe functions are serialized by Playwright and evaluated inside the
// page, in a realm that has none of this module's scope. They therefore repeat
// their own small DOM helpers instead of sharing them: self-containment is a
// requirement of where they run, not an oversight. Each one must return the
// same envelope -- capturedAt/url/title/readyState/runtime/gates/sseRuntime --
// because everything downstream (browserSideReady, classifySse, the multi-tab
// per-tab verdicts) reads a profile-independent shape.

const DEFAULT_PAGE = "index";

// These 4 are genuinely fetched by the real production frontend on every
// ordinary index.html load (data/app.js:534,574,633 -> /api/logs, /api/config,
// /api/actions; data/shell.js:157 -> /api/identity), so this list is correct
// against the current stack and must not be trimmed for convenience.
//
// Issue #73/#74 scope note: the PsychicHttp prototype (src/web/
// psychic_adapter.cpp) does not port any of these 4 routes -- confirmed by
// source, zero references. Against that build, browserGatesPassed/
// captureStatus will therefore NEVER reach "usable", on every run,
// regardless of concurrency/stall/multi-tab scenario -- that's an accurate
// reflection of a real, documented port gap (#72 scoped the prototype as
// partial), not a test-prep bug. Do not read a "usable"-gated failure
// against that build as evidence of a connection-handling defect; judge
// prototype runs on the sub-signals (requiredResources success, /api/status
// reachability, heap/connection metrics, SSE behavior) instead of this
// aggregate gate until the prototype's API surface is actually completed.
const INDEX_REQUIRED_APIS = Object.freeze([
  "/api/identity", "/api/logs", "/api/config", "/api/actions",
]);

const INDEX_REQUIRED_RESOURCES = Object.freeze([
  "/index.html",
  "/page_loader.js",
  "/web_api.js",
  "/diagnostics.js",
  "/status_stream.js",
  "/shell.js",
  "/health_signals.js",
  "/dome_command_map.js",
  "/dome_panel_model.js",
  "/dome_layout.js",
  "/dome_layout_render.js",
  "/dome_control.js",
  "/app.js",
  "/footer.js",
]);

// wifi.html carries no /page_loader.js at all: its inline recovery kernel
// (data/_recovery_kernel.html) fetches /page_bootstrap.js itself, with retry,
// and hands it the chain declared on <html data-scripts>. /style.css is listed
// because the recovery kernel deliberately renders without it, so a page that
// never got its stylesheet still reaches a visible state -- the resource has to
// be gated explicitly rather than inferred from the page looking rendered.
const WIFI_REQUIRED_RESOURCES = Object.freeze([
  "/wifi.html",
  "/style.css",
  "/page_bootstrap.js",
  "/web_api.js",
  "/status_stream.js",
  "/shell.js",
  "/wifi.js",
  "/footer.js",
]);

// The three sections wifi.js registers with the bootstrap (data/wifi.js:391,
// 400, 409). /api/identity is also fetched by shell.js on every page, which is
// why it appears once here rather than twice: the gate asks whether the route
// answered at all, not how many callers asked.
const WIFI_REQUIRED_APIS = Object.freeze([
  "/api/identity", "/api/config", "/api/wifi",
]);

// index.html's readiness gate. Unchanged from the inline probe this replaces:
// the dashboard predates the bootstrap and exposes no state machine, so the
// only evidence that it is up is that its rendered content stopped saying
// "loading".
async function indexDomProbe() {
  const element = (selector) => document.querySelector(selector);
  const text = (selector) => (element(selector)?.textContent || "").trim();
  const visible = (selector) => {
    const node = element(selector);
    if (!node) return false;
    const style = getComputedStyle(node);
    const rect = node.getBoundingClientRect();
    return style.display !== "none" && style.visibility !== "hidden" &&
      Number(style.opacity) !== 0 && rect.width > 0 && rect.height > 0;
  };
  const style = (selector) => {
    const node = element(selector);
    if (!node) return null;
    const computed = getComputedStyle(node);
    const rect = node.getBoundingClientRect();
    return {
      backgroundColor: computed.backgroundColor,
      fontFamily: computed.fontFamily,
      width: rect.width,
      height: rect.height,
    };
  };

  const HEALTH_IDS = ["h-sbus", "h-wifi", "h-fs", "h-heap", "h-dome-link", "h-sound", "h-dome-esc"];
  const healthIndicators = Object.fromEntries(HEALTH_IDS.map((id) => {
    const node = element(`#${id}`);
    const classes = node ? Array.from(node.classList) : [];
    const state = classes.find((cls) => ["ok", "warn", "fail", "off"].includes(cls)) || null;
    return [id, { exists: Boolean(node), state }];
  }));
  const healthReady = HEALTH_IDS.every((id) => healthIndicators[id].state !== null);

  const SNAPSHOT_IDS = ["snapshot-web-control", "snapshot-mode", "snapshot-estop", "snapshot-mood"];
  const snapshotPills = Object.fromEntries(SNAPSHOT_IDS.map((id) => [id, text(`#${id}`)]));
  const snapshotReady = SNAPSHOT_IDS.every((id) => {
    const value = snapshotPills[id];
    return value !== "" && !value.endsWith(": ...") && !value.endsWith("...");
  });

  const LOG_EMPTY_TEXT = "No log history available yet.";
  const logConsoleText = text("#log-console");
  const logConsoleReady = logConsoleText !== "" && logConsoleText !== LOG_EMPTY_TEXT;

  const shellReady = visible("#shell-top .topbar") && visible("#shell-top nav a.active");
  const bodyStyle = style("body");
  const stylingReady = bodyStyle?.backgroundColor && bodyStyle.backgroundColor !== "rgba(0, 0, 0, 0)" &&
    bodyStyle.fontFamily && !bodyStyle.fontFamily.includes("Times New Roman") &&
    bodyStyle.width > 0 && bodyStyle.height > 0;

  const runtime = {
    PAAssetsReady: window.PAAssetsReady === true,
    PAApi: Boolean(window.PAApi),
    PAStatusStream: Boolean(window.PAStatusStream),
    HealthSignalModel: Boolean(window.HEALTH_SIGNAL_MODEL || window.PAHealthSignals),
  };
  const runtimeReady = runtime.PAAssetsReady && runtime.PAApi && runtime.PAStatusStream;

  let interactive = false;
  if (document.readyState === "complete" && runtimeReady && healthReady && snapshotReady &&
      logConsoleReady && shellReady && stylingReady) {
    interactive = await new Promise((resolve) => {
      let settled = false;
      const started = performance.now();
      const finish = (value) => {
        if (settled) return;
        settled = true;
        resolve(value);
      };
      requestAnimationFrame(() => finish(performance.now() - started <= 500));
      setTimeout(() => finish(false), 500);
    });
  }

  const lastStatus = window.PAStatusStream?.getLastStatus?.() ?? null;
  return {
    capturedAt: new Date().toISOString(),
    url: location.href,
    title: document.title,
    readyState: document.readyState,
    runtime,
    healthIndicators,
    snapshotPills,
    logConsoleText,
    styles: { body: bodyStyle },
    gates: {
      documentReady: document.readyState === "complete",
      runtimeReady,
      healthReady,
      snapshotReady,
      logConsoleReady,
      shellReady,
      stylingReady,
      interactive,
    },
    sseRuntime: {
      supported: window.PAStatusStream?.isSupported?.() ?? false,
      visible: window.PAStatusStream?.isVisible?.() ?? false,
      hasLastStatus: lastStatus !== null,
      lastStatus,
      connectionText: text("#conn-status"),
      connectionClass: element("#conn-status")?.className || "",
    },
  };
}

// wifi.html's readiness gate. A migrated page reports its own startup state, so
// this asserts on window.PABootstrap.getState() rather than scraping rendered
// text: resourcesReady/sectionsStable/liveUpdatesStarted are the Page Startup
// Order boundary itself, and the per-step status/attempt values say which step
// stalled when it is not reached. Text scraping would only re-derive that from
// its symptoms, and would need new selectors for every page added later.
async function wifiDomProbe() {
  const element = (selector) => document.querySelector(selector);
  const text = (selector) => (element(selector)?.textContent || "").trim();
  const visible = (selector) => {
    const node = element(selector);
    if (!node) return false;
    const style = getComputedStyle(node);
    const rect = node.getBoundingClientRect();
    return style.display !== "none" && style.visibility !== "hidden" &&
      Number(style.opacity) !== 0 && rect.width > 0 && rect.height > 0;
  };
  const style = (selector) => {
    const node = element(selector);
    if (!node) return null;
    const computed = getComputedStyle(node);
    const rect = node.getBoundingClientRect();
    return {
      backgroundColor: computed.backgroundColor,
      fontFamily: computed.fontFamily,
      width: rect.width,
      height: rect.height,
    };
  };

  const summarizeSteps = (steps) => (Array.isArray(steps) ? steps : []).map((step) => ({
    name: step?.name ?? null,
    status: step?.status ?? null,
    attempt: step?.attempt ?? null,
    reason: step?.reason ?? null,
  }));
  const state = window.PABootstrap?.getState?.() ?? null;
  const bootstrap = state === null ? null : {
    resourcesReady: state.resourcesReady === true,
    sectionsStable: state.sectionsStable === true,
    liveUpdatesStarted: state.liveUpdatesStarted === true,
    resources: summarizeSteps(state.resources),
    sections: summarizeSteps(state.sections),
  };

  // The recovery view is evidence, not a gate failure on its own: it can appear
  // and clear again within one load. The gate below asks whether it is showing
  // at the moment the page is graded.
  const backdrop = element("#page-recovery-backdrop");
  const recovery = {
    backdropPresent: Boolean(backdrop),
    backdropActive: Boolean(backdrop?.classList.contains("active")),
    bodyRecoveryActive: document.body.classList.contains("recovery-active"),
  };

  const runtime = {
    PAAssetsReady: window.PAAssetsReady === true,
    PAApi: Boolean(window.PAApi),
    PAStatusStream: Boolean(window.PAStatusStream),
    PABootstrap: Boolean(window.PABootstrap),
  };
  const runtimeReady = runtime.PAAssetsReady && runtime.PAApi &&
    runtime.PAStatusStream && runtime.PABootstrap;

  const resourcesReady = bootstrap?.resourcesReady === true;
  const sectionsStable = bootstrap?.sectionsStable === true;
  const liveUpdatesStarted = bootstrap?.liveUpdatesStarted === true;
  const recoveryCleared = !recovery.backdropActive && !recovery.bodyRecoveryActive;
  const shellReady = visible("#shell-top .topbar") && visible("#shell-top nav a.active");
  const bodyStyle = style("body");
  const stylingReady = Boolean(
    bodyStyle?.backgroundColor && bodyStyle.backgroundColor !== "rgba(0, 0, 0, 0)" &&
    bodyStyle.fontFamily && !bodyStyle.fontFamily.includes("Times New Roman") &&
    bodyStyle.width > 0 && bodyStyle.height > 0,
  );

  let interactive = false;
  if (document.readyState === "complete" && runtimeReady && resourcesReady &&
      sectionsStable && liveUpdatesStarted && recoveryCleared && shellReady && stylingReady) {
    interactive = await new Promise((resolve) => {
      let settled = false;
      const started = performance.now();
      const finish = (value) => {
        if (settled) return;
        settled = true;
        resolve(value);
      };
      requestAnimationFrame(() => finish(performance.now() - started <= 500));
      setTimeout(() => finish(false), 500);
    });
  }

  const lastStatus = window.PAStatusStream?.getLastStatus?.() ?? null;
  return {
    capturedAt: new Date().toISOString(),
    url: location.href,
    title: document.title,
    readyState: document.readyState,
    runtime,
    bootstrap,
    recovery,
    styles: { body: bodyStyle },
    gates: {
      documentReady: document.readyState === "complete",
      runtimeReady,
      resourcesReady,
      sectionsStable,
      liveUpdatesStarted,
      recoveryCleared,
      shellReady,
      stylingReady,
      interactive,
    },
    sseRuntime: {
      supported: window.PAStatusStream?.isSupported?.() ?? false,
      visible: window.PAStatusStream?.isVisible?.() ?? false,
      hasLastStatus: lastStatus !== null,
      lastStatus,
      connectionText: text("#conn-status"),
      connectionClass: element("#conn-status")?.className || "",
    },
  };
}

const PAGE_PROFILES = Object.freeze({
  index: Object.freeze({
    name: "index",
    path: "/index.html",
    description: "Operator dashboard, unmigrated (still loaded by /page_loader.js)",
    requiredResources: INDEX_REQUIRED_RESOURCES,
    requiredApis: INDEX_REQUIRED_APIS,
    domProbe: indexDomProbe,
  }),
  wifi: Object.freeze({
    name: "wifi",
    path: "/wifi.html",
    description: "ADR 0019 tracer page, migrated to the inline recovery kernel + bootstrap",
    requiredResources: WIFI_REQUIRED_RESOURCES,
    requiredApis: WIFI_REQUIRED_APIS,
    domProbe: wifiDomProbe,
  }),
});

function pageNames() {
  return Object.keys(PAGE_PROFILES);
}

function resolveProfile(name) {
  const profile = PAGE_PROFILES[name];
  if (!profile) {
    throw new Error(
      `unknown page "${name}"; available pages: ${pageNames().join(", ")}`,
    );
  }
  return profile;
}

// Serializable view of a profile: the domProbe is a function and would vanish
// from JSON silently, so the description of a page that crosses a process
// boundary (evidence manifests, tools/webload_baseline_run.py) is built here.
function describeProfile(profile) {
  return {
    name: profile.name,
    path: profile.path,
    description: profile.description,
    requiredResources: profile.requiredResources,
    requiredApis: profile.requiredApis,
  };
}

function main(argv) {
  if (argv.includes("--help") || argv.length === 0) {
    process.stdout.write(
      "Usage: node tools/webload_page_profiles.js --list\n\n" +
      "Prints the page profiles the webload collectors can target, as JSON.\n" +
      "tools/webload_baseline_run.py reads this to validate --page and to build\n" +
      "the collector URLs, so the page list stays defined in one file.\n",
    );
    return 0;
  }
  if (argv.length === 1 && argv[0] === "--list") {
    process.stdout.write(`${JSON.stringify({
      defaultPage: DEFAULT_PAGE,
      pages: pageNames().map((name) => describeProfile(PAGE_PROFILES[name])),
    }, null, 2)}\n`);
    return 0;
  }
  process.stderr.write(`ERROR: unknown arguments: ${argv.join(" ")}\n`);
  return 1;
}

if (require.main === module) {
  process.exitCode = main(process.argv.slice(2));
}

module.exports = { PAGE_PROFILES, DEFAULT_PAGE, pageNames, resolveProfile, describeProfile };
