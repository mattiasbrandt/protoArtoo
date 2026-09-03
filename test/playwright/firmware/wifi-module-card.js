const assert = require("node:assert/strict");
const { chromium } = require("playwright");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/firmware.html";
const HEADLESS = process.env.HEADLESS !== "false";

const json = (payload, status = 200) => ({
  status,
  contentType: "application/json",
  body: JSON.stringify(payload),
});

const artooIdentity = {
  droidName: "artoo",
  mdnsUseName: true,
  board: "artoo_esp32",
  board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
  build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
};

const hostedIdentity = {
  droidName: "r2",
  mdnsUseName: true,
  board: "firebeetle2",
  board_capabilities: { PA_CAP_NATIVE_WIFI: false, PA_CAP_HOSTED_WIFI: true },
  build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
};

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  let identity = artooIdentity;
  let wifiModule = { updateSupport: "unknown", hostVersion: "2.12.11" };

  await page.route("**/api/**", async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/api/identity") {
      await route.fulfill(json(identity));
      return;
    }
    if (path === "/api/status") {
      await route.fulfill(json({
        uptimeMs: 1000,
        wifiModule,
      }));
      return;
    }
    if (path === "/api/events") {
      await route.fulfill({ status: 200, contentType: "text/event-stream", body: "" });
      return;
    }
    await route.fulfill(json({ ok: true }));
  });

  await page.goto(TARGET_URL, { waitUntil: "networkidle" });
  await page.waitForSelector("#wifi-module-card");

  const artooStatus = await page.locator("#wm-availability-status").textContent();
  assert.equal(artooStatus.trim(), "Not on this board");
  const artooReason = await page.locator("#wm-availability-reason").textContent();
  assert.match(artooReason, /cannot run a WiFi module/);
  assert.equal(await page.locator("#wifi-module-card").isVisible(), true);
  assert.equal(await page.locator("#wm-content").isVisible(), false);

  identity = hostedIdentity;
  wifiModule = { updateSupport: "unknown", hostVersion: "2.12.11" };
  await page.reload({ waitUntil: "networkidle" });
  await page.waitForSelector("#wifi-module-card");
  const unknownLine = await page.locator("#wm-support-line").textContent();
  assert.match(unknownLine, /not answering/);
  assert.doesNotMatch(unknownLine, /0\.0\.0/);
  assert.doesNotMatch(unknownLine, /cannot take an update over the air/);

  wifiModule = { updateSupport: "not_supported", hostVersion: "2.12.11" };
  await page.reload({ waitUntil: "networkidle" });
  await page.waitForSelector("#wm-support-line");
  const refused = await page.locator("#wm-support-line").textContent();
  assert.match(refused, /wired rewrite/);

  wifiModule = { updateSupport: "supported", version: "2.12.11", hostVersion: "2.12.11" };
  await page.reload({ waitUntil: "networkidle" });
  await page.waitForSelector("#wm-version-line");
  const versions = await page.locator("#wm-version-line").textContent();
  assert.match(versions, /2\.12\.11/);
  assert.equal(await page.locator("#upload-wm-button").isEnabled(), true);

  await browser.close();
  console.log("wifi-module-card: ok");
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
