// The Dashboard's log-level pill renders even when the droid's servo table
// cannot be read: it reads the config alone, and a failed Outputs read cannot
// take it down. Introduced by #422 and #423 (data/app.js loadLogLevel and
// loadOutputNames are separate sections).
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES. With every GET /api/servo/outputs aborted in the browser
// (at least one is, or the row says so), #log-level-pill reads one of the
// four level words - Error, Warning, Info, Debug - rather than its "..."
// placeholder.
//
// WHY A REAL BROWSER. The failure is a real aborted request through the real
// bootstrap sections.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/dashboard/log-level-pill-without-servo-table.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=placeholder puts "..." back on the pill; the row must
// FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/dashboard';
// data/app.js LOG_LEVELS labels.
const WORDS = ['Error', 'Warning', 'Info', 'Debug'];

lib.runCheck({
  rule: 'Dashboard log-level pill renders without the servo table',
  artifactDir: ARTIFACTS,
  selftests: ['placeholder'],
  run: async ({ page, report, selftest }) => {
    let aborted = 0;
    await page.route('**/api/servo/outputs*', (route) => {
      aborted += 1;
      return route.abort('failed');
    });
    await page.goto(`${lib.BASE_URL}/#home`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('#log-level-pill', { timeout: 20000 });
    await page.waitForFunction((words) => words.includes(document.getElementById('log-level-pill').textContent.trim()), WORDS, { timeout: 15000 }).catch(() => {});
    if (selftest === 'placeholder') await page.evaluate(() => { document.getElementById('log-level-pill').textContent = '...'; });
    const pill = (await page.textContent('#log-level-pill')).trim();
    await page.locator('#log-level-pill').screenshot({ path: `${ARTIFACTS}/log-level-pill.png` });
    report.add('a', 'The pill reads a level with the servo table aborted', lib.verdict(WORDS.includes(pill) && aborted > 0),
      `pill "${pill}"; ${aborted} servo-table read(s) aborted`);
  },
});
