// STOP can be pressed on every surface, dialog up or not (#359, ADR 0048).
// Bench day, #355 section E.
//
// WHAT IT PROVES. For every surface the served shell.js lists in SURFACES, it
// opens the surface through the nav the way an operator does, opens the
// surface's dialog where it has one, and then asks of the topbar's STOP
// (#shell-estop-button):
//   - it is visible and enabled;
//   - its computed pointer-events is not `none`;
//   - it is the topmost thing at its own centre: document.elementFromPoint()
//     there is the button or inside it. A dialog, a backdrop or an overlay
//     painted over the chrome fails here, and nowhere else;
//   - a real pointer press lands: the feedback line goes "Stopping the
//     droid..." then "Stop sent", and the state line reaches "Estop: latched".
// Then, with the estop latched, it walks every surface AGAIN pressing nothing:
// navigation must never clear a latch (ADR 0048, #359), so the state line must
// read "Estop: latched" on every surface, an observer fails any other text the
// line takes on the way, and no POST /api/estop may be sent. That walk has its
// own table and its own PASS/FAIL line.
//
// NOTHING BUT STOP REACHES THE DROID AS A WRITE. Some surfaces write on a plain
// visit - guided Setup, drawn over Configuration on a droid that is not set up,
// saves which questions it has shown (data/setup.js saveVisited, POST
// /api/config). A browser-side guard lets through only GETs, POST /api/estop,
// the Dashboard console's read-only `operations`/`help` and RC's verbose-log
// toggle (POST /api/rc/debug, runtime only), and aborts every other write
// before it leaves the browser. What it stopped is listed after the table; it
// is a fact about the surface, not a STOP failure.
//
// WHY A REAL BROWSER. The web suite's DOM (test/test_web/helpers/mini_dom.js)
// has no CSS engine, no layout, no hit testing and no pointer. A dialog that
// covers STOP, a `pointer-events: none` that wins its cascade, or a native
// modal <dialog> that makes the rest of the document inert are all invisible
// to it and all visible here - which is the #359 defect class.
//
// HOW EACH DIALOG IS OPENED.
//   seq     "Restore backup" is pressed, which opens its import dialog through
//           the surface's own showModal() helper (data/seq.js). Cancel closes
//           it; nothing is restored. The wipe dialog is the same helper and
//           needs a stored sequence, so it is not opened separately.
//   parts,  the move question is a native <dialog>, and the surface opens it
//   servo   only for a move that takes a Part off one Output onto another -
//           reaching it through the page means a Part already wired, and a move
//           that is NOT announced goes straight to POST /api/config. So the
//           script calls the same dialog.showModal() on the same element that
//           data/parts_mapping.js calls, and closes it with dialog.close().
//           No move is asked, so no answer can send one.
//
// WHAT IT DOES NOT DO. It never releases the estop: release is a deliberate
// operator act on Foot Drive or the Dashboard (ADR 0048), and the closing line
// says so. It writes no configuration, moves nothing and needs nothing wired
// but USB (Bench-Mode). Pressing STOP latches the real estop; that is the
// point, and POST /api/estop is idempotent (src/failsafe_gate.cpp). Once the
// first press has latched it, "reaches Estop: latched" on later surfaces is
// already true - the press on those is proved by the POST and its feedback.
// Keyboard reach (Tab to STOP behind a dialog) is not measured here.
//
// RUN (bench day, operator watching):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/shell/stop-every-surface.js
//   BASE_URL=http://<board>   the controller (default http://10.0.0.22)
//   STEP=1                    wait for Enter after each surface
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:4173 HEADLESS=true against
// python3 tools/serve_editor_fixture.py (routes in ./_fixture_routes.js).
// Self-tests: SELFTEST_COVER=1 lays a transparent sheet over STOP on every
// surface, and every row of the first table must FAIL; SELFTEST_UNLATCH=1
// (FIXTURE=1 only) has the fixture's droid answer "not latched" after the
// second navigation of the latch walk, and the latch walk must FAIL from there.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const { mkdirSync } = require('node:fs');
const readline = require('node:readline');

const BASE_URL = (process.env.BASE_URL || 'http://10.0.0.22').replace(/\/$/, '');
const HEADLESS = process.env.HEADLESS === 'true';
const STEP = process.env.STEP === '1';
const FIXTURE = process.env.FIXTURE === '1';
const SELFTEST_COVER = process.env.SELFTEST_COVER === '1';
const SELFTEST_UNLATCH = process.env.SELFTEST_UNLATCH === '1';
if (SELFTEST_UNLATCH && !FIXTURE) {
  // It changes what the droid answers, which only the fixture can do.
  console.error('SELFTEST_UNLATCH needs FIXTURE=1');
  process.exit(2);
}
const SETTLE_MS = Number(process.env.SETTLE_MS || 1500);
const ARTIFACT_DIR = 'output/playwright/issue-359';

