// bench-auto: droid
// On Wiring, a dome link that is switched off draws no line: it rides no wire,
// as a free Output draws none (operator, 2026-09-29 on #411: "foot drive is
// now set to "not fitted" so why is then wiring drawing still listing it as
// wired?"). The lane is joined to its Component Toggle by name, case-folded
// (#350, data/wiring.js componentIndex, sheetWires). The filename is the
// rule's history: it read "switched off", drawn idle, until #411.
//
// PRECONDITION: the droid reports a protor2link Board Lane (GET /api/identity
// board_lanes) and its dome link is switched OFF (GET /api/config
// components.protoR2link.enabled false). NOT ASSESSED otherwise - the rule is
// about the switched-off lane. Writes nothing.
//
// WHAT IT PROVES. Once the sheet has painted (#wiring-wires-summary carries
// its count), there is no .wd-link[data-wire="protor2link"] in it, and the
// link's row of the parts table - the one place the page says what is wired
// (#463) - is not marked wired and reads "not fitted".
//
// WHY A REAL BROWSER. The sheet is generated from three live reads.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wiring/dome-link-off-reads-switched-off.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-tests, each must FAIL the row: SELFTEST=drawn puts a line for the lane
// back into the sheet; SELFTEST=wired marks the link's table row wired.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/wiring';

lib.runCheck({
  rule: 'Wiring: a dome link switched off draws no line',
  artifactDir: ARTIFACTS,
  selftests: ['drawn', 'wired'],
  precondition: async ({ page }) => {
    const lanes = (await lib.readJson(page, '/api/identity')).json?.board_lanes || {};
    if (!lanes.protor2link) return 'this board reports no protor2link lane';
    const toggle = (await lib.readJson(page, '/api/config')).json?.components?.protoR2link;
    if (!toggle || toggle.enabled !== false) return `the dome link is not switched off (components.protoR2link ${JSON.stringify(toggle)})`;
    return null;
  },
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'wiring');
    await page.waitForFunction(() => /wire/.test(document.querySelector('#wiring-wires-summary')?.textContent || ''), null, { timeout: 15000 });
    if (selftest === 'drawn') {
      await page.evaluate(() => {
        const line = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        line.setAttribute('class', 'wd-link');
        line.setAttribute('data-wire', 'protor2link');
        document.querySelector('#wiring-wires').appendChild(line);
      });
    }
    await page.waitForSelector('#wiring-parts-table tr[data-link="protor2link"]', { timeout: 15000 });
    if (selftest === 'wired') {
      await page.evaluate(() => document.querySelector('#wiring-parts-table tr[data-link="protor2link"]').classList.add('is-wired'));
    }
    const lane = await page.evaluate(() => {
      const row = document.querySelector('#wiring-parts-table tr[data-link="protor2link"]');
      return {
        line: document.querySelectorAll('.wd-link[data-wire="protor2link"]').length,
        wired: row.classList.contains('is-wired'),
        says: row.children[2].textContent,
        summary: document.querySelector('#wiring-wires-summary').textContent,
      };
    });
    await page.locator('#wiring-wires').screenshot({ path: `${ARTIFACTS}/wires.png` });
    report.add('a', 'The switched-off dome link has no line, and its table row reads not fitted', lib.verdict(lane.line === 0 && !lane.wired && lane.says === 'not fitted'),
      `${lane.line} line(s); table row wired: ${lane.wired}, says "${lane.says}"; summary "${lane.summary}"`);
  },
});
