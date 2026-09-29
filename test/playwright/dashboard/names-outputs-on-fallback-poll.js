// bench-auto: droid
// The Dashboard's Components card names every Output the droid reports, by
// the name its board prints, even when the browser has no event stream and
// the status arrives on the fallback poll. Introduced by #412 (#415 moved the
// names onto GET /api/servo/outputs).
//
// PRECONDITION: none beyond a droid that answers. NOT ASSESSED when the droid
// reports no enabled Output in its status frame (there is then nothing to
// name). Writes nothing.
//
// HOW THE STREAM IS TAKEN AWAY. window.EventSource is set to undefined before
// any page script runs, so data/live_reading.js runs its one /api/status poll
// every 5 s (isSupported() false), and /api/events is aborted in the browser
// as well. The droid sees ordinary GETs.
//
// WHAT IT PROVES.
//   a  The poll is what is feeding the page: EventSource is gone and at least
//      two GET /api/status went out (the boot read and a poll).
//   b  Every Output GET /api/servo/outputs lists with a stored id that the
//      last status frame carries is a <dt> on the Components card, under its
//      name (data/app.js adoptOutputLabels, renderComponentStatus).
//
// WHY A REAL BROWSER. Real timers drive the poll, and the names arrive by a
// different read than the status they are joined to.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/dashboard/names-outputs-on-fallback-poll.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=blank blanks the card's names; b must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/dashboard';

lib.runCheck({
  rule: 'Dashboard names its Outputs on the fallback poll',
  artifactDir: ARTIFACTS,
  selftests: ['blank'],
  run: async ({ page, report, selftest }) => {
    await lib.landNeutral(page);
    const table = (await lib.readJson(page, '/api/servo/outputs')).json?.outputs || [];
    const statusReads = [];
    let lastStatus = null;
    page.on('request', (request) => {
      if (lib.pathOf(request.url()) === '/api/status') statusReads.push(Date.now());
    });
    page.on('response', async (response) => {
      if (lib.pathOf(response.url()) !== '/api/status' || !response.ok()) return;
      lastStatus = await response.json().catch(() => lastStatus);
    });
    await page.addInitScript(() => {
      window.EventSource = undefined;
    });
    await page.route('**/api/events*', (route) => route.abort('blockedbyclient'));
    await page.goto(`${lib.BASE_URL}/#home`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('#shell-estop-button', { timeout: 20000 });
    const deadline = Date.now() + 15000;
    while (statusReads.length < 2 && Date.now() < deadline) await page.waitForTimeout(500);
    await page.waitForTimeout(1500);
    const supported = await page.evaluate(() => window.PAStatusStream.isSupported());
    report.add('a', 'The fallback poll is feeding the page', lib.verdict(!supported && statusReads.length >= 2),
      `EventSource ${supported ? 'STILL THERE' : 'gone'}; ${statusReads.length} GET /api/status`);

    const expected = table
      .filter((row) => typeof row.id === 'string' && row.id !== '' && lastStatus && Object.prototype.hasOwnProperty.call(lastStatus, row.id))
      .map((row) => row.name || row.address);
    if (selftest === 'blank') await page.evaluate(() => document.querySelectorAll('#component-status-grid dt').forEach((dt) => { dt.textContent = ''; }));
    const shown = await page.evaluate(() => [...document.querySelectorAll('#component-status-grid dt')].map((dt) => dt.textContent.trim()));
    await page.screenshot({ path: `${ARTIFACTS}/fallback-poll-components.png`, fullPage: true });
    if (expected.length === 0) {
      report.add('b', 'Every reported Output is named on the Components card', lib.NOT_ASSESSED, 'the status frame carries no enabled Output');
      return;
    }
    const missing = expected.filter((name) => !shown.includes(name));
    report.add('b', 'Every reported Output is named on the Components card', lib.verdict(missing.length === 0),
      `expected ${expected.join(', ')}; the card: ${shown.join(', ') || 'empty'}${missing.length ? `; MISSING ${missing.join(', ')}` : ''}`);
  },
});
