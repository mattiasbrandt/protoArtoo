#!/usr/bin/env node
"use strict";

// Multi-tab Browser Load Profile collector for issue #73.
//
// CONTEXT.md's Browser Load Profile: "primarily one visible Firefox tab, with
// a second ordinary tab supported; development may add a parallel Playwright
// Chromium session and briefly reach three tabs." webload_browser_capture.js
// (issue #66/#72) only ever opens one tab -- this is a sibling collector for
// the 2-tab/brief-3-tab/refresh part of that profile, not a rewrite of it.
// Reuses that script's collectDomState/captureTerminalScreenshots exports
// (its own module.exports comment already anticipates this) rather than
// re-deriving page-readiness gating logic.
//
// Deliberately smaller in scope than #66's single-page collector: per-tab
// network tracking here is counts-only (not the full per-request timing
// ledger), since this scenario's question is "do N concurrent tabs and a
// refresh survive," not per-request latency analysis.

const fs = require("fs");
const path = require("path");
const { performance } = require("perf_hooks");
const { chromium } = require("playwright");
const { collectDomState, captureTerminalScreenshots } = require("./webload_browser_capture.js");

const VIEWPORT = Object.freeze({ width: 1080, height: 800 });
const COMMIT_RE = /^[0-9a-f]{40}$/;
const REQUIRED_RESOURCES = Object.freeze([
  "/index.html", "/page_loader.js", "/web_api.js", "/diagnostics.js",
  "/status_stream.js", "/shell.js", "/health_signals.js", "/dome_command_map.js",
  "/dome_panel_model.js", "/dome_layout.js", "/dome_layout_render.js",
  "/dome_control.js", "/app.js", "/footer.js",
]);
// See webload_browser_capture.js's REQUIRED_APIS comment: these 4 are
// genuinely fetched by the real frontend on every ordinary page load, and
// the #73 PsychicHttp prototype ports none of them -- requiredApisOk will
// therefore always read false against that build, by design, not from a
// per-tab connection defect. Judge multi-tab runs against that build on
// requiredResourcesOk/domGatesPassed and the per-tab network counts, not
// requiredApisOk, until the prototype's API surface is completed.
const REQUIRED_APIS = Object.freeze(["/api/identity", "/api/logs", "/api/config", "/api/actions"]);

// Scenario timeline (ms from t0): 2 ordinary tabs open together, briefly
// overlap with a 3rd, then one refresh on tab 1, then settle. Reasonable
// scope per CONTEXT.md's own "briefly reach three tabs" wording, not a
// sustained 3-tab soak.
const TAB2_OPEN_AT_MS = 500;
const TAB3_OPEN_AT_MS = 12_000;
const TAB3_CLOSE_AT_MS = 17_000;
const TAB1_REFRESH_AT_MS = 25_000;
const OBSERVE_MS = 40_000;
const RUN_ID = "MULTITAB1";

function usage() {
  return `Usage:
  node tools/webload_multitab_capture.js \\
    --url http://10.0.0.22/index.html \\
    --commit <full 40-char sha of the tip under test> \\
    --out tasks/evidence/webload/<run-id>/multitab

Options:
  --url URL           Exact http:// controller URL ending in /index.html.
  --commit SHA         Full 40-char git SHA of the tip under test (evidence only).
  --out DIR            Evidence output directory (must be under tasks/evidence/webload,
                        must not already exist).
  --dry-run            Validate and print the fixed plan without launching Chromium.
  --help                Show this text.

Scenario: tab 1 opens at t0, tab 2 at t0+${TAB2_OPEN_AT_MS}ms (2 ordinary tabs), a 3rd tab
opens at t0+${TAB3_OPEN_AT_MS}ms and closes at t0+${TAB3_CLOSE_AT_MS}ms (brief 3-tab overlap), tab 1
refreshes at t0+${TAB1_REFRESH_AT_MS}ms, observation ends at t0+${OBSERVE_MS}ms.`;
}

