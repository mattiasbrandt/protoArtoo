// Console-error sweep across every served page on the live controller.
// Standing requirement: zero console errors on any page.
const { chromium } = require("playwright");

const BASE = process.env.BASE || "http://10.0.0.22";
const PAGES = ["index.html","drive.html","dome.html","sound.html","servo.html",
               "seq.html","rc.html","setup.html","wifi.html","firmware.html"];
const SETTLE_MS = Number(process.env.SETTLE_MS || 6000);

(async () => {
  const browser = await chromium.launch({ headless: true });
  const summary = [];
  for (const p of PAGES) {
    const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    const jsErrors = [], consoleErrors = [], resourceErrors = [];
    page.on("pageerror", (e) => jsErrors.push(String(e).split("\n")[0]));
    page.on("console", (m) => {
      if (m.type() !== "error") return;
      const t = m.text();
      (t.startsWith("Failed to load resource") ? resourceErrors : consoleErrors).push(t.slice(0, 160));
    });
    try {
      await page.goto(`${BASE}/${p}`, { waitUntil: "domcontentloaded", timeout: 20000 });
      await page.waitForTimeout(SETTLE_MS);   // let the bootstrap finish; never networkidle (SSE stays open)
    } catch (e) {
      jsErrors.push("NAV FAILED: " + e.message.split("\n")[0]);
    }
    const uniq = (a) => [...new Set(a)];
    summary.push({ page: p, js: uniq(jsErrors), console: uniq(consoleErrors), resource: uniq(resourceErrors) });
    await page.close();
  }
  await browser.close();

  let bad = 0;
  console.log("page             jsErr consoleErr resourceErr");
  console.log("---------------- ----- ---------- -----------");
  for (const s of summary) {
    const n = s.js.length + s.console.length;
    if (n > 0) bad++;
    console.log(`${s.page.padEnd(16)} ${String(s.js.length).padStart(5)} ${String(s.console.length).padStart(10)} ${String(s.resource.length).padStart(11)}`);
  }
  console.log("");
  for (const s of summary) {
    if (s.js.length || s.console.length) {
      console.log(`--- ${s.page} ---`);
      s.js.forEach((e) => console.log("   [pageerror] " + e));
      s.console.forEach((e) => console.log("   [console]   " + e));
    }
    if (s.resource.length) s.resource.forEach((e) => console.log(`   [${s.page} resource] ` + e));
  }
  console.log(`\n=== pages with JS/console errors: ${bad}/${summary.length} ===`);
  process.exit(bad ? 1 : 0);
})().catch((e) => { console.error("sweep error:", e.message); process.exit(2); });
