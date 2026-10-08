// bench-auto: droid
// The Operator Shell's nav has no Setup entry: guided Setup is drawn over
// Configuration while a droid is not set up, and is never a destination of its
// own. Introduced by #404 (and #371, "Setup leaves the nav").
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES. No link in #shell-nav names Setup - neither in its text
// (words matched case-blind) nor in its route (data-surface-link or href) -
// and the nav has links at all.
//
// WHY A REAL BROWSER. The nav is written by the served data/shell.js from its
// SURFACES and ACTIVITY_GROUPS tables at boot.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/shell/nav-has-no-setup-entry.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=setup adds a Setup link to the nav; the row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/shell';

lib.runCheck({
  rule: 'The nav has no Setup entry',
  artifactDir: ARTIFACTS,
  selftests: ['setup'],
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'home');
    if (selftest === 'setup') {
      await page.evaluate(() => {
        const link = document.createElement('a');
        link.dataset.surfaceLink = 'setup';
        link.textContent = 'Setup';
        document.getElementById('shell-nav').appendChild(link);
      });
    }
    const links = await page.evaluate(() =>
      [...document.querySelectorAll('#shell-nav a')].map((link) => ({ to: link.dataset.surfaceLink || link.getAttribute('href') || '', text: link.textContent.trim() })));
    await page.locator('#shell-nav').screenshot({ path: `${ARTIFACTS}/nav.png` });
    const setup = links.filter((link) => /\bsetup\b/i.test(link.text) || /setup/i.test(link.to));
    report.add('a', 'No nav link names Setup', lib.verdict(links.length > 0 && setup.length === 0),
      setup.length ? setup.map((link) => `"${link.text}" -> ${link.to}`).join(', ') : `${links.length} links: ${links.map((link) => link.text).join(', ')}`);
  },
});
