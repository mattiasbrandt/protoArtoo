// Every served page, loaded on its own in a fresh browser at 1440x900.
//
// THE RULES IT HOLDS (history in brackets):
//   - Zero console errors on any page: no uncaught page error and no
//     console.error (a failed resource load is listed but not counted - the
//     controller sheds connections by design).
//   - At desktop width the Operator Shell never scrolls sideways, shows all
//     eight Status Plate cells, keeps STOP wholly on screen, and cuts no plate
//     value (history: #399, whose own check was never committed). Read on
//     every page that ends up in the shell (below):
//       overflow  the document does not scroll sideways:
//                 documentElement.scrollWidth == clientWidth;
//       cells     #status-plate-region shows its 8 chips, each with a box
//                 inside the viewport;
//       stop      the topbar's STOP (#shell-estop-button) is inside the
//                 viewport, all of it;
//       clipped   no plate chip's value is cut: every .status-chip-value has
//                 scrollWidth <= clientWidth (the value line is nowrap with
//                 overflow hidden, data/style.css .status-chip-value, so a
//                 value too wide for its cell is silently cut).
// It also prints each page's load time, from the browser's own Navigation
// Timing; that is a reading, not a rule.
//
// WHICH PAGES ARE SHELL PAGES is decided from each page's own source, not from
// what rendered: a page is one if it IS the shell document (it carries
// id="shell-status", which only data/index.html does) or if it is a shell
// delegate that hands a visit to the shell (window.PAShellDelegate and a
// location.replace("/#...") in its head - every surface's .html, and the
// setup.html forwarder). Anything else gets "n/a" in the layout columns. A page
// the source says is a shell page but that renders no #status-plate-region is
// a layout FAIL.
//
// WHAT IT WRITES. A browser-side guard lets out GETs, the Dashboard console's
// read-only `operations`/`help` catalog load, RC's verbose-log toggle
// (POST /api/rc/debug, runtime only), and ONE configuration write: guided
// Setup's own visit record, POST /api/config with nothing but
// guidedSetupVisited=<step> (data/setup.js saveVisited), which Configuration
// sends on a plain visit to a droid that is not set up. It is let through
// because this sweep measures the page's console, and a refused save logs
// "[setup] guided setup visited save failed" - an error the droid would never
// see. Every other write is aborted and listed after the table.
//
// WHY A REAL BROWSER. mini_dom has no CSS engine and no layout: overflow, a
// box inside the viewport and a clipped line of text only exist here.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/console-sweep.js
//   BASE=http://<board>   the controller (default http://10.0.0.22)
//   HEADED=1              open a real window for a bench session the
//                         operator watches (default headless, for unattended
//                         runs)
//   STEP=1                also wait for Enter after each page
// Offline proof: FIXTURE=1 BASE=http://127.0.0.1:4173 against python3
// tools/serve_editor_fixture.py (routes in ./shell/_fixture_routes.js).
// Self-test: SELFTEST_LAYOUT=1 puts a 1600 px wide block in the work area,
// hides one plate chip and moves STOP past the right edge on every shell
// page, and overflow, cells and stop must each FAIL.
const { chromium } = require("playwright");
const fs = require("node:fs");
const path = require("node:path");
const readline = require("node:readline");

const BASE = (process.env.BASE || "http://10.0.0.22").replace(/\/$/, "");
// Read from data/ rather than listed by hand: a hand list went stale as the
// epic added Parts, Wiring, Lights and the Dashboard document, and a sweep
// that never loads a page says nothing about it. Underscore-prefixed pages
// are excluded on purpose: _recovery_kernel.html is the recovery kernel,
// driven by its own scripts in test/playwright/recovery/.
const DATA_DIR = path.join(__dirname, "..", "..", "data");
const PAGES = fs.readdirSync(DATA_DIR)
  .filter((f) => f.endsWith(".html") && !f.startsWith("_"))
  .sort();
const SETTLE_MS = Number(process.env.SETTLE_MS || 6000);
const HEADED = process.env.HEADED === "1";
const STEP = process.env.STEP === "1";
const FIXTURE = process.env.FIXTURE === "1";
const SELFTEST_LAYOUT = process.env.SELFTEST_LAYOUT === "1";
const VIEWPORT = { width: 1440, height: 900 };
const PLATE_CELLS = 8;

