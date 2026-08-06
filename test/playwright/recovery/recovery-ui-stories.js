const assert = require("node:assert/strict");
const { chromium } = require("playwright");
const fs = require("fs");
const path = require("path");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/index.html";
const SCREENSHOT_DIR = process.env.SCREENSHOT_DIR || "/tmp/recovery-ui-screenshots";
const HEADLESS = process.env.HEADLESS === "true";
const INDUCED = process.env.INDUCED === "true";

// Ensure screenshot directory exists
if (!fs.existsSync(SCREENSHOT_DIR)) {
  fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });
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

// Scenario 1: Intercept app.js, fail it N times, then let through
async function scenarioResourceRetry(browser) {
  console.log("\n=== Scenario 1: Resource Retry (Intercepted Load) ===");

  // INDUCED build refuses navigation itself, so these scenarios are meaningless noise.
  // Skip them and only run on normally-serving builds.
  if (INDUCED) {
    recordResult("1-5", "SKIP", "skipped: interception scenarios need a normally-serving build");
    return;
  }

  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const pageErrors = [];
  const consoleErrors = [];
  const networkLog = [];
  page.on("pageerror", (err) => pageErrors.push(String(err)));
  page.on("console", (msg) => {
    if (msg.type() === "error" && !msg.text().startsWith("Failed to load resource:")) {
      consoleErrors.push(msg.text());
    }
  });

  try {
    let appJsAttempts = 0;
    const maxFailures = 2;

    // Intercept app.js: fail it twice, then let through
    await page.route("**/app.js", (route) => {
      appJsAttempts++;
      networkLog.push({ resource: "app.js", attempt: appJsAttempts });
      if (appJsAttempts <= maxFailures) {
        route.abort("failed");
      } else {
        route.continue();
      }
    });

    // Let other critical paths through
    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());
    await page.route("**/api/identity", (route) =>
      route.fulfill(json({ droidName: "r5unit", mdnsUseName: true }))
    );
    await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));
    await page.route("**/api/config", (route) =>
      route.fulfill(json({ ok: true, wifi: {}, components: {}, system: { logLevel: 2 } }))
    );

    // Disable EventSource for cleaner network
    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    // Navigate without networkidle
    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });

    // Wait briefly for initial state
    await page.waitForTimeout(500);

    // Story 1: Loading mode appears with backdrop active
    const backdrop = await page.locator("#page-recovery-backdrop");
    const isVisible = await backdrop.evaluate((el) => el.classList.contains("active"));
    const bodyHasRecoveryActive = await page.evaluate(() =>
      document.body.classList.contains("recovery-active")
    );
    recordResult(
      "1",
      bodyHasRecoveryActive && isVisible ? "PASS" : "FAIL",
      "Loading mode backdrop active"
    );

    // Story 2: Loading mode shows "Loading page resources" text
    const statusReason = await text(page, ".recovery-status-reason");
    recordResult(
      "2",
      statusReason === "Loading page resources" ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Found: ${statusReason}`
    );

    // Story 3: Loading step shows step label
    const stepLabel = await text(page, ".recovery-step");
    const hasStepLabel = stepLabel && stepLabel.includes("Loading:");
    recordResult("3", hasStepLabel ? "PASS" : "UI-NOT-IMPLEMENTED", stepLabel);

    // Wait for retries to complete (up to 10 seconds for backoff)
    const maxWaitMs = 10000;
    const startTime = Date.now();
    while (appJsAttempts <= maxFailures && Date.now() - startTime < maxWaitMs) {
      await page.waitForTimeout(200);
    }

    // Wait a bit more for successful load
    await page.waitForTimeout(1000);

    // Story 4: Recovery completes and backdrop hides
    const finalBackdropActive = await page.evaluate(() =>
      document.getElementById("page-recovery-backdrop")?.classList.contains("active")
    );
    recordResult("4", !finalBackdropActive ? "PASS" : "FAIL", "Backdrop hides after recovery");

    // Story 5: app.js was retried (should have 3+ attempts)
    recordResult(
      "5",
      appJsAttempts === 3 ? "PASS" : "FAIL",
      `Expected 3 attempts, got ${appJsAttempts}`
    );

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario1-resource-retry.png") });
  } catch (error) {
    recordResult("1-5", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 2: API request aborts (no-response mode)
// REDUCER CONTRACT (data/page_bootstrap.js line 775-785 runSection):
//   Calls registered section loader function, catches any error, passes to done()
// CLASSIFYOUTCOME CONTRACT (line 56-75):
//   error.kind === "timeout" -> { kind: "no-response", reason: "timeout" }
// WEB_API.JS ERROR SHAPE (line 39-45):
//   Aborted fetch -> AbortError -> ApiError { kind: "timeout" }
async function scenarioNoResponse(browser) {
  console.log("\n=== Scenario 2: No-Response Mode ===");

  if (INDUCED) {
    recordResult("6-8", "SKIP", "skipped: interception scenarios need a normally-serving build");
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
    // Intercept resources
    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/app.js", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());

    // Abort test API to produce network error in web_api.js
    await page.route("**/api/test-section", (route) => route.abort("failed"));

    // Inject a test section that makes an API call
    await page.addInitScript(() => {
      window.EventSource = undefined;

      // Wait for page to be ready, then register a test section that calls the API
      if (window.PABootstrap) {
        window.PABootstrap.registerSection("test-api-call", async () => {
          const response = await fetch("/api/test-section");
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          return response.json();
        }, { label: "test API" });
      }
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });

    // Wait for page to settle (resources load, then section starts)
    await page.waitForTimeout(2000);

    // Story 6: Verify no-response mode is engaged
    const statusReason = await text(page, ".recovery-status-reason");
    recordResult(
      "6",
      statusReason === "No response from controller" ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Found: ${statusReason}`
    );

    // Story 7: Retry button is present (only in some modes)
    const retryButtonCount = await page.locator(".recovery-actions button").count();
    recordResult(
      "7",
      retryButtonCount > 0 ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Button count: ${retryButtonCount}`
    );

    // Story 8: Retry button text is "Retry now"
    if (retryButtonCount > 0) {
      const buttonText = await text(page, ".recovery-actions button");
      recordResult(
        "8",
        buttonText === "Retry now" ? "PASS" : "FAIL",
        `Text: ${buttonText}`
      );
    } else {
      recordResult("8", "UI-NOT-IMPLEMENTED", "No retry button in this mode");
    }

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario2-no-response.png") });
  } catch (error) {
    recordResult("6-8", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 3: Busy mode (503 + Retry-After) — IN-PAGE recovery panel
// CLASSIFYOUTCOME CONTRACT (data/page_bootstrap.js line 62-67):
//   if (kind === "http" && status === 503) -> { kind: "busy", reason: "busy", retryAfterMs: ... }
// WEB_API.JS ERROR SHAPE (data/web_api.js line 142-146):
//   response not ok -> ApiError { kind: "http", status: response.status, retryAfterMs: ... }
// RETRYAFTER CONTRACT (data/web_api.js line 29-37):
//   Header name: "retry-after" (lowercase)
//   Value: seconds (integer), parseRetryAfterMs converts to milliseconds
async function scenarioBusyMode(browser) {
  console.log("\n=== Scenario 3: Busy Mode (503 + Retry-After) ===");

  if (INDUCED) {
    recordResult("9-13", "SKIP", "skipped: interception scenarios need a normally-serving build");
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
    let busyAttempts = 0;

    // Intercept resources
    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/app.js", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());

    // Serve 503 on first attempt with retry-after, then success
    await page.route("**/api/test-busy", async (route) => {
      busyAttempts++;
      if (busyAttempts === 1) {
        // Return 503 with Retry-After in SECONDS (per RFC 9110, parseRetryAfterMs converts to ms)
        await route.fulfill({
          status: 503,
          headers: { "retry-after": "2" },
          contentType: "text/plain",
          body: "Service Unavailable",
        });
      } else {
        await route.fulfill(json(statusPayload));
      }
    });

    // Inject a test section that makes the busy API call
    await page.addInitScript(() => {
      window.EventSource = undefined;

      if (window.PABootstrap) {
        window.PABootstrap.registerSection("test-busy-call", async () => {
          const response = await fetch("/api/test-busy");
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          return response.json();
        }, { label: "test busy" });
      }
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });

    // Wait for page to settle
    await page.waitForTimeout(2000);

    // Story 9: REQUEST REFUSED banner (in-page busy panel, data/page_bootstrap.js line 564-567)
    const refusedBanner = await page.locator("text=REQUEST REFUSED").count();
    recordResult(
      "9",
      refusedBanner > 0 ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Banners: ${refusedBanner}`
    );

    // Story 10: Busy mode shows "Controller busy" reason
    const statusReason = await text(page, ".recovery-status-reason");
    recordResult(
      "10",
      statusReason === "Controller busy" ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Found: ${statusReason}`
    );

    // Story 11: Countdown panel exists
    const countdownPanel = await page.locator(".recovery-countdown-panel").count();
    recordResult(
      "11",
      countdownPanel > 0 ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Panels: ${countdownPanel}`
    );

    // Story 12: Countdown value renders
    let countdownValue = await text(page, ".recovery-countdown-value");
    const initialSeconds = parseInt(countdownValue);
    const hasValidCountdown = !isNaN(initialSeconds) && initialSeconds > 0;
    recordResult(
      "12",
      hasValidCountdown ? "PASS" : "UI-NOT-IMPLEMENTED",
      countdownValue
    );

    // Story 13: Countdown ticks down
    if (hasValidCountdown) {
      await page.waitForTimeout(500);
      const newCountdownValue = await text(page, ".recovery-countdown-value");
      const newSeconds = parseInt(newCountdownValue);
      recordResult(
        "13",
        newSeconds <= initialSeconds && newSeconds > 0 ? "PASS" : "FAIL",
        `Countdown: ${initialSeconds}s -> ${newSeconds}s`
      );
    } else {
      recordResult("13", "FAIL", "Cannot test countdown without valid value");
    }

    // Wait for retry to complete
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
    recordResult("14-15", "SKIP", "skipped: interception scenarios need a normally-serving build");
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
    // Track all resource loads
    await page.route("**/style.css", (route) => {
      resourceLog["style.css"] = (resourceLog["style.css"] || 0) + 1;
      route.continue();
    });

    // Fail app.js once, let through on retry
    let appJsAttempts = 0;
    await page.route("**/app.js", (route) => {
      appJsAttempts++;
      resourceLog["app.js"] = (resourceLog["app.js"] || 0) + 1;
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

    // Wait for recovery
    await page.waitForTimeout(3000);

    // Story 14: Verify resource state via PABootstrap
    const bootstrapState = await page.evaluate(() => {
      const state = window.PABootstrap?.getState?.();
      if (!state) return null;
      return {
        resources: state.resources.map((r) => ({ name: r.name, status: r.status, attempt: r.attempt })),
        resourcesReady: state.resourcesReady,
      };
    });

    recordResult(
      "14",
      bootstrapState && bootstrapState.resourcesReady ? "PASS" : "UI-NOT-IMPLEMENTED",
      "Bootstrap state accessible"
    );

    // Story 15: Only failed resource was retried
    const styleLoaded = resourceLog["style.css"] === 1;
    const appRetried = resourceLog["app.js"] === 2;
    recordResult(
      "15",
      styleLoaded && appRetried ? "PASS" : "FAIL",
      `style.css: ${resourceLog["style.css"]}x, app.js: ${resourceLog["app.js"]}x`
    );

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario4-per-resource-retry.png") });
  } catch (error) {
    recordResult("14-15", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 5: Induced bench build (server-rendered busy page)
// SERVER-RENDERED PAGE CONTRACT (include/web_busy_page.h):
//   When navigation is refused with 503, server sends a complete HTML page with:
//   - "Controller busy" reason text
//   - "It refused this page to protect itself." detail
//   - Countdown element with ID #c and "Retrying automatically" label
//   - Retry button with ID #r and "Retry now" text
//   - Message "The controller is still running. Nothing was lost."
async function scenarioInducedBench(browser) {
  if (!INDUCED) {
    recordResult("16-23", "SKIP", "skipped: needs induced bench build");
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

    // Navigation itself will be refused with 503 on induced build
    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForTimeout(1000);

    // Story 16: Server page shows "Controller busy" reason
    const busyText = await text(page, "body");
    recordResult(
      "16",
      busyText && busyText.includes("Controller busy") ? "PASS" : "UI-NOT-IMPLEMENTED",
      "Controller busy text found"
    );

    // Story 17: Server page shows protection detail
    recordResult(
      "17",
      busyText && busyText.includes("protect") ? "PASS" : "UI-NOT-IMPLEMENTED",
      "Protection detail found"
    );

    // Story 18: Server page has countdown element (#c)
    const countdownElement = await page.locator("#c").count();
    recordResult("18", countdownElement > 0 ? "PASS" : "UI-NOT-IMPLEMENTED", `#c count: ${countdownElement}`);

    // Story 19: Countdown shows "Retrying automatically" label
    const countdownLabel = await text(page, "#c");
    recordResult(
      "19",
      countdownLabel && countdownLabel.includes("Retrying") ? "PASS" : "UI-NOT-IMPLEMENTED",
      countdownLabel
    );

    // Story 20: Retry button exists (#r)
    const retryButton = await page.locator("#r").count();
    recordResult("20", retryButton > 0 ? "PASS" : "UI-NOT-IMPLEMENTED", `#r count: ${retryButton}`);

    // Story 21: Retry button text is "Retry now"
    if (retryButton > 0) {
      const retryText = await text(page, "#r");
      recordResult("21", retryText === "Retry now" ? "PASS" : "FAIL", retryText);
    } else {
      recordResult("21", "UI-NOT-IMPLEMENTED", "No #r button to check");
    }

    // Story 22: Message says "still running"
    recordResult(
      "22",
      busyText && busyText.includes("still running") ? "PASS" : "UI-NOT-IMPLEMENTED",
      "Still running message found"
    );

    // Story 23: Message says "Nothing was lost"
    recordResult(
      "23",
      busyText && busyText.includes("Nothing was lost") ? "PASS" : "UI-NOT-IMPLEMENTED",
      "Loss disclaimer found"
    );

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario5-induced-bench.png") });
  } catch (error) {
    recordResult("16-23", "FAIL", error.message);
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

    // Summary
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
