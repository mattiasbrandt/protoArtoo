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

testWifiBootstrapRecovery().catch((err) => {
  console.error("Test failed:", err);
  process.exit(1);
});
