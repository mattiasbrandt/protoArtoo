// The Parts surface's standing rules, read in a real browser. History: #347
// built Parts and shipped no script; this is it.
//
// THE RULES IT HOLDS - the ones that need a browser, checked without writing
// anything. The rule is the quoted line; the ticket is history, in brackets.
//   - "One row per Part, grouped by class, with counts in the headings"
//     (history: #347): every group heading reads "<label> - <n> <unit>" and
//     <n> is the number of rows under it; every Part in the served catalog
//     (window.DroidParts) has exactly one row.
//   - "A Part with no Output reads - not wired -" (history: #347): every Part
//     no row of GET /api/servo/outputs carries shows "- not wired -" (en
//     dashes) as its chosen Output and is not marked wired; every Part an
//     Output carries shows that Output. The summary's two counts match the
//     same answer.
//   - "No row is hidden, in any state" (history: #347): every row is rendered
//     visible (Element.checkVisibility with opacity and visibility, and a box
//     with height) while the Outputs are still being found out, once they
//     have answered, while the Outputs read is failing, and with a Part picked
//     on the droid picture.
//   - "Per-frame updates touch only values; no full re-render occurs while a
//     control is under the pointer" (history: #347): the pointer rests on a
//     table picker, on a droid-picture marker and on an act button in the
//     picture's panel in turn, while at least three once-a-second Outputs
//     reads land; each hovered element must still be the same node, still
//     connected, still under the pointer, and no row, picker, marker or act
//     button may be removed from the document meanwhile. The Outputs read is
//     Parts' per-frame feed (data/outputs.js follow, once a second). Status
//     frames are counted and printed too, but an idle Bench-Mode droid pushes
//     one only when something changes (data/shell.js,
//     requestStatusBroadcastNow), so none arriving is not a failure.
//   - "Parts sends no write of its own on a visit" (history: #347).
//   - "A light Part shows what a light can promise and nothing else"
//     (history: #357; data/droid_part_kind.js, data/parts.js:84): every
//     catalog Part whose `kind` is "light" has a row carrying partkind-light
//     and the "light" tag, and no other row carries the class; the treatment
//     is DRAWN (the row heading's left border computes to dashed,
//     data/style.css); and a light row draws no position or release column -
//     it has exactly the table's header columns, the header names no position
//     or release column, and the row holds no position bar, commanded width,
//     release, throw or motion cell and no microsecond value in its heading.
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
//   - A move (choosing another Output for a Part), its announcement and
//     steal-not-share: every path ends in POST /api/config, a configuration
//     write, and a Part moved on the bench droid stays moved. The question
//     dialog's reach over STOP is stop-every-surface.js's.
//   - Open it / Close it, Drop from build / Add to build, Give it an output:
//     each either moves an Output (POST /api/servo, /api/dome/cmd) or writes
//     the Droid Build (POST /api/config). The act buttons are hovered, never
//     pressed. Picking a marker on the picture is pressed: it only selects
//     (data/parts.js, "THE VIEW NEVER WRITES").
//   - "A fresh droid shows every catalog Part, none on an Output, and all 5
//     default Outputs carry no Part": true only of a droid nobody has wired,
//     and the bench droid's mapping is whatever it holds. The script checks
//     the rows against the droid's own answer instead, which holds on any
//     droid.
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
// picker for a copy mid-frame; SELFTEST_HIDE=1 hides one row once answered;
// SELFTEST_NOTWIRED=1 rewords one unwired row's "- not wired -";
// SELFTEST_UNLIGHT=1 takes partkind-light off one light row;
// SELFTEST_POSITION=1 gives one light row a position cell;
// SELFTEST_DESELECT=1 picks the picked marker again (which lets it go) after
// the first Outputs read lands.
const { mkdirSync } = require('node:fs');
const lib = require('../_lib/checks.js');

