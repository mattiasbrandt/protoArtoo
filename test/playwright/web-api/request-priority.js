// Browser Request Priority (#52 Stories 32-34): verifies web_api.js's
// request queue actually orders by priority rather than plain FIFO, and
// that Estop bypasses the queue/slot entirely rather than waiting behind it.
const assert = require("node:assert/strict");
const { chromium } = require("playwright");

const BASE_URL = process.env.TARGET_URL || "http://127.0.0.1:4173";
const HEADLESS = process.env.HEADLESS !== "false";

async function testRequestPriority() {
  const browser = await chromium.launch({ headless: HEADLESS });

  // Test 1: command priority (POST) jumps ahead of already-queued background
  // work (GET), even though the background requests were issued first.
  console.log("Test 1: command priority jumps ahead of queued background work");
  {
    const page = await browser.newPage();
    const completionOrder = [];

    await page.route("**/slow-bg-*", async (route) => {
      await new Promise((resolve) => setTimeout(resolve, 150));
      route.fulfill({ status: 200, contentType: "application/json", body: "{}" });
    });
    await page.route("**/user-command", async (route) => {
      route.fulfill({ status: 200, contentType: "application/json", body: "{}" });
    });

    await page.goto(BASE_URL + "/wifi.html");
    await page.addScriptTag({ url: "/web_api.js" });

    const order = await page.evaluate(async () => {
      const seen = [];
      // First occupy the single slot with a slow background request, then
      // queue two more background requests behind it, THEN issue a command
      // -- if priority ordering works, the command must still complete
      // before the two background requests queued ahead of it in time.
      const bg0 = window.PAApi.get("/slow-bg-0").then(() => seen.push("bg0"));
      await new Promise((r) => setTimeout(r, 10)); // let bg0 grab the slot
      const bg1 = window.PAApi.get("/slow-bg-1").then(() => seen.push("bg1"));
      const bg2 = window.PAApi.get("/slow-bg-2").then(() => seen.push("bg2"));
      const cmd = window.PAApi.postForm("/user-command", {}).then(() => seen.push("cmd"));
      await Promise.all([bg0, bg1, bg2, cmd]);
      return seen;
    });

    console.log("  Completion order:", order.join(", "));
    // bg0 was already in flight (can't be preempted mid-request), but cmd
    // must finish before bg1 and bg2, which were queued earlier in time.
    const cmdIndex = order.indexOf("cmd");
    const bg1Index = order.indexOf("bg1");
    const bg2Index = order.indexOf("bg2");
    assert.ok(cmdIndex < bg1Index, "command should complete before background request queued earlier");
    assert.ok(cmdIndex < bg2Index, "command should complete before background request queued earlier");
    console.log("  ✓ Command priority jumped the background queue");

    await page.close();
  }

  // Test 2: Estop bypasses the queue entirely -- completes promptly even
  // while the single slot is occupied by a slow request.
  console.log("Test 2: Estop bypasses the queue/slot entirely");
  {
    const page = await browser.newPage();

    await page.route("**/slow-hold", async (route) => {
      await new Promise((resolve) => setTimeout(resolve, 2000));
      route.fulfill({ status: 200, contentType: "application/json", body: "{}" });
    });
    await page.route("**/api/estop", (route) => {
      route.fulfill({ status: 200, contentType: "application/json", body: "{}" });
    });

    await page.goto(BASE_URL + "/wifi.html");
    await page.addScriptTag({ url: "/web_api.js" });

    const estopMs = await page.evaluate(async () => {
      // Occupy the single slot with a slow request first.
      window.PAApi.get("/slow-hold");
      await new Promise((r) => setTimeout(r, 50)); // let it grab the slot

      const start = performance.now();
      await window.PAApi.postForm("/api/estop", {}, { priority: window.PAApi.PRIORITY.ESTOP });
      return performance.now() - start;
    });

    console.log(`  Estop completed in ${estopMs.toFixed(0)}ms while a 2000ms request held the only slot`);
    assert.ok(estopMs < 500, `Estop should not wait behind the held slot, took ${estopMs}ms`);
    console.log("  ✓ Estop bypassed the queue and completed promptly");

    await page.close();
  }

  await browser.close();
  console.log("\nAll request-priority tests passed! ✓");
}

testRequestPriority().catch((err) => {
  console.error("Test failed:", err);
  process.exit(1);
});
