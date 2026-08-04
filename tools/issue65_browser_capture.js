#!/usr/bin/env node
"use strict";

// Browser-side evidence collector for GitHub issue #65.
//
// This is deliberately not a Playwright test. It performs one visible Chromium
// navigation, records what the browser actually observes, and never retries,
// reloads, opens another controller tab, or polls a controller API itself.

const fs = require("fs");
const path = require("path");
const { performance } = require("perf_hooks");
const { chromium } = require("playwright");

const OBSERVE_MS = 30_000;
const POLL_MS = 200;
const TERMINAL_CAPTURE_RESERVE_MS = 3_500;
const CLOSE_RESERVE_MS = 800;
const VIEWPORT = Object.freeze({ width: 1080, height: 800 });
const FIXED_RUNS = Object.freeze({
  A1: {
    role: "A",
    commit: "1b2bed8e86cbfbe4756427233e75875ca7f189e3",
    requiredResources: [
      "/wifi.html",
      "/style.css",
      "/page_loader.js",
      "/web_api.js",
      "/status_stream.js",
      "/shell.js",
      "/wifi.js",
      "/footer.js",
    ],
  },
  B1: {
    role: "B",
    commit: "956c9360d51cc2cb49f5935e35568ca18784c562",
    requiredResources: [
      "/wifi.html",
      "/style.css",
      "/page_bootstrap.js",
      "/bootstrap_page_loader.js",
      "/web_api.js",
      "/status_stream.js",
      "/shell.js",
      "/wifi.js",
      "/footer.js",
    ],
  },
  A2: {
    role: "A",
    commit: "1b2bed8e86cbfbe4756427233e75875ca7f189e3",
    requiredResources: [
      "/wifi.html",
      "/style.css",
      "/page_loader.js",
      "/web_api.js",
      "/status_stream.js",
      "/shell.js",
      "/wifi.js",
      "/footer.js",
    ],
  },
  B2: {
    role: "B",
    commit: "956c9360d51cc2cb49f5935e35568ca18784c562",
    requiredResources: [
      "/wifi.html",
      "/style.css",
      "/page_bootstrap.js",
      "/bootstrap_page_loader.js",
      "/web_api.js",
      "/status_stream.js",
      "/shell.js",
      "/wifi.js",
      "/footer.js",
    ],
  },
});
const REQUIRED_APIS = Object.freeze(["/api/identity", "/api/config", "/api/wifi"]);

function usage() {
  return `Usage:
  node tools/issue65_browser_capture.js \\
    --run A1 \\
    --url http://10.0.0.22/wifi.html \\
    [--out tasks/evidence/issue-65/A1/browser] \\
    [--control-file tasks/evidence/issue-65/A1/control.json]

Options:
  --run RUN          One of A1, B1, A2, B2; locks the expected commit role.
  --url URL          Exact http:// controller URL ending in /wifi.html.
  --out DIR          Browser artifact directory (must be under tasks/evidence/issue-65).
  --control-file F   Optional coordinator file with statusReachableAt or stopReason.
  --dry-run          Validate and print the fixed plan without launching Chromium.
  --help             Show this text.

The output directory must not already exist. The collector navigates exactly once,
observes for at most 30 seconds, and never calls /api/status itself.`;
}

