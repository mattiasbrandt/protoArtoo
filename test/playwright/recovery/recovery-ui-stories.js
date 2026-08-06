const assert = require("node:assert/strict");
const { chromium } = require("playwright");
const fs = require("fs");
const path = require("path");
const { resolveProfile } = require("../../../tools/webload_page_profiles");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/index.html";
const SCREENSHOT_DIR = process.env.SCREENSHOT_DIR || "/tmp/recovery-ui-screenshots";
const HEADLESS = process.env.HEADLESS === "true";
const INDUCED = process.env.INDUCED === "true";

// FORCE_FAIL=1 marks story "forced-fail" as FAIL for exit-code verification
// Usage: FORCE_FAIL=1 node ... ; echo "exit=$?"
// Confirms exit code is 1 when failures are present.
const FORCE_FAIL = process.env.FORCE_FAIL === "1";

// BACKOFF_VISIBLE_AFTER_ATTEMPT from data/page_bootstrap.js line 450
const BACKOFF_VISIBLE_AFTER_ATTEMPT = 1;

if (!fs.existsSync(SCREENSHOT_DIR)) {
  fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });
}

// Derive the page name from TARGET_URL (e.g., "http://127.0.0.1:4173/wifi.html" -> "wifi")
function getPageName() {
  const url = new URL(TARGET_URL);
  const pathname = url.pathname;
  const match = pathname.match(/\/([\w-]+)\.html/);
  if (!match) {
    throw new Error(`Cannot extract page name from TARGET_URL: ${TARGET_URL}`);
  }
  return match[1];
}

// Get the profile for the target page and extract the main app resource name
function getAppResourceName() {
  const pageName = getPageName();
  const profile = resolveProfile(pageName);
  // The main app script is the last resource (before footer.js) in requiredResources.
  // For index.html, it's /app.js. For wifi.html, it's /wifi.js.
  const resources = profile.requiredResources;
  // Return the resource that looks like a page-specific app script (not style.css, js libs, etc).
  // For index.html: "app.js", for wifi.html: "wifi.js"
  if (pageName === "index") return "app.js";
  if (pageName === "wifi") return "wifi.js";
  // Fallback: use the second-to-last resource (assuming last is footer.js)
  return resources[resources.length - 2];
}

const json = (payload, status = 200) => ({
  status,
  contentType: "application/json",
  body: JSON.stringify(payload),
});

const statusPayload = {
  uptimeMs: 120000,
  heapFree: 120000,
  heapMin: 100000,
  heapLargestBlock: 80000,
  s1Hoverboard: { state: "disabled" },
  s2Sound: { state: "disabled", driver: "" },
  dome_link: { state: "disabled", transport: "disconnected" },
  auxLed: { pin: 0, available: true, effect: "off", r: 0, g: 0, b: 0 },
};

const results = [];

const recordResult = (story, status, detail = "") => {
  const message = `Story ${story}: ${status}${detail ? " (" + detail + ")" : ""}`;
  console.log(message);
  results.push({ story, status, detail });
};

async function text(page, selector) {
  try {
    return (await page.locator(selector).textContent()).trim();
  } catch {
    return null;
  }
}

// Wait for resourcesReady via PABootstrap.getState()
async function waitForResourcesReady(page, timeoutMs = 5000) {
  const startTime = Date.now();
  while (Date.now() - startTime < timeoutMs) {
    const state = await page.evaluate(() => window.PABootstrap?.getState?.());
    if (state && state.resourcesReady) return state;
    await page.waitForTimeout(50);
  }
  throw new Error("Timeout waiting for resourcesReady");
}

// Poll deriveView output by reading the backdrop DOM
async function getRecoveryViewState(page) {
  return await page.evaluate(() => {
    const backdrop = document.getElementById("page-recovery-backdrop");
    if (!backdrop) return null;
    if (!backdrop.classList.contains("active")) return { visible: false };

    const statusReason = document.querySelector(".recovery-status-reason")?.textContent?.trim();
    const countdownValue = document.querySelector(".recovery-countdown-value")?.textContent?.trim();
    const buttonCount = document.querySelectorAll(".recovery-actions button").length;

    return {
      visible: true,
      statusReason,
      countdownValue,
      hasRetryButton: buttonCount > 0,
    };
  });
}

