// Configuration's Component Picker draws every product the droid's lineup
// lists, exactly once, in its family - and a roadmap product is greyed words,
// never a button. Introduced by #369.
//
// PRECONDITION: none beyond a droid that answers. Writes nothing: the guard
// lets no write through, and nothing is pressed.
//
// WHAT IT PROVES.
//   a  One card per GET /api/identity/components part, in the family its
//      `category` names, exactly once - except the RC Receiver rows (protocol
//      standard_pwm, sbus, crsf), which the picker draws under a chosen radio
//      and never as cards (data/component_picker.js isReceiverRow). Every
//      lineup family has a host on the page.
//   b  Every roadmap card is not a control (its face is a <div>, nothing in it
//      takes a press) and is greyed: .availability-settled-no, computed
//      opacity below 1, with the pointer off it.
//
// WHY A REAL BROWSER. Greyed is computed style, which only a CSS engine has.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/configuration/component-picker-lineup.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=card removes one sound card and lifts the grey off every
// roadmap card; a and b must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/configuration';
// data/component_picker.js RC_RECEIVER.wires: the rows drawn under a radio.
const RECEIVER_PROTOCOLS = ['standard_pwm', 'sbus', 'crsf'];

lib.runCheck({
  rule: 'Component Picker: one card per lineup product, roadmap cards greyed and not buttons',
  artifactDir: ARTIFACTS,
  selftests: ['card'],
  run: async ({ page, report, selftest }) => {
    await lib.landNeutral(page);
    const lineup = (await lib.readJson(page, '/api/identity/components')).json || {};
    const parts = Array.isArray(lineup.parts) ? lineup.parts : [];
    await lib.loadSurface(page, 'configuration');
    await page.waitForSelector('[data-component-family] .component-plate', { timeout: 15000 });
    await page.mouse.move(2, 2);
    if (selftest === 'card') {
      await page.evaluate(() => {
        document.querySelector('[data-component-family="sound"] .component-plate[data-kind="supported"]')?.remove();
        document.querySelectorAll('.component-plate[data-kind="roadmap"]').forEach((plate) => plate.classList.remove('availability-settled-no'));
      });
    }
    const drawn = await page.evaluate(() =>
      [...document.querySelectorAll('[data-component-family]')].map((host) => ({
        family: host.dataset.componentFamily,
        plates: [...host.querySelectorAll(':scope > .component-cards > .component-plate')].map((plate) => ({
          id: plate.dataset.option,
          kind: plate.dataset.kind,
          face: plate.querySelector('.component-card')?.tagName.toLowerCase(),
          controls: plate.querySelectorAll('button, [role="button"], [role="radio"]').length,
          opacity: Number(getComputedStyle(plate).opacity),
          greyed: plate.classList.contains('availability-settled-no'),
        })),
      })));
    await page.screenshot({ path: `${ARTIFACTS}/component-picker.png`, fullPage: true });

    const reasons = [];
    const hosts = new Set(drawn.map((host) => host.family));
    [...new Set(parts.map((part) => part.category))].filter((family) => !hosts.has(family)).forEach((family) => reasons.push(`no host for ${family}`));
    drawn.forEach(({ family, plates }) => {
      const want = parts.filter((part) => part.category === family && !RECEIVER_PROTOCOLS.includes(part.protocol)).map((part) => part.id);
      const got = plates.filter((plate) => plate.kind !== 'not-fitted').map((plate) => plate.id);
      want.forEach((id) => {
        const times = got.filter((each) => each === id).length;
        if (times !== 1) reasons.push(`${family}: ${id} drawn ${times} time(s)`);
      });
      got.filter((id) => !want.includes(id)).forEach((id) => reasons.push(`${family}: ${id} is not in the lineup`));
    });
    const receivers = parts.filter((part) => RECEIVER_PROTOCOLS.includes(part.protocol)).map((part) => part.id);
    report.add('a', 'One card per lineup product, in its family, once', lib.verdict(parts.length > 0 && reasons.length === 0),
      reasons.join('; ') || `${parts.length - receivers.length} cards for ${parts.length} lineup rows in ${drawn.length} families; drawn under a radio instead: ${receivers.join(', ')}`);

    const roadmap = drawn.flatMap((host) => host.plates.filter((plate) => plate.kind === 'roadmap'));
    const wrong = roadmap
      .filter((plate) => plate.face !== 'div' || plate.controls !== 0 || !plate.greyed || !(plate.opacity < 1))
      .map((plate) => `${plate.id} (face ${plate.face}, ${plate.controls} control(s), ${plate.greyed ? 'greyed' : 'NOT greyed'}, opacity ${plate.opacity})`);
    report.add('b', 'Roadmap cards are greyed words, not buttons', roadmap.length ? lib.verdict(wrong.length === 0) : lib.NOT_ASSESSED,
      roadmap.length ? wrong.join('; ') || `${roadmap.length} roadmap cards: ${roadmap.map((plate) => plate.id).join(', ')}` : 'the lineup has no roadmap product');
  },
});
