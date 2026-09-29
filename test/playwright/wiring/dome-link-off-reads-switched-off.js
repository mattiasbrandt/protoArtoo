// bench-auto: droid
// On Wiring, a dome link that is switched off reads "switched off" on its
// lane, drawn idle - never as a live wire. Introduced by #350 (the lane is
// joined to its Component Toggle by name, case-folded: data/wiring.js
// componentIndex, laneWire).
//
// PRECONDITION: the droid reports a protor2link Board Lane (GET /api/identity
// board_lanes) and its dome link is switched OFF (GET /api/config
// components.protoR2link.enabled false). NOT ASSESSED otherwise - the rule is
// about the switched-off lane. Writes nothing.
//
// WHAT IT PROVES. The lane's wire in the sheet, .wd-link[data-wire="protor2link"],
// is drawn idle (.is-idle) and its role reads exactly "switched off".
//
// WHY A REAL BROWSER. The sheet is generated from three live reads.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wiring/dome-link-off-reads-switched-off.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=live rewrites the lane's role as a live one; the row
// must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/wiring';

lib.runCheck({
  rule: 'Wiring: a dome link switched off reads "switched off"',
  artifactDir: ARTIFACTS,
  selftests: ['live'],
  precondition: async ({ page }) => {
    const lanes = (await lib.readJson(page, '/api/identity')).json?.board_lanes || {};
    if (!lanes.protor2link) return 'this board reports no protor2link lane';
    const toggle = (await lib.readJson(page, '/api/config')).json?.components?.protoR2link;
    if (!toggle || toggle.enabled !== false) return `the dome link is not switched off (components.protoR2link ${JSON.stringify(toggle)})`;
    return null;
  },
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'wiring');
    await page.waitForSelector('.wd-link[data-wire="protor2link"]', { timeout: 15000 });
    if (selftest === 'live') await page.evaluate(() => { document.querySelector('.wd-link[data-wire="protor2link"] .wd-role').textContent = 'serial, both ways'; });
    const lane = await page.evaluate(() => {
      const link = document.querySelector('.wd-link[data-wire="protor2link"]');
      return { role: link.querySelector('.wd-role')?.textContent, note: link.querySelector('.wd-note')?.textContent, idle: link.classList.contains('is-idle') };
    });
    await page.locator('#wiring-wires').screenshot({ path: `${ARTIFACTS}/wires.png` });
    report.add('a', 'The dome link lane reads "switched off", drawn idle', lib.verdict(lane.role === 'switched off' && lane.idle),
      `role "${lane.role}", note "${lane.note}", ${lane.idle ? 'idle' : 'drawn LIVE'}`);
  },
});
