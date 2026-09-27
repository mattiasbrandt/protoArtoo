// Pressing "Save settings" on WiFi runs the save: the page does not throw.
// Introduced by #344 (WiFi mounted inside the Operator Shell).
//
// PRECONDITION: none beyond a droid that answers. Writes nothing: the save,
// if the page gets as far as sending it (POST /api/wifi), is recorded and
// aborted in the browser, so the droid's network settings are never touched.
// The page's error line then says the save failed, which is expected here.
//
// WHAT IT PROVES. Once the page has read its settings and enabled "Save
// settings", one press throws no uncaught error on the page (Playwright's
// pageerror). The row lists what the page tried to send.
//
// WHY A REAL BROWSER. It is the real form submit, through the real bootstrap.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wifi/save-does-not-throw.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=throw makes the submit handler throw; the row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/wifi';

lib.runCheck({
  rule: 'WiFi Save does not throw',
  artifactDir: ARTIFACTS,
  selftests: ['throw'],
  run: async ({ page, writes, report, pageErrors, selftest }) => {
    await lib.loadSurface(page, 'wifi');
    await page.waitForSelector('#wifi-save-settings-button:not([disabled])', { timeout: 15000 });
    if (selftest === 'throw') {
      await page.evaluate(() => {
        document.getElementById('wifi-save-settings-button').form.addEventListener('submit', () => {
          throw new Error('selftest: the save handler threw');
        });
      });
    }
    const errorsBefore = pageErrors.length;
    const since = writes.length;
    await page.click('#wifi-save-settings-button');
    await page.waitForTimeout(2000);
    const thrown = pageErrors.slice(errorsBefore);
    const said = await page.textContent('#wifi-settings-feedback').catch(() => '');
    await page.screenshot({ path: `${ARTIFACTS}/after-save.png` });
    report.add('a', 'One press of Save throws nothing', lib.verdict(thrown.length === 0),
      `${thrown.length ? `threw: ${thrown.join(' | ')}` : 'nothing thrown'}; the page says "${said}"; tried: ${writes.slice(since).map(lib.describeWrite).join(' | ') || 'no write'}`);
  },
});