const STOP = '#shell-estop-button';

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
  parts: nativeDialog('#parts-move-dialog'),
  servo: nativeDialog('#outputs-move-dialog'),
};

function nativeDialog(selector) {
  return {
    name: 'move question',
    open: async (page) => {
      await page.waitForSelector(selector, { state: 'attached', timeout: 10000 });
      await page.evaluate((sel) => {
        const dialog = document.querySelector(sel);
        dialog.querySelector('.move-title').textContent = 'Bench check';
        dialog.querySelector('.move-body').textContent = 'Opened by stop-every-surface.js. Nothing is being moved.';
        dialog.showModal();
      }, selector);
      assert.equal(await page.evaluate((sel) => document.querySelector(sel).open, selector), true, 'dialog did not open');
    },
    close: async (page) => {
      await page.evaluate((sel) => {
        const dialog = document.querySelector(sel);
        if (dialog && dialog.open) dialog.close();
      }, selector);
    },
  };
}

const waitForEnter = (prompt) =>
  new Promise((resolve) => {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
    rl.question(prompt, () => {
      rl.close();
      resolve();
    });
  });

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

// What the pointer would land on at STOP's centre, and the checks a press
// depends on.
const readStop = (page) =>
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
  }, STOP);

(async () => {
  mkdirSync(ARTIFACT_DIR, { recursive: true });
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  let fixture = null;
  const rows = [];
  const latchWalk = { precondition: null, rows: [] };
  // What the state line says as the run ends, for the closing line.
  let finalLine = null;
  try {
    const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
    if (FIXTURE) fixture = await require('./_fixture_routes.js').install(context);
    const page = await context.newPage();
    const pageErrors = [];
    page.on('pageerror', (error) => pageErrors.push(String(error).split('\n')[0]));

    // The write guard (header). A page route, so it runs before the fixture's
    // context route and hands everything it allows on to it.
    let blocked = [];
    await page.route('**/*', async (route) => {
      const request = route.request();
      if (request.method() === 'GET' || request.method() === 'HEAD') return route.fallback();
      const url = request.url();
      const path = url.slice(url.indexOf('/', url.indexOf('//') + 2)).split('?')[0];
      const body = request.postData() || '';
      const allowed =
        (request.method() === 'POST' && path === '/api/estop') ||
        (request.method() === 'POST' && path === '/api/rc/debug') ||
        (request.method() === 'POST' && path === '/api/console' && /^command=(operations|help)\b/.test(body));
      if (allowed) return route.fallback();
      blocked.push(`${request.method()} ${path} ${body.slice(0, 60)}`.trim());
      return route.abort('blockedbyclient');
    });
    const blockedBySurface = [];

    await page.goto(`${BASE_URL}/#home`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector(STOP, { timeout: 20000 });
    await page.waitForSelector('#shell-nav [data-surface-link]', { timeout: 20000 });

    // The surface list the droid is actually serving, read from its shell.js
    // rather than copied here, so a surface added later is walked too.
    const served = await page.evaluate(async () => (await fetch('/shell.js', { cache: 'no-store' })).text());
    const surfaces = [...served.matchAll(/page:\s*"([^"]+)"\s*,\s*doc:\s*"/g)].map((match) => match[1]);
    assert.ok(surfaces.length >= 10, `read only ${surfaces.length} surfaces out of the served shell.js`);
    console.log(`Surfaces in the served shell.js (${surfaces.length}): ${surfaces.join(', ')}`);

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

    for (const surface of surfaces) {
      const row = { surface, dialog: '-', reasons: [] };
      rows.push(row);
      blocked = [];
      const fail = (reason) => row.reasons.push(reason);
      try {
        await openSurface(page, surface, fail);

        const dialog = DIALOGS[surface];
        if (dialog) {
          row.dialog = dialog.name;
          await dialog.open(page);
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

        // The press itself. A covered button fails Playwright's own
        // actionability check, which is the operator's press not landing.
        await page.evaluate(() => {
          window.__stopFeedback = [];
        });
        const posted = page
          .waitForResponse((response) => response.url().includes('/api/estop') && response.request().method() === 'POST', {
            timeout: 8000,
          })
          .catch(() => null);
        try {
          await stop.click({ timeout: 4000 });
          row.pressed = true;
        } catch (error) {
          row.pressed = false;
          fail(`the press did not land: ${String(error.message).split('\n')[0]}`);
        }
        if (row.pressed) {
          const response = await posted;
          if (!response) fail('no POST /api/estop followed the press');
          else if (!response.ok()) fail(`POST /api/estop answered ${response.status()}`);
          const sent = await page
            .waitForFunction(() => window.__stopFeedback.includes('Stop sent'), null, { timeout: 8000 })
            .then(() => true, () => false);
          const trail = await page.evaluate(() => window.__stopFeedback.slice());
          row.stopSent = sent;
          if (!sent) fail(`feedback never said "Stop sent": ${JSON.stringify(trail)}`);
          else if (trail[0] !== 'Stopping the droid...') fail(`feedback did not start at "Stopping the droid...": ${JSON.stringify(trail)}`);
          row.latched = await page
            .waitForFunction(() => document.getElementById('shell-estop-state').textContent === 'Estop: latched', null, {
              timeout: 10000,
            })
            .then(() => true, () => false);
          if (!row.latched) {
            const said = await page.textContent('#shell-estop-state');
            fail(`state line never read "Estop: latched" (it says "${said}")`);
          }
        }
      } catch (error) {
        fail(`could not run: ${String(error.message).split('\n')[0]}`);
      } finally {
        await page.evaluate(() => document.getElementById('selftest-cover')?.remove()).catch(() => {});
        if (DIALOGS[surface]) {
          await DIALOGS[surface].close(page).catch((error) => fail(`dialog did not close: ${error.message}`));
        }
      }
      if (blocked.length) blockedBySurface.push({ surface, writes: [...new Set(blocked)] });
      row.result = row.reasons.length ? 'FAIL' : 'PASS';
      console.log(`${row.result} ${surface}${row.reasons.length ? ` - ${row.reasons.join('; ')}` : ''}`);
      if (STEP) await waitForEnter(`${surface}: ${row.result}. Enter for the next surface... `);
    }

    // -----------------------------------------------------------------------
    // Walk 2: the latch survives navigation
    //
    // Every surface again, pressing nothing. The estop is chrome the shell
    // renders once, and a navigation only changes #shell-content, so the state
    // line must read "Estop: latched" on every surface and at every moment in
    // between - an observer records each text the line takes during the walk,
    // so a flicker to "clear" between two checks fails too (ADR 0048, #359).
    // -----------------------------------------------------------------------
    const latchedAtStart = (await page.textContent('#shell-estop-state')) === 'Estop: latched';
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
        blocked = [];
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
          if (row.line !== 'Estop: latched') fail(`state line reads "${row.line}"`);
          const seen = await page.evaluate(() => window.__estopLine.splice(0));
          const other = seen.filter((entry) => entry.text !== 'Estop: latched');
          if (other.length) fail(`state line left "latched" on the way: ${[...new Set(other.map((entry) => `"${entry.text}" on ${entry.page}`))].join(', ')}`);
          if (presses !== pressesBefore) fail('a POST /api/estop was sent during this step of a walk that presses nothing');
        } catch (error) {
          fail(`could not run: ${String(error.message).split('\n')[0]}`);
        }
        if (blocked.length) blockedBySurface.push({ surface: `${surface} (latch walk)`, writes: [...new Set(blocked)] });
        row.result = row.reasons.length ? 'FAIL' : 'PASS';
        console.log(`${row.result} latch walk ${surface}${row.reasons.length ? ` - ${row.reasons.join('; ')}` : ''}`);
        if (STEP) await waitForEnter(`${surface}: latch ${row.result}. Enter for the next surface... `);
      }
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
    console.error('stop-every-surface could not complete:', error);
    process.exitCode = 1;
  } finally {
    await browser.close();
    if (fixture) await fixture.close();
  }

  const yes = (value) => (value === undefined ? '-' : value ? 'yes' : 'NO');
  console.log('\nsurface        dialog          visible enabled ptr-events topmost pressed stop-sent latched result');
  console.log('-------------- --------------- ------- ------- ---------- ------- ------- --------- ------- ------');
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
        yes(row.stopSent).padEnd(9),
        yes(row.latched).padEnd(7),
        row.result,
      ].join(' '),
    );
  }
  const failed = rows.filter((row) => row.result !== 'PASS');
  console.log(`\n=== STOP on every surface: ${rows.length - failed.length}/${rows.length} PASS ===`);

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
  console.log(`Screenshots under ${ARTIFACT_DIR}`);
  if (finalLine === 'Estop: latched') {
    console.log('\nThe estop is LATCHED on the droid. This script does not release it: release it on Foot Drive or the Dashboard when you are ready.');
  } else if (rows.some((row) => row.latched)) {
    console.log(`\nThe estop was latched during this run, and the state line now reads "${finalLine ?? 'unknown'}". This script released nothing: check the droid on Foot Drive or the Dashboard.`);
  }
  if (failed.length || rows.length === 0 || latchFailed || latchTotal === 0) process.exitCode = 1;
})();
