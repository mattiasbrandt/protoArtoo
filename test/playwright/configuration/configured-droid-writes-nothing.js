// bench-auto: droid
// Opening Configuration on a droid that is already configured draws no guided
// Setup and writes nothing - by its own address or by the old /#setup one.
// Introduced by #351 (guided Setup) and #404 (Setup drawn over Configuration).
//
// PRECONDITION, read before the app is opened (from /fw-version.json, a page
// on the droid's origin that runs none of the app): the droid is configured
// in data/setup.js's own sense (../_lib/checks.js guidedSetup) - its guided
// Setup run has ended, or it has no record at all and something is switched
// on or an Output is ticked wired (setup.js `grandfathered`). On any other
// droid guided Setup is DESIGNED to draw and to save which questions it
// showed, so the rule does not apply: NOT ASSESSED.
//
// WHAT IT PROVES. Two page loads, /#setup then /#configuration. On each:
//   a  guided Setup is not drawn - #wizard-head, #wizard-foot and the "Setup"
//      title stay hidden, the reading card #wizard-checking has gone, the
//      "Configuration" title and Configuration's cards are on screen;
//   b  the page tried no POST /api/config (the run's visited record,
//      setup.js saveVisited, goes 400 ms after a run draws);
//   c  the page tried no write of any other kind.
// The guard RECORDS every write before blocking it, so b and c read what the
// page tried, not what arrived. Nothing is allowed through.
//
// Run first after a flash, before any other browser visit, it is also the
// check that the FIRST visit writes nothing.
//
// WHAT IT DOES NOT DO. It presses nothing. The designed first-run path on a
// droid that is not set up is the web suite's (test/test_web/).
//
// WHY A REAL BROWSER. A real page load, the shell's alias routing and the
// debounce timers running for real, against the droid's own answer.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/configuration/configured-droid-writes-nothing.js
//   BASE_URL=http://<board>   the controller (default http://10.0.0.22)
//   STEP=1                    wait for Enter after each visit
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=wizard - once the precondition is read, the fixture's
// droid answers as one whose run has not been made, so the run draws and
// saves; rows a and b must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/configuration';
// Past the visited record's 400 ms debounce (data/setup.js markVisited) and
// the save's own round trip, so a run that drew and saved is seen.
const WATCH_MS = Number(process.env.WATCH_MS || 3000);

lib.runCheck({
  rule: 'Opening Configuration on a configured droid writes nothing',
  artifactDir: ARTIFACTS,
  selftests: ['wizard'],
  precondition: async ({ page }) => {
    const setup = await lib.guidedSetup(page);
    if (!setup.known) return setup.why;
    console.log(`guidedSetup: ${JSON.stringify(setup.record)} - ${setup.why}`);
    return setup.drawn ? `the droid is not set up (${setup.why}), so guided Setup is designed to draw and save` : null;
  },
  run: async ({ page, writes, fixture, report, selftest }) => {
    if (selftest === 'wizard') fixture.state.config.guidedSetup = { run: 'not-run', visited: [], recorded: true, summaryDone: false };
    for (const [id, address] of [['1', 'setup'], ['2', 'configuration']]) {
      const label = `/#${address}`;
      const since = writes.length;
      await lib.landNeutral(page);
      await page.goto(`${lib.BASE_URL}/#${address}`, { waitUntil: 'domcontentloaded' });
      await page.waitForSelector('body[data-page="configuration"]', { timeout: 20000 });
      const decided = await page
        .waitForFunction(() => document.getElementById('wizard-checking')?.classList.contains('hidden'), null, { timeout: 20000 })
        .then(() => true, () => false);
      await page.waitForTimeout(WATCH_MS);
      const layout = await page.evaluate(() => {
        const shown = (target) => Boolean(document.getElementById(target)?.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true }));
        return {
          hash: window.location.hash,
          wizard: shown('wizard-head') || shown('wizard-foot'),
          setupTitle: shown('setup-title'),
          configurationTitle: shown('configuration-title'),
        };
      });
      const cards = await lib.visibleCards(page, 'configuration');
      await page.screenshot({ path: `${ARTIFACTS}/configured-visit-${address}.png`, fullPage: true });
      const during = writes.slice(since);
      const configPosts = during.filter((entry) => entry.method === 'POST' && entry.path === '/api/config');
      const others = during.filter((entry) => !configPosts.includes(entry));
      report.add(`${id}a`, `${label}: Configuration is shown, guided Setup is not drawn`,
        lib.verdict(decided && !layout.wizard && !layout.setupTitle && layout.configurationTitle && cards.length > 0),
        `${decided ? '' : 'never left "reading this droid"; '}run ${layout.wizard ? 'DRAWN' : 'not drawn'}, "Setup" title ${layout.setupTitle ? 'SHOWN' : 'hidden'}, ` +
          `"Configuration" title ${layout.configurationTitle ? 'shown' : 'HIDDEN'}, address ${layout.hash}, cards: ${cards.join(' | ') || 'none'}`);
      report.add(`${id}b`, `${label}: no POST /api/config`, lib.verdict(configPosts.length === 0),
        configPosts.map(lib.describeWrite).join(' ; ') || 'none tried');
      report.add(`${id}c`, `${label}: no write of any other kind`, lib.verdict(others.length === 0),
        others.map(lib.describeWrite).join(' ; ') || 'none tried');
      await lib.step(`${label} checked.`);
    }
  },
});
