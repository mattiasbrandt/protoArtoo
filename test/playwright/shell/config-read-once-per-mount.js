// Opening a surface asks the droid for its config at most once: Wiring,
// Lights, Servos, Parts and the Dashboard each GET /api/config no more than
// once on their first mount. Every read spends one of the controller's few
// client slots. Introduced by #415 (data/outputs.js reads the config and the
// servo table as one snapshot, and a surface takes the config from it).
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES. The Dashboard is loaded (its count includes the shell's own
// boot, which is part of opening it), then Wiring, Lights, Servos and Parts
// are each opened for the first time through the nav; from the press to the
// surface settling, each sees at most one GET /api/config.
//
// WHY A REAL BROWSER. The real network log across real mounts.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/shell/config-read-once-per-mount.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=reread reads the config twice more on every mount;
// every row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/shell';
const SURFACES = [
  { page: 'home', name: 'Dashboard' },
  { page: 'wiring', name: 'Wiring' },
  { page: 'lights', name: 'Lights' },
  { page: 'servo', name: 'Servos' },
  { page: 'parts', name: 'Parts' },
];

lib.runCheck({
  rule: 'A surface reads GET /api/config at most once when it opens',
  artifactDir: ARTIFACTS,
  selftests: ['reread'],
  run: async ({ page, report, selftest }) => {
    const reads = [];
    page.on('request', (request) => {
      if (request.method() === 'GET' && lib.pathOf(request.url()) === '/api/config') reads.push(Date.now());
    });
    for (const [index, surface] of SURFACES.entries()) {
      const from = Date.now();
      if (index === 0) await lib.loadSurface(page, surface.page);
      else await lib.openSurface(page, surface.page);
      if (selftest === 'reread') {
        await page.evaluate(() => Promise.all([1, 2].map(() => fetch('/api/config', { cache: 'no-store' }).then((response) => response.text()))));
      }
      const count = reads.filter((at) => at >= from).length;
      report.add(String(index + 1), `${surface.name}: at most one GET /api/config on its first mount`, lib.verdict(count <= 1),
        `${count} read(s)${index === 0 ? ', from the page load (the shell\'s boot included)' : ''}`);
    }
  },
});
