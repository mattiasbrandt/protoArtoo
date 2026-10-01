// bench-auto: droid
// Every Part is in Wiring's parts table, and the table reads what the droid
// answered. History: #347 built the picker on Parts and its rules were held by
// test/playwright/parts/parts-surface.js; #411 moved it and its move question
// to Wiring (operator, 2026-09-28); #463 made it Wiring's one table of what is
// on which wire (operator, 2026-10-01): a Part is a row while it is on an
// Output and a pill among the Parts to add while it is not. The filename is
// the rule's history.
//
// THE RULES IT HOLDS. The rule is the quoted line; the ticket is history.
//   1 "No Part is missing from the table" (history: #296, #347, #463): every
//     body Part in the served catalog (window.DroidParts) is in it exactly
//     once - a row with a bar of Outputs when GET /api/servo/outputs carries
//     it, a pill under "add a part" when it does not; every dome Part (half
//     "dome", whatever its control) has one row in the Dome Controller group
//     once that group is shown, with its command or "No command yet" and
//     nothing to press (history: #411, operator 2026-09-29).
//   2 "The table says which Output is wired and which is free, as the droid
//     answered" (history: #347, #463): every Part on an Output has that Output
//     lit on its bar and is marked wired; the free row lists exactly the
//     Outputs no Part is on; the two group counts and the summary match the
//     same answer.
//   3 "Nothing is drawn before the droid answers, and no row is hidden after"
//     (history: #347): while the Outputs are being waited for the summary
//     shows the waiting dots and the table has no row; once answered every
//     row and pill is rendered visible (Element.checkVisibility with opacity
//     and visibility, and a box with height), and the table does not push the
//     page sideways.
//   4 "A light Part shows what a light can promise and nothing else" (history:
//     #357; data/droid_part_kind.js): every catalog Part whose `kind` is
//     "light" has a row carrying partkind-light and the "light" tag once it
//     has a row at all, and no other row carries the class; the treatment is
//     DRAWN (the row heading's left border computes to dashed,
//     data/style.css); and no column of the table is a position or a release.
//   5 "Wiring sends no write of its own on a visit" (history: #347): adding a
//     pill to the table and showing the dome's group are this page's own and
//     reach the droid as nothing.
//
// WHY A REAL BROWSER. mini_dom (test/test_web/helpers/mini_dom.js) has no CSS
// engine and no layout: "visible" and a computed dashed border only exist here.
//
// WHAT IT SKIPS, AND WHY.
//   - A move (pressing another Output on a Part's bar), its announcement and
//     steal-not-share: every path ends in POST /api/config, a configuration
//     write, and a Part moved on the bench droid stays moved. The web suite
//     holds the move (test/test_web/test_parts_table.js); the question
//     dialog's reach over STOP is stop-every-surface.js's.
//   - The rows against a fresh droid: the bench droid's mapping is whatever it
//     holds, so the table is checked against its own answer instead.
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
// Self-tests, each must FAIL its rule: SELFTEST=missing takes one pill out of
// the table (1b); SELFTEST=domebutton puts a button on one Dome Controller row
// (1c); SELFTEST=freeword adds an Output to the free row that is not free
// (2b); SELFTEST=hide hides one row once answered (3); SELFTEST=unlight takes
// partkind-light off one light row (4); SELFTEST=position gives the table a
// position column (4).
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/wiring';
const TABLE = '#wiring-parts-table';
const ROWS = `${TABLE} tr[data-part]`;
const PILLS = `${TABLE} [data-act="add"]`;
const SUMMARY = '#wiring-parts-summary';

// Every row and pill that is not rendered visible, by Part id.
const hiddenOnes = (page) =>
  page.evaluate(([rows, pills]) => {
    const ones = [...document.querySelectorAll(`${rows}, ${pills}, #wiring-parts-table [data-dome-part]`)];
    return {
      total: ones.length,
      hidden: ones
        .filter((node) => {
          const box = node.getBoundingClientRect();
          return !node.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true }) || box.height === 0;
        })
        .map((node) => node.dataset.part || node.dataset.domePart),
    };
  }, [ROWS, PILLS]);