const { BASE_URL, FIXTURE } = lib;
const SELFTEST_REBUILD = process.env.SELFTEST_REBUILD === '1';
const SELFTEST_HIDE = process.env.SELFTEST_HIDE === '1';
const SELFTEST_NOTWIRED = process.env.SELFTEST_NOTWIRED === '1';
const SELFTEST_UNLIGHT = process.env.SELFTEST_UNLIGHT === '1';
const SELFTEST_POSITION = process.env.SELFTEST_POSITION === '1';
const SELFTEST_DESELECT = process.env.SELFTEST_DESELECT === '1';
const HOLD_MS = Number(process.env.HOLD_MS || 2500);
const FRAMES = Number(process.env.FRAMES || 3);
const ARTIFACT_DIR = 'output/playwright/issue-347';

// data/parts_mapping.js NOT_WIRED, en dashes and all.
const NOT_WIRED = '\u2013 not wired \u2013';
const ROWS = '#parts-table [data-part]';

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
    await page.waitForSelector(ROWS, { timeout: 30000 });
    // Long enough to be seen, and to be sure the page is not about to answer
    // from somewhere else.
    await page.waitForTimeout(HOLD_MS);

    // -----------------------------------------------------------------------
    // State 1: the Outputs are still being waited for
    // -----------------------------------------------------------------------
    const waiting = await page.evaluate((selector) => ({
      summary: document.getElementById('parts-summary').textContent,
      dots: getComputedStyle(document.getElementById('parts-summary'), '::after').content,
      disabled: [...document.querySelectorAll(`${selector} select`)].every((select) => select.disabled),
      answered: window.PAOutputs.known().table,
    }), ROWS);
    if (waiting.answered) {
      check('no row hidden while waiting', false, 'the Outputs answered before the hold; state not reached');
    } else {
      const state1 = await hiddenRows(page);
      check('no row hidden while waiting', state1.hidden.length === 0 && state1.total > 0, `${state1.total} rows, hidden: ${state1.hidden.join(', ') || 'none'}`);
      check('summary shows the waiting dots, pickers wait', waiting.summary === '' && waiting.dots.includes('...') && waiting.disabled, `"${waiting.summary}", dots ${waiting.dots}, all disabled: ${waiting.disabled}`);
      await page.screenshot({ path: `${ARTIFACT_DIR}/parts-waiting.png` });
    }
    await page.unroute('**/api/servo/outputs*', hold);
    release();

    // -----------------------------------------------------------------------
    // State 2: answered
    // -----------------------------------------------------------------------
    await page.waitForFunction(() => /parts on an output/.test(document.getElementById('parts-summary').textContent), null, {
      timeout: 30000,
    });
    await page.waitForFunction(() => window.PAOutputs.known().table, null, { timeout: 10000 });
    if (SELFTEST_HIDE) {
      await page.evaluate((selector) => {
        document.querySelector(selector).style.display = 'none';
      }, ROWS);
    }

    // Counts in the headings, and one row per catalog Part.
    const groups = await page.evaluate(() => {
      const catalog = window.DroidParts.parts.map((part) => part.id);
      const rows = [...document.querySelectorAll('#parts-table [data-part]')].map((row) => row.dataset.part);
      return {
        catalog,
        rows,
        groups: [...document.querySelectorAll('#parts-table tbody[data-group]')].map((body) => ({
          id: body.dataset.group,
          heading: body.querySelector('.parts-group th').textContent,
          rows: body.querySelectorAll('[data-part]').length,
        })),
      };
    });
    const badHeadings = groups.groups.filter((group) => {
      const match = group.heading.match(/^(.+) \u2014 (\d+) (part|parts|placeholder|placeholders)$/);
      if (!match) return true;
      const count = Number(match[2]);
      const singular = match[3] === 'part' || match[3] === 'placeholder';
      return count !== group.rows || singular !== (count === 1);
    });
    check(
      'every group heading counts the rows under it',
      groups.groups.length > 0 && badHeadings.length === 0,
      badHeadings.length ? badHeadings.map((group) => `"${group.heading}" over ${group.rows} rows`).join('; ') : groups.groups.map((group) => group.heading).join(' | '),
    );
    const missing = groups.catalog.filter((id) => !groups.rows.includes(id));
    const doubled = groups.rows.filter((id, index) => groups.rows.indexOf(id) !== index);
    check(
      'one row per catalog Part',
      missing.length === 0 && doubled.length === 0 && groups.rows.length === groups.catalog.length,
      `${groups.rows.length} rows for ${groups.catalog.length} Parts${missing.length ? `, missing ${missing.join(', ')}` : ''}${doubled.length ? `, doubled ${doubled.join(', ')}` : ''}`,
    );

    // "- not wired -", against the droid's own answer.
    if (!lastOutputs) {
      const deadline = Date.now() + 5000;
      while (!lastOutputs && Date.now() < deadline) await page.waitForTimeout(200);
    }
    if (SELFTEST_NOTWIRED) {
      await page.evaluate((selector) => {
        const row = [...document.querySelectorAll(selector)].find((each) => !each.classList.contains('is-wired'));
        row.querySelector('select').selectedOptions[0].textContent = 'not wired';
      }, ROWS);
    }
    const onOutput = new Map();
    (lastOutputs?.outputs || []).forEach((output) => (output.parts || []).forEach((id) => onOutput.set(id, String(output.address))));
    const rowsNow = await page.evaluate((selector) =>
      [...document.querySelectorAll(selector)].map((row) => {
        const select = row.querySelector('select');
        return {
          id: row.dataset.part,
          wired: row.classList.contains('is-wired'),
          value: select.value,
          shown: select.selectedOptions[0] ? select.selectedOptions[0].textContent : '',
        };
      }), ROWS);
    const unwired = rowsNow.filter((row) => !onOutput.has(row.id));
    const unwiredWrong = unwired.filter((row) => row.shown !== NOT_WIRED || row.wired || row.value !== 'none');
    check(
      'every Part with no Output reads "- not wired -"',
      lastOutputs !== null && unwired.length > 0 && unwiredWrong.length === 0,
      lastOutputs === null
        ? 'no Outputs answer was seen'
        : `${unwired.length} unwired${unwiredWrong.length ? `; wrong: ${unwiredWrong.slice(0, 5).map((row) => `${row.id} shows "${row.shown}"`).join(', ')}` : ''}`,
    );
    const wiredRows = rowsNow.filter((row) => onOutput.has(row.id));
    const wiredWrong = wiredRows.filter((row) => row.value !== onOutput.get(row.id) || !row.wired || row.shown === NOT_WIRED);
    check(
      'every Part on an Output shows that Output',
      wiredWrong.length === 0,
      `${wiredRows.length} wired${wiredWrong.length ? `; wrong: ${wiredWrong.map((row) => `${row.id} shows "${row.shown}"`).join(', ')}` : ''}`,
    );
    const catalogIds = new Set(groups.catalog);
    const outputs = lastOutputs?.outputs || [];
    const expectSummary =
      `${groups.catalog.filter((id) => onOutput.has(id)).length} of ${groups.catalog.length} parts on an output \u00b7 ` +
      `${outputs.filter((output) => (output.parts || []).length === 0).length} of ${outputs.length} outputs driving nothing`;
    const summary = await page.textContent('#parts-summary');
    check('summary counts match the droid\'s answer', summary.startsWith(expectSummary), `"${summary}"`);
    const strangers = [...onOutput.keys()].filter((id) => !catalogIds.has(id));
    if (strangers.length) console.log(`Note: the droid drives Parts this page does not know: ${strangers.join(', ')}`);

    const state2 = await hiddenRows(page);
    check('no row hidden once answered', state2.hidden.length === 0, `${state2.total} rows, hidden: ${state2.hidden.join(', ') || 'none'}`);

    // -----------------------------------------------------------------------
    // Light-kind rows (#357)
    // -----------------------------------------------------------------------
    if (SELFTEST_UNLIGHT || SELFTEST_POSITION) {
      await page.evaluate(([unlight, position]) => {
        const light = document.querySelector('#parts-table tr.partkind-light');
        if (!light) return;
        if (position) light.insertAdjacentHTML('beforeend', '<td><span class="outputs-bar"></span>1500 \u00b5s</td>');
        if (unlight) light.classList.remove('partkind-light');
      }, [SELFTEST_UNLIGHT, SELFTEST_POSITION]);
    }
    const kinds = await page.evaluate(() => {
      const table = document.querySelector('#parts-table table');
      const headers = [...table.querySelectorAll('thead th')].map((th) => th.textContent.trim());
      const lightIds = window.DroidParts.parts.filter((part) => part.kind === 'light').map((part) => part.id);
      const drawnAway = '.outputs-bar, .outputs-now, .outputs-tick, .outputs-us, .outputs-width, .outputs-release, .outputs-throw, .outputs-motion';
      const rows = [...table.querySelectorAll('tr[data-part]')].map((row) => ({
        id: row.dataset.part,
        light: lightIds.includes(row.dataset.part),
        classed: row.classList.contains('partkind-light'),
        tag: row.querySelector('.parts-kind')?.textContent.trim() || '',
        cells: row.children.length,
        drawn: row.querySelector(drawnAway) !== null,
        micro: /\u00b5s/.test(row.querySelector('th').textContent),
        border: getComputedStyle(row.querySelector('th')).borderLeftStyle,
      }));
      return { headers, lightIds, rows };
    });
    const lightRows = kinds.rows.filter((row) => row.light);
    const positionHeaders = kinds.headers.filter((text) => /position|release|travel|throw/i.test(text));
    const lightWrong = lightRows
      .map((row) => {
        const why = [];
        if (!row.classed) why.push('no partkind-light');
        if (row.tag !== 'light') why.push(`tag "${row.tag}"`);
        if (row.border !== 'dashed') why.push(`heading border ${row.border}`);
        if (row.cells !== kinds.headers.length) why.push(`${row.cells} cells under ${kinds.headers.length} columns`);
        if (row.drawn) why.push('a position/release cell');
        if (row.micro) why.push('a microsecond value');
        return why.length ? `${row.id}: ${why.join(', ')}` : null;
      })
      .filter(Boolean);
    const strayClass = kinds.rows.filter((row) => !row.light && row.classed).map((row) => row.id);
    check(
      'light Parts carry partkind-light and draw no position or release column',
      lightRows.length > 0 && lightRows.length === kinds.lightIds.length && lightWrong.length === 0 && strayClass.length === 0 && positionHeaders.length === 0,
      `${lightRows.length} light rows of ${kinds.lightIds.length} light Parts (${kinds.lightIds.join(', ')}); columns: ${kinds.headers.join(' | ')}` +
        (positionHeaders.length ? `; a position/release column: ${positionHeaders.join(', ')}` : '') +
        (lightWrong.length ? `; wrong: ${lightWrong.join('; ')}` : '') +
        (strayClass.length ? `; partkind-light on a Part that is not a light: ${strayClass.join(', ')}` : ''),
    );
    await page.screenshot({ path: `${ARTIFACT_DIR}/parts-answered.png`, fullPage: true });

    // -----------------------------------------------------------------------
    // Per-frame updates touch only values
    // -----------------------------------------------------------------------
    // Anything the pointer can rest on, watched for removal from the document.
    await page.evaluate(() => {
      window.__removed = [];
      const watched = 'tr[data-part], select, [data-marker], [data-act], tbody, table';
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
      ['parts-table', 'bodyview-drawing', 'bodyview-panel'].forEach((id) => {
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

    // A table picker, on a Part the droid has nothing on where there is one.
    const pickerRow = unwired[0] ? unwired[0].id : rowsNow[0].id;
    await underPointer(
      'a table picker',
      `#parts-table [data-part="${pickerRow}"] select`,
      SELFTEST_REBUILD
        ? (sel) => {
            const select = document.querySelector(sel);
            select.replaceWith(select.cloneNode(true));
          }
        : null,
    );

    // The droid picture: pick a marker (selection only), then rest on it and
    // on an act button in its panel. Never pressed.
    const marker = page.locator('#bodyview-drawing [data-marker]:visible').first();
    if (await marker.count()) {
      const markerId = await marker.getAttribute('data-marker');
      await marker.click();
      await page.waitForTimeout(300);
      const state3 = await hiddenRows(page);
      check('no row hidden with a Part picked', state3.hidden.length === 0, `${state3.total} rows, hidden: ${state3.hidden.join(', ') || 'none'}`);

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
    check('no row hidden while the Outputs read fails', state4.hidden.length === 0, `${state4.total} rows, hidden: ${state4.hidden.join(', ') || 'none'}`);
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
