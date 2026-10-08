// bench-auto: droid estop=latched
// A latched estop has let go of every enabled Output: the droid reports each
// one with no pulse and "estop" as the reason, and Servos says "Limp - the
// estop let go" on its row. Introduced by #364 and #421 (ADR 0043: a stop
// releases every Output rather than choosing a position for any).
//
// PRECONDITION: GET /api/status says the estop is LATCHED, and at least one
// Output is enabled in ServoTask's sense - switchable, ticked wired, its wire
// carrying no light (src/tasks/servo_task.cpp isOutputEnabled; releaseAllOutputs
// releases exactly those). NOT ASSESSED otherwise. Never releases the estop.
// Writes nothing.
//
// WHAT IT PROVES, for every enabled Output:
//   a  GET /api/servo/outputs reports commandedUs null and limp "estop";
//   b  its row on Servos reads "Limp - the estop let go" in the release cell
//      (data/outputs.js LIMP_WORDS, data/servo.js paintOutputRow). Servos
//      lists only the Outputs with a Part on them (#411), so b is read on
//      those; on a droid the tick follows the Parts, so they are every
//      enabled Output, and the fixture's ARM2 - ticked with nothing on it -
//      is the state a start now clears.
//
// WHY A REAL BROWSER. The row's word is data/outputs.js's answer to the droid's
// own table, painted on the real surface.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/servo/estop-leaves-outputs-limp.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js;
// the fixture's droid starts latched).
// Self-test: SELFTEST=pulsing has the fixture's droid leave one enabled Output
// pulsing through the latch; a and b must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/servo';
const LIMP_ESTOP = 'Limp - the estop let go';
const enabledIn = (table) => table.filter((row) => row.switchable === true && row.wired === true && row.component !== 'rgb');

lib.runCheck({
  rule: 'A latched estop has let go of every enabled Output',
  artifactDir: ARTIFACTS,
  fixture: { estop: true },
  selftests: ['pulsing'],
  precondition: async (ctx) => {
    const estop = await lib.estopMustBe(true)(ctx);
    if (estop) return estop;
    const table = (await lib.readJson(ctx.page, '/api/servo/outputs')).json?.outputs || [];
    return enabledIn(table).length ? null : `no enabled Output (${table.length} rows: ${table.map((row) => `${row.name || row.address} wired=${row.wired} ${row.component}`).join(', ')})`;
  },
  run: async ({ page, fixture, report, selftest }) => {
    if (selftest === 'pulsing') {
      Object.assign(enabledIn(fixture.state.outputs)[0], { commandedUs: 1500, targetUs: 1500, limp: 'off' });
    }
    const table = (await lib.readJson(page, '/api/servo/outputs')).json?.outputs || [];
    const enabled = enabledIn(table);
    const wrong = enabled.filter((row) => row.commandedUs !== null || row.limp !== 'estop');
    report.add('a', 'The droid reports every enabled Output limp, reason "estop"', lib.verdict(wrong.length === 0),
      wrong.map((row) => `${row.name || row.address}: commandedUs ${row.commandedUs}, limp "${row.limp}"`).join('; ') || enabled.map((row) => row.name || row.address).join(', '));
    await lib.loadSurface(page, 'servo');
    await page.waitForSelector('.outputs-row[data-output]', { timeout: 15000 });
    const cells = await page.evaluate(() =>
      Object.fromEntries([...document.querySelectorAll('.outputs-row[data-output]')].map((row) => [row.dataset.output, row.querySelector('.outputs-release')?.textContent || ''])));
    await page.locator('#outputs-card').screenshot({ path: `${ARTIFACTS}/estop-limp-rows.png` });
    const listed = enabled.filter((row) => (row.parts || []).length > 0);
    const off = listed.filter((row) => cells[row.address] !== LIMP_ESTOP);
    report.add('b', `Servos reads "${LIMP_ESTOP}" on every enabled Output it lists`, lib.verdict(listed.length > 0 && off.length === 0),
      off.map((row) => `${row.name || row.address}: "${cells[row.address]}"`).join('; ') || listed.map((row) => `${row.name || row.address}: "${cells[row.address]}"`).join('; '));
  },
});
