const assert = require("node:assert/strict");
const { chromium } = require("playwright");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/wifi.html";
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
    mode: "standalone_ap",
    staSsid: "AstroHome",
    staPasswordSet: true,
    apSsid: "FieldArtoo",
    apPasswordSet: true,
    pendingApply: false,
  };

  let wifiDiagnostics = {
    apSsid: "FieldArtoo",
    apIp: "192.168.4.1",
    staEnabled: false,
    staConnected: false,
    staIp: "",
    wifiRssi: 0,
  };

  const postBodies = [];
  let rebootRequests = 0;

  await page.route("**/api/identity", (route) =>
    route.fulfill(json({ droidName: "r5unit", mdnsUseName: true })),
  );
  await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));
  await page.route("**/api/config", (route) => route.fulfill(json(configPayload(wifiConfig))));
  await page.route("**/api/reboot", (route) => {
    rebootRequests += 1;
    route.fulfill(json({ ok: true }));
  });
  await page.route("**/api/wifi", async (route) => {
    const request = route.request();
    if (request.method() === "GET") {
      await route.fulfill(json(wifiDiagnostics));
      return;
    }

    const body = new URLSearchParams(request.postData() || "");
    postBodies.push(body);
    const mode = body.get("wifiMode") || wifiConfig.mode;
    const apPassword = body.get("apPassword") || "";
    if (mode === "standalone_ap" && !body.get("apSsid")) {
      await route.fulfill(json({ ok: false, error: "apSsid is required for Standalone AP Mode" }, 400));
      return;
    }
    if (body.has("apPassword") && apPassword.length > 0 && apPassword.length < 8) {
      await route.fulfill(json({ ok: false, error: "apPassword must be empty or 8..63 characters" }, 400));
      return;
    }

    wifiConfig = {
      ...wifiConfig,
      provisioned: true,
      mode,
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

  return {
    postBodies,
    rebootRequests: () => rebootRequests,
    setScenario(nextWifiConfig, nextWifiDiagnostics) {
      wifiConfig = nextWifiConfig;
      wifiDiagnostics = nextWifiDiagnostics;
    },
  };
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
    const entry = msg.text();
    if (msg.type() === "error" && !entry.startsWith("Failed to load resource:")) {
      consoleErrors.push(entry);
    }
  });

  try {
    const routes = await installRoutes(page);

    await page.goto(TARGET_URL, { waitUntil: "networkidle" });
    await page.waitForSelector("#wifi-settings-form", { timeout: 10000 });

    assert.match(await text(page, "#wifi-active-mode"), /Standalone AP Mode/);
    assert.match(await text(page, "#wifi-client-state"), /Not active/);
    assert.match(await text(page, "#wifi-active-summary-network"), /FieldArtoo/);
    assert.match(await text(page, "#wifi-apply-guidance"), /FieldArtoo/);
    assert.match(await text(page, "#wifi-apply-guidance"), /192\.168\.4\.1/);
    assert.match(await text(page, "#wifi-apply-guidance"), /OTA/i);
    assert.equal(await page.locator("#wifi-mode-standalone-ap").isChecked(), true);
    assert.equal(await page.locator("#wifi-apply-reboot-button").isDisabled(), true);

    routes.setScenario(
      {
        provisioned: false,
        mode: "client",
        staSsid: "",
        staPasswordSet: false,
        apSsid: "protoArtoo-Setup",
        apPasswordSet: true,
        pendingApply: false,
      },
      {
        apSsid: "protoArtoo-Setup",
        apIp: "192.168.4.1",
        staEnabled: false,
        staConnected: false,
        staIp: "",
        wifiRssi: 0,
      },
    );
    await page.reload({ waitUntil: "networkidle" });
    assert.match(await text(page, "#wifi-active-mode"), /WiFi Provisioning/);
    assert.match(await text(page, "#wifi-posture-desc"), /waiting for saved Device WiFi Settings/);

    routes.setScenario(
      {
        provisioned: true,
        mode: "client",
        staSsid: "AstroHome",
        staPasswordSet: true,
        apSsid: "FieldArtoo",
        apPasswordSet: true,
        pendingApply: false,
      },
      {
        apSsid: "FieldArtoo",
        apIp: "192.168.4.1",
        staEnabled: true,
        staConnected: false,
        staIp: "",
        wifiRssi: 0,
      },
    );
    await page.reload({ waitUntil: "networkidle" });
    assert.match(await text(page, "#wifi-active-mode"), /WiFi Client Mode/);
    assert.match(await text(page, "#wifi-client-state"), /Not connected/);
    assert.match(await text(page, "#wifi-apply-guidance"), /WiFi Client Mode is active/);

    routes.setScenario(
      {
        provisioned: true,
        mode: "client",
        staSsid: "AstroHome",
        staPasswordSet: true,
        apSsid: "FieldArtoo",
        apPasswordSet: true,
        pendingApply: false,
      },
      {
        apSsid: "FieldArtoo",
        apIp: "",
        staEnabled: true,
        staConnected: true,
        staIp: "10.0.0.22",
        wifiRssi: -64,
      },
    );
    await page.reload({ waitUntil: "networkidle" });
    assert.match(await text(page, "#wifi-active-mode"), /WiFi Client Mode/);
    assert.match(await text(page, "#wifi-client-state"), /Connected/);
    assert.equal(await page.locator("#wifi-mode-client").isChecked(), true);

    await page.click("#wifi-mode-standalone-ap");
    assert.equal(await page.locator("#wifi-mode-standalone-ap").isChecked(), true);

    await page.fill("#wifi-ap-password", "short");
    await page.click("#wifi-save-settings-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-ap-password-error")?.textContent.includes("8..63"),
    );
    assert.equal(await page.locator("#wifi-ap-password").getAttribute("aria-invalid"), "true");

    await page.fill("#wifi-ap-password", "");
    await page.fill("#wifi-ap-ssid", "");
    await page.click("#wifi-save-settings-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-ap-ssid-error")?.textContent.includes("required"),
    );
    assert.equal(await page.locator("#wifi-ap-ssid").getAttribute("aria-invalid"), "true");

    await page.fill("#wifi-ap-ssid", "R2-FieldKit");
    await page.fill("#wifi-ap-password", "fieldpass1");
    await page.click("#wifi-save-settings-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-settings-feedback")?.textContent.includes("saved"),
    );
    const lastPost = routes.postBodies.at(-1);
    assert.equal(lastPost.get("wifiMode"), "standalone_ap");
    assert.equal(lastPost.get("apSsid"), "R2-FieldKit");
    assert.equal(lastPost.get("apPassword"), "fieldpass1");
    assert.equal(lastPost.has("staPassword"), false);
    assert.match(await text(page, "#wifi-pending-summary"), /pending/i);
    assert.match(await text(page, "#wifi-apply-guidance"), /R2-FieldKit/);
    assert.match(await text(page, "#wifi-apply-guidance"), /192\.168\.4\.1/);
    assert.match(await text(page, "#wifi-apply-guidance"), /OTA/i);
    assert.equal(await page.locator("#wifi-apply-reboot-button").isDisabled(), false);

    await page.click("#wifi-apply-reboot-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-apply-feedback")?.textContent.includes("Reboot command sent"),
    );
    assert.equal(routes.rebootRequests(), 1);

    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-ap-desktop.png", fullPage: true });

    await page.setViewportSize({ width: 820, height: 1100 });
    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-ap-tablet.png", fullPage: true });

    await page.setViewportSize({ width: 390, height: 860 });
    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-ap-mobile.png", fullPage: true });

    assert.deepEqual(pageErrors, []);
    assert.deepEqual(consoleErrors, []);

    console.log("WIFI_STANDALONE_AP_PAGE_START");
    console.log(JSON.stringify({
      postCount: routes.postBodies.length,
      rebootRequests: routes.rebootRequests(),
      screenshots: [
        "/tmp/wifi-ap-desktop.png",
        "/tmp/wifi-ap-tablet.png",
        "/tmp/wifi-ap-mobile.png",
      ],
    }, null, 2));
    console.log("WIFI_STANDALONE_AP_PAGE_END");
  } catch (error) {
    console.error("WiFi standalone AP page test failed:", error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();