function parseArgs(argv) {
  const args = { dryRun: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--help") args.help = true;
    else if (arg === "--dry-run") args.dryRun = true;
    else if (["--url", "--commit", "--out"].includes(arg)) {
      if (i + 1 >= argv.length) throw new Error(`${arg} requires a value`);
      args[{ "--url": "url", "--commit": "commit", "--out": "out" }[arg]] = argv[++i];
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }
  return args;
}

function validateArgs(args) {
  if (!args.url) throw new Error("--url is required");
  if (!args.commit || !COMMIT_RE.test(args.commit)) {
    throw new Error("--commit must be a full 40-character lowercase hex git SHA");
  }
  if (!args.out) throw new Error("--out is required");

  let parsedUrl;
  try {
    parsedUrl = new URL(args.url);
  } catch (_error) {
    throw new Error("--url must be a valid URL");
  }
  if (parsedUrl.protocol !== "http:" || parsedUrl.pathname !== "/index.html" ||
      parsedUrl.search || parsedUrl.hash) {
    throw new Error("--url must be an unmodified http:// controller URL ending in /index.html");
  }

  const repoRoot = path.resolve(__dirname, "..");
  const evidenceRoot = path.join(repoRoot, "tasks", "evidence", "webload");
  const out = path.resolve(args.out);
  const relative = path.relative(evidenceRoot, out);
  if (relative.startsWith("..") || path.isAbsolute(relative)) {
    throw new Error(`--out must be under ${evidenceRoot}`);
  }
  if (!args.dryRun && fs.existsSync(out)) {
    throw new Error(`refusing to overwrite existing evidence directory: ${out}`);
  }
  return { ...args, parsedUrl, out };
}

function wallNow() {
  return new Date().toISOString();
}

function monotonicMs(origin) {
  return Math.round((performance.now() - origin) * 1000) / 1000;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function appendNdjson(file, value) {
  fs.appendFileSync(file, `${JSON.stringify(value)}\n`);
}

function urlPath(rawUrl) {
  try {
    return new URL(rawUrl).pathname;
  } catch (_error) {
    return rawUrl;
  }
}

function killBrowserServer(browserServer) {
  const browserProcess = browserServer?.process();
  if (!browserProcess || browserProcess.exitCode !== null) return;
  try {
    browserProcess.kill("SIGKILL");
  } catch (_error) {
    // process may have already exited
  }
}

// One tab's own tracking: counts-only network summary (not a full per-request
// timing ledger, see file header), plus console/page-error NDJSON logs.
function makeTab(name, tabDir, origin) {
  fs.mkdirSync(tabDir, { recursive: true });
  const counts = new Map(); // path -> { attempts, successes, failures }
  const bump = (p, field) => {
    if (!counts.has(p)) counts.set(p, { attempts: 0, successes: 0, failures: 0 });
    counts.get(p)[field] += 1;
  };
  const networkLog = path.join(tabDir, "network.ndjson");
  const consoleLog = path.join(tabDir, "console.ndjson");
  const pageErrorsLog = path.join(tabDir, "page-errors.ndjson");

  const requestOrigins = new WeakMap();
  return {
    name,
    tabDir,
    onRequest(request) {
      const reqOrigin = new URL(request.url()).origin;
      const p = urlPath(request.url());
      requestOrigins.set(request, { origin: reqOrigin, path: p });
      if (reqOrigin === origin && (REQUIRED_RESOURCES.includes(p) || REQUIRED_APIS.includes(p))) {
        bump(p, "attempts");
      }
      appendNdjson(networkLog, { event: "request", method: request.method(), url: request.url(), at: wallNow() });
    },
    onResponse(response) {
      const info = requestOrigins.get(response.request());
      if (info && info.origin === origin && (REQUIRED_RESOURCES.includes(info.path) || REQUIRED_APIS.includes(info.path))) {
        if (response.status() >= 200 && response.status() < 300) bump(info.path, "successes");
      }
      appendNdjson(networkLog, { event: "response", status: response.status(), url: response.url(), at: wallNow() });
    },
    onRequestFailed(request) {
      const info = requestOrigins.get(request);
      if (info && info.origin === origin && (REQUIRED_RESOURCES.includes(info.path) || REQUIRED_APIS.includes(info.path))) {
        bump(info.path, "failures");
      }
      appendNdjson(networkLog, {
        event: "requestfailed", url: request.url(),
        failure: request.failure()?.errorText || "unknown", at: wallNow(),
      });
    },
    onConsole(message) {
      appendNdjson(consoleLog, { at: wallNow(), type: message.type(), text: message.text() });
    },
    onPageError(error) {
      appendNdjson(pageErrorsLog, { type: "pageerror", at: wallNow(), error: String(error) });
    },
    resetCounts() {
      counts.clear();
    },
    requiredResourcesOk() {
      return REQUIRED_RESOURCES.every((p) => (counts.get(p)?.successes || 0) > 0);
    },
    requiredApisOk() {
      return REQUIRED_APIS.every((p) => (counts.get(p)?.successes || 0) > 0);
    },
    countsSnapshot() {
      return Object.fromEntries(counts.entries());
    },
  };
}

async function attachTab(page, tab) {
  page.on("request", (r) => tab.onRequest(r));
  page.on("response", (r) => tab.onResponse(r));
  page.on("requestfailed", (r) => tab.onRequestFailed(r));
  page.on("console", (m) => tab.onConsole(m));
  page.on("pageerror", (e) => tab.onPageError(e));
}

async function finalizeTab(page, tab, config) {
  const domState = await collectDomState(page).catch((error) => {
    appendNdjson(path.join(tab.tabDir, "page-errors.ndjson"), {
      type: "final-dom-state-error", at: wallNow(), error: String(error),
    });
    return null;
  });
  const artifacts = {
    viewport: path.join(tab.tabDir, "final-viewport.png"),
    full: path.join(tab.tabDir, "final-full.png"),
  };
  const artifactErrors = [];
  await captureTerminalScreenshots(page, artifacts, artifactErrors, performance.now() + 5_000);
  const summary = {
    tab: tab.name,
    requiredResourcesOk: tab.requiredResourcesOk(),
    requiredApisOk: tab.requiredApisOk(),
    counts: tab.countsSnapshot(),
    domGatesPassed: domState ? Object.values(domState.gates).every(Boolean) : false,
    sseHasLastStatus: domState?.sseRuntime?.hasLastStatus ?? false,
    sseConnectionText: domState?.sseRuntime?.connectionText ?? null,
    domState,
    artifactErrors,
  };
  fs.writeFileSync(path.join(tab.tabDir, "page-state.json"), `${JSON.stringify(summary, null, 2)}\n`);
  return summary;
}

async function runCapture(config) {
  const origin = performance.now();
  const startedAt = wallNow();
  fs.mkdirSync(config.out, { recursive: true });

  const timeline = [];
  const record = (event, extra = {}) => {
    const entry = { event, at: wallNow(), monotonicMs: monotonicMs(origin), ...extra };
    timeline.push(entry);
    appendNdjson(path.join(config.out, "timeline.ndjson"), entry);
  };

  let browserServer;
  let browser;
  let context;
  const pages = {};
  const tabs = {};
  let hardStopTimer;

  try {
    browserServer = await chromium.launchServer({ headless: false });
    browser = await chromium.connect(browserServer.wsEndpoint());
    context = await browser.newContext({ viewport: VIEWPORT });

    const manifest = {
      issue: 73, run: RUN_ID, tipCommit: config.commit, url: config.url,
      startedAt, observeMs: OBSERVE_MS, viewport: VIEWPORT,
      browserVersion: browser.version(),
      playwrightVersion: require("playwright/package.json").version,
      scenario: {
        tab2OpenAtMs: TAB2_OPEN_AT_MS, tab3OpenAtMs: TAB3_OPEN_AT_MS,
        tab3CloseAtMs: TAB3_CLOSE_AT_MS, tab1RefreshAtMs: TAB1_REFRESH_AT_MS,
      },
      requiredResources: REQUIRED_RESOURCES, requiredApis: REQUIRED_APIS,
    };
    fs.writeFileSync(path.join(config.out, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);

    hardStopTimer = setTimeout(() => killBrowserServer(browserServer), OBSERVE_MS + 10_000);

    const openTab = async (name) => {
      const page = await context.newPage();
      const tabDir = path.join(config.out, name);
      const tab = makeTab(name, tabDir, config.parsedUrl.origin);
      await attachTab(page, tab);
      pages[name] = page;
      tabs[name] = tab;
      await page.goto(config.url, { waitUntil: "commit", timeout: 15_000 }).catch((error) => {
        record("navigation-error", { tab: name, error: String(error) });
      });
      record("tab-opened", { tab: name });
      return page;
    };

    await openTab("tab1");

    while (performance.now() - origin < OBSERVE_MS) {
      const elapsed = performance.now() - origin;
      if (elapsed >= TAB2_OPEN_AT_MS && !pages.tab2) {
        await openTab("tab2");
      }
      if (elapsed >= TAB3_OPEN_AT_MS && !pages.tab3) {
        await openTab("tab3");
      }
      if (elapsed >= TAB3_CLOSE_AT_MS && pages.tab3 && !pages.tab3.isClosed()) {
        await pages.tab3.close();
        record("tab-closed", { tab: "tab3" });
      }
      if (elapsed >= TAB1_REFRESH_AT_MS && !tabs.tab1.refreshed) {
        tabs.tab1.refreshed = true;
        tabs.tab1.resetCounts();
        await pages.tab1.reload({ waitUntil: "commit", timeout: 15_000 }).catch((error) => {
          record("refresh-error", { tab: "tab1", error: String(error) });
        });
        record("tab-refreshed", { tab: "tab1" });
      }
      await sleep(200);
    }

    record("observation-window-elapsed");

    const finalSummaries = {};
    for (const name of ["tab1", "tab2"]) {
      if (pages[name] && !pages[name].isClosed()) {
        finalSummaries[name] = await finalizeTab(pages[name], tabs[name], config);
      }
    }
    record("finalized");

    const outcome = {
      schemaVersion: 1, issue: 73, run: RUN_ID, tipCommit: config.commit,
      startedAt, finishedAt: wallNow(), observeMs: OBSERVE_MS,
      tabs: finalSummaries,
      allTabsPassed: Object.values(finalSummaries).every(
        (s) => s.requiredResourcesOk && s.requiredApisOk && s.domGatesPassed,
      ),
    };
    fs.writeFileSync(path.join(config.out, "outcome.json"), `${JSON.stringify(outcome, null, 2)}\n`);
    process.stdout.write(`${JSON.stringify({
      run: RUN_ID, allTabsPassed: outcome.allTabsPassed,
      tab1: finalSummaries.tab1 && {
        requiredResourcesOk: finalSummaries.tab1.requiredResourcesOk,
        requiredApisOk: finalSummaries.tab1.requiredApisOk,
        domGatesPassed: finalSummaries.tab1.domGatesPassed,
      },
      tab2: finalSummaries.tab2 && {
        requiredResourcesOk: finalSummaries.tab2.requiredResourcesOk,
        requiredApisOk: finalSummaries.tab2.requiredApisOk,
        domGatesPassed: finalSummaries.tab2.domGatesPassed,
      },
      output: config.out,
    })}\n`);
    return outcome.allTabsPassed ? 0 : 3;
  } finally {
    if (hardStopTimer) clearTimeout(hardStopTimer);
    for (const page of Object.values(pages)) {
      if (page && !page.isClosed()) await page.close().catch(() => {});
    }
    if (context) await context.close().catch(() => {});
    if (browser?.isConnected()) await browser.close().catch(() => {});
    killBrowserServer(browserServer);
  }
}

async function main() {
  let args;
  try {
    args = parseArgs(process.argv.slice(2));
    if (args.help) {
      process.stdout.write(`${usage()}\n`);
      return 0;
    }
    const config = validateArgs(args);
    if (config.dryRun) {
      process.stdout.write(`${JSON.stringify({
        issue: 73, run: RUN_ID, tipCommit: config.commit, url: config.url, output: config.out,
        scenario: {
          tab2OpenAtMs: TAB2_OPEN_AT_MS, tab3OpenAtMs: TAB3_OPEN_AT_MS,
          tab3CloseAtMs: TAB3_CLOSE_AT_MS, tab1RefreshAtMs: TAB1_REFRESH_AT_MS, observeMs: OBSERVE_MS,
        },
      }, null, 2)}\n`);
      return 0;
    }
    return await runCapture(config);
  } catch (error) {
    process.stderr.write(`ERROR: ${error.message}\n\n${usage()}\n`);
    return 1;
  }
}

if (require.main === module) {
  main().then((code) => {
    process.exitCode = code;
  });
}
