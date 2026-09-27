// Maintenance offers "Restore the ticked parts" only once a backup file has
// been chosen: before that the act and its ticks are not on screen at all.
// Introduced by #399 (the act was on screen, enabled, and did nothing).
//
// PRECONDITION: none beyond a droid that answers. Writes nothing: the guard
// lets no write through, and nothing is restored - the file chosen is a
// schema-only backup made here, and "Restore" is never pressed.
//
// WHAT IT PROVES.
//   a  Before a file is chosen, #restore-btn-row, its button and the tick list
//      #restore-sections are not on screen (data/style.css .button-row[hidden]
//      beats the grid rule that once showed it; data/maintenance.js
//      showRestorePanel).
//   b  Once a file is chosen, the act and its button are on screen, and
//      choosing the file sent nothing.
//
// WHY A REAL BROWSER. "Not on screen" is the cascade's answer - an author
// display rule beating [hidden] is exactly the defect - and mini_dom has no
// cascade.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/maintenance/restore-hidden-until-file-chosen.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=shown unhides the act before a file is chosen; a must
// FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/maintenance';

const onScreen = (page) =>
  page.evaluate(() => {
    const shown = (id) => document.getElementById(id).checkVisibility({ checkVisibilityCSS: true });
    return { act: shown('restore-btn-row'), button: shown('backup-restore-btn'), ticks: shown('restore-sections') };
  });

lib.runCheck({
  rule: 'Maintenance offers Restore only once a backup file is chosen',
  artifactDir: ARTIFACTS,
  selftests: ['shown'],
  run: async ({ page, writes, report, selftest }) => {
    await lib.loadSurface(page, 'maintenance');
    if (selftest === 'shown') await page.evaluate(() => { document.getElementById('restore-btn-row').hidden = false; });
    const before = await onScreen(page);
    report.add('a', 'Before a file: no restore act, no ticks', lib.verdict(!before.act && !before.button && !before.ticks),
      `act ${before.act ? 'SHOWN' : 'hidden'}, button ${before.button ? 'SHOWN' : 'hidden'}, ticks ${before.ticks ? 'SHOWN' : 'hidden'}`);
    const since = writes.length;
    await page.setInputFiles('#backup-file-input', {
      name: 'schema-only-backup.json',
      mimeType: 'application/json',
      buffer: Buffer.from(JSON.stringify({ schema: 1, generated: new Date().toISOString(), fw_version: 'check' })),
    });
    await page.waitForFunction(() => !document.getElementById('restore-btn-row').hidden, null, { timeout: 5000 }).catch(() => {});
    const after = await onScreen(page);
    const sent = writes.slice(since);
    await page.locator('#backup-file-trigger').evaluate((node) => node.closest('.card').scrollIntoView());
    await page.screenshot({ path: `${ARTIFACTS}/restore-after-file.png` });
    report.add('b', 'With a file: the restore act shows, nothing sent', lib.verdict(after.act && after.button && sent.length === 0),
      `act ${after.act ? 'shown' : 'HIDDEN'}, button ${after.button ? 'shown' : 'HIDDEN'}; ${sent.map(lib.describeWrite).join(' | ') || 'nothing sent'}`);
  },
});
