// bench-auto: droid
// The Parts surface's standing rules, read in a real browser. History: #347
// built Parts and shipped no script; this is it.
//
// THE RULES IT HOLDS - the ones that need a browser, checked without writing
// anything. The rule is the quoted line; the ticket is history, in brackets.
// The part-first picker's own rules (one row per Part, "- not wired -", light
// rows) moved to Wiring with the picker (operator, 2026-09-28 on #411):
// test/playwright/wiring/every-part-has-a-picker-row.js.
//   - "Unused lists every Part this image moves that no Output claims, and no
//     other" (history: #411, moved here from Wiring): the list's rows are the
//     catalog Parts whose control path is neither the dome link nor none, less
//     every Part a row of GET /api/servo/outputs carries - against the droid's
//     own answer - each row offers its Output there (history: #463, operator
//     2026-10-04: "Also pick it on Parts"): the Output bar on a body Part's
//     row, and on a dome Part's (PAParts.isDomePart()) the one sentence that
//     says the Dome Controller moves it (PAParts.domeMovesText()) - and the
//     summary counts the rows.
//   - "No row is hidden, in any state" (history: #347): every Unused row is
//     rendered visible (Element.checkVisibility with opacity and visibility,
//     and a box with height) once the Outputs have answered, while the
//     Outputs read is failing, and with a Part picked on the droid picture.
//   - "Per-frame updates touch only values; no full re-render occurs while a
//     control is under the pointer" (history: #347): the pointer rests on a
//     droid-picture marker, on the picture panel's Output bar (an act button
//     where the pick has no bar) and on an Unused row's Output bar in turn,
//     while at least three once-a-second Outputs
//     reads land; each hovered element must still be the same node, still
//     connected, still under the pointer, and no marker, act button or Unused
//     row may be removed from the document meanwhile. The Outputs read is
//     Parts' per-frame feed (data/outputs.js follow, once a second). Status
//     frames are counted and printed too, but an idle Bench-Mode droid pushes
//     one only when something changes (data/shell.js,
//     requestStatusBroadcastNow), so none arriving is not a failure.
//   - "Parts sends no write of its own on a visit" (history: #347).
//   - "A Part picked on the droid picture stays picked while the
//     once-a-second Outputs reads land" (history: #352): the marker keeps
//     .is-selected and aria-pressed="true", its row in the picture's Parts
//     list (where it has one) keeps .is-selected, and the panel keeps its
//     title, across at least FRAMES reads.
//
// WHY A REAL BROWSER. mini_dom (test/test_web/helpers/mini_dom.js) has no CSS
// engine, no layout and no pointer: "visible", "under the pointer" and a real
// :hover across real network frames only exist here.
//
// WHAT IT SKIPS, AND WHY.
//   - Open it / Close it, Drop from build / Add to build: each either moves an
//     Output (POST /api/servo, /api/dome/cmd) or writes the Droid Build (POST
//     /api/config). The act buttons are hovered, never pressed. Picking a
//     marker on the picture is pressed: it only selects (data/parts.js, "THE
//     VIEW NEVER WRITES"). An Output bar is hovered and never pressed: a press
//     is a move (POST /api/config), the same one Wiring's row sends.
//   - A fresh droid's Unused list: true only of a droid nobody has wired, and
//     the bench droid's mapping is whatever it holds. The script checks the
//     list against the droid's own answer instead, which holds on any droid.
//   - "A rename never produces a new id", "both targets build": not browser
//     questions.
//   - The estop-latched state: reaching it from here would be a press of STOP.
//     Run after stop-every-surface.js and that state is the one read.
// A browser-side guard aborts every write before it leaves the browser, so a
// slip in this script cannot reach the droid either.
//
// RUN (operator watching):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/parts/parts-surface.js
//   BASE_URL=http://<board>   the controller (default http://10.0.0.22)
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:4173 HEADLESS=true against
// python3 tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js,
// its 'bench' droid).
// Self-tests, each must FAIL its check: SELFTEST_REBUILD=1 swaps the hovered
// Unused bar button for a copy mid-frame; SELFTEST_HIDE=1 hides one Unused row once
// answered; SELFTEST_UNUSED=1 takes one row out of the Unused list;
// SELFTEST_DESELECT=1 picks the picked marker again (which lets it go) after
// the first Outputs read lands.
const { mkdirSync } = require('node:fs');
const lib = require('../_lib/checks.js');

