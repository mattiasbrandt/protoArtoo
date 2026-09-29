// bench-auto: droid
// On the Dashboard's Health card a signal goes amber only for memory, and no
// row calls a reading "stale": unknown, never asked and not fitted read grey,
// and how old the readings are is the Status Plate's to say. Introduced by
// #402 (CONTEXT.md "Status Color", "Health Signal").
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES, once the droid has reported (the Health subtitle carries its
// counts):
//   a  every .indicator.warn in #health-grid is #h-heap - the only signal
//      data/health_signals.js can make amber (evaluateHeap's "Low");
//   b  the Health card's text does not contain "stale" (applyStaleHealth, which
//      painted every row "Stale data", is gone and must stay gone).
//
// WHY A REAL BROWSER. The rows are painted from the live status stream.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/dashboard/health-amber-only-for-memory.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=amber lights the RC Receiver row amber and writes
// "Stale data" on it; a and b must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/dashboard';

lib.runCheck({
  rule: 'Dashboard Health: amber only for memory, and no "stale"',
  artifactDir: ARTIFACTS,
  selftests: ['amber'],
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'home');
    await page.waitForFunction(() => /·/.test(document.getElementById('health-summary')?.textContent || ''), null, { timeout: 15000 });
    if (selftest === 'amber') {
      await page.evaluate(() => {
        document.getElementById('h-sbus').className = 'indicator warn';
        document.getElementById('ht-sbus').textContent = 'Stale data';
      });
    }
    const seen = await page.evaluate(() => {
      const card = document.getElementById('health-grid').closest('.card');
      return {
        summary: document.getElementById('health-summary').textContent,
        amber: [...document.querySelectorAll('#health-grid .indicator.warn')].map((node) => node.id),
        stale: (card.innerText.match(/[^\n]*stale[^\n]*/i) || [''])[0],
      };
    });
    await page.locator('#health-grid').screenshot({ path: `${ARTIFACTS}/health.png` });
    const wrong = seen.amber.filter((id) => id !== 'h-heap');
    report.add('a', 'Amber only on the memory row', lib.verdict(wrong.length === 0), `${seen.summary}; amber: ${seen.amber.join(', ') || 'none'}`);
    report.add('b', 'No row says "stale"', lib.verdict(!seen.stale), seen.stale ? `"${seen.stale.trim()}"` : 'no such text');
  },
});