lib.runCheck({
  rule: 'Every Part is in the parts table on Wiring',
  artifactDir: ARTIFACTS,
  fixture: { droid: 'bench' },
  selftests: ['missing', 'domebutton', 'freeword', 'hide', 'unlight', 'position'],
  run: async ({ page, report, writes, selftest }) => {
    // The droid's own answer, so the table is checked against what the droid
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
    await page.waitForSelector(SUMMARY, { state: 'attached', timeout: 30000 });
    await page.waitForTimeout(2500);

    // -----------------------------------------------------------------------
    // While the Outputs are still being waited for
    // -----------------------------------------------------------------------
    const waiting = await page.evaluate(([table, summary]) => ({
      summary: document.querySelector(summary).textContent,
      dots: getComputedStyle(document.querySelector(summary), '::after').content,
      rows: document.querySelectorAll(`${table} tr`).length,
      answered: window.PAOutputs.known().table,
    }), [TABLE, SUMMARY]);
    if (waiting.answered) {
      report.add('3a', 'nothing drawn before the droid answers', lib.NOT_ASSESSED, 'the Outputs answered before the hold');
    } else {
      report.add('3a', 'the summary shows the waiting dots, and the table has no row yet', lib.verdict(waiting.summary === '' && waiting.dots.includes('...') && waiting.rows === 0), `"${waiting.summary}", dots ${waiting.dots}, ${waiting.rows} rows`);
      await page.screenshot({ path: `${ARTIFACTS}/table-waiting.png` });
    }
    await page.unroute('**/api/servo/outputs*', hold);
    release();

    // -----------------------------------------------------------------------
    // Answered
    // -----------------------------------------------------------------------
    await page.waitForFunction((summary) => / on \d+ outputs? · \d+ free/.test(document.querySelector(summary).textContent), SUMMARY, { timeout: 30000 });
    const deadline = Date.now() + 5000;
    while (!lastOutputs && Date.now() < deadline) await page.waitForTimeout(200);
    const outputs = lastOutputs?.outputs || [];
    const onOutput = new Map();
    outputs.forEach((output) => (output.parts || []).forEach((id) => onOutput.set(id, String(output.address))));

    if (selftest === 'missing') {
      await page.evaluate((pills) => document.querySelector(pills).remove(), PILLS);
    }
    if (selftest === 'freeword') {
      await page.evaluate((table) => {
        const taken = window.PAOutputs.list().find((output) => output.parts.length > 0);
        const entry = document.createElement('span');
        entry.dataset.free = taken.address;
        document.querySelector(`${table} .parts-free-list`).appendChild(entry);
      }, TABLE);
    }

    // 1b and 2, before anything on the page is pressed.
    const first = await page.evaluate(([table, rows, pills, summary]) => ({
      catalog: window.DroidParts.parts.filter((part) => part.half !== 'dome').map((part) => part.id),
      domeLink: window.DroidParts.parts.filter((part) => part.half === 'dome').map((part) => part.id),
      rows: [...document.querySelectorAll(rows)].map((row) => ({
        id: row.dataset.part,
        wired: row.classList.contains('is-wired'),
        bar: row.querySelector('[data-bar]') !== null,
        lit: row.querySelector('[data-bar] button.active')?.dataset.value || 'none',
      })),
      pills: [...document.querySelectorAll(pills)].map((pill) => pill.dataset.part),
      free: [...document.querySelectorAll(`${table} [data-free]`)].map((node) => node.dataset.free),
      heads: Object.fromEntries([...document.querySelectorAll(`${table} tbody[data-group]`)].map((body) =>
        [body.dataset.group, body.querySelector('.parts-group-count').firstChild.textContent])),
      summary: document.querySelector(summary).textContent,
    }), [TABLE, ROWS, PILLS, SUMMARY]);

    const places = (id) => first.rows.filter((row) => row.id === id).length + first.pills.filter((pill) => pill === id).length;
    const wrongPlace = first.catalog.filter((id) => {
      const row = first.rows.find((each) => each.id === id);
      return places(id) !== 1 || (onOutput.has(id) ? !row || !row.bar : !first.pills.includes(id));
    });
    report.add(
      '1b',
      'every body Part is in the table once: a row on an Output, a pill off one',
      lib.verdict(lastOutputs !== null && first.catalog.length > 0 && wrongPlace.length === 0),
      `${first.rows.length} rows and ${first.pills.length} pills for ${first.catalog.length} body Parts${wrongPlace.length ? `; wrong: ${wrongPlace.slice(0, 6).join(', ')}` : ''}`,
    );

    const wiredRows = first.rows.filter((row) => onOutput.has(row.id));
    const litWrong = wiredRows.filter((row) => !row.wired || (row.bar && row.lit !== onOutput.get(row.id)));
    report.add(
      '2a',
      'every Part on an Output is marked wired with that Output lit on its bar',
      lib.verdict(lastOutputs !== null && litWrong.length === 0),
      `${wiredRows.length} on an output${litWrong.length ? `; wrong: ${litWrong.map((row) => `${row.id} shows ${row.lit}`).join(', ')}` : ''}`,
    );
    const freeNow = outputs.filter((output) => (output.parts || []).length === 0).map((output) => String(output.address));
    report.add(
      '2b',
      'the free row lists exactly the Outputs no Part is on',
      lib.verdict(lastOutputs !== null && JSON.stringify(first.free) === JSON.stringify(freeNow)),
      `free row [${first.free.join(', ')}], the droid's free Outputs [${freeNow.join(', ')}]`,
    );
    const wiredCount = outputs.length - freeNow.length;
    const partCount = outputs.reduce((sum, output) => sum + (output.parts || []).length, 0);
    const expectHead = `${wiredCount} wired · ${freeNow.length} free`;
    const expectSummary = `${partCount} ${partCount === 1 ? 'part' : 'parts'} on ${wiredCount} ${wiredCount === 1 ? 'output' : 'outputs'} · ${freeNow.length} free`;
    report.add(
      '2c',
      'the board group\'s count and the summary match the droid\'s answer',
      lib.verdict(first.heads['board-outputs'] === expectHead && first.summary.startsWith(expectSummary)),
      `group "${first.heads['board-outputs']}", summary "${first.summary}"`,
    );

    // The dome's group, shown; and a row for every light Part still a pill.
    // Both are this page's own: nothing reaches the droid.
    await page.click(`${TABLE} [data-act="dome"]`);
    const lightPills = await page.evaluate((pills) => {
      const lights = window.DroidParts.parts.filter((part) => part.kind === 'light').map((part) => part.id);
      return [...document.querySelectorAll(pills)].map((pill) => pill.dataset.part).filter((id) => lights.includes(id));
    }, PILLS);
    for (const id of lightPills) await page.click(`${PILLS}[data-part="${id}"]`);

    if (selftest === 'domebutton') {
      await page.evaluate((table) => {
        document.querySelector(`${table} [data-dome-part] td`).appendChild(document.createElement('button'));
      }, TABLE);
    }
    const dome = await page.evaluate((table) => ({
      head: document.querySelector(`${table} tbody[data-group="dome-controller"] .parts-group-count`).firstChild.textContent,
      rows: [...document.querySelectorAll(`${table} [data-dome-part]`)].map((row) => ({
        id: row.dataset.domePart,
        pressable: row.querySelector('button, select, a, input') !== null,
        command: row.querySelector('.parts-command')?.textContent || '',
      })),
    }), TABLE);
    const domeWrong = first.domeLink.filter((id) => {
      const row = dome.rows.find((each) => each.id === id);
      return !row || row.pressable || row.command === '';
    });
    report.add(
      '1c',
      'every dome Part has a Dome Controller row with its command and nothing to press',
      lib.verdict(first.domeLink.length > 0 && domeWrong.length === 0 && dome.rows.length === first.domeLink.length &&
        dome.head === `${first.domeLink.length} ${first.domeLink.length === 1 ? 'part' : 'parts'}`),
      `${dome.rows.length} rows for ${first.domeLink.length} dome Parts, headed "${dome.head}"${domeWrong.length ? `; wrong: ${domeWrong.join(', ')}` : ''}` +
        ` (${dome.rows.slice(0, 2).map((row) => `${row.id} "${row.command}"`).join(', ')})`,
    );

    if (selftest === 'hide') {
      await page.evaluate((rows) => {
        document.querySelector(rows).style.display = 'none';
      }, ROWS);
    }
    const answered = await hiddenOnes(page);
    const sideways = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
    report.add('3b', 'no row or pill hidden once answered, and the page is not pushed sideways', lib.verdict(answered.total > 0 && answered.hidden.length === 0 && !sideways),
      `${answered.total} rows and pills, hidden: ${answered.hidden.join(', ') || 'none'}; sideways scroll: ${sideways}`);

    // -----------------------------------------------------------------------
    // Light-kind rows (#357)
    // -----------------------------------------------------------------------
    if (selftest === 'unlight' || selftest === 'position') {
      await page.evaluate(([table, which]) => {
        if (which === 'position') document.querySelector(`${table} thead tr`).insertAdjacentHTML('beforeend', '<th scope="col">Position</th>');
        if (which === 'unlight') document.querySelector(`${table} tr.partkind-light`)?.classList.remove('partkind-light');
      }, [TABLE, selftest]);
    }
    const kinds = await page.evaluate((table) => {
      const headers = [...document.querySelectorAll(`${table} thead th`)].map((th) => th.textContent.trim());
      const lightIds = window.DroidParts.parts.filter((part) => part.kind === 'light').map((part) => part.id);
      const drawnAway = '.outputs-bar, .outputs-now, .outputs-tick, .outputs-us, .outputs-width, .outputs-release, .outputs-throw, .outputs-motion';
      const rows = [...document.querySelectorAll(`${table} tr[data-part], ${table} tr[data-dome-part]`)].map((row) => ({
        id: row.dataset.part || row.dataset.domePart,
        light: lightIds.includes(row.dataset.part || row.dataset.domePart),
        classed: row.classList.contains('partkind-light'),
        tag: row.querySelector('.parts-kind')?.textContent.trim() || '',
        drawn: row.querySelector(drawnAway) !== null,
        micro: /µs/.test(row.textContent),
        border: getComputedStyle(row.querySelector('th')).borderLeftStyle,
      }));
      return { headers, lightIds, rows };
    }, TABLE);
    const lightRows = kinds.rows.filter((row) => row.light);
    const positionHeaders = kinds.headers.filter((text) => /position|release|travel|throw/i.test(text));
    const lightWrong = lightRows
      .map((row) => {
        const why = [];
        if (!row.classed) why.push('no partkind-light');
        if (row.tag !== 'light') why.push(`tag "${row.tag}"`);
        if (row.border !== 'dashed') why.push(`heading border ${row.border}`);
        if (row.drawn) why.push('a position/release cell');
        if (row.micro) why.push('a microsecond value');
        return why.length ? `${row.id}: ${why.join(', ')}` : null;
      })
      .filter(Boolean);
    const strayClass = kinds.rows.filter((row) => !row.light && row.classed).map((row) => row.id);
    report.add(
      '4',
      'light Parts carry partkind-light and the table has no position or release column',
      lib.verdict(lightRows.length > 0 && lightRows.length === kinds.lightIds.length && lightWrong.length === 0 && strayClass.length === 0 && positionHeaders.length === 0),
      `${lightRows.length} light rows of ${kinds.lightIds.length} light Parts; columns: ${kinds.headers.join(' | ')}` +
        (positionHeaders.length ? `; a position/release column: ${positionHeaders.join(', ')}` : '') +
        (lightWrong.length ? `; wrong: ${lightWrong.join('; ')}` : '') +
        (strayClass.length ? `; partkind-light on a Part that is not a light: ${strayClass.join(', ')}` : ''),
    );
    await page.screenshot({ path: `${ARTIFACTS}/table-answered.png`, fullPage: true });

    const blocked = lib.blockedWrites(writes);
    report.add('5', 'Wiring sent no write of its own', lib.verdict(blocked.length === 0), blocked.length ? [...new Set(blocked)].join(', ') : '');
  },
});
