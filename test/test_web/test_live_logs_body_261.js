// =============================================================================
// test/test_web/test_live_logs_body_261.js
//
// #261 - the Live Logs panel rendered whatever came back at 200 as log lines,
//        so a fixture server (or a captive portal / intercepting proxy) that
//        answered /api/logs with index.html painted the dashboard's own markup
//        into the operator's log view.
//
// Two production seams are exercised, both through the shipped files:
//   - data/web_api.js reports the response's content type, so a caller can
//     tell log text from an intercepted page (vm-hosted here with a fetch mock,
//     the way test_paapi_cancellation.js hosts it);
//   - data/app.js's app-recent-logs loader accepts a response as log history
//     only when it is text/plain, and otherwise shows the page's existing
//     unreachable line and rejects so the bootstrap retries.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { loadPageModule, ApiError } from "./helpers/page_module_env.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const webApiSrc = readFileSync(join(__dirname, "../../data/web_api.js"), "utf-8");

// A ring body in the shape src/web/api_logs.cpp actually sends: one
// "[<millis>][<level>][<tag>] message" line per entry, newline separated.
const DEVICE_LOG_BODY =
  "[1204][I][boot] protoArtoo starting\n[1731][I][wifi] connected 192.168.1.42\n[2050][W][rc] no SBUS frames yet";

// What tools/serve_editor_fixture.py used to answer /api/logs with before
// #261: the dashboard shell, at 200, as text/html.
const INDEX_HTML_BODY =
  '<!DOCTYPE html>\n<html lang="en">\n<div class="opmode-grid">\n<button id="topbar-reboot">Reboot</button>\n</script>\n</html>';

// -----------------------------------------------------------------------------
// The transport reports the media type it received
// -----------------------------------------------------------------------------

// Hosts the shipped data/web_api.js against a one-shot fetch mock, so the
// assertion is about what PAApi returns rather than about a hand-written model.
const runPAApi = async ({ body, contentType, status = 200 }) => {
  const context = {
    window: {
      setTimeout: (fn, ms) => setTimeout(fn, ms),
      clearTimeout: (id) => clearTimeout(id),
      addEventListener: () => {},
      location: { origin: "http://device" },
    },
    document: { addEventListener: () => {} },
    console: { warn: () => {}, log: () => {}, error: () => {} },
    AbortController,
    Date,
    URLSearchParams,
    JSON,
    Promise,
    Error,
    String,
    Number,
    Object,
    Math,
    fetch: async () => ({
      ok: status >= 200 && status < 300,
      status,
      headers: { get: (name) => (name.toLowerCase() === "content-type" ? contentType : null) },
      json: async () => JSON.parse(body),
      text: async () => body,
    }),
  };
  context.globalThis = context;
  vm.runInNewContext(webApiSrc, context);
  return context.window.PAApi.get("/api/logs", { cache: "no-store" });
};

test("PAApi reports the content type of a text/plain response", async () => {
  const result = await runPAApi({ body: DEVICE_LOG_BODY, contentType: "text/plain" });

  assert.equal(result.data, DEVICE_LOG_BODY, "a text body must still arrive as text");
  assert.equal(
    result.contentType,
    "text/plain",
    "PAApi must surface the media type; without it no caller can tell log text " +
      "from an intercepted page"
  );
});

test("PAApi reports the content type of an intercepted HTML response", async () => {
  const result = await runPAApi({ body: INDEX_HTML_BODY, contentType: "text/html; charset=utf-8" });

  assert.equal(
    result.contentType,
    "text/html; charset=utf-8",
    "the header must be reported as received, parameters included"
  );
});

test("PAApi reports an empty content type when the response declares none", async () => {
  const result = await runPAApi({ body: DEVICE_LOG_BODY, contentType: null });

  assert.equal(
    result.contentType,
    "",
    "a missing header must read as empty, never as undefined or a guessed default"
  );
});

// -----------------------------------------------------------------------------
// The Live Logs loader accepts log text only
// -----------------------------------------------------------------------------

// Brings the dashboard up with a controllable /api/logs answer and hands back
// the log panel element, so a test can read what the operator would see.
const loadDashboard = async (logsResponse) => {
  const env = loadPageModule("app.js", {
    respond: (path) => {
      if (path === "/api/logs") {
        return typeof logsResponse === "function" ? logsResponse() : logsResponse;
      }
      return { data: {} };
    },
  });
  await env.settle();
  return { env, panel: env.element("log-console") };
};

