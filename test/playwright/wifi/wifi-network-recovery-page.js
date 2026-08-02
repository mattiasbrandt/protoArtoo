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
  // Network Recovery Mode (ADR 0015): a local power-cycle gesture opened
  // WiFi Provisioning without touching saved Device WiFi Settings. The
  // controller is still "provisioned" — that is the whole point of recovery
  // preserving settings for inspection/repair — but diagnostics report
  // networkRecovery=true because that is the posture actually running.
  let wifiConfig = {
    provisioned: true,
    mode: "client",
    staSsid: "AstroHome-Typo",
    staPasswordSet: true,
    apSsid: "FieldArtoo",
    apPasswordSet: true,
    pendingApply: false,
  };

  const wifiDiagnostics = {
    apSsid: "protoArtoo-Setup",
    apIp: "192.168.4.1",
    staEnabled: false,
    staConnected: false,
    staIp: "",
    wifiRssi: 0,
    networkRecovery: true,
  };

  const postBodies = [];
  let rebootRequests = 0;

  await page.route("**/api/identity", (route) =>
    route.fulfill(json({ droidName: "r5unit", mdnsUseName: true })),
  );
  await page.route("**/api/status", (route) => route.fulfill(json(statusPayload)));
  await page.route("**/api/config", (route) =>
    route.fulfill(json(configPayload({ ...wifiConfig, networkRecovery: true }))),
  );
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

  return {
    postBodies,
    rebootRequests: () => rebootRequests,
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

    // Recovery posture must be visually and textually distinct from both
    // WiFi Provisioning (unprovisioned) and ordinary Client Mode failure,
    // even though wifi.provisioned reads true throughout.
    assert.equal(await page.locator("#wifi-posture-card").getAttribute("data-posture"), "recovery");
    assert.match(await text(page, "#wifi-provisioning-state"), /Network Recovery Mode/);
    assert.match(await text(page, "#wifi-active-mode"), /Network Recovery Mode/);
    assert.match(await text(page, "#wifi-pending-summary"), /Recovery/);
    assert.match(await text(page, "#wifi-posture-desc"), /power-cycle gesture/);
    assert.match(await text(page, "#wifi-posture-desc"), /untouched/);

    // Saved Device WiFi Settings must still be visible/editable for repair —
    // recovery does not erase them.
    assert.equal(await page.locator("#wifi-sta-ssid").inputValue(), "AstroHome-Typo");
    assert.match(await text(page, "#wifi-saved-summary-sta"), /AstroHome-Typo/);

    // Reconnect guidance points at the recovery AP, not a client-mode address.
    assert.match(await text(page, "#wifi-apply-guidance"), /Network Recovery Mode/);
    assert.match(await text(page, "#wifi-apply-guidance"), /protoArtoo-Setup/);
    assert.match(await text(page, "#wifi-apply-guidance"), /192\.168\.4\.1/);
    assert.equal(await page.locator("#wifi-apply-reboot-button").isDisabled(), true);

    // Operator repairs the saved STA SSID from within recovery and saves —
    // the existing staged-switch write path handles this with no special
    // "recovery repair" endpoint.
    await page.fill("#wifi-sta-ssid", "AstroHome");
    await page.click("#wifi-save-settings-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-settings-feedback")?.textContent.includes("saved"),
    );
    const lastPost = routes.postBodies.at(-1);
    assert.equal(lastPost.get("staSsid"), "AstroHome");

    // After the repair save, the page returns to the normal staged
    // apply/reboot flow: pending summary flips, guidance still explains the
    // recovery AP is where to be right now, and Reboot to Apply un-disables.
    assert.match(await text(page, "#wifi-pending-summary"), /Recovery/);
    assert.match(await text(page, "#wifi-apply-guidance"), /saved/i);
    assert.match(await text(page, "#wifi-apply-guidance"), /Reboot to Apply/);
    assert.equal(await page.locator("#wifi-apply-reboot-button").isDisabled(), false);

    await page.click("#wifi-apply-reboot-button");
    await page.waitForFunction(() =>
      document.getElementById("wifi-apply-feedback")?.textContent.includes("Reboot command sent"),
    );
    assert.equal(routes.rebootRequests(), 1);

    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-recovery-desktop.png", fullPage: true });

    await page.setViewportSize({ width: 820, height: 1100 });
    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-recovery-tablet.png", fullPage: true });

    await page.setViewportSize({ width: 390, height: 860 });
    await assertNoHorizontalOverflow(page);
    await page.screenshot({ path: "/tmp/wifi-recovery-mobile.png", fullPage: true });

    assert.deepEqual(pageErrors, []);
    assert.deepEqual(consoleErrors, []);

    console.log("WIFI_NETWORK_RECOVERY_PAGE_START");
    console.log(JSON.stringify({
      postCount: routes.postBodies.length,
      rebootRequests: routes.rebootRequests(),
      screenshots: [
        "/tmp/wifi-recovery-desktop.png",
        "/tmp/wifi-recovery-tablet.png",
        "/tmp/wifi-recovery-mobile.png",
      ],
    }, null, 2));
    console.log("WIFI_NETWORK_RECOVERY_PAGE_END");
  } catch (error) {
    console.error("WiFi network recovery page test failed:", error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();