// The decision above, made from the page's source.
const shellKindOf = (file) => {
  const source = fs.readFileSync(path.join(DATA_DIR, file), "utf8");
  if (/id="shell-status"/.test(source)) return "shell";
  const delegate = source.match(/window\.PAShellDelegate\s*=\s*true;\s*location\.replace\("(\/#[^"]*)"\)/);
  if (delegate) return `-> ${delegate[1]}`;
  return null;
};

const waitForEnter = (prompt) => new Promise((resolve) => {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  rl.question(prompt, () => { rl.close(); resolve(); });
});

// The layout reading, taken in the page.
const readLayout = (page) =>
  page.evaluate(() => {
    const doc = document.documentElement;
    const region = document.getElementById("status-plate-region");
    if (!region) return { shell: false };
    const inView = (box) =>
      box.width > 0 && box.height > 0 && box.left >= 0 && box.top >= 0 && box.right <= window.innerWidth && box.bottom <= window.innerHeight;
    const chips = [...region.querySelectorAll(".status-chip")];
    const stop = document.getElementById("shell-estop-button");
    const clipped = [...document.querySelectorAll(".status-chip-value")]
      .filter((value) => value.scrollWidth > value.clientWidth)
      .map((value) => {
        const chip = value.closest("[data-chip]");
        return `${chip ? chip.dataset.chip : "?"} "${value.textContent.trim()}" (${value.scrollWidth}>${value.clientWidth} px)`;
      });
    return {
      shell: true,
      scrollWidth: doc.scrollWidth,
      clientWidth: doc.clientWidth,
      cells: chips.length,
      cellsInView: chips.filter((chip) => inView(chip.getBoundingClientRect())).length,
      stopInView: Boolean(stop && inView(stop.getBoundingClientRect())),
      clipped,
    };
  });

