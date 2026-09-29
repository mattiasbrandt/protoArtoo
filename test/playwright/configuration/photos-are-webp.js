// bench-auto: droid
// Every photograph Configuration asks for is answered as image/webp and decodes:
// the product pictures on the Component Picker's cards and the design
// pictures on the Droid Build's. Introduced by #369 (ADR 0065).
//
// PRECONDITION: none beyond a droid that answers. Writes nothing.
//
// WHAT IT PROVES.
//   a  rc_radio's picture (a product: /rc_radio.webp) is answered with
//      Content-Type image/webp, and the <img> on its card decodes
//      (naturalWidth > 0; data/product_art.js removes an <img> that fails).
//   b  mrbaddeley's picture (a design picture, /mrbaddeley.webp, on the MK4
//      card) the same: a design picture is owed the answer a product picture
//      gets.
//   c  Every other .webp the page asked for, the same.
//
// WHY A REAL BROWSER. Response headers and image decoding are the browser's.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/configuration/photos-are-webp.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js,
// which answers the photographs the way the firmware does).
// Self-test: SELFTEST=plain has the fixture answer rc_radio and artoo_pcb as
// text/plain; a and c must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/configuration';
const NAMED = [
  { id: 'rc_radio', frame: '[data-component-family="radio_controller"] .component-plate[data-option="rc_radio"] .component-card-art' },
  { id: 'mrbaddeley', frame: '#droid-build-body .droid-build-plate[data-option="mk4"] .component-card-art' },
];

lib.runCheck({
  rule: 'Configuration\'s photographs are answered image/webp and decode',
  artifactDir: ARTIFACTS,
  selftests: ['plain'],
  fixture: lib.SELFTEST === 'plain' ? { textPlainPhotos: ['rc_radio', 'artoo_pcb'] } : {},
  run: async ({ page, report }) => {
    const answers = [];
    page.on('response', (response) => {
      const target = lib.pathOf(response.url());
      if (target.endsWith('.webp')) answers.push({ path: target, status: response.status(), type: response.headers()['content-type'] || '' });
    });
    await lib.loadSurface(page, 'configuration');
    // Pictures wait for the deferred-asset sweep, then load.
    await page.waitForFunction(() => [...document.querySelectorAll('.component-card-art img')].every((img) => img.complete), null, { timeout: 15000 }).catch(() => {});
    await page.waitForTimeout(1000);
    const frames = await page.evaluate((named) =>
      Object.fromEntries(named.map(({ id, frame }) => {
        const node = document.querySelector(frame);
        const img = node?.querySelector('img');
        return [id, { frame: Boolean(node), img: Boolean(img), width: img ? img.naturalWidth : 0 }];
      })), NAMED);
    NAMED.forEach(({ id }, index) => {
      const mine = answers.filter((answer) => answer.path === `/${id}.webp`);
      const seen = frames[id];
      const reasons = [];
      if (mine.length === 0) reasons.push('never asked for');
      mine.filter((answer) => answer.status !== 200 || !answer.type.startsWith('image/webp')).forEach((answer) => reasons.push(`answered ${answer.status} "${answer.type}"`));
      if (!seen.frame) reasons.push('no picture frame on its card');
      else if (!seen.img) reasons.push('the frame holds no <img>: it failed to load');
      else if (!(seen.width > 0)) reasons.push(`the <img> did not decode (naturalWidth ${seen.width})`);
      report.add('ab'[index], `${id}: image/webp, and it decodes`, lib.verdict(reasons.length === 0),
        reasons.join('; ') || `${mine.length} answer(s) image/webp; naturalWidth ${seen.width}`);
    });
    const others = answers.filter((answer) => !NAMED.some(({ id }) => answer.path === `/${id}.webp`));
    const wrong = others.filter((answer) => answer.status !== 200 || !answer.type.startsWith('image/webp')).map((answer) => `${answer.path} answered ${answer.status} "${answer.type}"`);
    const undecoded = await page.evaluate(() =>
      [...document.querySelectorAll('img')].filter((img) => /\.webp$/.test(img.getAttribute('src') || '') && img.complete && img.naturalWidth === 0).map((img) => img.getAttribute('src')));
    report.add('c', 'Every other photograph: image/webp, and it decodes', others.length ? lib.verdict(wrong.length === 0 && undecoded.length === 0) : lib.NOT_ASSESSED,
      [...wrong, ...undecoded.map((src) => `${src} did not decode`)].join('; ') || `${[...new Set(others.map((answer) => answer.path))].join(', ') || 'none asked for'}`);
    await page.screenshot({ path: `${ARTIFACTS}/photographs.png`, fullPage: true });
  },
});
