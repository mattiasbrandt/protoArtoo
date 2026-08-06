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
//
// Driven by tools/webload_baseline_run.py --stage full as one of that run's
// captures, and standalone for one-off scenarios. Two contracts exist so the
// coordinator can own this subprocess with the same code path it uses for the
// single-tab collector:
//   - exit codes match webload_browser_capture.js (0 usable, 2 evidence-artifact
//     failure, 3 failure observed, 4 stopped without a verdict),
//   - a root page-state.json carries the single-tab collector's verdict keys
//     (captureStatus/browserGatesPassed/terminalReason/sseState) alongside the
//     per-tab detail, so both captures are comparable without hand-diffing.
// --dry-run prints the scenario plan including ownershipBudgetMs, which is how
// the coordinator sizes its subprocess deadline instead of duplicating the
// timings below.

const fs = require("fs");
const path = require("path");
const { performance } = require("perf_hooks");
const { chromium } = require("playwright");
const {
  collectDomState, captureTerminalScreenshots, splitArtifactErrors, artifactLanded,
} = require("./webload_browser_capture.js");
const { DEFAULT_PAGE, pageNames, resolveProfile, describeProfile } =
  require("./webload_page_profiles.js");

const VIEWPORT = Object.freeze({ width: 1080, height: 800 });
const COMMIT_RE = /^[0-9a-f]{40}$/;
// Which page a run measures comes from --page (issue #94); the resources, APIs
// and readiness gate that define it live in tools/webload_page_profiles.js.
// Both collectors must gain page targeting together, or --stage full can only
// half-target a page.
//
// See that module's index API comment: the four index.html APIs are genuinely
// fetched by the real frontend on every ordinary load, and the #73 PsychicHttp
// prototype ports none of them -- requiredApisOk will therefore always read
// false against that build, by design, not from a per-tab connection defect.
// Judge multi-tab runs against that build on requiredResourcesOk/domGatesPassed
// and the per-tab network counts, not requiredApisOk, until the prototype's API
// surface is completed.
//
// Tracked but never gated on, exactly as in webload_browser_capture.js: SSE
// behavior is reportable evidence, not a pass/fail condition for a tab.
const SSE_PATH = "/api/events";

function isTrackedPath(profile, candidate) {
  return profile.requiredResources.includes(candidate) ||
    profile.requiredApis.includes(candidate) ||
    candidate === SSE_PATH;
}

// A conditional GET answered 304 Not Modified is a successful resource fetch --
// the browser has the bytes and the page uses them. This scenario refreshes tab
// 1 partway through, and on that reload the controller answers 304 for every
// already-cached asset, so counting 2xx alone reported a fully working page as
// a resource failure (observed live 2026-08-05: 11 of 14 required resources
// scored zero successes while every DOM gate passed).
function isSuccessStatus(status) {
  return (status >= 200 && status < 300) || status === 304;
}

// Scenario timeline (ms from t0): 2 ordinary tabs open together, briefly
// overlap with a 3rd, then one refresh on tab 1, then settle. Reasonable
// scope per CONTEXT.md's own "briefly reach three tabs" wording, not a
// sustained 3-tab soak.
const TAB2_OPEN_AT_MS = 500;
const TAB3_OPEN_AT_MS = 12_000;
const TAB3_CLOSE_AT_MS = 17_000;
const TAB1_REFRESH_AT_MS = 25_000;
const OBSERVE_MS = 40_000;
// Finalizing walks every surviving tab (DOM state + two screenshots each), so
// the self-kill has to sit well past the observation window or the collector
// would shoot itself while writing its own evidence.
const FINALIZE_RESERVE_MS = 15_000;
// Grace on top of the self-kill, so the coordinator's hard kill always lands
// strictly after this process has had its own chance to finish and flush.
const OWNERSHIP_GRACE_MS = 5_000;
const CONTROL_POLL_MS = 200;
const DEFAULT_TABS = 3;
const MIN_TABS = 2;
const MAX_TABS = 3;
// Which capture this is within a run, not which run it is -- the coordinator's
// --run-id names the run. Kept static because the label identifies the capture
// kind, and existing bundles are read by it.
const RUN_ID = "MULTITAB1";
// Provenance of this collector, never the ticket a given run is evidence for.
// Emitting the two as one field filed the #93 ADR 0017 acceptance bundle's
// multi-tab leg under issue 73. The run's own ticket arrives via --issue.
const HARNESS_ORIGIN_ISSUE = 73;

