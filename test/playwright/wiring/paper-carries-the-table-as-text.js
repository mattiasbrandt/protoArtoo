// bench-auto: droid
// Printed, Wiring carries its parts table as text and nothing to press, and
// the page itself never shows that printed copy. Introduced by #463 (operator,
// 2026-10-01: "the printed copy should not be listed on the actual page").
// The text copy is the generator's (data/wiring.js partsSheetHtml), written
// into a block only paper shows; data/style.css swaps it for the table's
// controls under @media print.
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES.
//   a  On the screen the text copy, the "made" line and the product cards'
//      plate are not rendered, and the table a builder sets the wiring in is.
//   b  Under print media nothing that can be pressed is rendered - no button,
//      link, select, input or dialog - and neither is Answers and readings,
//      whose every value is live.
//   c  Under print media the text copy is rendered, with one row per Part on
//      an Output and one "free" row, as GET /api/servo/outputs answered.
//   d  Under print media the sections come in the sheet's order: the wires,
//      Parts on outputs, Power wiring, then Product wiring where the image
//      carries the cards.
//
// WHY A REAL BROWSER. Which rule wins under @media print is the cascade's
// answer, and mini_dom (test/test_web/helpers/mini_dom.js) has no CSS engine.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wiring/paper-carries-the-table-as-text.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-tests: SELFTEST=control leaves the table's controls rendered on paper
// (b must FAIL); SELFTEST=onscreen shows the text copy on the screen (a must
// FAIL).
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/wiring';

// What is rendered of the surface right now, in the media the page is in.
const rendered = (page) =>
  page.evaluate(() => {
    const shown = (node) => Boolean(node) && node.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true });
    const surface = document.querySelector('.surface[data-surface="wiring"]');
    return {
      copy: shown(document.getElementById('wiring-parts-sheet')),
      made: shown(document.getElementById('wiring-made')),
      plate: shown(document.getElementById('wiring-products-card')),
      table: shown(document.querySelector('#wiring-parts-table table')),
      lineup: shown(document.querySelector('.wiring-lineup-card')),
      pressable: [...document.querySelectorAll('button, a, select, input, dialog')].filter(shown).map((node) => node.id || node.className || node.tagName),
      sections: [...surface.querySelectorAll('.sect h2')].filter(shown).map((node) => node.textContent),
      rows: [...document.querySelectorAll('#wiring-parts-sheet tr')].map((row) => [...row.children].map((cell) => cell.textContent.trim())),
      cards: document.getElementById('wiring-product-cards') !== null && document.querySelectorAll('#wiring-products .wcard').length > 0,
    };
  });

lib.runCheck({
  rule: 'Printed, Wiring carries its parts table as text and nothing to press',
  artifactDir: ARTIFACTS,
  selftests: ['control', 'onscreen'],
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'wiring');
    await page.waitForSelector('#wiring-save[aria-disabled="false"]', { timeout: 15000 });
    await page.waitForSelector('#wiring-parts-table table', { timeout: 15000 });
    const outputs = (await lib.readJson(page, '/api/servo/outputs')).json?.outputs || [];
    if (selftest === 'control') await page.addStyleTag({ content: '@media print { body[data-page="wiring"] .parts-table-wrap { display: block !important; } }' });
    if (selftest === 'onscreen') await page.addStyleTag({ content: '.print-only { display: block; }' });

    const screen = await rendered(page);
    report.add('a', 'On the screen: the table to set the wiring in, and no printed copy', lib.verdict(screen.table && !screen.copy && !screen.made && !screen.plate),
      `table ${screen.table}; text copy ${screen.copy}, made line ${screen.made}, cards plate ${screen.plate}`);

    await page.emulateMedia({ media: 'print' });
    await page.waitForTimeout(300);
    const paper = await rendered(page);
    await page.screenshot({ path: `${ARTIFACTS}/paper.png`, fullPage: true });
    report.add('b', 'On paper: nothing to press, and no live reading', lib.verdict(paper.pressable.length === 0 && !paper.lineup && !paper.table),
      `pressable: ${[...new Set(paper.pressable)].slice(0, 6).join(', ') || 'none'}; Answers and readings ${paper.lineup}; the table's controls ${paper.table}`);

    const wired = outputs.reduce((sum, output) => sum + (output.parts || []).length, 0);
    const free = outputs.filter((output) => (output.parts || []).length === 0).length;
    const partRows = paper.rows.filter((cells) => cells.length === 4 && cells[1] !== 'Part' && cells[1] !== 'free');
    const board = partRows.slice(0, wired);
    const freeRow = paper.rows.find((cells) => cells[1] === 'free');
    const freeListed = freeRow && freeRow[2] !== 'none' ? freeRow[2].split(', ').length : 0;
    report.add('c', 'On paper: the table as text, a row per Part on an Output and one free row', lib.verdict(paper.copy && paper.made && board.length === wired && Boolean(freeRow) && freeListed === free),
      `text copy ${paper.copy}; ${partRows.length} rows after the head, ${wired} Parts on Outputs; free row "${freeRow ? freeRow[2] : 'missing'}" for ${free} free Outputs`);

    const expected = ['The wires', 'Parts on outputs', 'Power wiring', ...(paper.cards ? ['Product wiring'] : [])];
    report.add('d', 'On paper: the wires, the table, the power, then the product cards', lib.verdict(JSON.stringify(paper.sections) === JSON.stringify(expected)),
      `${paper.sections.join(' | ')}${paper.cards ? '' : ' (this image carries no product cards)'}`);
    await page.emulateMedia({ media: 'screen' });
  },
});
