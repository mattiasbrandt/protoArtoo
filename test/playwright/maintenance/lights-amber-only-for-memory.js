// bench-auto: droid
// On Maintenance a light goes amber only for memory, and nothing on the
// surface calls a reading "stale": a link the droid has not reported is grey,
// never amber, and how old the readings are is the Status Plate's to say.
// Introduced by #402 (GLOSSARY.md "Status Color", "Health Signal").
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES, once the droid has reported (the uptime readout has left
// "..."):
//   a  every .indicator.warn on the surface is one of the three memory lights
//      (#diag-heap-free-light, -min-, -largest-): amber is "degraded and you
//      can act on it", and data/health_signals.js's word tables have no amber
//      row for either link;
//   b  the surface's text does not contain "stale".
//
// WHY A REAL BROWSER. The lights are painted from the live status stream.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/maintenance/lights-amber-only-for-memory.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=amber lights the Sound link amber and writes "Stale
// data" into the status line; a and b must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/maintenance';

lib.runCheck({
  rule: 'Maintenance: amber only for memory, and no "stale"',
  artifactDir: ARTIFACTS,
  selftests: ['amber'],
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'maintenance');
    await page.waitForFunction(() => (document.getElementById('diag-uptime')?.textContent || '') !== '', null, { timeout: 15000 });
    if (selftest === 'amber') {
      await page.evaluate(() => {
        document.getElementById('serial-s2-light').className = 'indicator warn';
        document.getElementById('serial-status-line').textContent = 'Stale data';
      });
    }
    const seen = await page.evaluate(() => {
      const root = document.querySelector('#shell-content > .surface[data-surface="maintenance"]');
      return { amber: [...root.querySelectorAll('.indicator.warn')].map((node) => node.id || node.className), stale: (root.innerText.match(/[^\n]*stale[^\n]*/i) || [''])[0] };
    });
    await page.screenshot({ path: `${ARTIFACTS}/lights.png` });
    const wrong = seen.amber.filter((id) => !/^diag-heap-(free|min|largest)-light$/.test(id));
    report.add('a', 'Amber only on a memory light', lib.verdict(wrong.length === 0), `amber: ${seen.amber.join(', ') || 'none'}`);
    report.add('b', 'Nothing says "stale"', lib.verdict(!seen.stale), seen.stale ? `"${seen.stale.trim()}"` : 'no such text');
  },
});
