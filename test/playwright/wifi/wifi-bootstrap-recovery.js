const assert = require("node:assert/strict");
const { chromium } = require("playwright");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/wifi.html";
const HEADLESS = process.env.HEADLESS !== "false";

async function testWifiBootstrapRecovery() {
  const browser = await chromium.launch({ headless: HEADLESS });
  const page = await browser.newPage();

  // Test 1: Loading state renders
  console.log("Test 1: Loading state renders recovery modal");
  await page.route("**/shell.js", (route) => {
    // Delay shell.js to keep page in loading state
    setTimeout(() => route.abort(), 100);
  });

  await page.goto(TARGET_URL);

  // Recovery backdrop should be visible during load
  await page.waitForTimeout(200);
  const backdropVisible = await page.locator("#recovery-backdrop").evaluate((el) =>
    window.getComputedStyle(el).display !== "none"
  );
  assert.strictEqual(backdropVisible, true, "Recovery backdrop should be visible during load");
  console.log("  ✓ Loading state displays recovery modal");

  await browser.close();

  // Test 2: Busy (503) state from server
  console.log("Test 2: Busy (503) recovery page state");
  const browser2 = await chromium.launch({ headless: HEADLESS });
  const page2 = await browser2.newPage();

  await page2.route("**/web_api.js", (route) => {
    // Simulate server-side 503 Busy Recovery Page
    route.fulfill({
      status: 503,
      contentType: "text/html",
      headers: { "Retry-After": "5" },
      body: "<html><body>Busy</body></html>",
    });
  });

  await page2.goto(TARGET_URL);

  // The busy state is only reachable once loadScript() actually dispatches
  // outcome.kind === "busy" -- wait for the real transition instead of a
  // fixed sleep, and fail if it never happens.
  await page2.waitForFunction(
    () => {
      const el = document.getElementById("recovery-busy");
      return el && window.getComputedStyle(el).display !== "none";
    },
    { timeout: 5000 }
  );

  const busyModalVisible = await page2
    .locator("#recovery-busy")
    .evaluate((el) => window.getComputedStyle(el).display !== "none");
  assert.strictEqual(busyModalVisible, true, "Busy modal should be visible after a 503 response");

  const countdown = await page2.locator("#recovery-busy-countdown").textContent();
  assert.ok(/^\d+ s$/.test(countdown.trim()), "Countdown should show a numeric seconds value, got: " + countdown);
  console.log("  ✓ Busy state renders with countdown: " + countdown);

  await browser2.close();

  // Test 3: No-response (timeout) state
  console.log("Test 3: No-response (timeout) state renders");
  const browser3 = await chromium.launch({ headless: HEADLESS });
  const page3 = await browser3.newPage();

  // Set a very short timeout
  await page3.route("**/status_stream.js", async (route) => {
    // Hang the request to trigger timeout
    await new Promise(() => {}); // Never resolves
  });

  await page3.goto(TARGET_URL);

  // Wait for the real timeout-driven transition (OPERATION_DEADLINE_MS is
  // 6s) instead of asserting DOM presence regardless of whether it fired.
  await page3.waitForFunction(
    () => {
      const el = document.getElementById("recovery-no-response");
      return el && window.getComputedStyle(el).display !== "none";
    },
    { timeout: 8000 }
  );

  const noResponseModalVisible = await page3
    .locator("#recovery-no-response")
    .evaluate((el) => window.getComputedStyle(el).display !== "none");
  assert.strictEqual(
    noResponseModalVisible,
    true,
    "No-response modal should be visible after a script load times out"
  );

  const attemptText = await page3.locator("#recovery-no-response-attempt").textContent();
  assert.ok(/^Attempt \d+$/.test(attemptText.trim()), "Attempt counter should render, got: " + attemptText);
  console.log("  ✓ No-response state renders after real timeout: " + attemptText);

  await browser3.close();

  // Test 4: Retry now button
  console.log("Test 4: Retry now button exists and is clickable");
  const browser4 = await chromium.launch({ headless: HEADLESS });
  const page4 = await browser4.newPage();

  await page4.goto(TARGET_URL);

  const retryBtn = await page4.locator('button:has-text("Retry now")').first().count();
  assert.ok(retryBtn > 0, "Retry now button should exist");
  console.log("  ✓ Retry now button present");

  await browser4.close();

  // Test 5: Recovery modal CSS and styling
  console.log("Test 5: Recovery modal styling and CSS applied");
  const browser5 = await chromium.launch({ headless: HEADLESS });
  const page5 = await browser5.newPage();

  // Delay a resource so the loading modal is actually visible when we check
  // its box model -- checking immediately after goto() races the real load
  // (often already-hidden by the time the check runs), which made this
  // assertion fail nondeterministically against a display:none element.
  await page5.route("**/shell.js", (route) => {
    setTimeout(() => route.abort(), 300);
  });

  await page5.goto(TARGET_URL);
  await page5.waitForFunction(
    () => {
      const el = document.querySelector(".recovery-modal-panel");
      return el && window.getComputedStyle(el).display !== "none";
    },
    { timeout: 3000 }
  );

  const modalPanel = await page5.locator(".recovery-modal-panel").first();
  const panelBox = await modalPanel.boundingBox();
  assert.ok(panelBox, "Recovery modal should have valid box model while visible");
  assert.ok(panelBox.width > 0 && panelBox.height > 0, "Recovery modal should have nonzero size");
  console.log("  ✓ Modal panel styled and positioned: " + JSON.stringify(panelBox));

  await browser5.close();

  // Test 6: Current step name updates
  console.log("Test 6: Current step name renders");
  const browser6 = await chromium.launch({ headless: HEADLESS });
  const page6 = await browser6.newPage();

  await page6.goto(TARGET_URL);
  await page6.waitForTimeout(300);

  const stepEl = await page6.locator("#recovery-current-step").textContent();
  assert.ok(stepEl && stepEl.includes("Loading:"), "Step name should show current resource");
  console.log("  ✓ Current step name displays: " + stepEl);

  await browser6.close();

  console.log("\nAll wifi bootstrap recovery tests passed! ✓");
}