function parseArgs(argv) {
  const args = { dryRun: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--help") {
      args.help = true;
    } else if (arg === "--dry-run") {
      args.dryRun = true;
    } else if (["--run", "--url", "--out", "--control-file"].includes(arg)) {
      if (i + 1 >= argv.length) throw new Error(`${arg} requires a value`);
      const key = {
        "--run": "run",
        "--url": "url",
        "--out": "out",
        "--control-file": "controlFile",
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

function validateArgs(args) {
  if (!FIXED_RUNS[args.run]) {
    throw new Error("--run must be one of A1, B1, A2, B2");
  }
  if (!args.url) throw new Error("--url is required");

  let parsedUrl;
  try {
    parsedUrl = new URL(args.url);
  } catch (_error) {
    throw new Error("--url must be a valid URL");
  }
  if (parsedUrl.protocol !== "http:" || parsedUrl.pathname !== "/wifi.html" ||
      parsedUrl.search || parsedUrl.hash || parsedUrl.username || parsedUrl.password) {
    throw new Error("--url must be an unmodified http:// controller URL ending in /wifi.html");
  }

  const repoRoot = path.resolve(__dirname, "..");
  const evidenceRoot = path.join(repoRoot, "tasks", "evidence", "issue-65");
  const runRoot = path.join(evidenceRoot, args.run);
  const out = ensureInsideEvidence(
    args.out || path.join(evidenceRoot, args.run, "browser"),
    runRoot,
    "--out",
  );
  const controlFile = args.controlFile
    ? ensureInsideEvidence(args.controlFile, runRoot, "--control-file")
    : null;

  if (!args.dryRun && fs.existsSync(out)) {
    throw new Error(`refusing to overwrite existing evidence directory: ${out}`);
  }
  return { ...args, parsedUrl, repoRoot, evidenceRoot, out, controlFile };
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

function killBrowserServer(browserServer) {
  const browserProcess = browserServer?.process();
  if (!browserProcess || browserProcess.exitCode !== null) return;
  try {
    browserProcess.kill("SIGKILL");
  } catch (_error) {
    // The process may have exited between the exitCode check and kill.
  }
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

function summarizeAttempts(attempts, requiredPaths, controllerOrigin) {
  const summary = {};
  for (const requiredPath of requiredPaths) {
    const matching = attempts.filter((attempt) =>
      attempt.origin === controllerOrigin &&
      attempt.method === "GET" &&
      attempt.path === requiredPath
    );
    summary[requiredPath] = {
      attempts: matching.length,
      successfulAttempts: matching.filter(
        (attempt) => attempt.finishedAt && attempt.status >= 200 && attempt.status < 300,
      ).length,
      statuses: matching
        .filter((attempt) => Number.isInteger(attempt.status))
        .map((attempt) => attempt.status),
      failures: matching
        .filter((attempt) => attempt.failure)
        .map((attempt) => attempt.failure),
    };
  }
  return summary;
}

function allSummariesSuccessful(summary) {
  return Object.values(summary).every((item) => item.successfulAttempts > 0);
}

function readControlFile(controlFile) {
  if (!controlFile || !fs.existsSync(controlFile)) return null;
  try {
    const value = JSON.parse(fs.readFileSync(controlFile, "utf8"));
    return value && typeof value === "object" ? value : null;
  } catch (_error) {
    return null;
  }
}

async function bounded(label, action, artifactErrors, timeoutMs = 2_000) {
  let timer;
  try {
    return await Promise.race([
      action(),
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(`${label} timed out`)), timeoutMs);
      }),
    ]);
  } catch (error) {
    artifactErrors.push({ label, error: String(error), at: wallNow() });
    return null;
  } finally {
    if (timer) clearTimeout(timer);
  }
}

async function collectDomState(page, role) {
  return page.evaluate(async (expectedRole) => {
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
        borderTopColor: computed.borderTopColor,
        borderTopStyle: computed.borderTopStyle,
        fontFamily: computed.fontFamily,
        width: rect.width,
        height: rect.height,
      };
    };

    const posture = element("#wifi-posture-card")?.dataset.posture || "";
    const allowedPostures = [
      "recovery",
      "provisioning",
      "client",
      "client-failure",
      "standalone-ap",
    ];
    const selectedModes = [
      element("#wifi-mode-client")?.checked === true,
      element("#wifi-mode-standalone-ap")?.checked === true,
    ].filter(Boolean).length;
    const feedbackNode = element("#wifi-settings-feedback");
    const bodyStyle = style("body");
    const cardStyle = style("#wifi-posture-card");
    const recoverySelectors = [
      "#recovery-backdrop",
      "#recovery-loading",
      "#recovery-busy",
      "#recovery-no-response",
      "#recovery-retrying",
    ];
    const recovery = Object.fromEntries(recoverySelectors.map((selector) => {
      const node = element(selector);
      return [selector, {
        exists: Boolean(node),
        display: node ? getComputedStyle(node).display : null,
        visible: visible(selector),
      }];
    }));
    const bRecoveryReady = expectedRole !== "B" || recoverySelectors.every(
      (selector) => recovery[selector].exists && !recovery[selector].visible,
    );
    const runtime = {
      PAAssetsReady: window.PAAssetsReady === true,
      PAApi: Boolean(window.PAApi),
      PAStatusStream: Boolean(window.PAStatusStream),
      PageBootstrap: Boolean(window.PageBootstrap),
      PABootstrapWifi: Boolean(window.PABootstrapWifi),
    };
    const runtimeReady = runtime.PAAssetsReady && runtime.PAApi && runtime.PAStatusStream &&
      (expectedRole !== "B" || (runtime.PageBootstrap && runtime.PABootstrapWifi));
    const controls = {
      formVisible: visible("#wifi-settings-form"),
      saveVisible: visible("#wifi-save-settings-button"),
      saveEnabled: element("#wifi-save-settings-button")?.disabled === false,
      reloadVisible: visible("#reload-wifi-button"),
      reloadEnabled: element("#reload-wifi-button")?.disabled === false,
      applyVisible: visible("#wifi-apply-reboot-button"),
      selectedModes,
    };
    const hydrated = {
      feedback: text("#wifi-settings-feedback"),
      feedbackSuccess: Boolean(feedbackNode?.classList.contains("success")) &&
        text("#wifi-settings-feedback").startsWith("WiFi settings loaded at"),
      pendingSummary: text("#wifi-pending-summary"),
      postureDescription: text("#wifi-posture-desc"),
      posture,
      activeMode: text("#wifi-active-mode"),
      provisioningState: text("#wifi-provisioning-state"),
      clientState: text("#wifi-client-state"),
      applyGuidance: text("#wifi-apply-guidance"),
    };
    const dataReady = hydrated.feedbackSuccess &&
      hydrated.pendingSummary !== "" && hydrated.pendingSummary !== "Loading" &&
      hydrated.postureDescription !== "" &&
      hydrated.postureDescription !== "Loading WiFi posture..." &&
      allowedPostures.includes(hydrated.posture) &&
      hydrated.activeMode !== "" && hydrated.activeMode !== "--" &&
      hydrated.provisioningState !== "" && hydrated.provisioningState !== "--" &&
      hydrated.clientState !== "" && hydrated.clientState !== "--" &&
      hydrated.applyGuidance !== "" &&
      hydrated.applyGuidance !== "Loading reconnect guidance...";
    const shellReady = visible("#shell-top .topbar") && visible("#shell-top nav a.active");
    const controlsReady = controls.formVisible && controls.saveVisible &&
      controls.saveEnabled && controls.reloadVisible && controls.reloadEnabled &&
      controls.applyVisible && controls.selectedModes === 1;
    const stylingReady = bodyStyle?.backgroundColor === "rgb(12, 21, 37)" &&
      bodyStyle.fontFamily && !bodyStyle.fontFamily.includes("Times New Roman") &&
      cardStyle?.backgroundColor === "rgb(21, 34, 56)" &&
      cardStyle.borderTopStyle !== "none" && cardStyle.width > 0 && cardStyle.height > 0;

    let interactive = false;
    if (document.readyState === "complete" && runtimeReady && dataReady &&
        shellReady && controlsReady && stylingReady && bRecoveryReady) {
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
      controls,
      hydrated,
      styles: { body: bodyStyle, postureCard: cardStyle },
      recovery,
      gates: {
        documentReady: document.readyState === "complete",
        runtimeReady,
        dataReady,
        shellReady,
        controlsReady,
        stylingReady,
        recoveryReady: bRecoveryReady,
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
  }, role);
}

function browserSideReady(domState) {
  return domState && Object.values(domState.gates).every(Boolean);
}

function classifySse(attempts, domState) {
  const sseAttempts = attempts.filter((attempt) => attempt.path === "/api/events");
  const hasFailure = sseAttempts.some((attempt) => attempt.failure);
  const hasResponse = sseAttempts.some(
    (attempt) => attempt.status >= 200 && attempt.status < 300,
  );
  const connectionText = domState?.sseRuntime?.connectionText || "";
  if (hasFailure || /connection lost/i.test(connectionText)) return "error-retrying";
  if (domState?.sseRuntime?.hasLastStatus) return "status-received";
  if (hasResponse) return "open-no-status";
  if (sseAttempts.length > 0) return "connecting";
  return "not-started";
}

async function runCapture(config) {
  const fixed = FIXED_RUNS[config.run];
  const origin = performance.now();
  const startedAt = wallNow();
  const artifacts = {
    manifest: path.join(config.out, "browser-manifest.json"),
    network: path.join(config.out, "network.ndjson"),
    console: path.join(config.out, "console.ndjson"),
    pageErrors: path.join(config.out, "page-errors.ndjson"),
    state: path.join(config.out, "page-state.json"),
    dom: path.join(config.out, "dom.html"),
    viewport: path.join(config.out, "final-viewport.png"),
    full: path.join(config.out, "final-full.png"),
  };
  fs.mkdirSync(path.dirname(config.out), { recursive: true });
  // Atomic final-directory creation preserves the no-overwrite contract even
  // if two collectors for the same run start concurrently.
  fs.mkdirSync(config.out);

  const attempts = [];
  const attemptByRequest = new WeakMap();
  const artifactErrors = [];
  let browserServer;
  let browser;
  let context;
  let page;
  let hardStopTimer;
  let navigationError = null;
  let domCandidateAt = null;
  let usableAt = null;
  let finalDomState = null;
  let terminalReason = "observation-deadline";
  let terminalAt = null;
  let closingAt = null;
  let signalStop = null;
  process.once("SIGINT", () => { signalStop = "operator-interrupt"; });
  process.once("SIGTERM", () => { signalStop = "external-termination"; });

  try {
    // launchServer gives this one-run collector an owned process handle. If a
    // Playwright operation hangs at the hard workload deadline, finally can
    // terminate this browser without leaving page polling/SSE alive.
    browserServer = await chromium.launchServer({ headless: false });
    browser = await chromium.connect(browserServer.wsEndpoint());
    context = await browser.newContext({ viewport: VIEWPORT });
    page = await context.newPage();
    const userAgent = await page.evaluate(() => navigator.userAgent);

    const manifest = {
      issue: 65,
      run: config.run,
      role: fixed.role,
      expectedCommit: fixed.commit,
      url: config.url,
      startedAt,
      observeMs: OBSERVE_MS,
      viewport: VIEWPORT,
      browserVersion: browser.version(),
      userAgent,
      playwrightVersion: require("playwright/package.json").version,
      cachePolicy: "fresh non-persistent browser context; no storage state",
      navigationCount: 1,
      requiredResources: fixed.requiredResources,
      requiredApis: REQUIRED_APIS,
      controlFile: config.controlFile,
    };
    fs.writeFileSync(artifacts.manifest, `${JSON.stringify(manifest, null, 2)}\n`);

    browser.on("disconnected", () => {
      if (closingAt === null) signalStop = "unexpected-browser-disconnect";
      appendNdjson(artifacts.pageErrors, {
        type: "browser-disconnected",
        at: wallNow(),
        monotonicMs: monotonicMs(origin),
      });
    });
    page.on("request", (request) => {
      const attempt = {
        id: attempts.length + 1,
        method: request.method(),
        url: request.url(),
        origin: new URL(request.url()).origin,
        path: urlPath(request.url()),
        resourceType: request.resourceType(),
        startedAt: wallNow(),
        startedMonotonicMs: monotonicMs(origin),
        responseAt: null,
        status: null,
        finishedAt: null,
        failure: null,
        startedDuringHarnessShutdown: closingAt !== null,
      };
      attempts.push(attempt);
      attemptByRequest.set(request, attempt);
      appendNdjson(artifacts.network, { event: "request", ...attempt });
    });
    page.on("response", (response) => {
      const attempt = attemptByRequest.get(response.request());
      if (!attempt) return;
      attempt.responseAt = wallNow();
      attempt.status = response.status();
      attempt.responseHeaders = response.headers();
      appendNdjson(artifacts.network, {
        event: "response",
        id: attempt.id,
        at: attempt.responseAt,
        monotonicMs: monotonicMs(origin),
        status: attempt.status,
        headers: attempt.responseHeaders,
        duringHarnessShutdown: closingAt !== null,
      });
    });
    page.on("requestfinished", (request) => {
      const attempt = attemptByRequest.get(request);
      if (!attempt) return;
      attempt.finishedAt = wallNow();
      attempt.timing = request.timing();
      appendNdjson(artifacts.network, {
        event: "requestfinished",
        id: attempt.id,
        at: attempt.finishedAt,
        monotonicMs: monotonicMs(origin),
        timing: attempt.timing,
        duringHarnessShutdown: closingAt !== null,
      });
    });
    page.on("requestfailed", (request) => {
      const attempt = attemptByRequest.get(request);
      if (!attempt) return;
      attempt.failure = request.failure()?.errorText || "unknown request failure";
      appendNdjson(artifacts.network, {
        event: "requestfailed",
        id: attempt.id,
        at: wallNow(),
        monotonicMs: monotonicMs(origin),
        failure: attempt.failure,
        duringHarnessShutdown: closingAt !== null,
      });
    });
    page.on("console", (message) => {
      appendNdjson(artifacts.console, {
        at: wallNow(),
        monotonicMs: monotonicMs(origin),
        type: message.type(),
        text: message.text(),
        location: message.location(),
      });
    });
    page.on("pageerror", (error) => {
      appendNdjson(artifacts.pageErrors, {
        type: "pageerror",
        at: wallNow(),
        monotonicMs: monotonicMs(origin),
        error: String(error),
      });
    });
    page.on("crash", () => {
      signalStop = "browser-page-crash";
      appendNdjson(artifacts.pageErrors, {
        type: "page-crash",
        at: wallNow(),
        monotonicMs: monotonicMs(origin),
      });
    });
    page.on("close", () => {
      if (closingAt === null) signalStop = "unexpected-page-close";
      appendNdjson(artifacts.pageErrors, {
        type: "page-close",
        at: wallNow(),
        monotonicMs: monotonicMs(origin),
        expected: closingAt !== null,
      });
    });

    const t0 = wallNow();
    const t0EpochMs = Date.parse(t0);
    const deadline = performance.now() + OBSERVE_MS;
    hardStopTimer = setTimeout(() => killBrowserServer(browserServer), OBSERVE_MS);
    // Event listeners continue collecting while Playwright awaits the one
    // navigation. Wait for its commit before sampling the DOM so the initial
    // about:blank execution context cannot be destroyed under page.evaluate().
    try {
      await page.goto(config.url, { waitUntil: "commit", timeout: OBSERVE_MS });
    } catch (error) {
      navigationError = String(error);
    }

    while (performance.now() < deadline) {
      const control = readControlFile(config.controlFile);
      if (signalStop || (control && typeof control.stopReason === "string")) {
        terminalReason = signalStop || control.stopReason;
        break;
      }

      // Do not start a DOM sample so late that it can push workload
      // observation beyond the fixed 30-second ceiling.
      const sampleBudget = deadline - performance.now() - TERMINAL_CAPTURE_RESERVE_MS;
      if (sampleBudget <= 0) break;
      const domState = await bounded(
        "DOM readiness sample",
        () => collectDomState(page, fixed.role),
        artifactErrors,
        Math.min(1_000, sampleBudget),
      );
      if (domState) finalDomState = domState;

      const resourceSummary = summarizeAttempts(
        attempts,
        fixed.requiredResources,
        config.parsedUrl.origin,
      );
      const apiSummary = summarizeAttempts(
        attempts,
        REQUIRED_APIS,
        config.parsedUrl.origin,
      );
      const candidate = browserSideReady(domState) &&
        allSummariesSuccessful(resourceSummary) &&
        allSummariesSuccessful(apiSummary);
      if (candidate && !domCandidateAt) domCandidateAt = wallNow();

      const statusReachableAt = control?.statusReachableAt;
      if (domCandidateAt && typeof statusReachableAt === "string" &&
          Date.parse(statusReachableAt) >= Math.max(t0EpochMs, Date.parse(domCandidateAt))) {
        usableAt = statusReachableAt;
        terminalReason = "usable";
        break;
      }
      const sleepBudget = deadline - performance.now() - TERMINAL_CAPTURE_RESERVE_MS;
      if (sleepBudget <= 0) break;
      await sleep(Math.min(POLL_MS, sleepBudget));
    }

    terminalAt = wallNow();
    closingAt = wallNow();
    // Freeze the workload ledger at the terminal boundary. Requests generated
    // while artifacts are captured or the context is closing remain in the
    // raw event log, but cannot turn a failed workload into a pass.
    const workloadAttempts = JSON.parse(JSON.stringify(
      attempts.filter((attempt) => !attempt.startedDuringHarnessShutdown),
    ));
    finalDomState = await bounded(
      "final DOM state",
      () => collectDomState(page, fixed.role),
      artifactErrors,
      Math.max(1, Math.min(600, deadline - performance.now() - CLOSE_RESERVE_MS)),
    ) || finalDomState;
    const finalHtml = await bounded(
      "final DOM HTML",
      () => page.content(),
      artifactErrors,
      Math.max(1, Math.min(400, deadline - performance.now() - CLOSE_RESERVE_MS)),
    );
    if (finalHtml !== null) fs.writeFileSync(artifacts.dom, finalHtml);
    await bounded(
      "viewport screenshot",
      () => page.screenshot({ path: artifacts.viewport }),
      artifactErrors,
      Math.max(1, Math.min(700, deadline - performance.now() - CLOSE_RESERVE_MS)),
    );
    await bounded(
      "full-page screenshot",
      () => page.screenshot({ path: artifacts.full, fullPage: true }),
      artifactErrors,
      Math.max(1, Math.min(700, deadline - performance.now() - CLOSE_RESERVE_MS)),
    );

    // Stop page timers, polling, and SSE before deriving or writing results.
    if (!page.isClosed()) {
      await bounded(
        "page close",
        () => page.close(),
        artifactErrors,
        Math.max(1, Math.min(500, deadline - performance.now())),
      );
    }
    if (!page.isClosed() && context) {
      await bounded(
        "context close",
        () => context.close(),
        artifactErrors,
        Math.max(1, Math.min(250, deadline - performance.now())),
      );
    }
    if (!page.isClosed() && browser?.isConnected()) {
      await bounded(
        "browser close",
        () => browser.close(),
        artifactErrors,
        Math.max(1, deadline - performance.now()),
      );
    }
    const resourceSummary = summarizeAttempts(
      workloadAttempts,
      fixed.requiredResources,
      config.parsedUrl.origin,
    );
    const apiSummary = summarizeAttempts(
      workloadAttempts,
      REQUIRED_APIS,
      config.parsedUrl.origin,
    );
    const browserGatesPassed = browserSideReady(finalDomState) &&
      allSummariesSuccessful(resourceSummary) &&
      allSummariesSuccessful(apiSummary);
    const captureStatus = terminalReason === "usable" && browserGatesPassed
      ? "usable"
      : terminalReason === "observation-deadline"
        ? "browser-failure-observed"
        : "stopped";
    const result = {
      issue: 65,
      run: config.run,
      role: fixed.role,
      expectedCommit: fixed.commit,
      t0,
      domCandidateAt,
      usableAt,
      terminalAt,
      terminalReason,
      captureStatus,
      browserGatesPassed,
      navigationError,
      resourceSummary,
      apiSummary,
      sseState: classifySse(workloadAttempts, finalDomState),
      domState: finalDomState,
      attempts: workloadAttempts,
      artifactErrors,
    };
    fs.writeFileSync(artifacts.state, `${JSON.stringify(result, null, 2)}\n`);
    process.stdout.write(`${JSON.stringify({
      run: config.run,
      terminalReason,
      captureStatus,
      domCandidateAt,
      usableAt,
      artifactErrors,
      output: config.out,
    })}\n`);
    if (artifactErrors.length > 0) return 2;
    if (captureStatus === "usable") return 0;
    if (captureStatus === "browser-failure-observed") return 3;
    return 4;
  } finally {
    closingAt = closingAt || wallNow();
    if (hardStopTimer) clearTimeout(hardStopTimer);
    // Do not await uncancellable Playwright cleanup here: the live controller
    // workload has a strict 30-second ceiling. The server process belongs only
    // to this fresh collector, so a synchronous hard stop is the safe fallback.
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
    const fixed = FIXED_RUNS[config.run];
    if (config.dryRun) {
      process.stdout.write(`${JSON.stringify({
        issue: 65,
        run: config.run,
        role: fixed.role,
        expectedCommit: fixed.commit,
        url: config.url,
        output: config.out,
        controlFile: config.controlFile,
        observeMs: OBSERVE_MS,
        viewport: VIEWPORT,
        navigationCount: 1,
        controllerApiRequestsByCollector: 0,
        requiredResources: fixed.requiredResources,
        requiredApis: REQUIRED_APIS,
      }, null, 2)}\n`);
      return 0;
    }
    return await runCapture(config);
  } catch (error) {
    process.stderr.write(`ERROR: ${error.message}\n\n${usage()}\n`);
    return 1;
  }
}

main().then((code) => {
  process.exitCode = code;
});
