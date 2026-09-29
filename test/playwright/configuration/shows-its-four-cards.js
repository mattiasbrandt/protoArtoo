// bench-auto: droid
// Configuration, on a droid whose guided Setup is not running, shows exactly
// its four cards: Body Controller, Droid Build, Hardware components and Droid
// identity. Introduced by #404 (Setup split into Configuration and
// Maintenance).
//
// The count is the source's: data/configuration.html has eight top-level
// cards, and data/setup.js hides the run's own four (#wizard-checking,
// #wizard-head, #wizard-wifi-card, #wizard-foot) and the end-of-run summary
// (#setup-summary) once the summary is dismissed or was never owed.
//
// PRECONDITION: guided Setup is not drawn and owes no summary, by
// data/setup.js's own rule (../_lib/checks.js guidedSetup): the run has ended
// and its summary was dismissed, or the droid was configured before the
// record existed. NOT ASSESSED otherwise, because the run's cards or the
// summary are then correctly on screen. Writes nothing.
//
// WHY A REAL BROWSER. Visibility is computed style and layout.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/configuration/shows-its-four-cards.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=card adds a fifth card; the row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/configuration';
const CARDS = ['Body Controller', 'Droid Build', 'Hardware components', 'Droid identity'];

lib.runCheck({
  rule: 'Configuration shows its four cards',
  artifactDir: ARTIFACTS,
  selftests: ['card'],
  precondition: async ({ page }) => {
    const setup = await lib.guidedSetup(page);
    if (!setup.known) return setup.why;
    if (setup.drawn) return `guided Setup is drawn over Configuration on this droid (${setup.why}), so its own cards are meant to show`;
    if (setup.summaryOwed) return `${setup.why} and its summary card is still owed, so it is meant to show`;
    return null;
  },
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'configuration');
    if (selftest === 'card') {
      await page.evaluate(() => {
        const card = document.createElement('div');
        card.className = 'card';
        card.innerHTML = '<h2>Selftest</h2>';
        document.querySelector('#shell-content > .surface[data-surface="configuration"]').appendChild(card);
      });
    }
    const cards = await lib.visibleCards(page, 'configuration');
    report.add('a', `Configuration shows exactly ${CARDS.join(', ')}`, lib.verdict(JSON.stringify(cards) === JSON.stringify(CARDS)),
      `${cards.length}: ${cards.join(' | ')}`);
  },
});