async function testSectionBootstrapRecovery() {
  console.log("\n--- Section Bootstrap Recovery Tests ---\n");

  // Test 7: Section busy (503) recovery
  console.log("Test 7: Section busy (503) recovery state");
  const browser7 = await chromium.launch({ headless: HEADLESS });
  const page7 = await browser7.newPage();

  // Make posture section respond with 503 + Retry-After
  await page7.route("**/api/wifi", (route) => {
    route.fulfill({
      status: 503,
      headers: { "Retry-After": "3" },
      body: JSON.stringify({ error: "Device busy" }),
    });
  });

  await page7.goto(TARGET_URL);

  // Wait for the busy modal to appear for the section
  await page7.waitForFunction(
    () => {
      const el = document.getElementById("recovery-busy");
      return el && window.getComputedStyle(el).display !== "none";
    },
    { timeout: 5000 }
  );

  const sectionBusyModalVisible = await page7
    .locator("#recovery-busy")
    .evaluate((el) => window.getComputedStyle(el).display !== "none");
  assert.strictEqual(
    sectionBusyModalVisible,
    true,
    "Busy modal should be visible when a section gets 503"
  );

  const sectionCountdown = await page7.locator("#recovery-busy-countdown").textContent();
  assert.ok(/^\d+ s$/.test(sectionCountdown.trim()),
    "Countdown should show numeric seconds for section busy, got: " + sectionCountdown);
  console.log("  ✓ Section busy state renders with countdown: " + sectionCountdown);

  await browser7.close();

  // Test 8: Section no-response (timeout) recovery
  console.log("Test 8: Section no-response (timeout) state");
  const browser8 = await chromium.launch({ headless: HEADLESS });
  const page8 = await browser8.newPage();

  // Make settings section hang to trigger timeout
  await page8.route("**/api/config", async (route) => {
    // Never resolve to force timeout
    await new Promise(() => {});
  });

  await page8.goto(TARGET_URL);

  // Wait for the no-response modal for the section (OPERATION_DEADLINE_MS is 6s)
  await page8.waitForFunction(
    () => {
      const el = document.getElementById("recovery-no-response");
      return el && window.getComputedStyle(el).display !== "none";
    },
    { timeout: 8000 }
  );

  const sectionNoResponseModalVisible = await page8
    .locator("#recovery-no-response")
    .evaluate((el) => window.getComputedStyle(el).display !== "none");
  assert.strictEqual(
    sectionNoResponseModalVisible,
    true,
    "No-response modal should be visible when section times out"
  );

  const sectionAttemptText = await page8.locator("#recovery-no-response-attempt").textContent();
  assert.ok(/^Attempt \d+$/.test(sectionAttemptText.trim()),
    "Attempt counter should render for section timeout, got: " + sectionAttemptText);
  console.log("  ✓ Section no-response state renders after timeout: " + sectionAttemptText);

  await browser8.close();

  // Test 9: Multiple sections with one failing - others should still work
  console.log("Test 9: One section failure doesn't block other sections");
  const browser9 = await chromium.launch({ headless: HEADLESS });
  const page9 = await browser9.newPage();

  let wifiAttempts = 0;
  let configAttempts = 0;

  // Make posture fail with 503, but let settings and apply succeed
  await page9.route("**/api/wifi", (route) => {
    wifiAttempts++;
    route.fulfill({
      status: 503,
      headers: { "Retry-After": "2" },
      body: JSON.stringify({ error: "Busy" }),
    });
  });

  // settings and apply use /api/config, let them succeed
  await page9.route("**/api/config", (route) => {
    configAttempts++;
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({
        wifi: {
          mode: "client",
          provisioned: true,
          staSsid: "TestNetwork",
          pendingApply: false,
        },
      }),
    });
  });

  await page9.goto(TARGET_URL);

  // posture should show busy modal
  await page9.waitForFunction(
    () => {
      const el = document.getElementById("recovery-busy");
      return el && window.getComputedStyle(el).display !== "none";
    },
    { timeout: 5000 }
  );

  const busyShown = await page9
    .locator("#recovery-busy")
    .evaluate((el) => window.getComputedStyle(el).display !== "none");
  assert.strictEqual(busyShown, true, "Busy modal should show for failed posture section");

  // Wait a bit for settings and apply to load while posture is retrying
  await page9.waitForTimeout(3000);

  // Verify that settings and apply were attempted (configAttempts > 0) while posture was busy
  assert.ok(configAttempts > 0, "/api/config should be called for settings/apply even while posture fails");
  console.log("  ✓ Posture failed (503), but other sections still attempted: config calls: " + configAttempts);

  await browser9.close();

  // Test 10: Section retry countdown timer decrements
  console.log("Test 10: Section retry countdown timer updates");
  const browser10 = await chromium.launch({ headless: HEADLESS });
  const page10 = await browser10.newPage();

  // Make a section return 503 with 5 second retry
  await page10.route("**/api/wifi", (route) => {
    route.fulfill({
      status: 503,
      headers: { "Retry-After": "5" },
      body: JSON.stringify({ error: "Busy" }),
    });
  });

  await page10.goto(TARGET_URL);

  // Wait for busy modal
  await page10.waitForFunction(
    () => {
      const el = document.getElementById("recovery-busy");
      return el && window.getComputedStyle(el).display !== "none";
    },
    { timeout: 5000 }
  );

  // Get initial countdown
  const initialCountdown = await page10.locator("#recovery-busy-countdown").textContent();
  const initialSeconds = parseInt(initialCountdown.match(/\d+/)[0]);
  assert.ok(initialSeconds > 0, "Initial countdown should be positive, got: " + initialCountdown);

  // Wait a moment and check it decreased
  await page10.waitForTimeout(1500);
  const laterCountdown = await page10.locator("#recovery-busy-countdown").textContent();
  const laterSeconds = parseInt(laterCountdown.match(/\d+/)[0]);
  assert.ok(
    laterSeconds < initialSeconds,
    `Countdown should decrease: ${initialSeconds}s -> ${laterSeconds}s`
  );
  console.log(`  ✓ Section countdown timer decrements: ${initialSeconds}s -> ${laterSeconds}s`);

  await browser10.close();

  // Test 11: Actual API calls are made (not simulated success)
  console.log("Test 11: Sections make actual API calls");
  const browser11 = await chromium.launch({ headless: HEADLESS });
  const page11 = await browser11.newPage();

  let configAttempts11 = 0;

  // Intercept the config endpoint (used by settings and apply sections)
  await page11.route("**/api/config", (route) => {
    configAttempts11++;
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({
        wifi: {
          mode: "client",
          provisioned: true,
          staSsid: "TestNetwork",
          staPasswordSet: true,
          apSsid: "TestAP",
          apPasswordSet: false,
          pendingApply: false,
        },
      }),
    });
  });

  // Also intercept wifi endpoint
  await page11.route("**/api/wifi", (route) => {
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({
        staIp: "192.168.1.100",
        staConnected: true,
        staEnabled: true,
        wifiRssi: -50,
        apIp: "192.168.4.1",
        apSsid: "TestAP",
        provisioned: true,
      }),
    });
  });

  await page11.goto(TARGET_URL);

  // Wait for sections to be requested
  await page11.waitForTimeout(3000);

  // At least one config call should have been made (for settings or apply section)
  assert.ok(configAttempts11 > 0, "/api/config should be called for settings or apply sections (attempts: " + configAttempts11 + ")");
  console.log("  ✓ Actual API calls made: /api/config was called " + configAttempts11 + " time(s)");

  await browser11.close();

  console.log("\nAll section bootstrap recovery tests passed! ✓");
}

Promise.resolve()
  .then(() => testWifiBootstrapRecovery())
  .then(() => testSectionBootstrapRecovery())
  .catch((err) => {
    console.error("Test failed:", err);
    process.exit(1);
  });
