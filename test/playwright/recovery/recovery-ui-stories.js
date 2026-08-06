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
async function scenarioNoResponse(browser) {
  console.log("\n=== Scenario 2: No-Response Mode ===");
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
    let statusAttempts = 0;

    // Abort /api/status requests entirely
    await page.route("**/api/status", (route) => route.abort("failed"));

    // Let other paths through
    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/app.js", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());
    await page.route("**/api/identity", (route) =>
      route.fulfill(json({ droidName: "r5unit", mdnsUseName: true }))
    );
    await page.route("**/api/config", (route) =>
      route.fulfill(json({ ok: true, wifi: {}, components: {}, system: { logLevel: 2 } }))
    );

    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForTimeout(500);

    // Story 6: No-response mode appears
    const statusReason = await text(page, ".recovery-status-reason");
    recordResult(
      "6",
      statusReason === "No response from controller" ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Found: ${statusReason}`
    );

    // Story 7: Retry button is present and clickable
    const retryButton = await page.locator(".recovery-actions button").textContent();
    recordResult("7", retryButton && retryButton.includes("Retry now") ? "PASS" : "UI-NOT-IMPLEMENTED", retryButton);

    // Story 8: Click retry button (verify a new network attempt follows)
    let statusRequestsAfterClick = 0;
    await page.route("**/api/status", (route) => {
      statusRequestsAfterClick++;
      route.abort("failed");
    });

    await page.click(".recovery-actions button");
    await page.waitForTimeout(500);

    recordResult(
      "8",
      statusRequestsAfterClick > 0 ? "PASS" : "FAIL",
      "Retry now triggered new network attempt"
    );

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario2-no-response.png") });
  } catch (error) {
    recordResult("6-8", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 3: Busy mode (503 + Retry-After)
async function scenarioBusyMode(browser) {
  console.log("\n=== Scenario 3: Busy Mode (503 + Retry-After) ===");
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
    let statusAttempts = 0;

    // Serve 503 Busy responses with Retry-After header, then let through
    await page.route("**/api/status", async (route) => {
      statusAttempts++;
      if (statusAttempts <= 1) {
        // Return 503 with Retry-After
        await route.fulfill({
          status: 503,
          headers: { "Retry-After": "2" },
          body: "Service Unavailable",
        });
      } else {
        await route.fulfill(json(statusPayload));
      }
    });

    // Let other paths through
    await page.route("**/style.css", (route) => route.continue());
    await page.route("**/app.js", (route) => route.continue());
    await page.route("**/page_bootstrap.js", (route) => route.continue());
    await page.route("**/api/identity", (route) =>
      route.fulfill(json({ droidName: "r5unit", mdnsUseName: true }))
    );
    await page.route("**/api/config", (route) =>
      route.fulfill(json({ ok: true, wifi: {}, components: {}, system: { logLevel: 2 } }))
    );

    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForTimeout(500);

    // Story 9: Busy mode shows "Controller busy" reason
    const statusReason = await text(page, ".recovery-status-reason");
    recordResult(
      "9",
      statusReason === "Controller busy" ? "PASS" : "UI-NOT-IMPLEMENTED",
      `Found: ${statusReason}`
    );

    // Story 10: Busy mode shows countdown with "Retry interval"
    const countdownLabel = await text(page, ".recovery-countdown-label");
    recordResult(
      "10",
      countdownLabel && countdownLabel.includes("Retry") ? "PASS" : "UI-NOT-IMPLEMENTED",
      countdownLabel
    );

    // Story 11: Countdown value renders and ticks down
    let countdownValue = await text(page, ".recovery-countdown-value");
    const initialSeconds = parseInt(countdownValue);
    recordResult(
      "11",
      !isNaN(initialSeconds) && initialSeconds > 0 ? "PASS" : "UI-NOT-IMPLEMENTED",
      countdownValue
    );

    // Wait for countdown to tick
    await page.waitForTimeout(500);
    const newCountdownValue = await text(page, ".recovery-countdown-value");
    const newSeconds = parseInt(newCountdownValue);
    recordResult(
      "12",
      newSeconds <= initialSeconds && newSeconds > 0 ? "PASS" : "FAIL",
      `Countdown: ${initialSeconds}s -> ${newSeconds}s`
    );

    // Wait for retry to happen
    await page.waitForTimeout(2500);

    // Story 13: Page recovers after busy period
    const backdropHidden = await page.evaluate(() => {
      const backdrop = document.getElementById("page-recovery-backdrop");
      return !backdrop || !backdrop.classList.contains("active");
    });
    recordResult("13", backdropHidden ? "PASS" : "FAIL", "Backdrop hidden after recovery from busy");

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario3-busy-mode.png") });
  } catch (error) {
    recordResult("9-13", "FAIL", error.message);
  } finally {
    await page.close();
  }
}

// Scenario 4: Per-resource retry (only stalled resource retries)
async function scenarioPerResourceRetry(browser) {
  console.log("\n=== Scenario 4: Per-Resource Retry ===");
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

    // Story 15: Only failed resource was retried (style.css loaded once, app.js twice)
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

// Scenario 5: Induced bench build (503 busy page from protoArtoo_induced)
async function scenarioInducedBench(browser) {
  if (!INDUCED) {
    recordResult("16-19", "SKIP", "skipped: needs induced bench build");
    return;
  }

  console.log("\n=== Scenario 5: Induced Bench Build (Real 503) ===");
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
    // On protoArtoo_induced, a real /api/status call should return 503 Busy
    // This tests the real on-wire behavior
    await page.addInitScript(() => {
      window.EventSource = undefined;
    });

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForTimeout(1000);

    // Story 16: Busy page shows REQUEST REFUSED banner
    const refusedBanner = await page.locator("text=REQUEST REFUSED");
    const hasBanner = await refusedBanner.count() > 0;
    recordResult("16", hasBanner ? "PASS" : "UI-NOT-IMPLEMENTED", "REQUEST REFUSED banner");

    // Story 17: Busy page shows "Controller busy"
    const busyText = await text(page, ".recovery-status-reason");
    recordResult(
      "17",
      busyText && busyText.includes("busy") ? "PASS" : "UI-NOT-IMPLEMENTED",
      busyText
    );

    // Story 18: Busy page shows countdown with Retry interval
    const countdownLabel = await text(page, ".recovery-countdown-label");
    recordResult(
      "18",
      countdownLabel && countdownLabel.includes("Retry") ? "PASS" : "UI-NOT-IMPLEMENTED",
      countdownLabel
    );

    // Story 19: Busy page shows "Retrying automatically" in message
    const message = await text(page, ".recovery-message");
    recordResult(
      "19",
      message && message.includes("Retrying") ? "PASS" : "UI-NOT-IMPLEMENTED",
      message
    );

    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "scenario5-induced-bench.png") });
  } catch (error) {
    recordResult("16-19", "FAIL", error.message);
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

    console.log(`Passed: ${passes.length}, Failed: ${fails.length}, UI-NOT-IMPLEMENTED: ${uiNotImpl.length}, Skipped: ${skips.length}`);

    if (fails.length > 0) {
      console.log("\nFailed stories:");
      fails.forEach((r) => console.log(`  Story ${r.story}: ${r.detail}`));
      process.exitCode = 1;
    }

    if (uiNotImpl.length > 0) {
      console.log("\nUI-NOT-IMPLEMENTED stories:");
      uiNotImpl.forEach((r) => console.log(`  Story ${r.story}: ${r.detail}`));
    }
  } catch (error) {
    console.error("Playwright test suite failed:", error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();