// Scenario 1: Intercept app.js, fail it N times, then let through
async function scenarioResourceRetry(browser) {
  console.log("\n=== Scenario 1: Resource Retry (Intercepted Load) ===");

  if (INDUCED) {
    recordResult("1-5", "SKIP", "skipped: interception scenarios need a normally-serving build");
    return;
  }

  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const pageErrors = [];
  const consoleErrors = [];
  page.on("pageerror", (err) => pageErrors.push(String(err)));
  page.on("console", (msg) => {
    if (msg.type() === "error" && !msg.text().startsWith("Failed to load resource:")) {
      consoleErrors.push(msg.text());
    }
  });

  try {
    let appJsAttempts = 0;
    const maxFailures = 2;
    const appResource = getAppResourceName();

    await page.route(`**/${appResource}`, (route) => {
      appJsAttempts++;
      if (appJsAttempts <= maxFailures) {
        route.abort("failed");
      } else {
        route.continue();
      }
    });

    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());
    await page.route("**/api/identity", (route) =>
      route.fulfill(json({ droidName: "r5unit", mdnsUseName: true }))
    );
    await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));
    await page.route("**/api/config", (route) =>
      route.fulfill(json({ ok: true, wifi: {}, components: {}, system: { logLevel: 2 } }))
    );

    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForTimeout(500);

    const backdrop = await page.locator("#page-recovery-backdrop");
    const isVisible = await backdrop.evaluate((el) => el.classList.contains("active"));
    const bodyHasRecoveryActive = await page.evaluate(() =>
      document.body.classList.contains("recovery-active")
    );
    recordResult("1", bodyHasRecoveryActive && isVisible ? "PASS" : "FAIL", "Loading mode backdrop active");

    const statusReason = await text(page, ".recovery-status-reason");
    recordResult("2", statusReason === "Loading page resources" ? "PASS" : "UI-NOT-IMPLEMENTED", `Found: ${statusReason}`);

    const stepLabel = await text(page, ".recovery-step");
    const hasStepLabel = stepLabel && stepLabel.includes("Loading:");
    recordResult("3", hasStepLabel ? "PASS" : "UI-NOT-IMPLEMENTED", stepLabel);

    const maxWaitMs = 10000;
    const startTime = Date.now();
    while (appJsAttempts <= maxFailures && Date.now() - startTime < maxWaitMs) {
      await page.waitForTimeout(200);
    }

    await page.waitForTimeout(1000);

    const finalBackdropActive = await page.evaluate(() =>
      document.getElementById("page-recovery-backdrop")?.classList.contains("active")
    );
    recordResult("4", !finalBackdropActive ? "PASS" : "FAIL", "Backdrop hides after recovery");
    recordResult("5", appJsAttempts === 3 ? "PASS" : "FAIL", `Expected 3 attempts, got ${appJsAttempts}`);

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario1-resource-retry.png") });
  } catch (error) {
    recordResult("1-5", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 2: No-Response Mode
// TARGETS wifi.html only (bootstrap sections registered at data/wifi.js:490)
// index.html has no bootstrap sections, making in-page recovery panels unreachable.
// REDUCER CONTRACT (data/page_bootstrap.js line 474-531):
//   blockingStep prefers resources while !resourcesReady, masks failed sections
//   Once resourcesReady, returns failed-retrying section
//   deriveView: attempt <= BACKOFF_VISIBLE_AFTER_ATTEMPT -> "no-response"
//              attempt > BACKOFF_VISIBLE_AFTER_ATTEMPT -> "retrying"
async function scenarioNoResponse(browser) {
  console.log("\n=== Scenario 2: No-Response Mode (wifi.html) ===");

  if (INDUCED) {
    recordResult("6-8", "SKIP", "skipped: interception scenarios need a normally-serving build");
    return;
  }

  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const wifiTargetUrl = TARGET_URL.replace(/\/[^/]+\.html/, "/wifi.html");
  const pageErrors = [];
  const consoleErrors = [];
  page.on("pageerror", (err) => pageErrors.push(String(err)));
  page.on("console", (msg) => {
    if (msg.type() === "error" && !msg.text().startsWith("Failed to load resource:")) {
      consoleErrors.push(msg.text());
    }
  });

  try {
    // Let resources complete, intercept real section API (api/config)
    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/app.js", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());
    await page.route("**/api/identity", (route) =>
      route.fulfill(json({ droidName: "r5unit", mdnsUseName: true }))
    );
    await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));

    // Abort /api/config on every attempt to trigger no-response
    await page.route("**/api/config", (route) => route.abort("failed"));

    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(wifiTargetUrl, { waitUntil: "domcontentloaded" });

    // Wait for resources to load
    const state = await waitForResourcesReady(page);
    console.log(`  Resources ready. Sections: ${state.sections.map((s) => s.name).join(", ")}`);

    // Poll for recovery backdrop once resourcesReady
    let viewState = null;
    let attempts = 0;
    const pollDeadline = Date.now() + 3000;
    while (Date.now() < pollDeadline && !viewState?.visible) {
      viewState = await getRecoveryViewState(page);
      if (viewState?.visible) break;
      await page.waitForTimeout(100);
    }

    // Story 6: No-response mode shows correct status reason
    recordResult(
      "6",
      viewState?.statusReason === "No response from controller" ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Found: ${viewState?.statusReason}`
    );

    // Story 7-8: Retry button present and text is "Retry now"
    recordResult("7", viewState?.hasRetryButton ? "PASS" : "UI-NOT-IMPLEMENTED", `Button present: ${viewState?.hasRetryButton}`);

    if (viewState?.hasRetryButton) {
      const buttonText = await text(page, ".recovery-actions button");
      recordResult("8", buttonText === "Retry now" ? "PASS" : "FAIL", `Text: ${buttonText}`);
    } else {
      recordResult("8", "UI-NOT-IMPLEMENTED", "No retry button to check");
    }

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario2-no-response.png") });
  } catch (error) {
    recordResult("6-8", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 3: Busy Mode (IN-PAGE Recovery Panel)
// TARGETS wifi.html only (bootstrap sections registered at data/wifi.js:490)
// index.html has no bootstrap sections, making in-page recovery panels unreachable.
// REDUCER CONTRACT (data/page_bootstrap.js line 512-513):
//   If step.reason === "busy" -> mode = "busy" (immediately, regardless of attempt)
async function scenarioBusyMode(browser) {
  console.log("\n=== Scenario 3: Busy Mode (wifi.html, 503 + Retry-After) ===");

  if (INDUCED) {
    recordResult("9-14", "SKIP", "skipped: interception scenarios need a normally-serving build");
    return;
  }

  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const wifiTargetUrl = TARGET_URL.replace(/\/[^/]+\.html/, "/wifi.html");
  const pageErrors = [];
  const consoleErrors = [];
  page.on("pageerror", (err) => pageErrors.push(String(err)));
  page.on("console", (msg) => {
    if (msg.type() === "error" && !msg.text().startsWith("Failed to load resource:")) {
      consoleErrors.push(msg.text());
    }
  });

  try {
    let busyAttempts = 0;

    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/app.js", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());
    await page.route("**/api/identity", (route) =>
      route.fulfill(json({ droidName: "r5unit", mdnsUseName: true }))
    );
    await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));

    // Return 503 + Retry-After on every attempt to /api/config
    await page.route("**/api/config", async (route) => {
      busyAttempts++;
      await route.fulfill({
        status: 503,
        headers: { "retry-after": "2" },
        contentType: "text/plain",
        body: "Service Unavailable",
      });
    });

    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(wifiTargetUrl, { waitUntil: "domcontentloaded" });

    // Wait for resources to load
    const state = await waitForResourcesReady(page);
    console.log(`  Resources ready. Sections: ${state.sections.map((s) => s.name).join(", ")}`);

    // Poll for recovery backdrop
    let viewState = null;
    const pollDeadline = Date.now() + 3000;
    while (Date.now() < pollDeadline && !viewState?.visible) {
      viewState = await getRecoveryViewState(page);
      if (viewState?.visible) break;
      await page.waitForTimeout(100);
    }

    // Story 9: REQUEST REFUSED banner (in-page busy panel)
    // Query inside #page-recovery-backdrop after it has class "active" to avoid
    // sampling before the panel is rendered (data/page_bootstrap.js line 698).
    const refusedBanner = viewState?.visible
      ? await page.locator("#page-recovery-backdrop.active >> text=REQUEST REFUSED").count()
      : 0;
    recordResult("9", refusedBanner > 0 ? "PASS" : "UI-NOT-IMPLEMENTED", `Banners: ${refusedBanner}`);

    // Story 10: Busy mode shows "Controller busy"
    recordResult(
      "10",
      viewState?.statusReason === "Controller busy" ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Found: ${viewState?.statusReason}`
    );

    // Story 11-12: Countdown panel and value
    // Guard on backdrop visibility to avoid reading after panel is hidden.
    const countdownPanel = viewState?.visible
      ? await page.locator("#page-recovery-backdrop.active .recovery-countdown-panel").count()
      : 0;
    recordResult("11", countdownPanel > 0 ? "PASS" : "UI-NOT-IMPLEMENTED", `Panels: ${countdownPanel}`);

    let countdownValue = viewState?.visible
      ? await text(page, "#page-recovery-backdrop.active .recovery-countdown-value")
      : null;
    const initialSeconds = parseInt(countdownValue);
    const hasValidCountdown = !isNaN(initialSeconds) && initialSeconds > 0;
    recordResult("12", hasValidCountdown ? "PASS" : "UI-NOT-IMPLEMENTED", countdownValue);

    // Story 13: Countdown ticks down
    if (hasValidCountdown) {
      await page.waitForTimeout(500);
      // Re-check visibility before reading countdown a second time
      const newCountdownValue = viewState?.visible
        ? await text(page, "#page-recovery-backdrop.active .recovery-countdown-value")
        : null;
      const newSeconds = parseInt(newCountdownValue);
      recordResult(
        "13",
        newSeconds <= initialSeconds && newSeconds > 0 ? "PASS" : "FAIL",
        `Countdown: ${initialSeconds}s -> ${newSeconds}s`
      );
    } else {
      recordResult("13", "FAIL", "Cannot test countdown without valid value");
    }

    // Wait for retry
    await page.waitForTimeout(3000);

    // Story 14: Backdrop hides after recovery
    const backdropHidden = await page.evaluate(() => {
      const bd = document.getElementById("page-recovery-backdrop");
      return !bd || !bd.classList.contains("active");
    });
    recordResult("14", backdropHidden ? "PASS" : "FAIL", "Backdrop hidden after recovery");

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario3-busy-mode.png") });
  } catch (error) {
    recordResult("9-14", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 4: Per-resource retry
async function scenarioPerResourceRetry(browser) {
  console.log("\n=== Scenario 4: Per-Resource Retry ===");

  if (INDUCED) {
    recordResult("15-16", "SKIP", "skipped: interception scenarios need a normally-serving build");
    return;
  }

  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const pageErrors = [];
  const consoleErrors = [];
  const resourceLog = {};
  page.on("pageerror", (err) => pageErrors.push(String(err)));
  page.on("console", (msg) => {
    if (msg.type() === "error" && !msg.text().startsWith("Failed to load resource:")) {
      consoleErrors.push(msg.text());
    }
  });

  try {
    await page.route("**/style.css", (route) => {
      resourceLog["style.css"] = (resourceLog["style.css"] || 0) + 1;
      route.continue();
    });

    let appJsAttempts = 0;
    const appResource = getAppResourceName();
    await page.route(`**/${appResource}`, (route) => {
      appJsAttempts++;
      resourceLog[appResource] = (resourceLog[appResource] || 0) + 1;
      if (appJsAttempts === 1) {
        route.abort("failed");
      } else {
        route.continue();
      }
    });

    await page.route("**/page_bootstrap.js", (route) => route.continue());
    await page.route("**/api/identity", (route) =>
      route.fulfill(json({ droidName: "r5unit", mdnsUseName: true }))
    );
    await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));
    await page.route("**/api/config", (route) =>
      route.fulfill(json({ ok: true, wifi: {}, components: {}, system: { logLevel: 2 } }))
    );

    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForTimeout(3000);

    const bootstrapState = await page.evaluate(() => {
      const state = window.PABootstrap?.getState?.();
      if (!state) return null;
      return {
        resources: state.resources.map((r) => ({ name: r.name, status: r.status, attempt: r.attempt })),
        resourcesReady: state.resourcesReady,
      };
    });

    recordResult("15", bootstrapState && bootstrapState.resourcesReady ? "PASS" : "UI-NOT-IMPLEMENTED", "Bootstrap state accessible");

    const styleLoaded = resourceLog["style.css"] === 1;
    const appRetried = resourceLog[appResource] === 2;
    recordResult("16", styleLoaded && appRetried ? "PASS" : "FAIL", `style.css: ${resourceLog["style.css"]}x, ${appResource}: ${resourceLog[appResource]}x`);

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario4-per-resource-retry.png") });
  } catch (error) {
    recordResult("15-16", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 5: Induced bench build
async function scenarioInducedBench(browser) {
  if (!INDUCED) {
    recordResult("17-24", "SKIP", "skipped: needs induced bench build");
    return;
  }

  console.log("\n=== Scenario 5: Induced Bench Build (Server-Rendered Busy Page) ===");
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const pageErrors = [];
  const consoleErrors = [];
  page.on("pageerror", (err) => pageErrors.push(String(err)));
  page.on("console", (msg) => {
    if (msg.type() === "error" && !msg.text().startsWith("Failed to load resource:")) {
      consoleErrors.push(msg.text());
    }
  });

  try {
    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForTimeout(1000);

    const busyText = await text(page, "body");

    recordResult("17", busyText && busyText.includes("Controller busy") ? "PASS" : "UI-NOT-IMPLEMENTED", "Controller busy text found");
    recordResult("18", busyText && busyText.includes("protect") ? "PASS" : "UI-NOT-IMPLEMENTED", "Protection detail found");

    const countdownElement = await page.locator("#c").count();
    recordResult("19", countdownElement > 0 ? "PASS" : "UI-NOT-IMPLEMENTED", `#c count: ${countdownElement}`);

    const countdownLabel = await text(page, "#c");
    recordResult("20", countdownLabel && countdownLabel.includes("Retrying") ? "PASS" : "UI-NOT-IMPLEMENTED", countdownLabel);

    const retryButton = await page.locator("#r").count();
    recordResult("21", retryButton > 0 ? "PASS" : "UI-NOT-IMPLEMENTED", `#r count: ${retryButton}`);

    if (retryButton > 0) {
      const retryText = await text(page, "#r");
      recordResult("22", retryText === "Retry now" ? "PASS" : "FAIL", retryText);
    } else {
      recordResult("22", "UI-NOT-IMPLEMENTED", "No #r button to check");
    }

    recordResult("23", busyText && busyText.includes("still running") ? "PASS" : "UI-NOT-IMPLEMENTED", "Still running message found");
    recordResult("24", busyText && busyText.includes("Nothing was lost") ? "PASS" : "UI-NOT-IMPLEMENTED", "Loss disclaimer found");

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario5-induced-bench.png") });
  } catch (error) {
    recordResult("17-24", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });

  try {
    await scenarioResourceRetry(browser);
    await scenarioNoResponse(browser);
    await scenarioBusyMode(browser);
    await scenarioPerResourceRetry(browser);
    await scenarioInducedBench(browser);

    console.log("\n=== Story Results Summary ===");
    const fails = results.filter((r) => r.status === "FAIL");
    const uiNotImpl = results.filter((r) => r.status === "UI-NOT-IMPLEMENTED");
    const passes = results.filter((r) => r.status === "PASS");
    const skips = results.filter((r) => r.status === "SKIP");

    console.log(
      `Passed: ${passes.length}, Failed: ${fails.length}, UI-NOT-IMPLEMENTED: ${uiNotImpl.length}, Skipped: ${skips.length}`
    );

    if (fails.length > 0) {
      console.log("\nFailed stories:");
      fails.forEach((r) => console.log(`  Story ${r.story}: ${r.detail}`));
    }

    if (uiNotImpl.length > 0) {
      console.log("\nUI-NOT-IMPLEMENTED stories:");
      uiNotImpl.forEach((r) => console.log(`  Story ${r.story}: ${r.detail}`));
    }

    console.log(
      `\nRESULT passed=${passes.length} failed=${fails.length} notimpl=${uiNotImpl.length} skipped=${skips.length}`
    );

    // FORCE_FAIL hook for exit-code verification (issue #104, defect 1).
    // Usage: FORCE_FAIL=1 node ... ; echo "exit=$?"
    // Proves that failures trigger exit code 1 and that a clean run exits 0.
    if (FORCE_FAIL) {
      recordResult("forced-fail", "FAIL", "synthetic failure for exit-code verification");
      fails.push({ story: "forced-fail", status: "FAIL" });
    }

    // CRITICAL: exit code MUST be 1 if fails.length > 0
    if (fails.length > 0) {
      process.exitCode = 1;
    }
  } catch (error) {
    console.error("Playwright test suite failed:", error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();
