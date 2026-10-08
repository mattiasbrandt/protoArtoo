// bench-auto: droid estop=clear
// STOP can be pressed from every surface, in every state a page can be in,
// and it is one toggle: a press on a droid heard latched releases it, any
// other press stops it (ADR 0048, 2026-09-29 amendment). Every STOP press
// below is held to the direction the button showed before it: lit
// (aria-pressed="true") means the press must send POST /api/estop/clear, say
// "Estop released", read "Estop: clear" and go out; unlit means POST
// /api/estop, "Stop sent", "Estop: latched" and lit. The plate's ESTOP chip is
// the same toggle, held to the same direction (#359, 2026-10-04).
//
// THE RULES IT HOLDS. Each is a standing rule of the Operator Shell; the
// ticket or fix that introduced it is history, in brackets. The numbers are
// also the run order.
//   1. A refused control answers a press. Pressing a disabled control with a
//      real pointer raises the Ignored Input Notice, and its link routes to
//      the surface it names (history: #346). Read on Foot Drive's forward pad
//      key (.pad-fwd), which is refused while web control is off or anything
//      holds the feet (data/drive.js updateDriveControlsEnabled): pressed by
//      page.mouse at its centre - a scripted click would wait for an enabled
//      button - the notice (#ignored-input-notice) must read "That control is
//      switched off right now. <cause>." (data/shell.js showNotice), and its
//      link must open the surface in its href (which may be Foot Drive
//      itself: the feet and web control are both changed there). It needs
//      the estop clear, or the cause is the latch, which names STOP and has
//      no link, so it runs first.
//   2. A posture the droid is in never covers the estop (history: #330, #359,
//      fix 11a30d27). Sleep is asked for through the Dashboard's own Sleep
//      button (POST /api/sleep); with the overlay up (.sleep-overlay.active,
//      data/dashboard.html) the topbar STOP and the plate's ESTOP chip
//      (#chip-estop) must each be the topmost thing at its own centre
//      (document.elementFromPoint), and a press of each must land. STOP is
//      unlit, so its press LATCHES the estop; the chip's press on the now
//      latched droid is the same toggle, so it must RELEASE it
//      (POST /api/estop/clear, "Estop released", "Estop: clear", STOP out).
//      The overlay's "Wake Droid" is then pressed (POST /api/wake) and the
//      overlay must come down.
//   3. A dialog is never a keyboard trap: STOP is reachable by Tab from inside
//      one and a key press on it stops the droid (history: #359). With the
//      Sequences "Restore backup" dialog open (data/seq.js showModal), Tab
//      from the dialog's first control must reach STOP, and Enter and then
//      Space on it must each land: the droid is clear from rule 2, so Enter
//      stops and Space releases. Shift+Tab is walked and reported.
//   4. The Page Recovery View covers the work area and never the chrome
//      (ADR 0048; history: #359, fix aa63f311). On two surfaces not yet
//      mounted, the surface's document (its SURFACES `doc`, e.g. /dome.html)
//      is (a) held in flight by page.route - a first surface load, the view
//      reading "Loading page resources" - and (b) aborted by page.route - a
//      fault, the view in its retry state. In each, with body.recovery-active
//      set, STOP and the ESTOP chip must be topmost and a STOP press must
//      land, in whichever direction the button showed. The route is then
//      lifted and "Retry now" pressed, so the surface loads for the walk below.
//   5. STOP is on top and takes a press on every surface, its dialog open
//      where it has one (ADR 0048; history: #359). Every surface the served
//      shell.js lists in SURFACES is opened through the nav, and of STOP:
//        - visible and enabled; computed pointer-events not `none`;
//        - topmost at its own centre (a dialog, backdrop or overlay painted
//          over the chrome fails here and nowhere else);
//        - a real pointer press lands in the direction the button showed, so
//          the walk alternates: a stop ("Stopping the droid..." then "Stop
//          sent", "Estop: latched", lit) on one surface and a release
//          ("Releasing the estop..." then "Estop released", "Estop: clear",
//          unlit) on the next. Both directions are proved on every kind of
//          surface the walk meets. If the walk ends released, one more STOP
//          press latches it for rule 6.
//   6. Navigation never clears a latch (ADR 0048; history: #359). Every
//      surface AGAIN, pressing nothing: the state line must read
//      "Estop: latched" on every surface, an observer fails any other text it
//      takes on the way, and no POST /api/estop or /api/estop/clear may be
//      sent.
//   7. A new browser reads the latch the droid holds, and shows STOP lit
//      (history: #346 admission edge, #359 seeded-frame race). A NEW browser
//      context (no cache, no session) opens /#home. Its state line must
//      settle on "Estop: latched" - never "Estop: clear" at any moment, and
//      not stuck on the waiting dots - and its STOP must be lit
//      (.is-latched, aria-pressed="true"), read twice 3 s apart, so a later
//      repaint that puts it out is caught. It is NOT pressed.
//
// PRECONDITION: the estop is CLEAR and the droid is NOT in Sleep Mode. The
// script reads GET /api/status first and refuses to run otherwise, because
// rules 1 and 2 mean nothing on a droid already latched or asleep.
//
// IT LEAVES THE ESTOP CLEAR. Its last act is one more STOP press in the first
// browser, which releases the latch rule 6 needed, and the closing line says
// which state the droid is in. A run that stops half way says so too: then
// the droid may be left latched, and STOP (lit) releases it.
//
// NOTHING BUT STOP AND SLEEP REACH THE DROID AS WRITES. Some surfaces write on
// a plain visit - guided Setup, drawn over Configuration on a droid that is
// not set up, saves which questions it has shown (data/setup.js saveVisited,
// POST /api/config). A browser-side guard lets through only GETs, POST
// /api/estop and /api/estop/clear, the Dashboard console's read-only
// `operations`/`help`, RC's
// verbose-log toggle (POST /api/rc/debug, runtime only), and - during rule 2
// only - POST /api/sleep and POST /api/wake. It aborts every other write
// before it leaves the browser; what it stopped is listed after the table, a
// fact about the surface, not a STOP failure. The fresh context in rule 7
// lets out only GETs and the console's read-only catalog load.
//
// WHY A REAL BROWSER. The web suite's DOM (test/test_web/helpers/mini_dom.js)
// has no CSS engine, no layout, no hit testing, no focus order and no
// pointer. A dialog that covers STOP, a `pointer-events: none` that wins its
// cascade, a native modal <dialog> that makes the rest of the document inert,
// a stacking context that traps the chrome under an overlay, or a Tab order
// that never leaves a dialog are all invisible to it and all visible here -
// which is the #359 defect class.
//
// HOW EACH DIALOG IS OPENED.
//   seq     "Restore backup" is pressed, which opens its import dialog through
//           the surface's own showModal() helper (data/seq.js). Cancel closes
//           it; nothing is restored. The wipe dialog is the same helper and
//           needs a stored sequence, so it is not opened separately.
//   wiring, the move question, opened through the page the way a builder
//   servo   opens it: the surface asks it only for a move that takes a Part off
//           the Output it is on and puts it on another (data/parts_mapping.js
//           moveFor, `announce`). On Wiring, a Part already on an Output has
//           another Output pressed on its row's bar in the parts table (moved
//           there from Parts, #411; a bar since #463); on Servos, a Part
//           already on one Output is picked in another Output's "Put a part
//           on" picker. Either change reaches the page's own mover.request(),
//           which opens the dialog - no POST is sent until the question is
//           answered "Move it", and it never is: the dialog's Cancel closes it.
//           With no Part on any Output there is no move to ask about, and the
//           row is NOT ASSESSED with that reason (never PASS). The guard aborts
//           any write regardless.
//
// WHAT IT DOES NOT DO. It writes no configuration, moves nothing and needs
// nothing wired but USB (Bench-Mode); the move question needs a Part on an
// Output in the droid's mapping, which is configuration, not a wire. Pressing
// STOP latches and releases the real estop; that is the point. POST /api/estop
// is idempotent (src/failsafe_gate.cpp), and a release on the bench board lets
// nothing move, because nothing is connected.
// Sleep is entered and left again; if the wake press fails the script says
// so and the droid is left asleep for the operator to wake on the Dashboard.
//
// RUN (operator watching):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/shell/stop-every-surface.js
//   BASE_URL=http://<board>   the controller (default http://10.0.0.22)
//   STEP=1                    wait for Enter after each surface
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:4173 HEADLESS=true against
// python3 tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js,
// its 'bench' droid).
// Self-tests, each must FAIL the rule it names:
//   SELFTEST_COVER=1     lays a transparent sheet over STOP on every surface of
//                        the walk (5), and every row of that table must FAIL;
//   SELFTEST_UNLATCH=1   (FIXTURE=1 only) the fixture's droid answers "not
//                        latched" after the second navigation of the latch
//                        walk (6), which must FAIL from there;
//   SELFTEST_NONOTICE=1  marks the pad key busy (.is-pending) before the
//                        press, which the shell deliberately does not report:
//                        rule 1 must FAIL;
//   SELFTEST_OLDSTACK=1  puts back the stacking and recovery rules the two
//                        fixes removed (the chrome unranked, the work area no
//                        stacking context, the recovery dim on every body
//                        child): rules 2 and 4 must FAIL;
//   SELFTEST_NOTAB=1     takes STOP out of the tab order: rule 3 must FAIL;
//   SELFTEST_FRESHCLEAR=1 (FIXTURE=1 only) the fixture's droid answers "not
//                        latched" before the fresh browser opens: rule 7
//                        must FAIL.
const assert = require('node:assert/strict');
const { mkdirSync } = require('node:fs');
const lib = require('../_lib/checks.js');

