// The Body Controller's picture on Configuration is still there after the
// operator leaves the surface and comes back, and coming back asks the droid
// for no picture again. Introduced by #404 (the artoo drawing vanished after
// one navigation); test/test_web/test_board_panel_identity_retry.js holds the
// same rule down without a browser.
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES.
//   a  Before leaving, the chosen Body Controller card carries a picture (the
//      set's drawing, or a photograph that decoded).
//   b  Configuration -> Maintenance -> Configuration through the nav: the card
//      still carries it, and no .webp request went out between leaving and
//      settling back.
//
// WHY A REAL BROWSER. Real navigation through the shell, a real image cache
// and the real network log.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/configuration/board-picture-survives-navigation.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=refetch asks for the board picture again on the way
// back; b must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/configuration';

const boardArt = (page) =>
  page.evaluate(() => {
    const frame = document.querySelector('[data-component-family="body_controller"] .component-plate.is-chosen .component-card-art');
    if (!frame) return { frame: false, drawn: false, photo: 0, src: '' };
    const img = frame.querySelector('img');
    return { frame: true, drawn: Boolean(frame.querySelector('svg use')), photo: img ? img.naturalWidth : 0, src: img?.getAttribute('src') || '' };
  });
const said = (art) => (!art.frame ? 'no chosen board card' : art.drawn ? 'the drawing' : art.photo > 0 ? `photo ${art.src}, ${art.photo}px` : `no picture (${art.src || 'no <img>'})`);

lib.runCheck({
  rule: 'The board picture survives leaving Configuration, and is not fetched again',
  artifactDir: ARTIFACTS,
  selftests: ['refetch'],
  run: async ({ page, report, selftest }) => {
    const asked = [];
    page.on('request', (request) => {
      if (lib.pathOf(request.url()).endsWith('.webp')) asked.push({ at: Date.now(), path: lib.pathOf(request.url()) });
    });
    await lib.loadSurface(page, 'configuration');
    await page.waitForFunction(() => [...document.querySelectorAll('.component-card-art img')].every((img) => img.complete), null, { timeout: 15000 }).catch(() => {});
    const before = await boardArt(page);
    report.add('a', 'The chosen board card carries a picture', lib.verdict(before.drawn || before.photo > 0), said(before));
    const left = Date.now();
    await lib.openSurface(page, 'maintenance');
    await lib.openSurface(page, 'configuration');
    if (selftest === 'refetch') await page.evaluate((src) => fetch(`${src}?selftest`).then((response) => response.blob()), before.src || '/artoo_pcb.webp');
    await page.waitForTimeout(1000);
    const after = await boardArt(page);
    const again = asked.filter((each) => each.at >= left).map((each) => each.path);
    await page.screenshot({ path: `${ARTIFACTS}/board-picture-after-return.png` });
    report.add('b', 'Back on Configuration: still there, nothing fetched again', lib.verdict((after.drawn || after.photo > 0) && again.length === 0),
      `${said(after)}; .webp asked for since leaving: ${again.join(', ') || 'none'}`);
  },
});
