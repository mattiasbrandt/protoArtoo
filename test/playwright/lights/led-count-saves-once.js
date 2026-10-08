// bench-auto: droid
// !!! WRITES CONFIGURATION TO THE DROID - and puts it back !!!
//
// A lit wire's LED count, typed on Lights, is saved exactly once, carrying the
// number typed, and the droid then reports that number. Introduced by #410
// and #415 (a "20 then 16" double post was seen once on #415).
//
// WHAT IT WRITES. The LED count of ONE lit wire - the number of LEDs on that
// wire's strip, saved on the droid through POST /api/config `outputs`
// [{address, ledCount}] (data/outputs.js save) - is set to one more than it
// holds (one less at 255), checked, then set BACK to the count it started
// with and checked again. The strip reads its count once at start
// (src/tasks/aux_led.cpp), so nothing lights differently meanwhile and no
// restart is asked for. The starting count is printed first; if the script
// stops between the two saves, set it back on Lights by hand.
//
// PRECONDITION: GET /api/servo/outputs has a wire carrying the LED strip
// Light Type (component "rgb") with an LED count, and Lights draws the count
// box on a body light's plate on that wire (data/lights.js litPlate). NOT
// ASSESSED otherwise. The estop does not matter: a count is a setting.
//
// WRITES IT ALLOWS, and nothing else: POST /api/config whose JSON body is
// exactly {"outputs":[{"address":..., "ledCount":...}]}.
//
// WHAT IT PROVES, for the new count and then for the original:
//   a  typed as an operator types it (click the box, select all, type, Enter,
//      Tab away), exactly one save goes out, carrying that number;
//   b  GET /api/servo/outputs then reports that number on that wire.
//
// WHAT IT DOES NOT DO. It does not restart the droid; whether a different
// count lights differently needs a strip on the wire.
//
// WHY A REAL BROWSER. A double post would live in how Chromium fires `change`
// on a number box across Enter, blur and the redraw a save answer causes.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/lights/led-count-saves-once.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)
//   STEP=1                    wait for Enter between the two saves
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=double fires a second `change` with another number right
// after Enter; both a rows must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/lights';
const WATCH_MS = Number(process.env.WATCH_MS || 3000);

const isCountSave = (entry) => {
  if (entry.method !== 'POST' || entry.path !== '/api/config' || !entry.contentType.includes('application/json')) return false;
  try {
    const body = JSON.parse(entry.body);
    return Object.keys(body).length === 1 && Array.isArray(body.outputs) && body.outputs.length === 1 &&
      Object.keys(body.outputs[0]).sort().join(',') === 'address,ledCount';
  } catch (_error) {
    return false;
  }
};

let wire = null;

lib.runCheck({
  rule: 'Lights: an LED count is saved once, and the droid reports it (WRITES CONFIG)',
  artifactDir: ARTIFACTS,
  allow: isCountSave,
  selftests: ['double'],
  precondition: async ({ page }) => {
    const table = (await lib.readJson(page, '/api/servo/outputs')).json?.outputs || [];
    const lit = table.filter((row) => row.component === 'rgb' && Number.isInteger(row.ledCount));
    if (lit.length === 0) return `no wire carries the LED strip with a count (${table.length} rows read)`;
    await lib.loadSurface(page, 'lights');
    const plates = await page.evaluate(() =>
      [...document.querySelectorAll('#lights-body .light-plate')].filter((plate) => plate.querySelector('.light-count-input')).map((plate) => plate.dataset.part));
    const row = lit.find((each) => (each.parts || []).some((id) => plates.includes(id)));
    if (!row) return `Lights draws no count box for a Part on a lit wire (lit: ${lit.map((each) => `${each.name || each.address} [${(each.parts || []).join(', ')}]`).join('; ')})`;
    wire = { ...row, part: row.parts.find((id) => plates.includes(id)) };
    return null;
  },
  run: async ({ page, writes, report, selftest }) => {
    const original = wire.ledCount;
    const changed = original >= 255 ? original - 1 : original + 1;
    console.log(`\n*** WRITES CONFIG: ${wire.name || wire.address}'s LED count ${original} -> ${changed} -> back to ${original}. If this stops half way, set it back to ${original} on Lights. ***\n`);
    const reported = async () => {
      const table = (await lib.readJson(page, '/api/servo/outputs')).json?.outputs || [];
      return table.find((row) => row.address === wire.address)?.ledCount ?? null;
    };
    const setCount = async (value) => {
      const box = page.locator(`#lights-body .light-plate[data-part="${wire.part}"] .light-count-input`);
      const since = writes.length;
      await box.click();
      await page.keyboard.press('ControlOrMeta+A');
      await page.keyboard.type(String(value), { delay: 120 });
      await page.keyboard.press('Enter');
      if (selftest === 'double') {
        await page.evaluate(({ part, other }) => {
          const input = document.querySelector(`#lights-body .light-plate[data-part="${part}"] .light-count-input`);
          input.value = String(other);
          input.dispatchEvent(new Event('change'));
        }, { part: wire.part, other: value === 255 ? 254 : value + 1 });
      }
      await page.keyboard.press('Tab');
      await page.waitForTimeout(WATCH_MS);
      const saves = writes.slice(since).filter((entry) => entry.method === 'POST' && entry.path === '/api/config');
      const counts = saves.map((entry) => {
        try {
          return JSON.parse(entry.body).outputs[0].ledCount;
        } catch (_error) {
          return entry.body.slice(0, 60);
        }
      });
      report.add(value === changed ? '1a' : '2a', `Typing ${value}: exactly one save, carrying ${value}`,
        lib.verdict(saves.length === 1 && counts[0] === value && saves[0].allowed), `${saves.length} save(s): [${counts.join(', ')}]`);
      const held = await reported();
      report.add(value === changed ? '1b' : '2b', `The droid then reports ${value}`, lib.verdict(held === value), `ledCount ${held}`);
      return held;
    };
    let restored = null;
    try {
      await setCount(changed);
      await page.screenshot({ path: `${ARTIFACTS}/led-count-changed.png` });
      await lib.step(`Count set to ${changed}. Putting ${original} back next.`);
    } finally {
      restored = await setCount(original);
      await page.screenshot({ path: `${ARTIFACTS}/led-count-restored.png` });
      console.log(restored === original
        ? `\nThe LED count on ${wire.name || wire.address} is back at ${original}, as the droid reports it.`
        : `\n!!! The droid reports ${restored} on ${wire.name || wire.address}, not ${original}. Set it back to ${original} on Lights. !!!`);
    }
  },
});