const { BASE_URL, FIXTURE } = lib;
const SELFTEST_REBUILD = process.env.SELFTEST_REBUILD === '1';
const SELFTEST_HIDE = process.env.SELFTEST_HIDE === '1';
const SELFTEST_UNUSED = process.env.SELFTEST_UNUSED === '1';
const SELFTEST_DESELECT = process.env.SELFTEST_DESELECT === '1';
const HOLD_MS = Number(process.env.HOLD_MS || 2500);
const FRAMES = Number(process.env.FRAMES || 3);
const ARTIFACT_DIR = 'output/playwright/issue-347';

const ROWS = '#parts-unused [data-part]';

// Every row that is not rendered visible, by Part id.
const hiddenRows = (page) =>
  page.evaluate((selector) => {
    const rows = [...document.querySelectorAll(selector)];
    return {
      total: rows.length,
      hidden: rows
        .filter((row) => {
          const box = row.getBoundingClientRect();
          return !row.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true }) || box.height === 0;
        })
        .map((row) => row.dataset.part),
    };
  }, ROWS);

(async () => {
  mkdirSync(ARTIFACT_DIR, { recursive: true });
  const browser = await lib.launchBrowser();
  let fixture = null;
  const report = lib.createReport('Parts surface');
  // Rows are numbered in the order they are checked.
  const check = (name, ok, detail) => report.add(report.rows.length + 1, name, lib.verdict(ok), detail);
  try {
    const context = await browser.newContext({ viewport: lib.VIEWPORT });
    if (FIXTURE) fixture = await require('../_lib/fixture_routes.js').install(context, { droid: 'bench' });
    const page = await context.newPage();

    // The write guard: nothing but reads leaves this browser.
    const writes = await lib.installGuard(page);

    // The droid's own answer, each time it lands, so the rows are checked
    // against what the droid said rather than against a copy made here.
    let outputsFrames = 0;
    let lastOutputs = null;
    page.on('response', async (response) => {
      if (!response.url().includes('/api/servo/outputs') || !response.ok()) return;
      try {
        lastOutputs = await response.json();
        outputsFrames += 1;
      } catch (_error) {
        // A body cut short by navigation; the next frame replaces it.
      }
    });

    // Held from the first load until the waiting state has been read.
    // Browser-side only: the request reaches the droid late, nothing more.
    let release;
    const released = new Promise((resolve) => {
      release = resolve;
    });
    const hold = async (route) => {
      await released;
      return route.fallback();
    };
    await page.route('**/api/servo/outputs*', hold);

    await page.goto(`${BASE_URL}/#parts`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('#parts-unused-summary', { timeout: 30000 });
    // Long enough to be seen, and to be sure the page is not about to answer
    // from somewhere else.
    await page.waitForTimeout(HOLD_MS);

    // -----------------------------------------------------------------------
    // State 1: the Outputs are still being waited for
    // -----------------------------------------------------------------------
    const waiting = await page.evaluate(() => ({
      summary: document.getElementById('parts-unused-summary').textContent,
      dots: getComputedStyle(document.getElementById('parts-unused-summary'), '::after').content,
      answered: window.PAOutputs.known().table,
    }));
    if (waiting.answered) {
      check('the Unused summary waits for the Outputs', false, 'the Outputs answered before the hold; state not reached');
    } else {
      check('the Unused summary waits for the Outputs', waiting.summary === '' && waiting.dots.includes('...'), `"${waiting.summary}", dots ${waiting.dots}`);
      await page.screenshot({ path: `${ARTIFACT_DIR}/parts-waiting.png` });
    }
    await page.unroute('**/api/servo/outputs*', hold);
    release();

    // -----------------------------------------------------------------------
    // State 2: answered
    // -----------------------------------------------------------------------
    await page.waitForFunction(() => /^\d+ parts?$/.test(document.getElementById('parts-unused-summary').textContent), null, {
      timeout: 30000,
    });
    if (!lastOutputs) {
      const deadline = Date.now() + 5000;
      while (!lastOutputs && Date.now() < deadline) await page.waitForTimeout(200);
    }
    if (SELFTEST_HIDE) {
      await page.evaluate((selector) => {
        document.querySelector(selector).style.display = 'none';
      }, ROWS);
    }
    if (SELFTEST_UNUSED) {
      await page.evaluate((selector) => document.querySelector(selector).remove(), ROWS);
    }

    // The list against the droid's own answer: what this image moves (a
    // control path that is neither the dome link nor none), less every Part
    // an Output carries.
    const claimed = new Set();
    (lastOutputs?.outputs || []).forEach((output) => (output.parts || []).forEach((id) => claimed.add(id)));
    const unusedNow = await page.evaluate((selector) => ({
      expected: window.DroidParts.parts
        .filter((part) => part.control !== null && part.control !== undefined && part.control !== 'dome-link')
        .map((part) => part.id),
      rows: [...document.querySelectorAll(selector)].map((row) => {
        const part = window.PAParts.partById.get(row.dataset.part);
        const cell = row.querySelector('[data-bar-for]');
        const offers = part && window.PAParts.isDomePart(part)
          ? cell?.textContent.trim() === window.PAParts.domeMovesText(window.PAParts.partLabel(part.id))
          : Boolean(cell?.querySelector('.output-seg[role="radiogroup"] button'));
        return { id: row.dataset.part, offers };
      }),
      summary: document.getElementById('parts-unused-summary').textContent,
    }), ROWS);
    const expected = unusedNow.expected.filter((id) => !claimed.has(id));
    const shownIds = unusedNow.rows.map((row) => row.id);
    const missingUnused = expected.filter((id) => !shownIds.includes(id));
    const strayUnused = shownIds.filter((id) => !expected.includes(id));
    const noAct = unusedNow.rows.filter((row) => !row.offers).map((row) => row.id);
    const countOk = unusedNow.summary === `${shownIds.length} ${shownIds.length === 1 ? 'part' : 'parts'}`;
    check(
      'Unused lists every Part this image moves that no Output claims, and no other',
      lastOutputs !== null && missingUnused.length === 0 && strayUnused.length === 0 && noAct.length === 0 && countOk,
      lastOutputs === null
        ? 'no Outputs answer was seen'
        : `${shownIds.length} rows, summary "${unusedNow.summary}"` +
            (missingUnused.length ? `; missing ${missingUnused.join(', ')}` : '') +
            (strayUnused.length ? `; claimed but listed ${strayUnused.join(', ')}` : '') +
            (noAct.length ? `; no Output offered on ${noAct.join(', ')}` : ''),
    );

    const state2 = await hiddenRows(page);
    check('no Unused row hidden once answered', state2.hidden.length === 0, `${state2.total} rows, hidden: ${state2.hidden.join(', ') || 'none'}`);
    await page.screenshot({ path: `${ARTIFACT_DIR}/parts-answered.png`, fullPage: true });

    // -----------------------------------------------------------------------
    // Per-frame updates touch only values
    // -----------------------------------------------------------------------
    // Anything the pointer can rest on, watched for removal from the document.
    await page.evaluate(() => {
      window.__removed = [];
      const watched = 'tr[data-part], [data-bar-for] button, .bodyview-panel-slot button, [data-marker], [data-act], tbody, table';
      const observer = new MutationObserver((records) => {
        records.forEach((record) =>
          record.removedNodes.forEach((node) => {
            if (node.nodeType !== 1) return;
            if (node.matches(watched) || node.querySelector(watched)) {
              window.__removed.push(`${node.tagName.toLowerCase()}${node.dataset?.part ? `[data-part=${node.dataset.part}]` : ''}${node.dataset?.marker ? `[data-marker=${node.dataset.marker}]` : ''}${node.dataset?.act ? `[data-act=${node.dataset.act}]` : ''}`);
            }
          }),
        );
      });
      ['parts-unused', 'bodyview-drawing', 'bodyview-panel'].forEach((id) => {
        const host = document.getElementById(id);
        if (host) observer.observe(host, { childList: true, subtree: true });
      });
    });

    const frameCounter = () =>
      page.evaluate(() => {
        window.__statusFrames = 0;
        if (!window.__statusCounting) {
          window.__statusCounting = true;
          window.PALiveReading.subscribe(() => {
            window.__statusFrames += 1;
          });
        }
      });

    // Rest the pointer on `selector`, let FRAMES Outputs reads land, and ask
    // whether the element under it survived. A step that cannot run is a
    // FAIL of that step, not the end of the script.
    let hoverSerial = 0;
    const underPointer = async (label, selector, mutate) => {
      try {
        await restOn(label, selector, mutate);
      } catch (error) {
        check(`${label} stays the same node under the pointer`, false, `could not run: ${String(error.message).split('\n')[0]}`);
      }
    };
    const restOn = async (label, selector, mutate) => {
      hoverSerial += 1;
      const tag = `hovered-${hoverSerial}`;
      const target = page.locator(selector).first();
      await target.scrollIntoViewIfNeeded();
      await target.hover();
      const box = await target.boundingBox();
      const point = { x: box.x + box.width / 2, y: box.y + box.height / 2 };
      await page.evaluate(([sel, value]) => {
        document.querySelector(sel).__hoverTag = value;
        window.__removed = [];
      }, [selector, tag]);
      await frameCounter();
      const start = outputsFrames;
      let mutated = false;
      const deadline = Date.now() + 20000;
      while (outputsFrames < start + FRAMES && Date.now() < deadline) {
        await page.waitForTimeout(200);
        if (mutate && !mutated && outputsFrames > start) {
          mutated = true;
          await page.evaluate(mutate, selector);
        }
      }
      const frames = outputsFrames - start;
      const verdict = await page.evaluate(
        ([sel, at, value]) => {
          const now = document.querySelector(sel);
          const hit = document.elementFromPoint(at.x, at.y);
          const tagged = [...document.querySelectorAll('*')].find((el) => el.__hoverTag === value) || null;
          return {
            same: Boolean(now && now.__hoverTag === value),
            connected: Boolean(tagged && tagged.isConnected),
            underPointer: Boolean(tagged && hit && (hit === tagged || tagged.contains(hit))),
            hovered: Boolean(tagged && tagged.matches(':hover')),
            removed: window.__removed.slice(),
            statusFrames: window.__statusFrames,
          };
        },
        [selector, point, tag],
      );
      const ok = frames >= FRAMES && verdict.same && verdict.connected && verdict.underPointer && verdict.hovered && verdict.removed.length === 0;
      check(
        `${label} stays the same node under the pointer`,
        ok,
        `${frames} Outputs frames, ${verdict.statusFrames} status frames; same=${verdict.same} connected=${verdict.connected} under-pointer=${verdict.underPointer} :hover=${verdict.hovered}` +
          (verdict.removed.length ? `; removed: ${[...new Set(verdict.removed)].slice(0, 6).join(', ')}` : ''),
      );
    };

    // An Unused row's Output bar, where the list has one. Hovered, never
    // pressed.
    const unusedBar = await page.$(`${ROWS} [data-bar-for] .output-seg`);
    if (unusedBar) {
      const barPart = await unusedBar.evaluate((node) => node.closest('[data-bar-for]').dataset.barFor);
      await underPointer(
        'an Unused row\'s Output bar',
        `#parts-unused [data-bar-for="${barPart}"] .output-seg button`,
        SELFTEST_REBUILD
          ? (sel) => {
              const button = document.querySelector(sel);
              button.replaceWith(button.cloneNode(true));
            }
          : null,
      );
    } else {
      report.add(report.rows.length + 1, 'an Unused row\'s Output bar stays the same node under the pointer', lib.NOT_ASSESSED, 'no body Part is off an output');
    }

    // The droid picture: pick a marker (selection only), then rest on it and
    // on an act button in its panel. Never pressed.
    const marker = page.locator('#bodyview-drawing [data-marker]:visible').first();
    if (await marker.count()) {
      const markerId = await marker.getAttribute('data-marker');
      await marker.click();
      await page.waitForTimeout(300);
      const state3 = await hiddenRows(page);
      check('no Unused row hidden with a Part picked', state3.hidden.length === 0, `${state3.total} rows, hidden: ${state3.hidden.join(', ') || 'none'}`);

      // The pick survives the once-a-second Outputs reads (#352).
      const readPick = () =>
        page.evaluate((id) => {
          const cell = document.querySelector(`#bodyview-drawing [data-marker="${id}"]`);
          const listRow = document.querySelector(`[data-list-marker="${id}"]`);
          return {
            marker: Boolean(cell && cell.classList.contains('is-selected') && cell.getAttribute('aria-pressed') === 'true'),
            listRow: listRow ? listRow.classList.contains('is-selected') : null,
            title: document.querySelector('#bodyview-panel .bodyview-panel-title')?.textContent || '',
          };
        }, markerId);
      const picked = await readPick();
      const pickStart = outputsFrames;
      let deselected = false;
      const pickDeadline = Date.now() + 20000;
      while (outputsFrames < pickStart + FRAMES && Date.now() < pickDeadline) {
        await page.waitForTimeout(200);
        if (SELFTEST_DESELECT && !deselected && outputsFrames > pickStart) {
          deselected = true;
          await marker.click();
        }
      }
      const pickFrames = outputsFrames - pickStart;
      const stillPicked = await readPick();
      check(
        'a picked Part stays picked across Outputs frames',
        picked.marker &&
          picked.title !== '' &&
          pickFrames >= FRAMES &&
          stillPicked.marker &&
          stillPicked.listRow !== false &&
          stillPicked.title === picked.title,
        `${markerId} "${picked.title}", ${pickFrames} Outputs frames; after them: marker selected=${stillPicked.marker}, ` +
          `list row selected=${stillPicked.listRow === null ? 'no list row' : stillPicked.listRow}, panel "${stillPicked.title}"`,
      );
      await underPointer('a droid-picture marker', `#bodyview-drawing [data-marker="${markerId}"]`, null);
      // The panel's Output bar, where the pick has one (#463), else an act.
      const panelBar = page.locator('#bodyview-panel .bodyview-panel-slot .output-seg button').first();
      if (await panelBar.count()) {
        await underPointer('the panel\'s Output bar', '#bodyview-panel .bodyview-panel-slot .output-seg button', null);
      }
      // An act the Part can take, if it has one. A refused act button takes no
      // pointer by design (data/style.css, .btn:disabled pointer-events: none),
      // so with none offered the pointer rests on the act row that holds them.
      const act = page.locator('#bodyview-panel [data-act]:visible:not([disabled])').first();
      if (await act.count()) {
        const actId = await act.getAttribute('data-act');
        await underPointer(`the panel's "${(await act.textContent()).trim()}" button`, `#bodyview-panel [data-act="${actId}"]`, null);
      } else {
        await underPointer('the panel\'s act row (every act refused)', '#bodyview-panel .bodyview-panel-acts', null);
      }
      await page.screenshot({ path: `${ARTIFACT_DIR}/parts-picked.png` });
    } else {
      check('the droid picture draws a marker to pick', false, 'no visible [data-marker] in #bodyview-drawing');
    }

    // -----------------------------------------------------------------------
    // State 4: the Outputs read is failing
    // -----------------------------------------------------------------------
    const refuse = (route) => route.abort('internetdisconnected');
    await page.route('**/api/servo/outputs*', refuse);
    await page.waitForTimeout(3500);
    const state4 = await hiddenRows(page);
    check('no Unused row hidden while the Outputs read fails', state4.hidden.length === 0, `${state4.total} rows, hidden: ${state4.hidden.join(', ') || 'none'}`);
    await page.screenshot({ path: `${ARTIFACT_DIR}/parts-read-failing.png` });
    await page.unroute('**/api/servo/outputs*', refuse);

    const blocked = lib.blockedWrites(writes);
    check('Parts sent no write of its own', blocked.length === 0, blocked.length ? [...new Set(blocked)].join(', ') : '');
  } catch (error) {
    console.error('parts-surface could not complete:', error);
    process.exitCode = 1;
  } finally {
    await lib.closeAll(browser, [fixture]);
  }

  report.print();
  const failed = report.rows.filter((row) => row.result !== lib.PASS).length;
  console.log(`Screenshots under ${ARTIFACT_DIR}`);
  if (failed || report.rows.length === 0) process.exitCode = 1;
})();
