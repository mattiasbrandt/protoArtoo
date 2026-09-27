// SAFETY. The calibration dial holds its Output while the builder looks, stops
// asking the moment the estop latches or the tab is hidden, and never takes
// back an Output the droid has let go: the droid's own bounds decide when a
// hold ends, and only a press takes one. Introduced by #364 and ADR 0064; the
// "never takes back" half by #417 F8 (the keepalive could re-take a released
// servo).
//
// THIS SCRIPT DRIVES AN OUTPUT AND PRESSES STOP. It opens the dial on one
// enabled Output - the dial takes the Output and holds a pulse on its pin, at
// the width the droid last put there or the middle of its band - and Part 1
// then presses the topbar STOP, latching the real estop. With only USB
// connected the pin is bare; with a servo on that Output, the servo MOVES.
//
// PRECONDITION: GET /api/status says the estop is CLEAR (for PART=2 alone, it
// may be latched: Part 2 waits for the operator's clear), and GET
// /api/servo/outputs has an enabled Output with a name the servo route takes -
// switchable, ticked wired, its wire carrying no light, not a row of lights
// only (ServoTask isArmEnabled, data/servo.js isDriveable). NOT ASSESSED
// otherwise.
//
// WRITES IT ALLOWS, and nothing else: POST /api/servo action=hold (a press, or
// refresh=1) or action=release for the chosen Output, and POST /api/estop.
//
// WHAT IT PROVES.
//   Part 1, STOP while the dial holds:
//     1a Opening the dial holds: one taking hold goes out, then keepalive
//        refreshes (refresh=1). If none follow, the row FAILS and the script
//        presses "take it again" (the builder's own way back) so the rest of
//        the part still has a hold to test; if that does not hold either, 1b
//        is NOT ASSESSED.
//     1b After STOP, for WATCH_MS (4.5 s, past SERVO_HOLD_EXPIRY_MS 3 s): no
//        taking hold at all, and no hold of any kind once the page shows
//        "Estop: latched". A refresh already in flight between the press and
//        the page hearing the latch is counted in the row, not failed: the
//        droid drops a refresh with no hold standing (src/web/api_servo.cpp).
//     1c The Output is let go: GET /api/servo/outputs reports commandedUs null
//        and limp "estop", and its Servos row reads "Limp - the estop let go".
//   Part 2, a hidden tab (fresh page, estop cleared by the operator):
//     2a Opening the dial holds (as 1a).
//     2b While the tab is hidden, for WATCH_MS: no hold of any kind goes out.
//     2c The droid's expiry lets go: commandedUs null, limp "expiry".
//     2d The tab shown again, the Output is still let go 2.5 s later and no
//        taking hold went out: the keepalive only refreshes.
//   Then "done" closes the dial, which sends one release.
//
// THE CLEAR BETWEEN THE PARTS IS THE OPERATOR'S. This script never releases
// the estop (ADR 0048). Before Part 2, while the estop reads latched, it says
// what to do and watches the droid - never stdin - for up to CLEAR_WAIT_S
// (120 s). Still latched: Part 2 is NOT ASSESSED; run PART=2 once it is clear.
//
// HOW THE TAB IS HIDDEN. document.visibilityState and document.hidden are
// overridden on the page and visibilitychange is dispatched - to every script
// on the page, a tab gone to the background. Chromium does not background a
// page Playwright drives.
//
// WHAT IT DOES NOT DO. It records no end (no Set MIN/CENTER/MAX), writes no
// configuration, and does not wait out the ten-minute ceiling.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/servo/dial-never-retakes-a-released-output.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)
//   PART=1 | PART=2           one part only (default both)
//   CLEAR_WAIT_S=120          how long Part 2 waits for the operator's clear
//   STEP=1                    wait for Enter between steps
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js;
// the fixture stands in for the operator's clear).
// Self-test: SELFTEST=keepalive keeps sending hold refreshes through the latch,
// as the #417 page did (1b must FAIL), and does not hide the tab (2b, 2c and
// 2d must FAIL).
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/servo';
const PART = process.env.PART || 'all';
const CLEAR_WAIT_S = Number(process.env.CLEAR_WAIT_S || 120);
// Past SERVO_HOLD_EXPIRY_MS (3000, include/config.h), with room for a read.
const WATCH_MS = Number(process.env.WATCH_MS || 4500);
const LIMP_ESTOP = 'Limp - the estop let go';

