// RC's radio and receiver cards show the waiting dots until the droid has
// answered - never "not picked" for a choice nobody has read yet.
// Introduced by #412 (data/rc.js paintProductCards).
//
// PRECONDITION: none beyond a droid that answers. Writes nothing: the guard
// lets no write through (RC's runtime-only verbose-log toggle included).
//
// WHAT IT PROVES. With GET /api/config held back in the browser for 2.5 s -
// the droid answers late, nothing more - #rc-radio-card and #rc-receiver-card
// each hold nothing but an empty .hint.waiting line whose ::after draws the
// dots (data/style.css), and say nothing about a pick. The hold is then let go and the page left to
// finish.
//
// WHY A REAL BROWSER. The late answer is a real request held in flight.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/rc/cards-say-reading-while-config-held.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=picked writes "No radio picked yet." on the radio card
// while the config is held; the row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/rc';
const HOLD_MS = Number(process.env.HOLD_MS || 2500);

lib.runCheck({
  rule: 'RC cards show the waiting dots until the droid answers',
  artifactDir: ARTIFACTS,
  selftests: ['picked'],
  run: async ({ page, report, selftest }) => {
    let release;
    const released = new Promise((resolve) => { release = resolve; });
    const hold = async (route) => {
      await released;
      return route.fallback();
    };
    await page.route('**/api/config*', hold);
    await page.goto(`${lib.BASE_URL}/#rc`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('#rc-radio-card', { timeout: 20000 });
    await page.waitForTimeout(HOLD_MS);
    if (selftest === 'picked') await page.evaluate(() => { document.getElementById('rc-radio-card').textContent = 'No radio picked yet.'; });
    const cards = await page.evaluate(() => {
      const waits = (id) => {
        const card = document.getElementById(id);
        const line = card.querySelector('p.hint.waiting');
        return card.textContent.trim() === '' && line !== null && getComputedStyle(line, '::after').content.includes('...');
      };
      return {
        radio: waits('rc-radio-card'),
        receiver: waits('rc-receiver-card'),
        said: document.getElementById('rc-radio-card').textContent.trim(),
      };
    });
    await page.screenshot({ path: `${ARTIFACTS}/cards-config-held.png` });
    await page.unroute('**/api/config*', hold);
    release();
    report.add('a', 'Both cards show the waiting dots while the config is held', lib.verdict(cards.radio && cards.receiver),
      `radio waits: ${cards.radio}, receiver waits: ${cards.receiver}, radio says "${cards.said}"`);
    await page.waitForTimeout(1500);
  },
});
