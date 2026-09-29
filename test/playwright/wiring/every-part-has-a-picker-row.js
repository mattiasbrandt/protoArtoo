// bench-auto: droid
// Every Part has one row in the part-first picker on Wiring, and each row reads
// what the droid answered. History: #347 built the picker on Parts and its
// rules were held by test/playwright/parts/parts-surface.js; #411 moved the
// picker and its move question to Wiring's Outputs section (operator,
// 2026-09-28), and these rules moved with it.
//
// THE RULES IT HOLDS. The rule is the quoted line; the ticket is history.
//   1 "One row per Part, grouped by class, with counts in the headings"
//     (history: #347): every group heading reads "<label> - <n> <unit>" and <n>
//     is the number of rows under it; every Part in the served catalog
//     (window.DroidParts) that sits on the body has exactly one row with an
//     Output select, and every dome Part (half "dome", whatever its control)
//     has one row in the Dome Controller group with no select, and its command
//     or "No command yet" instead (history: #411, operator 2026-09-29).
//   2 "A Part with no Output reads - not wired -" (history: #347): every Part no
//     row of GET /api/servo/outputs carries shows "- not wired -" (en dashes) as
//     its chosen Output and is not marked wired; every Part an Output carries
//     shows that Output. The summary's two counts match the same answer.
//   3 "No row is hidden, in any state" (history: #347): every row is rendered
//     visible (Element.checkVisibility with opacity and visibility, and a box
//     with height) while the Outputs are still being found out, and once they
//     have answered.
//   4 "A light Part shows what a light can promise and nothing else" (history:
//     #357; data/droid_part_kind.js): every catalog Part whose `kind` is
//     "light" has a row carrying partkind-light and the "light" tag, and no
//     other row carries the class; the treatment is DRAWN (the row heading's
//     left border computes to dashed, data/style.css); and a light row has
//     spans exactly the table's header columns and no position or release cell.
//   5 "Wiring sends no write of its own on a visit" (history: #347).
//
// WHY A REAL BROWSER. mini_dom (test/test_web/helpers/mini_dom.js) has no CSS
// engine and no layout: "visible" and a computed dashed border only exist here.
//
// WHAT IT SKIPS, AND WHY.
//   - A move (choosing another Output for a Part), its announcement and
//     steal-not-share: every path ends in POST /api/config, a configuration
//     write, and a Part moved on the bench droid stays moved. The web suite
//     holds the move (test/test_web/test_parts_table.js); the question
//     dialog's reach over STOP is stop-every-surface.js's.
//   - "Per-frame updates touch only values" under the pointer: it was a rule of
//     Parts, which reads the Outputs once a second. Wiring is a reference
//     surface with no cadence (data/wiring.js), so no frame lands under a
//     hovered picker here.
//   - The rows against a fresh droid: the bench droid's mapping is whatever it
//     holds, so the rows are checked against its own answer instead.
// A browser-side guard aborts every write before it leaves the browser.
//
// RUN (operator watching):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wiring/every-part-has-a-picker-row.js
//   BASE_URL=http://<board>   the controller (default http://10.0.0.22)
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against python3 tools/serve_editor_fixture.py (routes in
// ../_lib/fixture_routes.js, its 'bench' droid).
// Self-tests, each must FAIL its rule: SELFTEST=hide hides one row once
// answered (3); SELFTEST=notwired rewords one unwired row's "- not wired -"
// (2); SELFTEST=unlight takes partkind-light off one light row (4);
// SELFTEST=position gives one light row a position cell (4); SELFTEST=domeselect
// puts a select on one Dome Controller row (1c).
//   Rule 4 reads a light Part's row in either group: a dome light sits in the
//   Dome Controller group and keeps its treatment there.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/wiring';
// data/parts_mapping.js NOT_WIRED, en dashes and all.
const NOT_WIRED = '– not wired –';
const ROWS = '#wiring-parts-table [data-part]';
const SUMMARY = '#wiring-parts-summary';

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

lib.runCheck({
  rule: 'Every Part has one row in the picker on Wiring',
  artifactDir: ARTIFACTS,
  fixture: { droid: 'bench' },
  selftests: ['hide', 'notwired', 'unlight', 'position', 'domeselect'],
  run: async ({ page, report, writes, selftest }) => {
    // The droid's own answer, so the rows are checked against what the droid
    // said rather than against a copy made here.
    let lastOutputs = null;
    page.on('response', async (response) => {
      if (!response.url().includes('/api/servo/outputs') || !response.ok()) return;
      try {
        lastOutputs = await response.json();
      } catch (_error) {
        // A body cut short by navigation; the next read replaces it.
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
    await page.goto(`${lib.BASE_URL}/#wiring`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector(ROWS, { timeout: 30000 });
    await page.waitForTimeout(2500);

    // -----------------------------------------------------------------------
    // While the Outputs are still being waited for
    // -----------------------------------------------------------------------
    const waiting = await page.evaluate(([selector, summary]) => ({
      summary: document.querySelector(summary).textContent,
      dots: getComputedStyle(document.querySelector(summary), '::after').content,
      disabled: [...document.querySelectorAll(`${selector} select`)].every((select) => select.disabled),
      answered: window.PAOutputs.known().table,
    }), [ROWS, SUMMARY]);
    if (waiting.answered) {
      report.add('3a', 'no row hidden while waiting', lib.NOT_ASSESSED, 'the Outputs answered before the hold');
    } else {
      const state = await hiddenRows(page);
      report.add('3a', 'no row hidden while waiting', lib.verdict(state.hidden.length === 0 && state.total > 0), `${state.total} rows, hidden: ${state.hidden.join(', ') || 'none'}`);
      report.add('3b', 'the summary shows the waiting dots, the pickers wait', lib.verdict(waiting.summary === '' && waiting.dots.includes('...') && waiting.disabled), `"${waiting.summary}", dots ${waiting.dots}, all disabled: ${waiting.disabled}`);
      await page.screenshot({ path: `${ARTIFACTS}/picker-waiting.png` });
    }
    await page.unroute('**/api/servo/outputs*', hold);
    release();

    // -----------------------------------------------------------------------
    // Answered
    // -----------------------------------------------------------------------
    await page.waitForFunction((summary) => /parts on an output/.test(document.querySelector(summary).textContent), SUMMARY, { timeout: 30000 });
    if (selftest === 'hide') {
      await page.evaluate((selector) => {
        document.querySelector(selector).style.display = 'none';
      }, ROWS);
    }

    if (selftest === 'domeselect') {
      await page.evaluate(() => {
        document.querySelector('#wiring-parts-table [data-dome-part] td').appendChild(document.createElement('select'));
      });
    }
    const groups = await page.evaluate(() => ({
      catalog: window.DroidParts.parts.filter((part) => part.half !== 'dome').map((part) => part.id),
      domeLink: window.DroidParts.parts.filter((part) => part.half === 'dome').map((part) => part.id),
      rows: [...document.querySelectorAll('#wiring-parts-table [data-part]')].map((row) => row.dataset.part),
      domeRows: [...document.querySelectorAll('#wiring-parts-table [data-dome-part]')].map((row) => ({
        id: row.dataset.domePart,
        select: row.querySelector('select') !== null,
        command: row.querySelector('.parts-command')?.textContent || '',
      })),
      groups: [...document.querySelectorAll('#wiring-parts-table tbody[data-group]')].map((body) => ({
        heading: body.querySelector('.parts-group th').textContent,
        rows: body.querySelectorAll('[data-part], [data-dome-part]').length,
      })),
    }));
    const badHeadings = groups.groups.filter((group) => {
      const match = group.heading.match(/^(.+) — (\d+) (part|parts|placeholder|placeholders)$/);
      if (!match) return true;
      const count = Number(match[2]);
      const singular = match[3] === 'part' || match[3] === 'placeholder';
      return count !== group.rows || singular !== (count === 1);
    });
    report.add(
      '1a',
      'every group heading counts the rows under it',
      lib.verdict(groups.groups.length > 0 && badHeadings.length === 0),
      badHeadings.length ? badHeadings.map((group) => `"${group.heading}" over ${group.rows} rows`).join('; ') : groups.groups.map((group) => group.heading).join(' | '),
    );
    const missing = groups.catalog.filter((id) => !groups.rows.includes(id));
    const doubled = groups.rows.filter((id, index) => groups.rows.indexOf(id) !== index);
    report.add(
      '1b',
      'one row with an Output select per body Part',
      lib.verdict(missing.length === 0 && doubled.length === 0 && groups.rows.length === groups.catalog.length),
      `${groups.rows.length} rows for ${groups.catalog.length} Parts${missing.length ? `, missing ${missing.join(', ')}` : ''}${doubled.length ? `, doubled ${doubled.join(', ')}` : ''}`,
    );

    const domeWrong = groups.domeLink.filter((id) => {
      const row = groups.domeRows.find((each) => each.id === id);
      return !row || row.select || row.command === '';
    });
    report.add(
      '1c',
      'every dome Part has a Dome Controller row with its command and no Output select',
      lib.verdict(groups.domeLink.length > 0 && domeWrong.length === 0 && groups.domeRows.length === groups.domeLink.length),
      `${groups.domeRows.length} rows for ${groups.domeLink.length} dome Parts${domeWrong.length ? `; wrong: ${domeWrong.join(', ')}` : ''}` +
        ` (${groups.domeRows.slice(0, 2).map((row) => `${row.id} "${row.command}"`).join(', ')})`,
    );

    // "- not wired -", against the droid's own answer.
    const deadline = Date.now() + 5000;
    while (!lastOutputs && Date.now() < deadline) await page.waitForTimeout(200);
    if (selftest === 'notwired') {
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
    report.add(
      '2a',
      'every Part with no Output reads "- not wired -"',
      lib.verdict(lastOutputs !== null && unwired.length > 0 && unwiredWrong.length === 0),
      lastOutputs === null
        ? 'no Outputs answer was seen'
        : `${unwired.length} unwired${unwiredWrong.length ? `; wrong: ${unwiredWrong.slice(0, 5).map((row) => `${row.id} shows "${row.shown}"`).join(', ')}` : ''}`,
    );
    const onRows = rowsNow.filter((row) => onOutput.has(row.id));
    const onWrong = onRows.filter((row) => row.value !== onOutput.get(row.id) || !row.wired || row.shown === NOT_WIRED);
    report.add(
      '2b',
      'every Part on an Output shows that Output',
      lib.verdict(onWrong.length === 0),
      `${onRows.length} on an output${onWrong.length ? `; wrong: ${onWrong.map((row) => `${row.id} shows "${row.shown}"`).join(', ')}` : ''}`,
    );
    const outputs = lastOutputs?.outputs || [];
    const expectSummary =
      `${groups.catalog.filter((id) => onOutput.has(id)).length} of ${groups.catalog.length} parts on an output · ` +
      `${outputs.filter((output) => (output.parts || []).length === 0).length} of ${outputs.length} outputs free`;
    const summary = await page.textContent(SUMMARY);
    report.add('2c', 'the summary counts match the droid\'s answer', lib.verdict(summary.startsWith(expectSummary)), `"${summary}"`);

    const answered = await hiddenRows(page);
    report.add('3c', 'no row hidden once answered', lib.verdict(answered.hidden.length === 0), `${answered.total} rows, hidden: ${answered.hidden.join(', ') || 'none'}`);

    // -----------------------------------------------------------------------
    // Light-kind rows (#357)
    // -----------------------------------------------------------------------
    if (selftest === 'unlight' || selftest === 'position') {
      await page.evaluate((which) => {
        const light = document.querySelector('#wiring-parts-table tr.partkind-light');
        if (!light) return;
        if (which === 'position') light.insertAdjacentHTML('beforeend', '<td><span class="outputs-bar"></span>1500 µs</td>');
        if (which === 'unlight') light.classList.remove('partkind-light');
      }, selftest);
    }
    const kinds = await page.evaluate(() => {
      const table = document.querySelector('#wiring-parts-table table');
      const headers = [...table.querySelectorAll('thead th')].map((th) => th.textContent.trim());
      const lightIds = window.DroidParts.parts.filter((part) => part.kind === 'light').map((part) => part.id);
      const drawnAway = '.outputs-bar, .outputs-now, .outputs-tick, .outputs-us, .outputs-width, .outputs-release, .outputs-throw, .outputs-motion';
      const rows = [...table.querySelectorAll('tr[data-part], tr[data-dome-part]')].map((row) => ({
        id: row.dataset.part || row.dataset.domePart,
        light: lightIds.includes(row.dataset.part || row.dataset.domePart),
        classed: row.classList.contains('partkind-light'),
        tag: row.querySelector('.parts-kind')?.textContent.trim() || '',
        // Columns spanned, not cells: a dome row's command spans the Output
        // and Type columns (data/parts_mapping.js domeRowHtml()).
        cells: [...row.children].reduce((spanned, cell) => spanned + (cell.colSpan || 1), 0),
        drawn: row.querySelector(drawnAway) !== null,
        micro: /µs/.test(row.querySelector('th').textContent),
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
    report.add(
      '4',
      'light Parts carry partkind-light and draw no position or release column',
      lib.verdict(lightRows.length > 0 && lightRows.length === kinds.lightIds.length && lightWrong.length === 0 && strayClass.length === 0 && positionHeaders.length === 0),
      `${lightRows.length} light rows of ${kinds.lightIds.length} light Parts; columns: ${kinds.headers.join(' | ')}` +
        (positionHeaders.length ? `; a position/release column: ${positionHeaders.join(', ')}` : '') +
        (lightWrong.length ? `; wrong: ${lightWrong.join('; ')}` : '') +
        (strayClass.length ? `; partkind-light on a Part that is not a light: ${strayClass.join(', ')}` : ''),
    );
    await page.screenshot({ path: `${ARTIFACTS}/picker-answered.png`, fullPage: true });

    const blocked = lib.blockedWrites(writes);
    report.add('5', 'Wiring sent no write of its own', lib.verdict(blocked.length === 0), blocked.length ? [...new Set(blocked)].join(', ') : '');
  },
});
