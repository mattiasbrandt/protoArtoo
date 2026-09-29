// bench-auto: droid
// The RC Receiver's channel ticks on Configuration fit their words at desktop
// width: nothing in a channel plate runs past its own box. Introduced by #370
// (the fix 40c4d3ca sized the column for "CH 6" and "OFF").
//
// PRECONDITION: none beyond a droid that answers; the ticks must be on screen
// (they are, in the form, or inside the chosen receiver's answer when the
// droid has a radio with a receiver). NOT ASSESSED when they are not. Writes
// nothing.
//
// WHAT IT PROVES. At 1440x900, no element in #rc-channel-plates that is on
// screen has scrollWidth > clientWidth: a channel name, its badge or its state
// wider than the tick it sits in is content spilling over its neighbour. The
// row names what spilled and says whether the ticks sat inside a receiver
// (the case data/style.css .component-sub-channels sizes) or in the form.
//
// WHY A REAL BROWSER. Layout: mini_dom has none.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/configuration/rc-channel-plates-fit.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=wide adds an unbreakable long name to one tick; the row
// must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/configuration';

lib.runCheck({
  rule: 'RC channel ticks fit their words at 1440',
  artifactDir: ARTIFACTS,
  selftests: ['wide'],
  run: async ({ page, report, selftest }) => {
    await lib.loadSurface(page, 'configuration');
    if (selftest === 'wide') {
      await page.evaluate(() => {
        const row = [...document.querySelectorAll('#rc-channel-plates [data-feature-entry]')].find((node) => !node.hidden);
        const wide = document.createElement('span');
        wide.textContent = 'SELFTEST-AN-UNBREAKABLE-CHANNEL-NAME';
        wide.style.whiteSpace = 'nowrap';
        row?.querySelector('.toggle-label')?.appendChild(wide);
      });
    }
    const fit = await page.evaluate(() => {
      const plates = document.getElementById('rc-channel-plates');
      if (!plates || !plates.checkVisibility({ checkVisibilityCSS: true })) return null;
      const describe = (el) => `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ''}${el.classList.length ? `.${[...el.classList].slice(0, 2).join('.')}` : ''}`;
      const nodes = [plates, ...plates.querySelectorAll('*')].filter((el) => el.checkVisibility({ checkVisibilityCSS: true }));
      return {
        inReceiver: Boolean(plates.closest('.component-sub-channels')),
        rows: [...plates.querySelectorAll('[data-feature-entry]')].filter((row) => !row.hidden).map((row) => row.querySelector('.toggle-label')?.textContent.trim()),
        checked: nodes.length,
        over: nodes.filter((el) => el.clientWidth > 0 && el.scrollWidth > el.clientWidth).map((el) => `${describe(el)} ${el.scrollWidth}>${el.clientWidth}`),
      };
    });
    if (!fit) {
      report.add('a', 'Nothing in a channel tick is wider than its box', lib.NOT_ASSESSED, 'the channel ticks are not on screen');
      return;
    }
    await page.locator('#rc-channel-plates').screenshot({ path: `${ARTIFACTS}/rc-channel-plates.png` });
    report.add('a', 'Nothing in a channel tick is wider than its box', lib.verdict(fit.rows.length > 0 && fit.over.length === 0),
      `${fit.rows.length} tick(s) ${fit.inReceiver ? 'inside the chosen receiver' : 'in the form'} [${fit.rows.join(' | ')}]; ` +
        `${fit.over.length ? `spilling: ${[...new Set(fit.over)].join(', ')}` : `${fit.checked} elements, none spill`}`);
  },
});
