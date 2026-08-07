const assert = require("node:assert/strict");
const { chromium } = require("playwright");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/wifi.html";
const BASE_URL = new URL(TARGET_URL).origin;
const HEADLESS = process.env.HEADLESS === "true";

const json = (payload, status = 200) => ({
  status,
  contentType: "application/json",
  body: JSON.stringify(payload),
});

const configPayload = (wifi) => ({
  ok: true,
  wifi,
  components: {},
  system: { logLevel: 2 },
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

async function installRoutes(page) {
  let wifiConfig = {
    provisioned: true,
    mode: "client",
    staSsid: "AstroHome",
    staPasswordSet: true,
    apSsid: "protoArtoo-AP",
    apPasswordSet: true,
    pendingApply: true,
  };

  const wifiDiagnostics = {
    apSsid: "protoArtoo-Setup",
    apIp: "192.168.4.1",
    staEnabled: true,
    staConnected: true,
    staIp: "10.0.0.22",
    wifiRssi: -64,
  };

  const postBodies = [];

  await page.route("**/api/identity", (route) =>
    route.fulfill(json({ droidName: "r5unit", mdnsUseName: true })),
  );
  await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));
  await page.route("**/api/config", (route) => route.fulfill(json(configPayload(wifiConfig))));
  await page.route("**/api/wifi", async (route) => {
    const request = route.request();
    if (request.method() === "GET") {
      await route.fulfill(json(wifiDiagnostics));
      return;
    }

    const body = new URLSearchParams(request.postData() || "");
    postBodies.push(body);
    if (body.get("wifiMode") === "client" && !body.get("staSsid")) {
      await route.fulfill(json({ ok: false, error: "staSsid is required for WiFi Client Mode" }, 400));
      return;
    }

    wifiConfig = {
      ...wifiConfig,
      provisioned: true,
      mode: body.get("wifiMode") || wifiConfig.mode,
      staSsid: body.get("staSsid") || wifiConfig.staSsid,
      apSsid: body.get("apSsid") || wifiConfig.apSsid,
      staPasswordSet: body.has("staPassword") ? Boolean(body.get("staPassword")) : wifiConfig.staPasswordSet,
      apPasswordSet: body.has("apPassword") ? Boolean(body.get("apPassword")) : wifiConfig.apPasswordSet,
      pendingApply: true,
    };
    await route.fulfill(json({ ok: true, wifi: wifiConfig }));
  });

  await page.addInitScript(() => {
    window.EventSource = undefined;
  });

  return { postBodies };
}

async function text(page, selector) {
  return (await page.locator(selector).textContent()).trim();
}

async function assertNoHorizontalOverflow(page) {
  const metrics = await page.evaluate(() => ({
    width: window.innerWidth,
    scrollWidth: document.documentElement.scrollWidth,
  }));
  assert.equal(metrics.scrollWidth <= metrics.width, true, JSON.stringify(metrics));
}

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const pageErrors = [];
  const consoleErrors = [];
  page.on("pageerror", (err) => pageErrors.push(String(err)));
  page.on("console", (msg) => {
    const text = msg.text();
    if (msg.type() === "error" && !text.startsWith("Failed to load resource:")) {
      consoleErrors.push(text);
    }
  });

  try {
    const { postBodies } = await installRoutes(page);

    await page.goto(TARGET_URL, { waitUntil: "networkidle" });
    await page.waitForSelector("#wifi-settings-form", { timeout: 10000 });

    assert.match(await text(page, "#wifi-provisioning-state"), /Provisioned/);
    assert.match(await text(page, "#wifi-active-mode"), /WiFi Client Mode/);
    assert.match(await text(page, "#wifi-client-state"), /Connected/);
    assert.match(await text(page, "#wifi-sta-ip"), /10\.0\.0\.22/);
    assert.match(await text(page, "#wifi-ap-ip"), /192\.168\.4\.1/);
    assert.match(await text(page, "#wifi-signal"), /Excellent/);
    assert.match(await text(page, "#wifi-pending-summary"), /pending/i);
    assert.match(await text(page, "#wifi-active-summary-mode"), /WiFi Client Mode/);
    assert.match(await text(page, "#wifi-active-summary-address"), /r5unit\.local/);
    assert.match(await text(page, "#wifi-saved-summary-mode"), /WiFi Client Mode/);
    assert.match(await text(page, "#wifi-saved-summary-sta"), /AstroHome/);
    assert.match(await text(page, "#wifi-saved-summary-ap"), /protoArtoo-AP/);
    assert.match(await text(page, "#wifi-apply-guidance"), /r5unit\.local/);
    assert.match(await text(page, "#wifi-apply-guidance"), /10\.0\.0\.22/);
    assert.equal(await page.locator("#wifi-sta-ssid").inputValue(), "AstroHome");

    await page.fill("#wifi-sta-password", "");
    await page.fill("#wifi-ap-password", "");
    await page.click("#wifi-save-settings-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-settings-feedback")?.textContent.includes("saved"),
    );
    assert.equal(postBodies.length, 1);
    assert.equal(postBodies[0].get("wifiMode"), "client");
    assert.equal(postBodies[0].get("staSsid"), "AstroHome");
    assert.equal(postBodies[0].has("staPassword"), false);
    assert.equal(postBodies[0].has("apPassword"), false);

    await page.fill("#wifi-sta-ssid", "");
    await page.click("#wifi-save-settings-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-sta-ssid-error")?.textContent.includes("required"),
    );
    assert.equal(await page.locator("#wifi-sta-ssid").getAttribute("aria-invalid"), "true");

    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-client-desktop.png", fullPage: true });

    await page.setViewportSize({ width: 820, height: 1100 });
    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-client-tablet.png", fullPage: true });

    await page.setViewportSize({ width: 390, height: 860 });
    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-client-mobile.png", fullPage: true });

    await page.goto(`${BASE_URL}/setup.html`, { waitUntil: "networkidle" });
    await page.waitForSelector("#feature-form", { timeout: 10000 });
    // WiFi configuration lives only on the WiFi page; Setup carries no WiFi controls.
    assert.equal(await page.locator("[name='wifiMode']").count(), 0);
    await page.screenshot({ path: "/tmp/setup-wifi-deferral.png", fullPage: true });

    assert.deepEqual(pageErrors, []);
    assert.deepEqual(consoleErrors, []);

    console.log("WIFI_CLIENT_MODE_PAGE_START");
    console.log(JSON.stringify({
      postCount: postBodies.length,
      screenshots: [
        "/tmp/wifi-client-desktop.png",
        "/tmp/wifi-client-tablet.png",
        "/tmp/wifi-client-mobile.png",
        "/tmp/setup-wifi-deferral.png",
      ],
    }, null, 2));
    console.log("WIFI_CLIENT_MODE_PAGE_END");
  } catch (error) {
    console.error("WiFi client mode page test failed:", error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();
