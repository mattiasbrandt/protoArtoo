// Focus containment + restoration check for the Page Recovery View (issue #115).
//
// The recovery panel auto-hides once sections stabilise, so a SECTION failure
// cannot hold it open long enough to test focus. A RESOURCE failure can:
// resourcesReady stays false, so deriveView keeps reporting the blocking
// resource and the panel remains visible until the resource finally loads.
// That is the same mechanism recovery-ui-stories.js Scenario 1 uses.

const { chromium } = require("playwright");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/index.html";
const HEADLESS = process.env.HEADLESS !== "false";

const results = [];
const record = (id, verdict, detail) => {
  results.push({ id, verdict, detail });
  console.log(`${verdict.padEnd(4)} ${id} - ${detail}`);
};

const state = (page, fn) => page.evaluate(fn);

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  let released = false;
  // Fail app.js until we release it, so resourcesReady stays false and the panel stays up.
  await page.route("**/app.js", (route) => (released ? route.continue() : route.abort("failed")));

  await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });

  // Wait for the panel to actually be visible (never networkidle - SSE keeps it open).
  let visible = false;
  for (let i = 0; i < 60; i++) {
    visible = await state(page, () =>
      document.getElementById("page-recovery-backdrop")?.classList.contains("active") === true);
    if (visible) break;
    await page.waitForTimeout(250);
  }
  if (!visible) {
    record("panel-visible", "FAIL", "recovery panel never became visible; cannot test focus");
    await browser.close();
    process.exit(1);
  }
  record("panel-visible", "PASS", "recovery panel is visible via resource failure");

  const focusInfo = () => state(page, () => {
    const bd = document.getElementById("page-recovery-backdrop");
    const a = document.activeElement;
    return {
      inside: bd.contains(a),
      el: a ? a.tagName.toLowerCase() + (a.className ? "." + String(a.className).split(" ")[0] : "") : null,
      focusables: bd.querySelectorAll('button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])').length,
    };
  });

  const f0 = await focusInfo();
  record("focus-moved-in", f0.inside ? "PASS" : "FAIL",
    `focus after panel appeared: ${f0.el} (inside=${f0.inside}, focusables=${f0.focusables})`);

  // Tab several times with REAL key presses; focus must never leave the overlay.
  const trail = [];
  let escaped = false;
  for (let i = 0; i < 8; i++) {
    await page.keyboard.press("Tab");
    await page.waitForTimeout(80);
    const f = await focusInfo();
    trail.push(`${f.el}${f.inside ? "" : " <-ESCAPED"}`);
    if (!f.inside) escaped = true;
  }
  record("tab-containment", escaped ? "FAIL" : "PASS", `Tab x8 -> ${trail.join(" | ")}`);

  // Shift+Tab backwards must also stay inside.
  let escapedBack = false;
  const backTrail = [];
  for (let i = 0; i < 5; i++) {
    await page.keyboard.press("Shift+Tab");
    await page.waitForTimeout(80);
    const f = await focusInfo();
    backTrail.push(`${f.el}${f.inside ? "" : " <-ESCAPED"}`);
    if (!f.inside) escapedBack = true;
  }
  record("shift-tab-containment", escapedBack ? "FAIL" : "PASS", `Shift+Tab x5 -> ${backTrail.join(" | ")}`);

  // Release the resource; panel should clear and focus should leave the overlay sanely.
  released = true;
  await state(page, () => window.PABootstrap && window.PABootstrap.retryNow("/app.js"));
  let cleared = false;
  for (let i = 0; i < 40; i++) {
    cleared = await state(page, () =>
      document.getElementById("page-recovery-backdrop")?.classList.contains("active") !== true);
    if (cleared) break;
    await page.waitForTimeout(250);
  }
  record("panel-cleared", cleared ? "PASS" : "FAIL", `panel hidden after resource recovered: ${cleared}`);

  if (cleared) {
    const f = await focusInfo();
    const body = await state(page, () => document.activeElement === document.body);
    record("focus-restored", !f.inside ? "PASS" : "FAIL",
      `focus after clear: ${f.el} (inside=${f.inside}, isBody=${body})`);
  }

  const failed = results.filter((r) => r.verdict === "FAIL");
  console.log(`\n=== ${results.length - failed.length}/${results.length} passed ===`);
  await browser.close();
  process.exit(failed.length ? 1 : 0);
})().catch((e) => { console.error("harness error:", e.message); process.exit(2); });