test("A text/plain log body is rendered as log lines", async () => {
  const { env, panel } = await loadDashboard({ data: DEVICE_LOG_BODY, contentType: "text/plain" });

  await env.runSection("app-recent-logs");

  assert.ok(
    panel.innerHTML.includes("connected 192.168.1.42"),
    `the log history must reach the panel; panel was: ${panel.innerHTML}`
  );
  assert.equal(
    (panel.innerHTML.match(/class="log-line/g) || []).length,
    3,
    "each ring line must render as its own log line"
  );
  assert.ok(
    !panel.innerHTML.includes("connection lost"),
    "a good body must not show the unreachable notice"
  );
});

test("A text/plain body with a charset parameter is still log text", async () => {
  const { env, panel } = await loadDashboard({
    data: DEVICE_LOG_BODY,
    contentType: "text/plain; charset=utf-8",
  });

  await env.runSection("app-recent-logs");

  assert.ok(
    panel.innerHTML.includes("protoArtoo starting"),
    "the media type must be compared without its parameters, or every server " +
      "that sends a charset is refused"
  );
});

test("An HTML body at 200 is refused instead of rendered as log lines", async () => {
  const { env, panel } = await loadDashboard({
    data: INDEX_HTML_BODY,
    contentType: "text/html; charset=utf-8",
    status: 200,
  });

  await assert.rejects(
    () => env.runSection("app-recent-logs"),
    (error) => error?.kind === "network",
    "a body that is not log text must reject the section so the bootstrap retries"
  );

  assert.ok(
    !panel.innerHTML.includes("opmode-grid") && !panel.innerHTML.includes("topbar-reboot"),
    `the refused body must not reach the panel; panel was: ${panel.innerHTML}`
  );
  assert.ok(
    panel.innerHTML.includes("connection lost"),
    `the refusal must show the existing unreachable line; panel was: ${panel.innerHTML}`
  );
  assert.ok(
    !panel.innerHTML.includes("No log history available yet."),
    "a refusal is not an empty ring - claiming the controller has no history " +
      "would send the operator looking in the wrong place"
  );
});

test("A refused response leaves the retry live rather than freezing on the notice", async () => {
  let answer = { data: INDEX_HTML_BODY, contentType: "text/html; charset=utf-8" };
  const { env, panel } = await loadDashboard(() => answer);

  await assert.rejects(() => env.runSection("app-recent-logs"));

  // The bootstrap retries a failed section. That retry must actually re-fetch:
  // if the notice were pushed into the log model, loadRecentLogs would see a
  // non-empty history and return early, stranding the panel on the notice.
  answer = { data: DEVICE_LOG_BODY, contentType: "text/plain" };
  await env.runSection("app-recent-logs");

  assert.equal(
    env.requests.filter((r) => r.path === "/api/logs").length,
    2,
    "the retry must go back to the controller"
  );
  assert.ok(
    panel.innerHTML.includes("connected 192.168.1.42"),
    `the retry's history must replace the notice; panel was: ${panel.innerHTML}`
  );
  assert.ok(
    !panel.innerHTML.includes("connection lost"),
    "the notice must not survive a successful retry"
  );
});

test("A response with no content type is refused", async () => {
  const { env, panel } = await loadDashboard({ data: DEVICE_LOG_BODY, contentType: "" });

  await assert.rejects(
    () => env.runSection("app-recent-logs"),
    "an undeclared media type cannot be confirmed as log text"
  );
  assert.ok(
    !panel.innerHTML.includes("protoArtoo starting"),
    "an unconfirmed body must not be rendered as history"
  );
});

test("An empty text/plain body shows the empty state, not the unreachable line", async () => {
  const { env, panel } = await loadDashboard({ data: "", contentType: "text/plain" });

  await env.runSection("app-recent-logs");

  assert.ok(
    panel.innerHTML.includes("No log history available yet."),
    `an empty ring is its own fact and has its own line; panel was: ${panel.innerHTML}`
  );
  assert.ok(
    !panel.innerHTML.includes("connection lost"),
    "an empty ring is not a connection fault"
  );
});

test("A whitespace-only text/plain body shows the empty state", async () => {
  const { env, panel } = await loadDashboard({ data: "\n\n   \n", contentType: "text/plain" });

  await env.runSection("app-recent-logs");

  assert.ok(
    panel.innerHTML.includes("No log history available yet."),
    "blank lines are not log history and must not render as empty log lines"
  );
});

// -----------------------------------------------------------------------------
// A fetch that never produced a body says so in the panel too
//
// The bootstrap's recovery overlay names these while it is up, but it hides
// again as soon as every section has settled - and the panel was then left
// literally blank, which is the state the ticket's outcome rules out.
// -----------------------------------------------------------------------------

const rejectingDashboard = async (error) =>
  loadDashboard(() => {
    throw error;
  });

test("The device's 503 log-buffer exit shows the unreachable line, not a blank panel", async () => {
  const { env, panel } = await rejectingDashboard(
    new ApiError("Device unavailable", { kind: "http", status: 503 })
  );

  await assert.rejects(
    () => env.runSection("app-recent-logs"),
    "a 503 must still reach the bootstrap, which honours its Retry-After"
  );
  assert.ok(
    panel.innerHTML.includes("connection lost"),
    `the panel must say the logs did not load; panel was: ${JSON.stringify(panel.innerHTML)}`
  );
});

test("A transport failure shows the unreachable line, not a blank panel", async () => {
  const { env, panel } = await rejectingDashboard(
    new ApiError("Network request failed", { kind: "network" })
  );

  await assert.rejects(() => env.runSection("app-recent-logs"));
  assert.ok(
    panel.innerHTML.includes("connection lost"),
    `the panel must say the logs did not load; panel was: ${JSON.stringify(panel.innerHTML)}`
  );
});

test("A cancelled section run leaves the panel alone", async () => {
  const { env, panel } = await rejectingDashboard(
    new ApiError("Request cancelled", { kind: "cancelled" })
  );

  await assert.rejects(() => env.runSection("app-recent-logs"));
  assert.ok(
    !panel.innerHTML.includes("connection lost"),
    "the page cancelled its own run - reporting that as a lost connection " +
      `would be false; panel was: ${JSON.stringify(panel.innerHTML)}`
  );
});
