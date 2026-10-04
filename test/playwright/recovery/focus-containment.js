// bench-auto: fixture index.html
// Focus containment + restoration check for the Page Recovery View (issue #115).
//
// The recovery panel auto-hides once sections stabilise, so a SECTION failure
// cannot hold it open long enough to test focus. A RESOURCE failure can:
// resourcesReady stays false, so deriveView keeps reporting the blocking
// resource and the panel remains visible until the resource finally loads.
// That is the same mechanism recovery-ui-stories.js Scenario 1 uses.
//
// CONTAINMENT IS THE SURFACE'S, NOT A TRAP (aa63f311, ADR 0048, #359). The view
// used to hold Tab inside the panel and wrap it at both ends; that also held
// focus away from the Latching Estop on the chrome, at exactly the moment an
// operator reaches for it. The failed surface is now `inert` instead
// (data/page_bootstrap.js holdSurfacesInert), so #115's concern - Tab must not
// wander into a page that is not there - is checked as: a whole Tab cycle,
// each way, from the panel back to the panel, never lands in the surface, and
// reaches the Status Plate's ESTOP cell and STOP on the way. The surface being
// inert while the view is up, and released after, is checked directly.

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

  // Wait until the BOOTSTRAP owns the visible backdrop -- not merely until it is
  // active. The inlined kernel creates and activates #page-recovery-backdrop long
  // before page_bootstrap.js loads, so waiting on `.active` alone samples the
  // kernel's own panel and reports a false failure: no tabindex, no announcer,
  // no focus management, because none of that code has run yet.
  // The announcer is created only by ensureBackdrop(), so it is a reliable
  // marker that the bootstrap has taken over. Never networkidle - SSE stays open.
  let visible = false;
  for (let i = 0; i < 80; i++) {
    visible = await state(page, () => {
      const bd = document.getElementById("page-recovery-backdrop");
      return !!(window.PARecoveryView && bd
        && bd.classList.contains("active")
        && bd.querySelector(".recovery-countdown-announcer"));
    });
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
    // The surface is everything the panel's host holds besides the panel:
    // the work area's children, which the view marks inert.
    const host = bd.parentElement;
    const inSurface = !!(a && host && host !== document.body && host.contains(a) && !bd.contains(a));
    // Which attempt the blocking resource is on: a retry rebuilds the panel,
    // and the rebuild moves focus into it (see stays-on-stop below).
    const st = window.PABootstrap?.getState?.();
    const step = st && !st.resourcesReady ? st.resources[st.resourceCursor] : null;
    return {
      attempt: step ? step.attempt : null,
      inside: bd.contains(a),
      inSurface,
      id: a ? a.id : "",
      el: a ? a.tagName.toLowerCase() + (a.id ? "#" + a.id : "") + (a.className ? "." + String(a.className).split(" ")[0] : "") : null,
      focusables: bd.querySelectorAll('button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])').length,
    };
  });

  // The surface the view stands in for: the host's other children, each inert.
  const surfaceInert = () => state(page, () => {
    const bd = document.getElementById("page-recovery-backdrop");
    const host = bd.parentElement;
    const others = host && host !== document.body ? Array.from(host.children).filter((c) => c !== bd) : [];
    return { count: others.length, inert: others.filter((c) => c.inert).length };
  });

  const f0 = await focusInfo();
  record("focus-moved-in", f0.inside ? "PASS" : "FAIL",
    `focus after panel appeared: ${f0.el} (inside=${f0.inside}, focusables=${f0.focusables})`);

  const held = await surfaceInert();
  record("surface-inert", held.count > 0 && held.inert === held.count ? "PASS" : "FAIL",
    `surface nodes beside the panel: ${held.count}, inert: ${held.inert}`);

  // One whole Tab cycle each way, with REAL key presses, from the panel until
  // focus is back in it. The chrome is long (Plate cells, topbar, nav), so the
  // cycle is bounded generously; one that never comes back is a failure too.
  const CYCLE_LIMIT = 80;
  // A retry landing mid-cycle rebuilds the panel and pulls focus into it,
  // which would read as the cycle coming home. Such a cycle is walked again
  // (the retry interval grows, so a second walk fits); landing in the surface
  // fails it whether or not a retry landed.
  const cycle = async (key) => {
    let walk;
    for (let tries = 0; tries < 4; tries++) {
      await page.locator("#page-recovery-backdrop .btn.accent").focus();
      const startAttempt = (await focusInfo()).attempt;
      walk = { trail: [], reached: new Set(), intoSurface: false, returned: false, interrupted: false };
      for (let i = 0; i < CYCLE_LIMIT; i++) {
        await page.keyboard.press(key);
        await page.waitForTimeout(40);
        const f = await focusInfo();
        if (f.inSurface) walk.intoSurface = true;
        if (f.attempt !== startAttempt) { walk.interrupted = true; break; }
        if (f.inside) { walk.returned = true; break; }
        walk.trail.push(`${f.el}${f.inSurface ? " <-IN SURFACE" : ""}`);
        if (f.id) walk.reached.add(f.id);
      }
      if (walk.intoSurface || !walk.interrupted) break;
    }
    return walk;
  };

  const forward = await cycle("Tab");
  record("tab-containment", !forward.intoSurface && forward.returned ? "PASS" : "FAIL",
    `Tab x${forward.trail.length + 1}, back in the panel: ${forward.returned} -> ${forward.trail.join(" | ")}`);
  const back = await cycle("Shift+Tab");
  record("shift-tab-containment", !back.intoSurface && back.returned ? "PASS" : "FAIL",
    `Shift+Tab x${back.trail.length + 1}, back in the panel: ${back.returned} -> ${back.trail.join(" | ")}`);
  // What the trap could not do and the reason it went (#359).
  const reachesEstop = (c) => c.reached.has("shell-estop-button") && c.reached.has("chip-estop");
  record("estop-reachable", reachesEstop(forward) && reachesEstop(back) ? "PASS" : "FAIL",
    `STOP and the ESTOP cell by Tab: ${reachesEstop(forward)}, by Shift+Tab: ${reachesEstop(back)}`);

  // Reaching STOP is only half of #359: focus left on it must stay there
  // while the view keeps retrying. Hold it on STOP across the next retry.
  await page.locator("#shell-estop-button").focus();
  const onStop = await focusInfo();
  let after = onStop;
  for (let i = 0; i < 100 && after.attempt === onStop.attempt; i++) {
    await page.waitForTimeout(200);
    after = await focusInfo();
  }
  const retried = after.attempt !== onStop.attempt;
  record("stays-on-stop", retried && after.id === "shell-estop-button" ? "PASS" : "FAIL",
    retried
      ? `focus on STOP, retry ${onStop.attempt} -> ${after.attempt}: focus now on ${after.el}`
      : `no retry within 20 s of attempt ${onStop.attempt}; cannot tell`);

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
    // A surface left inert on the way out would come back dead.
    const released = await state(page, () =>
      Array.from(document.querySelectorAll("#shell-content > *")).filter((c) => c.inert).length);
    record("surface-released", released === 0 ? "PASS" : "FAIL", `inert nodes left in the work area: ${released}`);
  }

  const failed = results.filter((r) => r.verdict === "FAIL");
  console.log(`\n=== ${results.length - failed.length}/${results.length} passed ===`);
  await browser.close();
  process.exit(failed.length ? 1 : 0);
})().catch((e) => { console.error("harness error:", e.message); process.exit(2); });
