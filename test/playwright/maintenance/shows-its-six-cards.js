// bench-auto: droid
// Maintenance shows exactly its six cards: Serial links, Diagnostics, Memory
// profiler, Backup, Guided Setup and Restart. Introduced by #404 (Setup split
// into Configuration and Maintenance).
//
// The count is the source's: data/maintenance.html has six top-level cards,
// and data/maintenance.js shows the Memory profiler card in every
// availability state (renderAvailability sets card.hidden = false), saying
// "not in this build" rather than disappearing.
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHY A REAL BROWSER. Visibility is computed style and layout.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/maintenance/shows-its-six-cards.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=card removes one card; the row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/maintenance';
const CARDS = ['Serial links', 'Diagnostics', 'Memory profiler', 'Backup', 'Guided Setup', 'Restart'];

lib.runCheck({
  rule: 'Maintenance shows its six cards',
  artifactDir: ARTIFACTS,
  selftests: ['card'],
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'maintenance');
    if (selftest === 'card') await page.evaluate(() => document.querySelector('#shell-content > .surface[data-surface="maintenance"] .card').remove());
    const cards = await lib.visibleCards(page, 'maintenance');
    await page.screenshot({ path: `${ARTIFACTS}/cards.png`, fullPage: true });
    report.add('a', `Maintenance shows exactly ${CARDS.join(', ')}`, lib.verdict(JSON.stringify(cards) === JSON.stringify(CARDS)),
      `${cards.length}: ${cards.join(' | ')}`);
  },
});