const { BASE_URL, FIXTURE, SETTLE_MS } = lib;
const SELFTEST_COVER = process.env.SELFTEST_COVER === '1';
const SELFTEST_UNLATCH = process.env.SELFTEST_UNLATCH === '1';
const SELFTEST_NONOTICE = process.env.SELFTEST_NONOTICE === '1';
const SELFTEST_OLDSTACK = process.env.SELFTEST_OLDSTACK === '1';
const SELFTEST_NOTAB = process.env.SELFTEST_NOTAB === '1';
const SELFTEST_FRESHCLEAR = process.env.SELFTEST_FRESHCLEAR === '1';
if ((SELFTEST_UNLATCH || SELFTEST_FRESHCLEAR) && !FIXTURE) {
  // They change what the droid answers, which only the fixture can do.
  console.error('SELFTEST_UNLATCH and SELFTEST_FRESHCLEAR need FIXTURE=1');
  process.exit(2);
}
const ARTIFACT_DIR = 'output/playwright/issue-359';

const STOP = '#shell-estop-button';
const CHIP = '#chip-estop';
const LATCHED = 'Estop: latched';
const CLEAR = 'Estop: clear';
// The refused control rule 1 presses.
const REFUSED = '.pad-fwd';

// The rules 11a30d27 and aa63f311 removed, put back for SELFTEST_OLDSTACK: the
// chrome unranked, the work area no longer a stacking context (so its overlays
// and the recovery backdrop rank against the chrome again), and the old
// kernel's dim-and-block over every body child while recovery is up.
const OLD_STACK_CSS = `
  #shell-top, #shell-status, .status-plate-region, .shell-estop { z-index: auto !important; }
  #shell-content { position: static !important; z-index: auto !important; }
  body.recovery-active > *:not(#page-recovery-backdrop) { opacity: 0.4 !important; pointer-events: none !important; }
`;

// The surfaces with a dialog, and how each is opened and closed.
const DIALOGS = {
  seq: {
    name: 'import',
    open: async (page) => {
      await page.locator('#seq-btn-import').waitFor({ state: 'visible', timeout: 10000 });
      // seq.js binds its listeners after its markup lands; a press before
      // that does nothing, so press again until the dialog answers.
      for (let attempt = 0; attempt < 5; attempt += 1) {
        await page.click('#seq-btn-import');
        const shown = await page
          .waitForSelector('#seq-modal-import:not(.hidden)', { timeout: 1500 })
          .then(() => true, () => false);
        if (shown) return;
      }
      throw new Error('the Restore backup dialog never opened');
    },
    close: async (page) => {
      const cancel = page.locator('#seq-modal-import-cancel');
      if (await cancel.isVisible()) await cancel.click({ timeout: 3000 });
    },
  },
  wiring: moveQuestion('#wiring-move-dialog', async (page) => {
    // A Part row whose bar shows an Output, and another Output to press.
    const pick = await page.evaluate(() => {
      for (const row of document.querySelectorAll('#wiring-parts-table tr.is-wired[data-part]')) {
        const bar = [...row.querySelectorAll('[data-bar] button')];
        const other = bar.find((button) => !button.classList.contains('active') && !button.disabled);
        if (bar.some((button) => button.classList.contains('active')) && other) return { part: row.dataset.part, to: other.dataset.value };
      }
      return null;
    });
    if (!pick) return null;
    await page.click(`#wiring-parts-table tr[data-part="${pick.part}"] [data-bar] button[data-value="${pick.to}"]`, { timeout: 5000 });
    return `${pick.part} to ${pick.to}`;
  }),
  servo: moveQuestion('#outputs-move-dialog', async (page) => {
    // A Part on one Output, pressed in another Output's "+ part" pills.
    const pick = await page.evaluate(() => {
      const outputs = window.PAOutputs.list();
      const from = outputs.find((output) => output.parts.length > 0);
      if (!from) return null;
      const to = outputs.find((output) => output !== from && document.querySelector(`#outputs-table [data-output="${output.address}"] .outputs-add-open`));
      return to ? { part: from.parts[0], to: to.address } : null;
    });
    if (!pick) return null;
    const row = `#outputs-table [data-output="${pick.to}"]`;
    await page.click(`${row} .outputs-add-open`, { timeout: 5000 });
    await page.click(`${row} .outputs-add [data-part="${pick.part}"]`, { timeout: 5000 });
    return `${pick.part} to ${pick.to}`;
  }),
};