(async () => {
  const browser = await chromium.launch({ headless: !HEADED });
  const summary = [];
  // Closed on every exit, a failed one included: headed, an abandoned run
  // leaves a real window on the operator's desktop.
  try {
    for (const p of PAGES) {
      const shellKind = shellKindOf(p);
      const context = await browser.newContext({ viewport: VIEWPORT });
      const fixture = FIXTURE ? await require("./shell/_fixture_routes.js").install(context) : null;
      const page = await context.newPage();
      const jsErrors = [], consoleErrors = [], resourceErrors = [], blocked = [];
      page.on("pageerror", (e) => jsErrors.push(String(e).split("\n")[0]));
      page.on("console", (m) => {
        if (m.type() !== "error") return;
        const t = m.text();
        (t.startsWith("Failed to load resource") ? resourceErrors : consoleErrors).push(t.slice(0, 160));
      });
      // The write guard (header).
      await page.route("**/*", async (route) => {
        const request = route.request();
        if (request.method() === "GET" || request.method() === "HEAD") return route.fallback();
        const url = request.url();
        const routePath = url.slice(url.indexOf("/", url.indexOf("//") + 2)).split("?")[0];
        const body = request.postData() || "";
        const allowed =
          (request.method() === "POST" && routePath === "/api/rc/debug") ||
          (request.method() === "POST" && routePath === "/api/console" && /^command=(operations|help)\b/.test(body)) ||
          (request.method() === "POST" && routePath === "/api/config" && /^guidedSetupVisited=[^&]*$/.test(body));
        if (allowed) return route.fallback();
        blocked.push(`${request.method()} ${routePath} ${body.slice(0, 60)}`.trim());
        return route.abort("blockedbyclient");
      });
      if (SELFTEST_LAYOUT) {
        await page.addInitScript(() => {
          window.addEventListener("load", () => {
            const wide = document.createElement("div");
            wide.id = "selftest-wide";
            wide.style.cssText = "width:1600px;height:4px;";
            (document.getElementById("shell-content") || document.body).appendChild(wide);
            const style = document.createElement("style");
            style.textContent = "#chip-sleep { display: none !important; } #shell-estop-button { position: fixed !important; left: 1430px !important; }";
            document.head.appendChild(style);
          });
        });
      }
      let domMs = null, loadMs = null, layout = null;
      try {
        await page.goto(`${BASE}/${p}`, { waitUntil: "domcontentloaded", timeout: 20000 });
        await page.waitForTimeout(SETTLE_MS);   // let the bootstrap finish; never networkidle (SSE stays open)
        // The browser's own Navigation Timing, not a stopwatch around goto():
        // the performance axis of a bench session is measured, never felt.
        const nav = await page.evaluate(() => {
          const e = performance.getEntriesByType("navigation")[0];
          return e ? { dom: e.domContentLoadedEventEnd, load: e.loadEventEnd } : null;
        });
        if (nav) { domMs = Math.round(nav.dom); loadMs = Math.round(nav.load); }
        if (shellKind) layout = await readLayout(page);
      } catch (e) {
        jsErrors.push("NAV FAILED: " + e.message.split("\n")[0]);
      }
      const uniq = (a) => [...new Set(a)];
      const layoutReasons = [];
      if (shellKind) {
        if (!layout) layoutReasons.push("not read");
        else if (!layout.shell) layoutReasons.push("the source is a shell page but no Status Plate rendered");
        else {
          if (layout.scrollWidth !== layout.clientWidth) layoutReasons.push(`scrolls sideways (scrollWidth ${layout.scrollWidth} vs clientWidth ${layout.clientWidth})`);
          if (layout.cells !== PLATE_CELLS || layout.cellsInView !== PLATE_CELLS) layoutReasons.push(`plate shows ${layout.cells} cells, ${layout.cellsInView} inside the viewport (want ${PLATE_CELLS})`);
          if (!layout.stopInView) layoutReasons.push("STOP is not wholly inside the viewport");
          if (layout.clipped.length) layoutReasons.push(`clipped chip values: ${layout.clipped.join(", ")}`);
        }
      }
      summary.push({ page: p, shellKind, js: uniq(jsErrors), console: uniq(consoleErrors), resource: uniq(resourceErrors), blocked: uniq(blocked), domMs, loadMs, layout, layoutReasons });
      if (STEP) await waitForEnter(`${p}: ${jsErrors.length + consoleErrors.length} errors, layout ${shellKind ? (layoutReasons.length ? "FAIL" : "PASS") : "n/a"}. Enter for the next page... `);
      await context.close();
      if (fixture) await fixture.close();
    }
  } finally {
    await browser.close();
  }

  let bad = 0;
  let layoutBad = 0;
  const mark = (s, ok) => (!s.shellKind ? "n/a" : !s.layout || !s.layout.shell ? "-" : ok ? "ok" : "FAIL");
  console.log("page                 jsErr consoleErr resourceErr domMs loadMs  shell page        overflow cells stop clipped layout");
  console.log("-------------------- ----- ---------- ----------- ----- ------  ----------------- -------- ----- ---- ------- ------");
  for (const s of summary) {
    const n = s.js.length + s.console.length;
    if (n > 0) bad++;
    if (s.layoutReasons.length) layoutBad++;
    const l = s.layout && s.layout.shell ? s.layout : null;
    console.log([
      s.page.padEnd(20),
      String(s.js.length).padStart(5),
      String(s.console.length).padStart(10),
      String(s.resource.length).padStart(11),
      String(s.domMs ?? "-").padStart(5),
      String(s.loadMs ?? "-").padStart(6),
      "",
      String(s.shellKind ?? "no").padEnd(17),
      mark(s, l && l.scrollWidth === l.clientWidth).padEnd(8),
      (l ? `${l.cellsInView}/${l.cells}` : mark(s, false)).padEnd(5),
      mark(s, l && l.stopInView).padEnd(4),
      (l ? String(l.clipped.length) : mark(s, false)).padEnd(7),
      !s.shellKind ? "n/a" : s.layoutReasons.length ? "FAIL" : "PASS",
    ].join(" "));
  }
  console.log("");
  for (const s of summary) {
    if (s.js.length || s.console.length) {
      console.log(`--- ${s.page} ---`);
      s.js.forEach((e) => console.log("   [pageerror] " + e));
      s.console.forEach((e) => console.log("   [console]   " + e));
    }
    if (s.resource.length) s.resource.forEach((e) => console.log(`   [${s.page} resource] ` + e));
    if (s.layoutReasons.length) console.log(`   [${s.page} layout] ${s.layoutReasons.join("; ")}`);
    if (s.blocked.length) console.log(`   [${s.page} write stopped by the guard] ${s.blocked.join(" | ")}`);
  }
  console.log(`\n=== pages with JS/console errors: ${bad}/${summary.length} ===`);
  console.log(`=== shell pages with a layout fault at ${VIEWPORT.width}x${VIEWPORT.height}: ${layoutBad}/${summary.filter((s) => s.shellKind).length} ===`);
  process.exit(bad || layoutBad || summary.length === 0 ? 1 : 0);
})().catch((e) => { console.error("sweep error:", e.message); process.exit(2); });