// The single description of what a run of N tabs does. Everything downstream
// -- the dry-run plan, the evidence manifest, the scenario loop, the
// coordinator's subprocess deadline -- reads this rather than the constants,
// so there is one place where the timeline can drift.
function buildScenario(tabs) {
  const usesThirdTab = tabs >= 3;
  return {
    tabs,
    steadyTabs: Math.min(tabs, 2),
    peakTabs: tabs,
    tab2OpenAtMs: TAB2_OPEN_AT_MS,
    tab3OpenAtMs: usesThirdTab ? TAB3_OPEN_AT_MS : null,
    tab3CloseAtMs: usesThirdTab ? TAB3_CLOSE_AT_MS : null,
    tab1RefreshAtMs: TAB1_REFRESH_AT_MS,
    observeMs: OBSERVE_MS,
    finalizeReserveMs: FINALIZE_RESERVE_MS,
    ownershipBudgetMs: OBSERVE_MS + FINALIZE_RESERVE_MS + OWNERSHIP_GRACE_MS,
  };
}

function usage() {
  return `Usage:
  node tools/webload_multitab_capture.js \\
    --url http://10.0.0.22/index.html \\
    --commit <full 40-char sha of the tip under test> \\
    --out tasks/evidence/webload/<run-id>/multitab \\
    [--page ${DEFAULT_PAGE}] \\
    [--tabs ${DEFAULT_TABS}] \\
    [--control-file tasks/evidence/webload/<run-id>/control.json]

Options:
  --url URL           Exact http:// controller URL whose path is the target page's.
  --commit SHA         Full 40-char git SHA of the tip under test (evidence only).
  --page NAME          Page profile to measure: ${pageNames().join(", ")} (default ${DEFAULT_PAGE}).
                        Selects the required resources, required APIs and readiness
                        gate together; see tools/webload_page_profiles.js.
  --out DIR            Evidence output directory (must be under tasks/evidence/webload,
                        must not already exist).
  --tabs N             Peak tab count, ${MIN_TABS}-${MAX_TABS} (default ${DEFAULT_TABS}). ${MIN_TABS} keeps two ordinary
                        tabs open for the whole window; ${MAX_TABS} adds the brief third-tab
                        overlap on top of that, so one run covers both scenarios.
  --control-file F     Optional coordinator file with a stopReason. Polled during the
                        observation window; a stopReason ends observation early so the
                        coordinator can react to a controller failure without waiting
                        out the full window.
  --dry-run            Validate and print the scenario plan without launching Chromium.
  --help                Show this text.

Scenario: tab 1 opens at t0, tab 2 at t0+${TAB2_OPEN_AT_MS}ms (2 ordinary tabs), with --tabs ${MAX_TABS} a
3rd tab opens at t0+${TAB3_OPEN_AT_MS}ms and closes at t0+${TAB3_CLOSE_AT_MS}ms (brief 3-tab overlap), tab 1
refreshes at t0+${TAB1_REFRESH_AT_MS}ms, observation ends at t0+${OBSERVE_MS}ms.

Exit codes match tools/webload_browser_capture.js: 0 all tabs usable, 2 evidence-artifact
failure, 3 a tab failure was observed, 4 stopped before a verdict was reached.`;
}