// The move question, opened by the page (header). `choose` makes the builder's
// pick and says what it asked for, or null when no Part is on an Output. open()
// returns null once the dialog is up, or the reason the row is NOT ASSESSED.
function moveQuestion(selector, choose) {
  return {
    name: 'move question',
    open: async (page) => {
      await page.waitForSelector(selector, { state: 'attached', timeout: 10000 });
      await page.waitForFunction(() => window.PAOutputs?.known().table, null, { timeout: 20000 });
      const asked = await choose(page);
      if (!asked) return 'no Part is on an Output, so there is no move for the page to ask about';
      const shown = await page
        .waitForFunction((sel) => document.querySelector(sel)?.open === true, selector, { timeout: 5000 })
        .then(() => true, () => false);
      assert.equal(shown, true, `the page did not open the move question for ${asked}`);
      return null;
    },
    close: async (page) => {
      const open = () => page.evaluate((sel) => document.querySelector(sel)?.open === true, selector);
      if (!(await open())) return;
      await page.locator(`${selector} .move-cancel`).click({ timeout: 3000 });
      assert.equal(await open(), false, 'Cancel did not close the move question');
    },
  };
}

// Opens a surface through the nav, as an operator does, and waits for it to
// mount and settle. A surface in two Activity Groups has two links to the one
// route; either will do. The link to the surface already on screen takes no
// press by design (data/style.css, nav a.active), so the one a walk starts on
// is not pressed. A surface with no nav entry fails, and is still reached by
// its address so the walk covers it too.
const openSurface = async (page, surface, fail) => {
  const link = page.locator(`#shell-nav [data-surface-link="${surface}"]`).first();
  const here = await page.evaluate((target) => document.body.dataset.page === target, surface);
  const inNav = (await link.count()) > 0;
  if (!inNav) fail('no nav entry');
  if (here) {
    // Already there.
  } else if (inNav) {
    await link.click();
  } else {
    await page.evaluate((target) => {
      window.location.hash = target;
    }, surface);
  }
  await page.waitForSelector(`body[data-page="${surface}"]`, { timeout: 10000 });
  await page.waitForFunction(
    (target) => {
      const node = document.querySelector(`#shell-content > .surface[data-surface="${target}"]`);
      return Boolean(node && node.childElementCount > 0);
    },
    surface,
    { timeout: 20000 },
  );
  await page.waitForTimeout(SETTLE_MS);
};

// What the pointer would land on at a control's centre, and the checks a
// press depends on.
const readControl = (page, selector) =>
  page.evaluate((sel) => {
    const button = document.querySelector(sel);
    if (!button) return { present: false };
    const box = button.getBoundingClientRect();
    const x = box.left + box.width / 2;
    const y = box.top + box.height / 2;
    const hit = document.elementFromPoint(x, y);
    const describe = (el) =>
      el ? `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ''}${el.classList.length ? `.${[...el.classList].join('.')}` : ''}` : 'nothing';
    return {
      present: true,
      pointerEvents: getComputedStyle(button).pointerEvents,
      topmost: hit === button || button.contains(hit),
      hit: describe(hit),
      inertAncestor: Boolean(button.closest('[inert]')),
    };
  }, selector);
const readStop = (page) => readControl(page, STOP);

// The path a response answered, without its query.
const pathOf = (response) => new URL(response.url()).pathname;
const isEstopPost = (response) => response.request().method() === 'POST' && pathOf(response).startsWith('/api/estop');

// How many stops and releases the droid answered in this run.
let stopsAnswered = 0;
let releasesAnswered = 0;

// Whether STOP is lit: the droid reported a latch (data/shell.js
// renderEstopState).
const stopLit = (page) =>
  page.evaluate((sel) => {
    const button = document.querySelector(sel);
    return Boolean(button && button.classList.contains('is-latched') && button.getAttribute('aria-pressed') === 'true');
  }, STOP);

// A press of STOP, or of the ESTOP chip (`selector`) - by pointer, or by `key`
// on the focused control - held to the direction STOP showed before it
// (header): the two are one toggle on one latch. Waits for the press's
// own confirmation read (GET /api/status) as well, because a press that
// follows a stop before that read is back is deliberately another stop
// (data/shell.js pressEstop), so a quicker next press would test the guard
// rather than the toggle. Returns { reasons, did } - `did` is "stop" or
// "release", the direction it was held to.
const pressToggles = async (page, { key = null, selector = STOP } = {}) => {
  const reasons = [];
  const lit = await stopLit(page);
  const want = lit
    ? { did: 'release', path: '/api/estop/clear', first: 'Releasing the estop...', said: 'Estop released', line: CLEAR, lit: false }
    : { did: 'stop', path: '/api/estop', first: 'Stopping the droid...', said: 'Stop sent', line: LATCHED, lit: true };
  await page.evaluate(() => {
    window.__stopFeedback = [];
  });
  const posted = page.waitForResponse(isEstopPost, { timeout: 8000 }).catch(() => null);
  const confirmed = page
    .waitForResponse((response) => response.request().method() === 'GET' && pathOf(response) === '/api/status', { timeout: 8000 })
    .catch(() => null);
  try {
    if (key) await page.keyboard.press(key);
    else await page.locator(selector).click({ timeout: 4000 });
  } catch (error) {
    reasons.push(`the press did not land: ${String(error.message).split('\n')[0]}`);
    return { reasons, did: want.did };
  }
  const response = await posted;
  if (!response) reasons.push(`no POST ${want.path} followed the press`);
  else if (pathOf(response) !== want.path) reasons.push(`STOP was ${lit ? 'lit' : 'unlit'}, so the press should send POST ${want.path}; it sent POST ${pathOf(response)}`);
  else if (!response.ok()) reasons.push(`POST ${want.path} answered ${response.status()}`);
  else if (want.did === 'stop') stopsAnswered += 1;
  else releasesAnswered += 1;
  const said = await page
    .waitForFunction((text) => window.__stopFeedback.includes(text), want.said, { timeout: 8000 })
    .then(() => true, () => false);
  const trail = await page.evaluate(() => window.__stopFeedback.slice());
  if (!said) reasons.push(`feedback never said "${want.said}": ${JSON.stringify(trail)}`);
  else if (trail[0] !== want.first) reasons.push(`feedback did not start at "${want.first}": ${JSON.stringify(trail)}`);
  const reached = await page
    .waitForFunction((text) => document.getElementById('shell-estop-state').textContent === text, want.line, { timeout: 10000 })
    .then(() => true, () => false);
  if (!reached) reasons.push(`state line never read "${want.line}" (it says "${await page.textContent('#shell-estop-state')}")`);
  else if ((await stopLit(page)) !== want.lit) reasons.push(`the state line reads "${want.line}" but STOP is ${want.lit ? 'not lit' : 'still lit'}`);
  await confirmed;
  await page.waitForTimeout(250);
  return { reasons, did: want.did };
};

