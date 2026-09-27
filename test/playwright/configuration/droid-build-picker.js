// Configuration's Droid Build names MK4 as the default, shows a design's
// Design Variants only under the design shown, and draws a roadmap design as
// words, never as a button. Introduced by #368.
//
// PRECONDITION: none beyond a droid that answers. Writes nothing: the guard
// lets no write through, and nothing is pressed.
//
// WHAT IT PROVES, in each half (Dome Design, Body Design) the droid is asked:
//   a  the pre-selected design (MK4, docs/droid-parts.yaml `preselected`)
//      carries the "Default" pill, and no other design does;
//   b  the design shown carries its Design Variant row - Basic and Complex, as
//      buttons, under MK4 when MK4 is the droid's answer for that half - and
//      MK4.1 and "My own build" carry none (data/droid_build_picker.js
//      designPlate, variantRow);
//   c  MK3 reads exactly "We intend to carry it. Not yet." and is not a
//      button: its plate is an <article>, its face a <div>, and nothing in it
//      takes a press.
// When the droid's answer for a half is not MK4, b says so and checks only
// that MK4 carries no row there.
//
// WHY A REAL BROWSER. The picker is drawn from the served catalog over the
// droid's own answer; element roles are read as Chromium built them.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/configuration/droid-build-picker.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=pill takes the Default pill off MK4; a must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/configuration';
// docs/droid-parts.yaml's MK3 blurb, which data/droid_parts.js carries.
const ROADMAP_SENTENCE = 'We intend to carry it. Not yet.';

lib.runCheck({
  rule: 'Droid Build: MK4 is the default, variants sit under the design shown, a roadmap design is not a button',
  artifactDir: ARTIFACTS,
  selftests: ['pill'],
  run: async ({ page, report, selftest }) => {
    await lib.landNeutral(page);
    const build = (await lib.readJson(page, '/api/config')).json?.droidBuild || {};
    await lib.loadSurface(page, 'configuration');
    await page.waitForSelector('#droid-build-body [data-half] .droid-build-plate', { timeout: 15000 });
    if (selftest === 'pill') {
      await page.evaluate(() => document.querySelectorAll('.droid-build-plate[data-option="mk4"] .status-pill').forEach((pill) => pill.remove()));
    }
    const halves = await page.evaluate(() =>
      [...document.querySelectorAll('#droid-build-body [data-half]')].map((half) => ({
        half: half.dataset.half,
        plates: [...half.querySelectorAll('.droid-build-plate[data-option]')].map((plate) => ({
          id: plate.dataset.option,
          tag: plate.tagName.toLowerCase(),
          face: plate.querySelector('.droid-build-card')?.tagName.toLowerCase(),
          pills: [...plate.querySelectorAll('.droid-build-card-head .status-pill')].map((pill) => pill.textContent.trim()),
          variantRow: Boolean(plate.querySelector('.droid-build-variant-block')),
          variants: [...plate.querySelectorAll('.droid-build-variant-block [data-variant]')]
            .map((node) => `${node.dataset.variant}:${node.textContent.trim()}:${node.tagName.toLowerCase()}`),
          controls: plate.querySelectorAll('button, [role="button"], [role="radio"]').length,
          blurb: plate.querySelector('.droid-build-card-blurb')?.textContent.trim(),
        })),
      })));
    await page.locator('#droid-build-body').screenshot({ path: `${ARTIFACTS}/droid-build-picker.png` });
    if (halves.length !== 2) {
      report.add('0', 'Both halves are drawn', lib.FAIL, `${halves.length} half(s)`);
      return;
    }
    for (const { half, plates } of halves) {
      const byId = new Map(plates.map((plate) => [plate.id, plate]));
      const mk4 = byId.get('mk4');
      const others = plates.filter((plate) => plate.id !== 'mk4' && plate.pills.includes('Default')).map((plate) => plate.id);
      report.add(`${half[0]}a`, `${half}: MK4 carries "Default", nothing else does`,
        lib.verdict(Boolean(mk4) && mk4.pills.includes('Default') && others.length === 0),
        `${mk4 ? `MK4 pills [${mk4.pills.join(', ')}]` : 'no MK4 plate'}${others.length ? `; also Default: ${others.join(', ')}` : ''}`);

      const shown = build[`${half}Design`] || 'mk4';
      const reasons = [];
      if (mk4 && shown === 'mk4') {
        const want = ['basic:Basic:button', 'complex:Complex:button'];
        if (!mk4.variantRow || JSON.stringify(mk4.variants) !== JSON.stringify(want)) reasons.push(`MK4's row is [${mk4.variants.join(', ')}]`);
      } else if (mk4 && mk4.variantRow) {
        reasons.push(`MK4 carries a variant row though the droid's answer is ${shown}`);
      }
      ['mk41', 'own'].forEach((id) => {
        if (byId.get(id)?.variantRow) reasons.push(`${id} carries a variant row`);
      });
      report.add(`${half[0]}b`, `${half}: variants only under the design shown`, lib.verdict(reasons.length === 0),
        reasons.join('; ') || `the droid's answer is ${shown}${shown === 'mk4' ? ': Basic, Complex under MK4' : ''}; none under MK4.1 or My own build`);

      const mk3 = byId.get('mk3');
      report.add(`${half[0]}c`, `${half}: MK3 is words, not a button`,
        lib.verdict(Boolean(mk3) && mk3.blurb === ROADMAP_SENTENCE && mk3.tag === 'article' && mk3.face === 'div' && mk3.controls === 0),
        mk3 ? `"${mk3.blurb}"; ${mk3.tag}, face ${mk3.face}, ${mk3.controls} control(s)` : 'no MK3 plate');
    }
  },
});