function parseArgs(argv) {
  const args = { dryRun: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--help") args.help = true;
    else if (arg === "--dry-run") args.dryRun = true;
    else if (["--url", "--commit", "--out", "--tabs", "--control-file", "--page", "--issue"].includes(arg)) {
      if (i + 1 >= argv.length) throw new Error(`${arg} requires a value`);
      const key = {
        "--url": "url",
        "--commit": "commit",
        "--out": "out",
        "--tabs": "tabs",
        "--control-file": "controlFile",
        "--page": "page",
        "--issue": "issue",
      }[arg];
      args[key] = argv[++i];
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }
  return args;
}

function ensureInsideEvidence(candidate, evidenceRoot, label) {
  const resolved = path.resolve(candidate);
  const relative = path.relative(evidenceRoot, resolved);
  if (relative.startsWith("..") || path.isAbsolute(relative)) {
    throw new Error(`${label} must be under ${evidenceRoot}`);
  }
  return resolved;
}

function parseTabs(raw) {
  if (raw === undefined) return DEFAULT_TABS;
  if (!/^\d+$/.test(String(raw))) {
    throw new Error(`--tabs must be an integer between ${MIN_TABS} and ${MAX_TABS}`);
  }
  const tabs = Number(raw);
  if (tabs < MIN_TABS || tabs > MAX_TABS) {
    throw new Error(`--tabs must be between ${MIN_TABS} and ${MAX_TABS}`);
  }
  return tabs;
}

function validateArgs(args) {
  if (!args.url) throw new Error("--url is required");
  if (!args.commit || !COMMIT_RE.test(args.commit)) {
    throw new Error("--commit must be a full 40-character lowercase hex git SHA");
  }
  if (!args.out) throw new Error("--out is required");
  const tabs = parseTabs(args.tabs);
  const profile = resolveProfile(args.page || DEFAULT_PAGE);

  let parsedUrl;
  try {
    parsedUrl = new URL(args.url);
  } catch (_error) {
    throw new Error("--url must be a valid URL");
  }
  if (parsedUrl.protocol !== "http:" || parsedUrl.pathname !== profile.path ||
      parsedUrl.search || parsedUrl.hash) {
    throw new Error(
      `--url must be an unmodified http:// controller URL ending in ${profile.path} ` +
      `(the "${profile.name}" page profile); pass --page to measure a different one`,
    );
  }

  const repoRoot = path.resolve(__dirname, "..");
  const evidenceRoot = path.join(repoRoot, "tasks", "evidence", "webload");
  const out = ensureInsideEvidence(args.out, evidenceRoot, "--out");
  const controlFile = args.controlFile
    ? ensureInsideEvidence(args.controlFile, evidenceRoot, "--control-file")
    : null;
  if (!args.dryRun && fs.existsSync(out)) {
    throw new Error(`refusing to overwrite existing evidence directory: ${out}`);
  }
  // null rather than a default ticket number: a bundle that does not know which
  // issue it belongs to should say so, not name the collector's origin ticket.
  let issue = null;
  if (args.issue !== undefined) {
    if (!/^[0-9]+$/.test(args.issue)) throw new Error("--issue must be a positive integer");
    issue = Number(args.issue);
  }
  return {
    ...args, profile, parsedUrl, out, controlFile, tabs, issue,
    scenario: buildScenario(tabs),
  };
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

// Same coordinator handshake webload_browser_capture.js uses: the file is
// written by the harness's StopArbiter, may not exist yet, and may be caught
// mid-write, so an unreadable file is simply "no stop requested yet".
function readControlFile(controlFile) {
  if (!controlFile || !fs.existsSync(controlFile)) return null;
  try {
    const value = JSON.parse(fs.readFileSync(controlFile, "utf8"));
    return value && typeof value === "object" ? value : null;
  } catch (_error) {
    return null;
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
function makeTab(name, tabDir, origin, profile) {
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
      if (reqOrigin === origin && isTrackedPath(profile, p)) bump(p, "attempts");
      appendNdjson(networkLog, { event: "request", method: request.method(), url: request.url(), at: wallNow() });
    },
    onResponse(response) {
      const info = requestOrigins.get(response.request());
      if (info && info.origin === origin && isTrackedPath(profile, info.path)) {
        if (isSuccessStatus(response.status())) bump(info.path, "successes");
      }
      appendNdjson(networkLog, { event: "response", status: response.status(), url: response.url(), at: wallNow() });
    },
    onRequestFailed(request) {
      const info = requestOrigins.get(request);
      if (info && info.origin === origin && isTrackedPath(profile, info.path)) {
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
    // A tab whose navigation was refused never gets a document, so every DOM
    // gate below reads false for a reason that has nothing to do with the page.
    // Recording it on the tab keeps that distinction in the verdict itself
    // rather than only in timeline.ndjson. Deliberately not retried: "the
    // second tab's navigation was reset and the tab stayed dead" is the
    // finding this scenario exists to capture, and a retry would hide it.
    navigationErrors: [],
    recordNavigationError(phase, error) {
      this.navigationErrors.push({ phase, at: wallNow(), error: String(error) });
    },
    resetCounts() {
      counts.clear();
    },
    requiredResourcesOk() {
      return profile.requiredResources.every((p) => (counts.get(p)?.successes || 0) > 0);
    },
    requiredApisOk() {
      return profile.requiredApis.every((p) => (counts.get(p)?.successes || 0) > 0);
    },
    // Same vocabulary webload_browser_capture.js's classifySse() produces, so
    // single-tab and multi-tab SSE evidence is read the same way. Derived from
    // this collector's counts rather than a per-request ledger, which is why
    // it cannot distinguish a failed attempt's ordering -- a failure anywhere
    // in the window classifies the tab as error-retrying.
    sseState(domState) {
      const sse = counts.get(SSE_PATH);
      const connectionText = domState?.sseRuntime?.connectionText || "";
      if ((sse?.failures || 0) > 0 || /connection lost/i.test(connectionText)) {
        return "error-retrying";
      }
      if (domState?.sseRuntime?.hasLastStatus) return "status-received";
      if ((sse?.successes || 0) > 0) return "open-no-status";
      if ((sse?.attempts || 0) > 0) return "connecting";
      return "not-started";
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

// Collects one tab's evidence but does not yet grade its artifacts: a
// screenshot that overran its cap is still being written from Node at this
// point, so grading here would report a file that is about to exist as
// missing. gradeTabArtifacts() finishes the summary once the pages are closed.
async function finalizeTab(page, tab, profile) {
  const domState = await collectDomState(page, profile).catch((error) => {
    appendNdjson(path.join(tab.tabDir, "page-errors.ndjson"), {
      type: "final-dom-state-error", at: wallNow(), error: String(error),
    });
    return null;
  });
  const artifacts = {
    viewport: path.join(tab.tabDir, "final-viewport.png"),
    full: path.join(tab.tabDir, "final-full.png"),
  };
  const rawArtifactErrors = [];
  await captureTerminalScreenshots(
    page, artifacts, rawArtifactErrors, performance.now() + 5_000,
  );
  return {
    tab: tab.name,
    refreshed: tab.refreshed === true,
    navigationErrors: tab.navigationErrors,
    requiredResourcesOk: tab.requiredResourcesOk(),
    requiredApisOk: tab.requiredApisOk(),
    counts: tab.countsSnapshot(),
    domGatesPassed: domState ? Object.values(domState.gates).every(Boolean) : false,
    sseState: tab.sseState(domState),
    sseHasLastStatus: domState?.sseRuntime?.hasLastStatus ?? false,
    sseConnectionText: domState?.sseRuntime?.connectionText ?? null,
    domState,
    artifacts,
    rawArtifactErrors,
  };
}

// Grades a finalized tab's artifacts and writes its page-state.json. Must run
// after that tab's page is closed, so no further artifact writes can land.
async function gradeTabArtifacts(summary, tabDir) {
  const { artifacts, rawArtifactErrors, ...rest } = summary;
  const { errors: artifactErrors, warnings: artifactWarnings } = await splitArtifactErrors(
    rawArtifactErrors,
    { "viewport screenshot": artifacts.viewport, "full-page screenshot": artifacts.full },
  );
  const graded = { ...rest, artifactErrors, artifactWarnings };
  fs.writeFileSync(path.join(tabDir, "page-state.json"), `${JSON.stringify(graded, null, 2)}\n`);
  return graded;
}

// SSE vocabulary ordered worst-first, so a run-level sseState reports the
// least healthy tab rather than averaging a failure away.
const SSE_SEVERITY = Object.freeze([
  "error-retrying", "not-started", "connecting", "open-no-status", "status-received",
]);

function worstSseState(summaries) {
  let worst = null;
  for (const summary of summaries) {
    const rank = SSE_SEVERITY.indexOf(summary.sseState);
    if (rank < 0) continue;
    if (worst === null || rank < SSE_SEVERITY.indexOf(worst)) worst = summary.sseState;
  }
  return worst;
}

async function runCapture(config) {
  const origin = performance.now();
  const startedAt = wallNow();
  const scenario = config.scenario;
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
      issue: config.issue, harnessOriginIssue: HARNESS_ORIGIN_ISSUE,
      run: RUN_ID, tipCommit: config.commit,
      page: config.profile.name, pageProfile: describeProfile(config.profile),
      url: config.url,
      startedAt, observeMs: scenario.observeMs, viewport: VIEWPORT,
      browserVersion: browser.version(),
      playwrightVersion: require("playwright/package.json").version,
      controlFile: config.controlFile,
      scenario,
      requiredResources: config.profile.requiredResources,
      requiredApis: config.profile.requiredApis,
    };
    fs.writeFileSync(path.join(config.out, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);

    hardStopTimer = setTimeout(
      () => killBrowserServer(browserServer),
      scenario.observeMs + scenario.finalizeReserveMs,
    );

    const openTab = async (name) => {
      const page = await context.newPage();
      const tabDir = path.join(config.out, name);
      const tab = makeTab(name, tabDir, config.parsedUrl.origin, config.profile);
      await attachTab(page, tab);
      pages[name] = page;
      tabs[name] = tab;
      await page.goto(config.url, { waitUntil: "commit", timeout: 15_000 }).catch((error) => {
        tab.recordNavigationError("open", error);
        record("navigation-error", { tab: name, error: String(error) });
      });
      record("tab-opened", { tab: name });
      return page;
    };

    const t0 = wallNow();
    const t0EpochMs = Date.parse(t0);
    await openTab("tab1");

    // "observation-deadline" means the scenario ran to completion; any other
    // value means the coordinator cut the window short and the tab verdicts
    // below describe an interrupted run, not a finished one.
    let terminalReason = "observation-deadline";
    while (performance.now() - origin < scenario.observeMs) {
      const control = readControlFile(config.controlFile);
      if (control && typeof control.stopReason === "string") {
        terminalReason = control.stopReason;
        record("coordinator-stop", { stopReason: control.stopReason });
        break;
      }
      const elapsed = performance.now() - origin;
      if (elapsed >= scenario.tab2OpenAtMs && !pages.tab2) {
        await openTab("tab2");
      }
      if (scenario.tab3OpenAtMs !== null && elapsed >= scenario.tab3OpenAtMs && !pages.tab3) {
        await openTab("tab3");
      }
      if (scenario.tab3CloseAtMs !== null && elapsed >= scenario.tab3CloseAtMs &&
          pages.tab3 && !pages.tab3.isClosed()) {
        await pages.tab3.close();
        record("tab-closed", { tab: "tab3" });
      }
      if (elapsed >= scenario.tab1RefreshAtMs && !tabs.tab1.refreshed) {
        tabs.tab1.refreshed = true;
        tabs.tab1.resetCounts();
        await pages.tab1.reload({ waitUntil: "commit", timeout: 15_000 }).catch((error) => {
          tabs.tab1.recordNavigationError("refresh", error);
          record("refresh-error", { tab: "tab1", error: String(error) });
        });
        record("tab-refreshed", { tab: "tab1" });
      }
      await sleep(CONTROL_POLL_MS);
    }

    const terminalAt = wallNow();
    record("observation-ended", { terminalReason });

    // Only the tabs the scenario expects to survive the window are graded. The
    // third tab is closed by design partway through, so its absence is not a
    // failure and it never gets a verdict.
    const collected = {};
    for (const name of ["tab1", "tab2"]) {
      if (pages[name] && !pages[name].isClosed()) {
        collected[name] = await finalizeTab(pages[name], tabs[name], config.profile);
      }
    }
    // Close every page before grading artifacts. A screenshot that overran its
    // cap is still being written from Node at this point; closing first means
    // nothing new can land, so what is on disk afterwards is the final answer.
    for (const page of Object.values(pages)) {
      if (page && !page.isClosed()) await page.close().catch(() => {});
    }
    const finalSummaries = {};
    for (const [name, summary] of Object.entries(collected)) {
      finalSummaries[name] = await gradeTabArtifacts(summary, tabs[name].tabDir);
    }
    record("finalized");

    const summaries = Object.values(finalSummaries);
    const expectedTabs = ["tab1", "tab2"];
    const missingTabs = expectedTabs.filter((name) => !finalSummaries[name]);
    const allTabsPassed = missingTabs.length === 0 && summaries.every(
      (s) => s.requiredResourcesOk && s.requiredApisOk && s.domGatesPassed,
    );
    const browserGatesPassed = missingTabs.length === 0 && summaries.every(
      (s) => s.domGatesPassed,
    );
    const artifactErrors = summaries.flatMap(
      (s) => s.artifactErrors.map((error) => ({ tab: s.tab, ...error })),
    );
    const artifactWarnings = summaries.flatMap(
      (s) => s.artifactWarnings.map((warning) => ({ tab: s.tab, ...warning })),
    );
    const navigationErrors = summaries.flatMap(
      (s) => s.navigationErrors.map((error) => ({ tab: s.tab, ...error })),
    );
    // The single-tab collector reaches "usable" through the coordinator's
    // statusReachableAt handshake. This scenario has no such handshake -- it
    // runs a fixed window on purpose -- so completing the window with every
    // graded tab passing is what "usable" means here.
    const captureStatus = terminalReason !== "observation-deadline"
      ? "stopped"
      : allTabsPassed ? "usable" : "browser-failure-observed";

    // Verdict keys first, mirroring webload_browser_capture.js's page-state.json
    // so the coordinator classifies both captures with one code path; per-tab
    // detail hangs off `tabs`.
    const result = {
      issue: config.issue, harnessOriginIssue: HARNESS_ORIGIN_ISSUE,
      run: RUN_ID, tipCommit: config.commit,
      // Which page this measurement is of; see webload_browser_capture.js.
      page: config.profile.name,
      t0, startedAt, finishedAt: wallNow(),
      observeMs: scenario.observeMs,
      observedWindowMs: Date.parse(terminalAt) - t0EpochMs,
      terminalAt, terminalReason, captureStatus,
      browserGatesPassed,
      allTabsPassed,
      sseState: worstSseState(summaries),
      navigationErrors,
      scenario,
      gradedTabs: expectedTabs,
      missingTabs,
      tabs: finalSummaries,
      artifactErrors,
      artifactWarnings,
    };
    const statePath = path.join(config.out, "page-state.json");
    let stateWritten = true;
    try {
      fs.writeFileSync(statePath, `${JSON.stringify(result, null, 2)}\n`);
      // outcome.json is kept for standalone runs that already consume it; the
      // coordinator reads page-state.json.
      fs.writeFileSync(
        path.join(config.out, "outcome.json"),
        `${JSON.stringify({ schemaVersion: 1, ...result }, null, 2)}\n`,
      );
    } catch (error) {
      stateWritten = false;
      process.stderr.write(`failed to write page state: ${error}\n`);
    }
    process.stdout.write(`${JSON.stringify({
      run: RUN_ID, page: config.profile.name, tabs: scenario.tabs,
      terminalReason, captureStatus,
      allTabsPassed, browserGatesPassed, sseState: result.sseState,
      missingTabs, navigationErrors, artifactErrors,
      artifactWarningCount: artifactWarnings.length, output: config.out,
    })}\n`);
    // Exit 2 is reserved for the measurement itself being absent, matching
    // webload_browser_capture.js. Missing terminal screenshots are recorded in
    // each tab's artifactErrors and in the run summary, but do not discard the
    // capture -- every tab's verdict, counts and DOM state are in the page
    // states written above.
    if (!stateWritten || !artifactLanded(statePath)) return 2;
    if (captureStatus === "usable") return 0;
    if (captureStatus === "browser-failure-observed") return 3;
    return 4;
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
        issue: config.issue, harnessOriginIssue: HARNESS_ORIGIN_ISSUE,
      run: RUN_ID, tipCommit: config.commit,
        page: config.profile.name, url: config.url,
        output: config.out, controlFile: config.controlFile,
        scenario: config.scenario,
        requiredResources: config.profile.requiredResources,
        requiredApis: config.profile.requiredApis,
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

// Exported for verification: the scenario plan the coordinator sizes its
// subprocess deadline from, and the response-status predicate whose 304 case
// only shows up on a refresh against a controller that answers conditional GETs.
module.exports = { buildScenario, isSuccessStatus };