if (!['all', '1', '2'].includes(PART)) {
  console.error('PART is 1, 2 or unset');
  process.exit(2);
}

let output = null;
const holdsIn = (writes, from, to = Infinity) =>
  writes
    .filter((entry) => entry.path === '/api/servo' && entry.at >= from && entry.at <= to && lib.formOf(entry.body).action === 'hold')
    .map((entry) => ({ at: entry.at, refresh: lib.formOf(entry.body).refresh === '1' }));

lib.runCheck({
  rule: 'The dial holds while watched, stops on the estop and a hidden tab, and never takes back a released Output (SAFETY)',
  artifactDir: ARTIFACTS,
  selftests: ['keepalive'],
  allow: (entry) => {
    if (entry.path === '/api/estop') return true;
    if (entry.path !== '/api/servo' || output === null) return false;
    const form = lib.formOf(entry.body);
    return form.arm === output.name && (form.action === 'hold' || form.action === 'release');
  },
  precondition: async ({ page }) => {
    const estop = await lib.readEstop(page);
    if (!estop.known) return estop.why;
    if (estop.latched && PART !== '2') return 'the estop is LATCHED. Clear it on Foot Drive or the Dashboard, then run this again.';
    const table = (await lib.readJson(page, '/api/servo/outputs')).json?.outputs || [];
    // Which Parts are lights is data/droid_part_kind.js's answer over the
    // catalog the droid serves, run here rather than kept as a list.
    const lights = new Set(await page.evaluate(async () => {
      const load = async (src) => new Function(await (await fetch(src, { cache: 'no-store' })).text())(); // eslint-disable-line no-new-func
      if (!window.DroidParts) await load('/droid_parts.js');
      if (!window.DroidPartKind) await load('/droid_part_kind.js');
      return window.DroidParts.parts.filter((part) => window.DroidPartKind.isLight(part)).map((part) => part.id);
    }));
    output = table.find((row) => row.switchable === true && row.wired === true && row.component !== 'rgb' && typeof row.name === 'string' && row.name !== '' &&
      !(Array.isArray(row.parts) && row.parts.length > 0 && row.parts.every((id) => lights.has(id)))) || null;
    if (!output) return `no enabled Output with a name the servo route takes (${table.map((row) => `${row.name || row.address} wired=${row.wired} ${row.component}`).join(', ')})`;
    console.log(`\n*** This drives ${output.name} (${output.address}) through the calibration dial${PART !== '2' ? ' and presses STOP, which LATCHES THE ESTOP' : ''}. ***\n`);
    return null;
  },
  run: async ({ page: firstPage, writes: firstWrites, fixture, report, selftest, openPage }) => {
    let page = firstPage;
    let writes = firstWrites;
    const rowOf = async () => ((await lib.readJson(page, '/api/servo/outputs')).json?.outputs || []).find((row) => row.address === output.address) || null;

    // Opens the dial; reports whether it holds; tries "take it again" if not.
    const establishHold = async (id) => {
      const calibrate = `.outputs-row[data-output="${output.address}"] .outputs-calibrate`;
      await page.waitForSelector(`${calibrate}:not([disabled])`, { timeout: 15000 });
      const openedAt = Date.now();
      await page.click(calibrate);
      await page.waitForTimeout(2500);
      const first = holdsIn(writes, openedAt);
      const takes = first.filter((each) => !each.refresh).length;
      const refreshes = first.length - takes;
      if (takes === 1 && refreshes >= 1) {
        report.add(id, 'Opening the dial holds: one take, then keepalive refreshes', lib.PASS, `${takes} take, ${refreshes} refresh(es) in 2.5 s`);
        return true;
      }
      const note = await page.textContent('.cal-panel .cal-note').catch(() => '');
      let recovery = 'no "take it again" to press';
      let holding = false;
      if (await page.waitForSelector('.cal-panel .cal-resume:not([hidden]):not([disabled])', { timeout: 5000 }).then(() => true, () => false)) {
        const pressedAt = Date.now();
        await page.click('.cal-panel .cal-resume');
        await page.waitForTimeout(2500);
        const again = holdsIn(writes, pressedAt);
        holding = again.some((each) => !each.refresh) && again.some((each) => each.refresh);
        recovery = `"take it again" then sent ${again.filter((each) => !each.refresh).length} take, ${again.filter((each) => each.refresh).length} refresh(es): ${holding ? 'holding now' : 'still not holding'}`;
      }
      report.add(id, 'Opening the dial holds: one take, then keepalive refreshes', lib.FAIL,
        `${takes} take, ${refreshes} refresh(es) in 2.5 s - the keepalive never ran; the dial said "${note}"; ${recovery}`);
      return holding;
    };

    // Part 1 ---------------------------------------------------------------------
    if (PART !== '2') {
      await lib.loadSurface(page, 'servo');
      await page.waitForFunction(() => document.getElementById('shell-estop-state').textContent === 'Estop: clear', null, { timeout: 15000 });
      const holding = await establishHold('1a');
      await page.screenshot({ path: `${ARTIFACTS}/dial-holding.png` });
      await lib.step(`The dial is ${holding ? '' : 'NOT '}holding. STOP next.`);
      const pressedAt = Date.now();
      await page.click('#shell-estop-button');
      await page.waitForFunction(() => document.getElementById('shell-estop-state').textContent === 'Estop: latched', null, { timeout: 10000 });
      const latchedAt = Date.now();
      if (selftest === 'keepalive') {
        await page.evaluate((arm) => {
          window.__selftestKeepalive = window.setInterval(() => {
            fetch('/api/servo', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: `arm=${arm}&action=hold&positionUs=1500&refresh=1` }).catch(() => {});
          }, 1000);
        }, output.name);
      }
      await page.waitForTimeout(WATCH_MS);
      if (selftest === 'keepalive') await page.evaluate(() => window.clearInterval(window.__selftestKeepalive));
      const after = holdsIn(writes, pressedAt);
      const takes = after.filter((each) => !each.refresh).length;
      const afterLatch = after.filter((each) => each.at >= latchedAt).length;
      const heard = `${takes} take(s) after the press; ${afterLatch} hold(s) after "Estop: latched" (${latchedAt - pressedAt} ms after the press); ` +
        `${after.filter((each) => each.refresh && each.at < latchedAt).length} refresh(es) in between`;
      if (!holding && after.length === 0) report.add('1b', 'After STOP: no take, and no hold once the latch shows', lib.NOT_ASSESSED, `the dial never held. ${heard}`);
      else report.add('1b', 'After STOP: no take, and no hold once the latch shows', lib.verdict(takes === 0 && afterLatch === 0), heard);
      const row = await rowOf();
      const cell = await page.textContent(`.outputs-row[data-output="${output.address}"] .outputs-release`).catch(() => '');
      report.add('1c', 'The Output is let go: commandedUs null, limp "estop"', lib.verdict(Boolean(row) && row.commandedUs === null && row.limp === 'estop' && cell === LIMP_ESTOP),
        row ? `the droid: commandedUs ${row.commandedUs}, limp "${row.limp}"; the row: "${cell}"` : 'the Output left the droid\'s answer');
      await page.screenshot({ path: `${ARTIFACTS}/dial-after-stop.png` });
    }
    if (PART === '1') return;

    // Part 2 ---------------------------------------------------------------------
    let estop = await lib.readEstop(page);
    if (estop.known && estop.latched) {
      if (fixture) {
        fixture.clearEstop(); // the operator's clear, which this script never makes on a droid
      } else {
        console.log(`\nThe estop is LATCHED. Part 2 needs it clear: clear it on Foot Drive or the Dashboard, in this window or another. Watching for up to ${CLEAR_WAIT_S} s...`);
        const deadline = Date.now() + CLEAR_WAIT_S * 1000;
        while (Date.now() < deadline && estop.known && estop.latched) {
          await page.waitForTimeout(2000);
          estop = await lib.readEstop(page);
        }
      }
      estop = await lib.readEstop(page);
    }
    if (!estop.known || estop.latched) {
      ['2a', '2b', '2c', '2d'].forEach((id) => report.add(id, 'Hidden tab', lib.NOT_ASSESSED, 'the estop was not cleared. Clear it, then run PART=2.'));
      return;
    }
    // A fresh page, so nothing of Part 1's dial carries over.
    ({ page, writes } = await openPage());
    await lib.loadSurface(page, 'servo');
    await page.waitForFunction(() => document.getElementById('shell-estop-state').textContent === 'Estop: clear', null, { timeout: 15000 });
    const holding = await establishHold('2a');
    await lib.step(`The dial is ${holding ? '' : 'NOT '}holding. Hiding the tab next.`);
    const hiddenAt = Date.now();
    if (selftest !== 'keepalive') {
      await page.evaluate(() => {
        Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
        Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
        document.dispatchEvent(new Event('visibilitychange'));
      });
    }
    await page.waitForTimeout(WATCH_MS);
    const shownAt = Date.now();
    const whileHidden = holdsIn(writes, hiddenAt, shownAt);
    const expired = await rowOf();
    await page.evaluate(() => {
      delete document.visibilityState;
      delete document.hidden;
      document.dispatchEvent(new Event('visibilitychange'));
    });
    await page.waitForTimeout(2500);
    const back = await rowOf();
    const since = holdsIn(writes, shownAt);
    if (!holding) {
      ['2b', '2c', '2d'].forEach((id) => report.add(id, 'Hidden tab', lib.NOT_ASSESSED, `the dial never held, so hiding the tab had no keepalive to stop (the droid: limp "${expired?.limp}")`));
    } else {
      report.add('2b', 'While the tab is hidden: no hold goes out', lib.verdict(whileHidden.length === 0), `${whileHidden.length} hold(s) in ${WATCH_MS / 1000} s`);
      report.add('2c', 'The droid\'s expiry lets go: commandedUs null, limp "expiry"', lib.verdict(Boolean(expired) && expired.commandedUs === null && expired.limp === 'expiry'),
        expired ? `commandedUs ${expired.commandedUs}, limp "${expired.limp}"` : 'the Output left the droid\'s answer');
      report.add('2d', 'Shown again: the Output stays let go, nothing takes it', lib.verdict(Boolean(back) && back.commandedUs === null && since.every((each) => each.refresh)),
        back ? `commandedUs ${back.commandedUs}, limp "${back.limp}"; ${since.length} hold(s) since, ${since.filter((each) => !each.refresh).length} a take` : 'the Output left the droid\'s answer');
    }
    await page.screenshot({ path: `${ARTIFACTS}/dial-after-hidden.png` });
    const releases = () => writes.filter((entry) => lib.formOf(entry.body).action === 'release').length;
    const before = releases();
    await page.click('.cal-panel .cal-done', { timeout: 5000 }).catch((error) => console.log(`Could not press done: ${String(error.message).split('\n')[0]}`));
    await page.waitForTimeout(1000);
    console.log(`Closing the dial sent ${releases() - before} release(s). The estop line reads "${await page.textContent('#shell-estop-state')}".`);
  },
});
