// bench-auto: droid
// On Lights, a mode and a color picked for a dome light stay marked while the
// droid keeps reporting: a status frame redraws what the droid says, never
// what the builder asked for. Introduced by #410 (data/lights.js keeps each
// light's ask outside the plates it repaints).
//
// PRECONDITION: none beyond a droid that answers; Lights must draw a dome light
// that takes a command (NOT ASSESSED otherwise). Writes nothing: a pick is sent
// at once as POST /api/dome/cmd, and the guard records it and blocks it, so the
// dome is never told anything.
//
// WHAT IT PROVES. On the first dome light that offers modes, a mode chip and a
// color swatch not already marked are pressed. Then at least two status frames
// arrive, and both are still marked (aria-checked "true").
//
// HOW THE FRAMES ARE MADE, WITHOUT A WRITE. A client admitted to GET
// /api/events makes the droid send every open stream a status
// (src/web/api_events.cpp), so the script opens one stream of its own for a
// moment, twice (../_lib/checks.js nudgeFrames). NOT ASSESSED if fewer than
// two frames reach the page.
//
// WHY A REAL BROWSER. Real frames on a real event stream.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/lights/dome-light-pick-survives-frames.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=forget unmarks the pick after the frames, as a redraw
// that lost it would; the row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/lights';

lib.runCheck({
  rule: 'Lights: a dome light\'s picked mode and color survive status frames',
  artifactDir: ARTIFACTS,
  selftests: ['forget'],
  run: async ({ page, writes, fixture, report, selftest }) => {
    await lib.loadSurface(page, 'lights');
    const picked = await page.evaluate(() => {
      const plate = [...document.querySelectorAll('#lights-dome .light-plate')].find((node) => node.querySelector('.light-modes'));
      const mode = plate && [...plate.querySelectorAll('.light-mode')].find((button) => button.getAttribute('aria-checked') !== 'true');
      const color = plate && [...plate.querySelectorAll('.light-color:not(.light-color-default)')].find((button) => button.getAttribute('aria-checked') !== 'true');
      return mode && color ? { part: plate.dataset.part, mode: mode.textContent, color: color.getAttribute('aria-label') } : null;
    });
    const name = 'A picked mode and color are still marked after two status frames';
    if (!picked) {
      report.add('a', name, lib.NOT_ASSESSED, 'no dome light on Lights takes a command');
      return;
    }
    const plate = page.locator(`#lights-dome .light-plate[data-part="${picked.part}"]`);
    const since = writes.length;
    await plate.locator('.light-mode', { hasText: picked.mode }).first().click();
    await plate.locator(`.light-color[aria-label="${picked.color}"]`).click();
    await lib.countFrames(page);
    await lib.nudgeFrames(fixture, 2);
    await page.waitForFunction(() => (window.__checkFrames || 0) >= 2, null, { timeout: 10000 }).catch(() => {});
    const frames = await page.evaluate(() => window.__checkFrames || 0);
    if (selftest === 'forget') {
      await page.evaluate((part) => document.querySelectorAll(`#lights-dome .light-plate[data-part="${part}"] [aria-checked="true"]`)
        .forEach((node) => node.setAttribute('aria-checked', 'false')), picked.part);
    }
    const still = await page.evaluate(({ part, mode, color }) => {
      const node = document.querySelector(`#lights-dome .light-plate[data-part="${part}"]`);
      return {
        mode: [...(node?.querySelectorAll('.light-mode') || [])].find((button) => button.textContent === mode)?.getAttribute('aria-checked') === 'true',
        color: node?.querySelector(`.light-color[aria-label="${color}"]`)?.getAttribute('aria-checked') === 'true',
      };
    }, picked);
    await plate.screenshot({ path: `${ARTIFACTS}/dome-light-pick.png` });
    const detail = `${picked.part}: ${picked.mode} + ${picked.color}; ${frames} frame(s); mode ${still.mode ? 'marked' : 'LOST'}, color ${still.color ? 'marked' : 'LOST'}; ` +
      `the pick tried: ${writes.slice(since).map(lib.describeWrite).join(' | ') || 'nothing'}`;
    if (frames < 2) {
      report.add('a', name, lib.NOT_ASSESSED, `only ${frames} status frame(s) reached the page. ${detail}`);
      return;
    }
    report.add('a', name, lib.verdict(still.mode && still.color), detail);
  },
});
