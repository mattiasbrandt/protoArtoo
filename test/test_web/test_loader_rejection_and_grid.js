// =============================================================================
// test/test_web/test_loader_rejection_and_grid.js
//
// #107 - a home-dashboard section loader that cannot do its job must reject, so
//        the bootstrap shows recovery instead of the page sitting there empty.
// #109 - the component grid must emit valid dl/dt/dd markup and update in place
//        when only the values changed.
//
// The loaders and the renderer are the ones data/app.js ships, reached through
// the sections it registers and the status stream it subscribes to. The
// previous version of this file extracted their source into three variables,
// never used them, and asserted against local four-line copies instead - so
// deleting any of the real loaders would not have failed it. Issue #146.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule, ApiError } from "./helpers/page_module_env.js";
import { outputsModule, statusFrame } from "./helpers/fake_droid.js";

const OK_LOGS = "boot: ready\nwifi: connected";
const OK_CONFIG = { system: { logLevel: 2 } };
// Shaped like POST /api/console { command: "operations" }'s real response
// (docs/console-protocol.md s.2 / src/web/api_console.cpp): one "item"
// record per catalog entry, value "<name> (<type>)".
const OK_OPERATIONS = {
  records: [
    { id: 1, type: "begin", operation: "operations" },
    { id: 1, type: "item", value: "drive.action.move (action)" },
    { id: 1, type: "item", value: "sound.action.random-humming (action)" },
    { id: 1, type: "end", status: "ok", outcome: "completed" },
  ],
};

// Brings the home dashboard up with a controllable transport and status stream.
const loadDashboard = async ({ respond } = {}) => {
  let env = null;
  env = loadPageModule("app.js", {
    respond,
    overrides: {
      // The log level's config read goes through the shipped data/outputs.js
      // the Dashboard page loads first (#415).
      PAOutputs: outputsModule(() => env.window.PAApi),
    },
  });
  await env.settle();
  return env;
};

// The default transport: every endpoint answers successfully.
const healthyResponder = (path, opts = {}) => {
  // The device answers /api/logs as text/plain (src/web/api_logs.cpp), and
  // since #261 the loader refuses anything else - so the mock has to carry
  // the media type the real transport now reports.
  if (path === "/api/logs") return { data: OK_LOGS, contentType: "text/plain" };
  if (path === "/api/config") return { data: OK_CONFIG };
  if (path === "/api/console" && opts.body?.command === "operations") return { data: OK_OPERATIONS };
  if (path === "/api/status") return { data: {} };
  return { data: {} };
};

// -----------------------------------------------------------------------------
// #107: loader error propagation
// -----------------------------------------------------------------------------

test("A section loader with no API available rejects instead of resolving empty", async (t) => {
  const env = loadPageModule("app.js", {
    respond: healthyResponder,
    // The page can execute before web_api.js has published PAApi; a loader that
    // resolved here would report success having loaded nothing.
    overrides: { PAApi: null },
  });
  await env.settle();

  await assert.rejects(() => env.runSection("app-recent-logs"), /unavailable/);
  await assert.rejects(() => env.runSection("app-log-level"), /unavailable/);
  await assert.rejects(() => env.runSection("app-console-catalog"), /unavailable/);
});

test("A failed logs fetch reaches the bootstrap", async (t) => {
  const env = await loadDashboard({
    respond: (path) => {
      if (path === "/api/logs") throw new ApiError("Simulated logs failure", { kind: "network" });
      return healthyResponder(path);
    },
  });

  await assert.rejects(() => env.runSection("app-recent-logs"), /Simulated logs failure/);
});

test("An unrecognised log level is rejected rather than displayed", async (t) => {
  const env = await loadDashboard({
    respond: (path) => (path === "/api/config" ? { data: { system: { logLevel: 99 } } } : healthyResponder(path)),
  });

  await assert.rejects(
    () => env.runSection("app-log-level"),
    /Unknown log level: 99/,
    "an out-of-range level must not be rendered into the pill as if it were valid"
  );
});

test("A console operations response without a records array is rejected", async (t) => {
  const env = await loadDashboard({
    respond: (path, opts = {}) =>
      path === "/api/console" && opts.body?.command === "operations"
        ? { data: "not a records object" }
        : healthyResponder(path, opts),
  });

  await assert.rejects(
    () => env.runSection("app-console-catalog"),
    /not a records array/,
    "a malformed catalog response must surface, not leave Tab completion silently empty"
  );
});

// -----------------------------------------------------------------------------
// #109: the component grid
// -----------------------------------------------------------------------------

// Delivers a whole status frame through the stream and the Live Reading, which
// is what drives renderComponentStatus.
const pushStatus = (env, changes) => {
  env.pushStatus(statusFrame(changes));
  return env.element("component-status-grid").innerHTML;
};

test("A status field containing markup cannot inject into the grid", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  const html = pushStatus(env, { audio: { state: "ok", detail: '<img src=x onerror="alert(1)">' } });

  assert.ok(!html.includes("<img"), "a controller-supplied detail must not become markup");
  assert.match(html, /&lt;img/);
});

// -----------------------------------------------------------------------------
// Markup invariant over the shipped pages
// -----------------------------------------------------------------------------

