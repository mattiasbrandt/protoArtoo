// =============================================================================
// test/test_web/test_issue_107_109_fixes.js
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
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { loadPageModule, ApiError } from "./helpers/page_module_env.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");

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
const loadDashboard = async ({ respond, sseSupported = true } = {}) => {
  const stream = { subscriber: null };
  const env = loadPageModule("app.js", {
    respond,
    overrides: {
      PAStatusStream: {
        isSupported: () => sseSupported,
        getLastStatus: () => null,
        subscribe: (handler) => {
          stream.subscriber = handler;
          return () => {
            stream.subscriber = null;
          };
        },
      },
    },
  });
  await env.settle();
  return { ...env, stream };
};

// The default transport: every endpoint answers successfully.
const healthyResponder = (path, opts = {}) => {
  if (path === "/api/logs") return { data: OK_LOGS };
  if (path === "/api/config") return { data: OK_CONFIG };
  if (path === "/api/console" && opts.body?.command === "operations") return { data: OK_OPERATIONS };
  if (path === "/api/status") return { data: {} };
  return { data: {} };
};

// -----------------------------------------------------------------------------
// #107: loader error propagation
// -----------------------------------------------------------------------------

test("app.js registers its startup work as bootstrap sections", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  for (const name of ["app-initial-status", "app-recent-logs", "app-log-level", "app-console-catalog"]) {
    assert.ok(env.sectionNames().includes(name), `${name} must be a bootstrap section`);
  }
});

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

test("Recent logs are fetched once and not refetched on a later run", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  await env.runSection("app-recent-logs");
  const afterFirst = env.requests.filter((r) => r.path === "/api/logs").length;
  assert.equal(afterFirst, 1, "the section must fetch the log history");

  await env.runSection("app-recent-logs");

  assert.equal(
    env.requests.filter((r) => r.path === "/api/logs").length,
    afterFirst,
    "history already in the console must not be re-fetched over the live stream"
  );
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

test("A valid log level resolves", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  await assert.doesNotReject(() => env.runSection("app-log-level"));
  assert.ok(
    env.requests.some((r) => r.path === "/api/config"),
    "the level must come from the controller, not a default"
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

test("A well-formed console operations response resolves", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  await assert.doesNotReject(() => env.runSection("app-console-catalog"));
  assert.ok(
    env.requests.some((r) => r.path === "/api/console" && r.opts?.body?.command === "operations")
  );
});

// -----------------------------------------------------------------------------
// #109: the component grid
// -----------------------------------------------------------------------------

// Delivers a status payload the way the live stream does, which is what drives
// renderComponentStatus.
const pushStatus = (env, payload) => {
  assert.ok(env.stream.subscriber, "the dashboard must subscribe to the status stream");
  env.stream.subscriber("status", payload);
  return env.element("component-status-grid").innerHTML;
};

test("The component grid emits dt/dd wrapped in a dl", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  const html = pushStatus(env, { protoR2link: { state: "connected" }, audio: { state: "idle" } });

  assert.match(html, /^<dl class="status-grid">/, "dt and dd are only valid inside a dl");
  assert.match(html, /<\/dl>$/, "the list must be closed");
  assert.match(html, /<dt>/);
  assert.match(html, /<dd id="state-protoR2link">/);
  assert.match(html, /id="comp-protoR2link"/);
  assert.match(html, /id="detail-protoR2link"/);
});

test("The component grid renders the state the controller reported", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  const html = pushStatus(env, { audio: { state: "blocked_by_dome_uart", detail: "CHIRP blocked" } });

  assert.match(html, /blocked by dome uart/, "underscores must be softened for the operator");
  assert.match(html, /CHIRP blocked/);
});

test("A status field containing markup cannot inject into the grid", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });

  const html = pushStatus(env, { audio: { state: "ok", detail: '<img src=x onerror="alert(1)">' } });

  assert.ok(!html.includes("<img"), "a controller-supplied detail must not become markup");
  assert.match(html, /&lt;img/);
});

test("An unchanged component set is patched in place, not rebuilt", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });
  const grid = env.element("component-status-grid");

  pushStatus(env, { protoR2link: { state: "connected" } });
  // A marker that only survives if the grid's markup is left alone. Rebuilding
  // would discard it - and discard operator focus with it.
  grid.innerHTML += "<!-- not rebuilt -->";

  env.stream.subscriber("status", { protoR2link: { state: "spinning" } });

  assert.ok(grid.innerHTML.includes("<!-- not rebuilt -->"), "same components must not rebuild the grid");
  assert.equal(
    env.element("state-protoR2link").textContent,
    "spinning",
    "the changed value must still be patched into the existing element"
  );
});

test("A changed component set rebuilds the grid", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });
  const grid = env.element("component-status-grid");

  pushStatus(env, { protoR2link: { state: "connected" } });
  grid.innerHTML += "<!-- stale -->";

  env.stream.subscriber("status", { protoR2link: { state: "connected" }, audio: { state: "idle" } });

  assert.ok(!grid.innerHTML.includes("<!-- stale -->"), "a new component must force a rebuild");
  assert.match(grid.innerHTML, /id="comp-audio"/, "the new component must appear");
});

test("A status with no known components empties the grid", async (t) => {
  const env = await loadDashboard({ respond: healthyResponder });
  const grid = env.element("component-status-grid");

  pushStatus(env, { protoR2link: { state: "connected" } });
  assert.notEqual(grid.innerHTML, "");

  env.stream.subscriber("status", { estop: false });

  assert.equal(grid.innerHTML, "", "a grid with nothing to show must not keep showing stale rows");
});

// -----------------------------------------------------------------------------
// Markup invariant over the shipped pages
// -----------------------------------------------------------------------------

test("No shipped page puts a dt or dd outside a dl", (t) => {
  // Justified source-text assertion: this is an invariant about static markup in
  // the served HTML files. There is no code path to execute - the elements are
  // authored, not generated - so reading the files is the only way to check it.
  // The check is structural rather than a substring match: dl blocks are removed
  // first, and anything left over is by definition outside a list.
  for (const filename of ["index.html", "drive.html", "setup.html", "wifi.html"]) {
    const html = readFileSync(join(dataDir, filename), "utf-8");
    const outsideAnyDl = html.replace(/<dl[^>]*>[\s\S]*?<\/dl>/g, "");

    assert.ok(
      !/<dt[\s>]/.test(outsideAnyDl),
      `${filename}: found a <dt> outside any <dl> - invalid markup`
    );
    assert.ok(
      !/<dd[\s>]/.test(outsideAnyDl),
      `${filename}: found a <dd> outside any <dl> - invalid markup`
    );
  }
});