// STOP and the ESTOP chip, each topmost at its own centre and not inert.
const reachReasons = async (page) => {
  const reasons = [];
  for (const [label, selector] of [['STOP', STOP], ['the ESTOP chip', CHIP]]) {
    const probe = await readControl(page, selector);
    if (!probe.present) reasons.push(`${label} is not in the document`);
    else {
      if (probe.pointerEvents === 'none') reasons.push(`${label} has pointer-events: none`);
      if (probe.inertAncestor) reasons.push(`${label} sits inside an [inert] element`);
      if (!probe.topmost) reasons.push(`${label} is covered: the pointer lands on ${probe.hit}`);
    }
  }
  return reasons;
};

// PRECONDITION (header), read through the same routes the page uses (so
// FIXTURE=1 answers it too).
const precondition = lib.allOf(lib.estopMustBe(false), lib.sleepMustBe(false));

(async () => {
  mkdirSync(ARTIFACT_DIR, { recursive: true });
  const browser = await lib.launchBrowser();
  let fixture = null;
  const rows = [];
  const latchWalk = { precondition: null, rows: [] };
  // Checks 1-4 and 7, one row each.
  const report = lib.createReport('Notice, sleep, keyboard, recovery, fresh browser');
  const check = (id, name, reasons, detail = '') => {
    const ok = reasons.length === 0;
    report.add(id, name, lib.verdict(ok), ok ? detail : reasons.join('; '));
  };
  // What the state line says as the run ends, for the closing line.
  let finalLine = null;
  let refused = null;
  let leftAsleep = false;
  try {
    const context = await browser.newContext({ viewport: lib.VIEWPORT });
    if (FIXTURE) fixture = await require('../_lib/fixture_routes.js').install(context, { droid: 'bench' });
    const page = await context.newPage();
    const pageErrors = [];
    page.on('pageerror', (error) => pageErrors.push(String(error).split('\n')[0]));
    if (SELFTEST_OLDSTACK) {
      await page.addInitScript((css) => {
        document.addEventListener('DOMContentLoaded', () => {
          const style = document.createElement('style');
          style.id = 'selftest-oldstack';
          style.textContent = css;
          document.head.appendChild(style);
        });
      }, OLD_STACK_CSS);
    }

    // The write guard (header). A page route, so it runs before the fixture's
    // context route and hands everything it allows on to it.
    let allowSleep = false;
    const writes = await lib.installGuard(
      page,
      (entry) =>
        (entry.method === 'POST' && (entry.path === '/api/estop' || entry.path === '/api/estop/clear')) ||
        lib.isRcDebugToggle(entry) ||
        lib.isConsoleCatalogLoad(entry) ||
        (allowSleep && entry.method === 'POST' && (entry.path === '/api/sleep' || entry.path === '/api/wake')),
    );
    const blockedBySurface = [];

    await page.goto(`${BASE_URL}/#home`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector(STOP, { timeout: 20000 });
    await page.waitForSelector('#shell-nav [data-surface-link]', { timeout: 20000 });

    // PRECONDITION (header): estop clear, not asleep.
    refused = await precondition({ page });
    if (refused) throw new Error(refused);

    // The surface list the droid is actually serving, read from its shell.js
    // rather than copied here, so a surface added later is walked too.
    // The shell's script as the droid serves it: inside its staged bundle on an
    // image that bundles (#461, tools/gzip_fsdata.py SCRIPT_BUNDLES), else /shell.js.
    const served = await page.evaluate(async () => {
      const chain = (document.documentElement.getAttribute('data-scripts') || '').split(',').map((name) => name.trim());
      const source = chain.includes('/bundle_shell.js') ? '/bundle_shell.js' : '/shell.js';
      return (await fetch(source, { cache: 'no-store' })).text();
    });
    const surfaceDocs = [...served.matchAll(/page:\s*"([^"]+)"\s*,\s*doc:\s*"([^"]+)"/g)].map((match) => ({ page: match[1], doc: match[2] }));
    const surfaces = surfaceDocs.map((entry) => entry.page);
    assert.ok(surfaces.length >= 10, `read only ${surfaces.length} surfaces out of the served shell.js`);
    console.log(`Surfaces in the served shell.js (${surfaces.length}): ${surfaces.join(', ')}`);
    // Which surfaces have been mounted, so check 4 can pick two that have not.
    const mounted = new Set(['home']);

    // The feedback line's every text, in order, so a press is seen to go from
    // "Stopping the droid..." to "Stop sent" rather than read once.
    await page.evaluate(() => {
      window.__stopFeedback = [];
      const line = document.getElementById('shell-estop-feedback');
      new MutationObserver(() => window.__stopFeedback.push(line.textContent)).observe(line, {
        childList: true,
        characterData: true,
        subtree: true,
      });
    });

    // -----------------------------------------------------------------------
    // 1. The Ignored Input Notice, on a real pointer press of a refused control
    // -----------------------------------------------------------------------
    {
      const reasons = [];
      let detail = '';
      try {
        await openSurface(page, 'drive', (reason) => reasons.push(reason));
        mounted.add('drive');
        // Foot Drive has painted a reading (plate live), so the notice has
        // a cause to name.
        await page.waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness === 'live', null, { timeout: 20000 });
        await page.waitForTimeout(SETTLE_MS);
        const key = await page.evaluate((sel) => {
          const button = document.querySelector(sel);
          return button ? { disabled: button.disabled, aria: button.getAttribute('aria-disabled') } : null;
        }, REFUSED);
        if (!key || !key.disabled || key.aria !== 'true') {
          reasons.push(`the forward pad key is not refused (${JSON.stringify(key)}), so there is nothing to press - switch web control off and run again`);
        } else {
          if (SELFTEST_NONOTICE) await page.evaluate((sel) => document.querySelector(sel).classList.add('is-pending'), REFUSED);
          const box = await page.locator(REFUSED).boundingBox();
          await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
          const shown = await page
            .waitForSelector('#ignored-input-notice:not(.hidden)', { state: 'visible', timeout: 3000 })
            .then(() => true, () => false);
          const notice = await page.evaluate(() => ({
            text: document.getElementById('ignored-input-text').textContent,
            route: document.getElementById('ignored-input-route').textContent,
            href: document.getElementById('ignored-input-route').getAttribute('href'),
          }));
          if (!shown) reasons.push('no notice appeared after the press');
          else {
            if (!/^That control is switched off right now\. .+\.$/.test(notice.text)) reasons.push(`notice reads "${notice.text}"`);
            if (!/^Open .+, where that is changed$/.test(notice.route)) reasons.push(`notice link reads "${notice.route}"`);
            await page.screenshot({ path: `${ARTIFACT_DIR}/ignored-input-notice.png` });
            const destination = String(notice.href || '').replace(/^#/, '');
            await page.click('#ignored-input-route');
            const arrived = await page
              .waitForSelector(`body[data-page="${destination}"]`, { timeout: 10000 })
              .then(() => true, () => false);
            if (!arrived) reasons.push(`the link to ${notice.href} did not open that surface (on "${await page.evaluate(() => document.body.dataset.page)}")`);
            else mounted.add(destination);
            detail = `"${notice.text}" -> "${notice.route}" (${notice.href})`;
          }
        }
      } catch (error) {
        reasons.push(`could not run: ${String(error.message).split('\n')[0]}`);
      }
      check('1', 'ignored input notice on a refused press, and its route', reasons, detail);
    }

    // -----------------------------------------------------------------------
    // 2. Sleep overlay: STOP and the ESTOP chip stay on top and take a press
    // -----------------------------------------------------------------------
    {
      const reasons = [];
      try {
        await openSurface(page, 'home', (reason) => reasons.push(reason));
        allowSleep = true;
        const sleepPost = page
          .waitForResponse((response) => response.url().includes('/api/sleep') && response.request().method() === 'POST', { timeout: 8000 })
          .catch(() => null);
        await page.click('#sleep-toggle', { timeout: 5000 });
        const slept = await sleepPost;
        if (!slept || !slept.ok()) throw new Error(`POST /api/sleep ${slept ? `answered ${slept.status()}` : 'was not sent'}`);
        const overlayUp = await page
          .waitForSelector('#sleep-overlay.active', { timeout: 10000 })
          .then(() => true, () => false);
        if (!overlayUp) throw new Error('the sleep overlay never came up');
        leftAsleep = true;
        // Past the overlay's 0.2 s fade, so it is at full strength.
        await page.waitForTimeout(600);
        reasons.push(...(await reachReasons(page)));
        await page.screenshot({ path: `${ARTIFACT_DIR}/sleep-overlay.png` });
        const stopPress = await pressToggles(page);
        if (stopPress.did !== 'stop') reasons.push('STOP was lit on a droid the precondition read as clear');
        reasons.push(...stopPress.reasons.map((reason) => `STOP: ${reason}`));
        const chipPress = await pressToggles(page, { selector: CHIP });
        if (chipPress.did !== 'release') reasons.push('STOP was not lit after its own stop, so the chip could not be held to a release');
        reasons.push(...chipPress.reasons.map((reason) => `ESTOP chip: ${reason}`));
        // Wake, through the overlay's own button.
        const wakePost = page
          .waitForResponse((response) => response.url().includes('/api/wake') && response.request().method() === 'POST', { timeout: 8000 })
          .catch(() => null);
        await page.click('#sleep-overlay-wake', { timeout: 5000 });
        const woke = await wakePost;
        if (!woke || !woke.ok()) reasons.push(`POST /api/wake ${woke ? `answered ${woke.status()}` : 'was not sent'}`);
        const down = await page
          .waitForSelector('#sleep-overlay:not(.active)', { state: 'attached', timeout: 10000 })
          .then(() => true, () => false);
        if (!down) reasons.push('the sleep overlay did not come down after Wake Droid');
        else leftAsleep = false;
      } catch (error) {
        reasons.push(`could not run: ${String(error.message).split('\n')[0]}`);
      } finally {
        // A run that stopped half way still wakes the droid it put to sleep.
        if (leftAsleep) {
          await page.click('#sleep-overlay-wake', { timeout: 3000 }).catch(() => {});
          leftAsleep = !(await page
            .waitForSelector('#sleep-overlay:not(.active)', { state: 'attached', timeout: 8000 })
            .then(() => true, () => false));
        }
        allowSleep = false;
      }
      check('2', 'sleep overlay: STOP and ESTOP chip topmost, STOP latches, the chip releases', reasons);
    }

    // -----------------------------------------------------------------------
    // 3. Keyboard reach: Tab from the Sequences dialog to STOP, Enter, Space
    // -----------------------------------------------------------------------
    {
      const reasons = [];
      let detail = '';
      try {
        await openSurface(page, 'seq', (reason) => reasons.push(reason));
        mounted.add('seq');
        await DIALOGS.seq.open(page);
        if (SELFTEST_NOTAB) await page.evaluate((sel) => document.querySelector(sel).setAttribute('tabindex', '-1'), STOP);
        const focused = () =>
          page.evaluate(() => {
            const el = document.activeElement;
            return {
              stop: el && el.id === 'shell-estop-button',
              inDialog: Boolean(el && el.closest && el.closest('#seq-modal-import')),
              name: el ? `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ''}` : 'nothing',
            };
          });
        const walk = async (key) => {
          // The dialog's first control a builder can actually focus: a hidden
          // file input or a disabled button takes no focus, and the walk would
          // then start outside the dialog.
          await page.evaluate(() => {
            const first = [...document.querySelectorAll('#seq-modal-import :is(button, input, textarea, select, a[href], [tabindex]:not([tabindex="-1"]))')]
              .find((el) => !el.disabled && el.getClientRects().length > 0);
            if (first) first.focus();
          });
          const start = await focused();
          if (!start.inDialog) return { start: start.name, presses: null };
          for (let presses = 1; presses <= 60; presses += 1) {
            await page.keyboard.press(key);
            if ((await focused()).stop) return { start: start.name, presses };
          }
          return { start: start.name, presses: null };
        };
        const back = await walk('Shift+Tab');
        const forward = await walk('Tab');
        detail = `Tab: ${forward.presses ?? 'never'} presses from ${forward.start}; Shift+Tab: ${back.presses ?? 'never'}`;
        const stillOpen = await page.evaluate(() => !document.getElementById('seq-modal-import').classList.contains('hidden'));
        if (!stillOpen) reasons.push('the dialog closed on the way');
        if (forward.presses === null) reasons.push(`Tab from the dialog (${forward.start}) never reached STOP in 60 presses`);
        else {
          // Focus is on STOP, the dialog still up behind it.
          const enter = await pressToggles(page, { key: 'Enter' });
          reasons.push(...enter.reasons.map((reason) => `Enter (${enter.did}): ${reason}`));
          if (!(await focused()).stop) {
            await walk('Tab');
          }
          const space = await pressToggles(page, { key: 'Space' });
          reasons.push(...space.reasons.map((reason) => `Space (${space.did}): ${reason}`));
          detail += `; Enter ${enter.did}, Space ${space.did}`;
        }
        await page.screenshot({ path: `${ARTIFACT_DIR}/keyboard-stop-seq.png` });
      } catch (error) {
        reasons.push(`could not run: ${String(error.message).split('\n')[0]}`);
      } finally {
        await page.evaluate((sel) => document.querySelector(sel)?.removeAttribute('tabindex'), STOP).catch(() => {});
        await DIALOGS.seq.close(page).catch((error) => reasons.push(`dialog did not close: ${error.message}`));
      }
      check('3', 'keyboard: Tab from the Sequences dialog reaches STOP; Enter and Space each toggle it', reasons, detail);
    }

    // -----------------------------------------------------------------------
    // 4. Page Recovery View: a first surface load, and a fault
    // -----------------------------------------------------------------------
    const unmounted = surfaceDocs.filter((entry) => !mounted.has(entry.page));
    const recoveryCases = [
      { mode: 'first load', entry: unmounted[0] },
      { mode: 'fault', entry: unmounted[1] },
    ];
    for (const { mode, entry } of recoveryCases) {
      const reasons = [];
      let detail = '';
      if (!entry) {
        check('4', `recovery view (${mode}): STOP topmost and takes a press`, ['no unmounted surface left to load']);
        continue;
      }
      const pattern = `**${entry.doc}`;
      let release = () => {};
      const held = new Promise((resolve) => {
        release = resolve;
      });
      const route =
        mode === 'first load'
          ? async (r) => {
              await held;
              return r.fallback();
            }
          : (r) => r.abort('internetdisconnected');
      try {
        await openSurface(page, 'home', (reason) => reasons.push(reason));
        await page.route(pattern, route);
        await page.locator(`#shell-nav [data-surface-link="${entry.page}"]`).first().click();
        const up = await page
          .waitForFunction(
            (loading) => {
              const backdrop = document.getElementById('page-recovery-backdrop');
              if (!document.body.classList.contains('recovery-active') || !backdrop) return false;
              const says = backdrop.textContent.includes('Loading page resources');
              return loading ? says : !says;
            },
            mode === 'first load',
            { timeout: 20000 },
          )
          .then(() => true, () => false);
        const says = await page.evaluate(() => (document.getElementById('page-recovery-backdrop')?.textContent || '').slice(0, 60));
        detail = `${entry.doc}, the view reads "${says}"`;
        if (!up) reasons.push(`the recovery view never reached its ${mode} state (it reads "${says}")`);
        else {
          reasons.push(...(await reachReasons(page)));
          await page.screenshot({ path: `${ARTIFACT_DIR}/recovery-${mode.replace(' ', '-')}.png` });
          const press = await pressToggles(page);
          reasons.push(...press.reasons.map((reason) => `STOP (${press.did}): ${reason}`));
          detail += `; the press was a ${press.did}`;
          const stillUp = await page.evaluate(() => document.body.classList.contains('recovery-active'));
          if (!stillUp) reasons.push('recovery-active was gone by the time the press was read, so the press was not proved behind it');
        }
      } catch (error) {
        reasons.push(`could not run: ${String(error.message).split('\n')[0]}`);
      } finally {
        await page.unroute(pattern, route).catch(() => {});
        release();
        // Let the surface load for the walk: the held request goes through on
        // release; a refused one is asked again with Retry now.
        const retry = page.locator('#page-recovery-backdrop .btn.accent');
        if (mode === 'fault' && (await retry.count().catch(() => 0))) await retry.first().click({ timeout: 3000 }).catch(() => {});
        const cleared = await page
          .waitForFunction(() => !document.body.classList.contains('recovery-active'), null, { timeout: 30000 })
          .then(() => true, () => false);
        if (!cleared) reasons.push(`the view did not clear once ${entry.doc} was let through`);
        mounted.add(entry.page);
      }
      check('4', `recovery view (${mode}): STOP and ESTOP chip topmost, STOP press lands`, reasons, detail);
    }

    // -----------------------------------------------------------------------
    // 5. The walk
    // -----------------------------------------------------------------------
    for (const surface of surfaces) {
      const row = { surface, dialog: '-', reasons: [] };
      rows.push(row);
      const since = writes.length;
      const fail = (reason) => row.reasons.push(reason);
      try {
        await openSurface(page, surface, fail);

        const dialog = DIALOGS[surface];
        if (dialog) {
          row.dialog = dialog.name;
          row.notAssessed = (await dialog.open(page)) || null;
          if (row.notAssessed) row.dialog = 'not opened';
        }
        if (SELFTEST_COVER) {
          await page.evaluate(() => {
            const sheet = document.createElement('div');
            sheet.id = 'selftest-cover';
            sheet.style.cssText = 'position:fixed;inset:0;z-index:2147483647;background:transparent;';
            document.body.appendChild(sheet);
          });
        }

        const stop = page.locator(STOP);
        row.visible = await stop.isVisible();
        row.enabled = await stop.isEnabled();
        const probe = await readStop(page);
        row.pointerEvents = probe.pointerEvents;
        row.topmost = probe.topmost;
        if (!probe.present) fail('STOP is not in the document');
        if (!row.visible) fail('STOP is not visible');
        if (!row.enabled) fail('STOP is disabled');
        if (probe.pointerEvents === 'none') fail('STOP has pointer-events: none');
        if (probe.inertAncestor) fail('STOP sits inside an [inert] element');
        if (!probe.topmost) fail(`STOP is covered: the pointer lands on ${probe.hit}`);

        await page.screenshot({ path: `${ARTIFACT_DIR}/stop-${surface}.png` });

        // The press itself, in whichever direction STOP showed. A covered
        // button fails Playwright's own actionability check, which is the
        // operator's press not landing.
        const press = await pressToggles(page);
        row.did = press.did;
        row.pressed = !press.reasons.some((reason) => reason.startsWith('the press did not land'));
        row.landed = press.reasons.length === 0;
        press.reasons.forEach(fail);
      } catch (error) {
        fail(`could not run: ${String(error.message).split('\n')[0]}`);
      } finally {
        await page.evaluate(() => document.getElementById('selftest-cover')?.remove()).catch(() => {});
        if (DIALOGS[surface]) {
          await DIALOGS[surface].close(page).catch((error) => fail(`dialog did not close: ${error.message}`));
        }
      }
      const blocked = lib.blockedWrites(writes, since);
      if (blocked.length) blockedBySurface.push({ surface, writes: [...new Set(blocked)] });
      // A dialog that could not be opened leaves the row unproved, never PASS.
      row.result = row.reasons.length ? lib.FAIL : row.notAssessed ? lib.NOT_ASSESSED : lib.PASS;
      const why = row.reasons.length ? row.reasons.join('; ') : row.notAssessed || '';
      console.log(`${row.result} ${surface}${why ? ` - ${why}` : ''}`);
      await lib.step(`${surface}: ${row.result}.`);
    }

    // -----------------------------------------------------------------------
    // 6. The latch survives navigation
    //
    // Every surface again, pressing nothing. The estop is chrome the shell
    // renders once, and a navigation only changes #shell-content, so the state
    // line must read "Estop: latched" on every surface and at every moment in
    // between - an observer records each text the line takes during the walk,
    // so a flicker to "clear" between two checks fails too (ADR 0048, #359).
    // -----------------------------------------------------------------------
    // The walk alternates, so it may have ended on a release. Rule 6 needs a
    // latch, and a STOP press is how an operator makes one.
    if ((await page.textContent('#shell-estop-state')) !== LATCHED && (await stopLit(page)) === false) {
      const relatch = await pressToggles(page);
      if (relatch.reasons.length) console.log(`The STOP press that latches for the latch walk did not land: ${relatch.reasons.join('; ')}`);
    }
    const latchedAtStart = (await page.textContent('#shell-estop-state')) === LATCHED;
    if (!latchedAtStart) {
      latchWalk.precondition = `the state line reads "${await page.textContent('#shell-estop-state')}" before the walk, so the estop was never latched and the walk proves nothing`;
      console.log(`FAIL latch walk - ${latchWalk.precondition}`);
    } else {
      await page.evaluate(() => {
        window.__estopLine = [];
        const line = document.getElementById('shell-estop-state');
        new MutationObserver(() =>
          window.__estopLine.push({ page: document.body.dataset.page, text: line.textContent }),
        ).observe(line, { childList: true, characterData: true, subtree: true });
      });
      let presses = 0;
      page.on('request', (request) => {
        if (request.method() === 'POST' && request.url().includes('/api/estop')) presses += 1;
      });
      for (const [index, surface] of surfaces.entries()) {
        const row = { surface, reasons: [] };
        latchWalk.rows.push(row);
        const since = writes.length;
        const fail = (reason) => row.reasons.push(reason);
        try {
          const pressesBefore = presses;
          await openSurface(page, surface, fail);
          if (SELFTEST_UNLATCH && index === 1) {
            // The fixture's droid answers "not latched" after this navigation.
            fixture.state.estop = false;
            fixture.push();
            await page.waitForTimeout(500);
          }
          row.line = await page.textContent('#shell-estop-state');
          if (row.line !== LATCHED) fail(`state line reads "${row.line}"`);
          const seen = await page.evaluate(() => window.__estopLine.splice(0));
          const other = seen.filter((entry) => entry.text !== 'Estop: latched');
          if (other.length) fail(`state line left "latched" on the way: ${[...new Set(other.map((entry) => `"${entry.text}" on ${entry.page}`))].join(', ')}`);
          if (presses !== pressesBefore) fail('a POST /api/estop or /api/estop/clear was sent during this step of a walk that presses nothing');
        } catch (error) {
          fail(`could not run: ${String(error.message).split('\n')[0]}`);
        }
        const blocked = lib.blockedWrites(writes, since);
        if (blocked.length) blockedBySurface.push({ surface: `${surface} (latch walk)`, writes: [...new Set(blocked)] });
        row.result = row.reasons.length ? 'FAIL' : 'PASS';
        console.log(`${row.result} latch walk ${surface}${row.reasons.length ? ` - ${row.reasons.join('; ')}` : ''}`);
        await lib.step(`${surface}: latch ${row.result}.`);
      }
    }

    // -----------------------------------------------------------------------
    // 7. A fresh browser meets the latch
    // -----------------------------------------------------------------------
    {
      const reasons = [];
      let detail = '';
      const fresh = await browser.newContext({ viewport: lib.VIEWPORT });
      try {
        if (fixture) {
          await fixture.addContext(fresh);
          if (SELFTEST_FRESHCLEAR) fixture.state.estop = false;
        }
        const second = await fresh.newPage();
        // Nothing but reads leaves this one: GETs, and the Dashboard
        // console's read-only `operations`/`help` catalog load.
        const freshWrites = await lib.installGuard(second, lib.isConsoleCatalogLoad);
        // Every text the state line takes from the moment shell.js writes it.
        await second.addInitScript(() => {
          window.__estopTrail = [];
          const watch = () => {
            const line = document.getElementById('shell-estop-state');
            if (!line) return false;
            window.__estopTrail.push(line.textContent);
            new MutationObserver(() => window.__estopTrail.push(line.textContent)).observe(line, {
              childList: true,
              characterData: true,
              subtree: true,
            });
            return true;
          };
          const early = new MutationObserver(() => {
            if (watch()) early.disconnect();
          });
          document.addEventListener('DOMContentLoaded', () => {
            if (!watch()) early.observe(document.body, { childList: true, subtree: true });
          });
        });
        const opened = Date.now();
        await second.goto(`${BASE_URL}/#home`, { waitUntil: 'domcontentloaded' });
        const settled = await second
          .waitForFunction(() => {
            const line = document.getElementById('shell-estop-state');
            return Boolean(line && line.textContent !== 'Estop: ');
          }, null, { timeout: 20000 })
          .then(() => true, () => false);
        const line = await second.textContent('#shell-estop-state').catch(() => null);
        const readAfter = Date.now() - opened;
        if (!settled) reasons.push(`the state line was still "${line}" 20 s after opening`);
        else if (line !== LATCHED) reasons.push(`the state line reads "${line}"`);
        // STOP lit, twice: once the Dashboard has mounted and painted, and
        // again 3 s later, so a later repaint that puts it out is caught.
        const firstRead = await second
          .waitForFunction((sel) => {
            const button = document.querySelector(sel);
            return Boolean(button && button.classList.contains('is-latched') && button.getAttribute('aria-pressed') === 'true');
          }, STOP, { timeout: 15000 })
          .then(() => true, () => false);
        await second.waitForTimeout(3000);
        const secondRead = await stopLit(second);
        if (!firstRead) reasons.push('STOP was never lit');
        else if (!secondRead) reasons.push('STOP was lit, then went out again');
        const trail = await second.evaluate(() => window.__estopTrail.slice());
        if (trail.includes('Estop: clear')) reasons.push(`the state line read "Estop: clear" on the way: ${JSON.stringify(trail)}`);
        const freshBlocked = lib.blockedWrites(freshWrites);
        detail =
          `"${line}" after ${readAfter} ms; trail ${JSON.stringify(trail)}; STOP lit: ${firstRead && secondRead ? 'yes, both reads' : 'no'} (not pressed)` +
          (freshBlocked.length ? `; writes the guard stopped: ${[...new Set(freshBlocked)].join(' | ')}` : '');
        await second.screenshot({ path: `${ARTIFACT_DIR}/fresh-context-latched.png` });
      } catch (error) {
        reasons.push(`could not run: ${String(error.message).split('\n')[0]}`);
      } finally {
        await fresh.close();
      }
      check('7', 'a fresh browser reads the latch and shows STOP lit', reasons, detail);
    }

    // The droid is left clear (header): one more STOP press releases the
    // latch rule 6 walked with.
    if (await stopLit(page)) {
      const release = await pressToggles(page);
      if (release.reasons.length) console.log(`The closing STOP press did not release the latch: ${release.reasons.join('; ')}`);
    }
    finalLine = await page.textContent('#shell-estop-state');

    if (blockedBySurface.length) {
      console.log('\nWrites the guard stopped before they reached the droid (a surface writing on a visit, not a STOP failure):');
      blockedBySurface.forEach(({ surface, writes }) => console.log(`  ${surface}: ${writes.join(' | ')}`));
    }
    if (pageErrors.length) {
      console.log(`\nPage errors seen during the walk (reported, not failed on): ${pageErrors.length}`);
      [...new Set(pageErrors)].forEach((line) => console.log(`  ${line}`));
    }
  } catch (error) {
    if (!refused) {
      console.error('stop-every-surface could not complete:', error);
      process.exitCode = 1;
    }
  } finally {
    await lib.closeAll(browser, [fixture]);
  }
  if (refused) {
    lib.notAssessed(refused);
    return;
  }

  const yes = (value) => (value === undefined ? '-' : value ? 'yes' : 'NO');
  console.log('\nsurface        dialog          visible enabled ptr-events topmost pressed did     landed  result');
  console.log('-------------- --------------- ------- ------- ---------- ------- ------- ------- ------- ------');
  for (const row of rows) {
    console.log(
      [
        row.surface.padEnd(14),
        row.dialog.padEnd(15),
        yes(row.visible).padEnd(7),
        yes(row.enabled).padEnd(7),
        String(row.pointerEvents ?? '-').padEnd(10),
        yes(row.topmost).padEnd(7),
        yes(row.pressed).padEnd(7),
        String(row.did ?? '-').padEnd(7),
        yes(row.landed).padEnd(7),
        row.result,
      ].join(' '),
    );
  }
  const failed = rows.filter((row) => row.result === lib.FAIL);
  const unproved = rows.filter((row) => row.result === lib.NOT_ASSESSED);
  console.log(
    `\n=== STOP on every surface: ${rows.length - failed.length - unproved.length}/${rows.length} PASS` +
      `${unproved.length ? `, ${unproved.length} NOT ASSESSED (${unproved.map((row) => row.surface).join(', ')})` : ''} ===`,
  );

  console.log('\nLatch walk: every surface again, nothing pressed');
  console.log('surface        state line                 result');
  console.log('-------------- -------------------------- ------');
  if (latchWalk.precondition) console.log(`(not walked: ${latchWalk.precondition})`);
  for (const row of latchWalk.rows) {
    console.log([row.surface.padEnd(14), String(row.line ?? '-').padEnd(26), row.result].join(' '));
  }
  const latchFailed = latchWalk.rows.filter((row) => row.result !== 'PASS').length + (latchWalk.precondition ? 1 : 0);
  const latchTotal = latchWalk.rows.length + (latchWalk.precondition ? 1 : 0);
  console.log(`\n=== Latch survives navigation: ${latchTotal - latchFailed}/${latchTotal} PASS ===`);

  report.print();
  const checksFailed = report.rows.filter((row) => row.result !== lib.PASS).length;
  console.log(`Screenshots under ${ARTIFACT_DIR}`);
  if (leftAsleep) {
    console.log('\nThe droid was put in Sleep Mode and did NOT wake on "Wake Droid". Wake it on the Dashboard.');
  }
  console.log(`\nSTOP presses the droid answered: ${stopsAnswered} stops, ${releasesAnswered} releases.`);
  if (finalLine === CLEAR) {
    console.log('The droid is left with the estop CLEAR: the closing STOP press released it.');
  } else if (finalLine === LATCHED) {
    console.log('The droid is left with the estop LATCHED: the closing release did not land. STOP (lit) releases it.');
  } else if (stopsAnswered > 0) {
    console.log(`The estop was latched during this run, and the state line now reads "${finalLine ?? 'unknown'}". Check the droid: STOP is lit while it is latched, and a press releases it.`);
  }
  if (failed.length || rows.length === 0 || latchFailed || latchTotal === 0 || checksFailed || report.rows.length < 6) process.exitCode = 1;
})();
