// bench-auto: fixture rc.html
// RC's Sticks card and its RC Channel list, as a builder at the bench uses
// them: an SBUS receiver offers all 18 RC Channels without the items running
// into each other, and each axis sets its ends from the stick it reads.
// Introduced by #389.
//
// PRECONDITION: FIXTURE=1. Every answer the page reads here is this script's
// own droid, and its acts write calibration, which a real droid would keep;
// against a droid the script refuses to run (NOT ASSESSED).
//
// WHAT IT PROVES, at 1440x900:
//   a  Dual SBUS draws CH1-CH18 for SBUS1 and SBUS2, CH17 as On from the
//      binding that reads it, and no two items overlap or leave the list.
//   b  The three axis tiles sit in one row, every Set MIN/CENTER/MAX shows its
//      whole word, and nothing leaves its tile.
//   c  While the droid holds drive at boot the card says to center the sticks,
//      a stick read past MIN says so on its own tile, the Sources head counts
//      the trigger bindings against 11, and SBUS1's frame rate is shown.
//   d  Set MAX on Steer posts the map unchanged with {"drive_steer":{"max":
//      <live reading>}}, and the tile reads the stored MAX back.
//   e  The Reversed switch posts the other direction and reads back on.
//   f  A capture that would put MAX below CENTER is not sent, and the tile
//      says why.
//   g  A droid with Single SBUS saved and Dual SBUS running offers SBUS1 alone
//      and carries the restart line.
//   h  With Steer on SBUS1, Speed is offered on an SBUS1 channel and not on an
//      SBUS2 one: the droid refuses Speed and Steer on two receivers.
//
// WHY A REAL BROWSER. Layout, hit testing and the joined control's width are
// what a DOM stub cannot see.
//
// RUN (offline only):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true \
//     node test/playwright/rc/sticks-set-ends-from-the-stick.js
// against tools/serve_editor_fixture.py, or `make pw-fixture DIR=rc`.
// Self-test: SELFTEST=overlap pulls every channel item up over the one above
// it; row a must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/rc';
const AXIS_PROFILE_KEY = { drive_speed: 'driveSpeed', drive_steer: 'driveSteer', dome_speed: 'domeSpeed' };

// One droid: the map, the ends it holds, and what each read answers.
const droid = ({ saved, running }) => {
  const profile = {
    driveSpeed: { source: 'sbus1', channel: 1, min: 172, center: 992, max: 1811, deadband: 0, reverse: false },
    driveSteer: { source: 'sbus1', channel: 2, min: 172, center: 992, max: 1811, deadband: 0, reverse: true },
    domeSpeed: { source: 'sbus1', channel: 4, min: 172, center: 992, max: 1811, deadband: 0, reverse: false },
  };
  const map = [
    { source: 'sbus1', channel: 1, action: 'drive_speed' },
    { source: 'sbus1', channel: 2, action: 'drive_steer' },
    { source: 'sbus1', channel: 4, action: 'dome_speed' },
    { source: 'sbus1', channel: 9, action: 'sleep_toggle' },
    { source: 'sbus1', channel: 17, action: 'sound_next' },
  ];
  const spread = (base) => Array.from({ length: 16 }, (_, i) => base + i * 7);
  const posts = [];
  const diag = () => ({
    mode: running,
    updatedMs: 1,
    driveAwaitingCentre: true,
    sources: {
      sbus1: { enabled: true, linked: true, ageMs: 11, lostFrames: 0, failsafe: false, framesPerSecond: 71, decodeFails: 2 },
      sbus2: { enabled: running === 'dual_sbus', linked: false, ageMs: 0, lostFrames: 0, failsafe: false, framesPerSecond: 0, decodeFails: 0 },
      pwm: { enabled: false, linked: false, ageMs: 0, lostFrames: 0, failsafe: false },
    },
    channels: [],
    // CH17/CH18 of each receiver, whatever binds them (docs/api.md "GET /api/rc").
    // `digital` says CH17 is off: the page must read rawDigital, not it.
    rawDigital: { sbus1: [true, false], sbus2: [false, false] },
    digital: { sound: { activeSource: 'sbus1', bindingChannel: 17, pressed: false } },
    mappingProfile: { version: 1, channels: profile },
    // CH1 rests at 150, below MIN: a HotRC trigger at its end. CH2 reads 1700.
    raw: { sbus1: [150, 1700, 992, 1000, ...spread(980).slice(4)], sbus2: spread(1100) },
    reactions: [],
  });
  return { profile, map, posts, diag, saved, running };
};

const routeDroid = async (page, fixture, state) => {
  const json = (route, body) => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
  await page.route('**/api/config*', (route) => {
    if (route.request().method() !== 'GET') return route.fallback();
    const config = fixture.state.config;
    return json(route, { ...config, rc: { ...(config.rc || {}), inputMode: state.saved, activeInputMode: state.running, sbus: { recvCh2: false } } });
  });
  await page.route('**/api/rc', (route) => json(route, state.diag()));
  await page.route('**/api/rc/map', (route) => {
    if (route.request().method() !== 'POST') {
      return json(route, { mode: state.saved, map: state.map, capacity: { total: 14, used: state.map.length } });
    }
    const body = JSON.parse(new URLSearchParams(route.request().postData() || '').get('plain') || '{}');
    state.posts.push(body);
    Object.entries(body.calibration || {}).forEach(([token, fields]) => Object.assign(state.profile[AXIS_PROFILE_KEY[token]], fields));
    return json(route, { ok: true });
  });
  await page.route('**/api/actions', (route) => json(route, [
    { token: 'drive_speed', display_name: 'Speed', domain: 'drive', description: '' },
    { token: 'drive_steer', display_name: 'Steer', domain: 'drive', description: '' },
    { token: 'dome_speed', display_name: 'Dome Speed', domain: 'dome', name: 'dome.action.set-speed', description: '' },
    { token: 'sleep_toggle', display_name: 'Sleep Toggle', domain: 'system', description: '' },
    { token: 'sound_next', display_name: 'Next Sound', domain: 'sound', description: '' },
  ]));
};

const openRc = async (page) => {
  await page.goto(`${lib.BASE_URL}/#rc`, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => document.querySelectorAll('#rc-axes .cal-set').length === 9, null, { timeout: 20000 });
  await page.waitForTimeout(lib.SETTLE_MS || 500);
};

const tile = (axis) => `.rc-axis[data-axis="${axis}"]`;

lib.runCheck({
  rule: 'RC offers all 18 RC Channels and sets each axis\'s ends from the stick',
  artifactDir: ARTIFACTS,
  selftests: ['overlap'],
  precondition: async () => (lib.FIXTURE ? null : 'writes calibration a droid would keep: FIXTURE=1 only'),
  run: async ({ page, fixture, report, selftest, openPage }) => {
    const state = droid({ saved: 'dual_sbus', running: 'dual_sbus' });
    await routeDroid(page, fixture, state);
    await openRc(page);
    if (selftest === 'overlap') {
      await page.addStyleTag({ content: '#rc-channel-items .rc-channel-item { margin-top: -40px; }' });
    }

    const seen = await page.evaluate(() => {
      const items = [...document.querySelectorAll('#rc-channel-items .rc-channel-item')];
      const boxes = items.map((el) => el.getBoundingClientRect());
      const list = document.querySelector('.rc-channel-list').getBoundingClientRect();
      let overlaps = 0;
      for (let i = 0; i < boxes.length; i += 1) {
        for (let j = i + 1; j < boxes.length; j += 1) {
          const a = boxes[i];
          const b = boxes[j];
          if (a.left < b.right - 0.5 && b.left < a.right - 0.5 && a.top < b.bottom - 0.5 && b.top < a.bottom - 0.5) overlaps += 1;
        }
      }
      const tiles = [...document.querySelectorAll('#rc-axes .rc-axis')];
      const card = document.getElementById('rc-axes').getBoundingClientRect();
      const text = (selector) => document.querySelector(selector)?.textContent.trim() || '';
      return {
        sbus1: items.filter((el) => el.dataset.chkey.startsWith('sbus1:')).length,
        sbus2: items.filter((el) => el.dataset.chkey.startsWith('sbus2:')).length,
        ch17: text('.rc-channel-item[data-chkey="sbus1:17"] .rc-ch-raw'),
        overlaps,
        spill: boxes.filter((b) => b.right > list.right + 0.5 || b.left < list.left - 0.5).length,
        tileTops: [...new Set(tiles.map((el) => Math.round(el.getBoundingClientRect().top)))],
        tilesInside: tiles.every((el) => el.getBoundingClientRect().right <= card.right + 0.5),
        clipped: [...document.querySelectorAll('#rc-axes .cal-set')].filter((b) => b.scrollWidth > b.clientWidth + 1).map((b) => b.textContent),
        tileSpill: tiles.filter((el) => {
          const right = el.getBoundingClientRect().right;
          return [...el.querySelectorAll('*')].some((child) => child.getBoundingClientRect().right > right + 0.5);
        }).length,
        hold: getComputedStyle(document.getElementById('rc-drive-hold')).display === 'none' ? '' : text('#rc-drive-hold'),
        warn: text('.rc-axis[data-axis="drive_speed"] .rc-axis-warn'),
        capacity: text('#rc-capacity'),
        health: text('#rc-preview-source-health').replace(/\s+/g, ' '),
      };
    });
    report.add('a', 'Dual SBUS offers CH1-CH18 on both receivers, CH17 On, no item overlaps or leaves the list',
      lib.verdict(seen.sbus1 === 18 && seen.sbus2 === 18 && seen.ch17 === 'On' && seen.overlaps === 0 && seen.spill === 0),
      `SBUS1 ${seen.sbus1}, SBUS2 ${seen.sbus2}, CH17 ${seen.ch17}, overlaps ${seen.overlaps}, spilled ${seen.spill}`);
    report.add('b', 'Three axis tiles in one row, every capture whole, nothing leaves a tile',
      lib.verdict(seen.tileTops.length === 1 && seen.tilesInside && seen.clipped.length === 0 && seen.tileSpill === 0),
      `rows ${seen.tileTops.length}, clipped ${JSON.stringify(seen.clipped)}, tiles spilling ${seen.tileSpill}`);
    report.add('c', 'Boot hold, rest warning, trigger count and frame rate are shown',
      lib.verdict(seen.hold === 'Center both drive sticks to drive.' && seen.warn === 'Past MIN, so it reads as full travel.'
        && seen.capacity === '2 of 11 used' && /71 frames\/s/.test(seen.health)),
      `"${seen.hold}" / "${seen.warn}" / "${seen.capacity}" / ${seen.health}`);
    await page.locator('.card', { has: page.locator('#rc-axes') }).screenshot({ path: `${ARTIFACTS}/sticks.png` });

    await page.click(`${tile('drive_steer')} .cal-set[data-axis-set="max"]`);
    await page.waitForFunction((sel) => /Saved MAX 1700/.test(document.querySelector(`${sel} .cal-note`)?.textContent || ''),
      tile('drive_steer'), { timeout: 8000 }).catch(() => {});
    const steer = (await page.locator(tile('drive_steer')).textContent()).replace(/\s+/g, ' ');
    const first = state.posts[0];
    report.add('d', 'Set MAX on Steer posts the map unchanged with the live reading, and reads MAX back',
      lib.verdict(state.posts.length === 1 && JSON.stringify(first.calibration) === '{"drive_steer":{"max":1700}}'
        && JSON.stringify(first.map) === JSON.stringify(state.map) && /MAX 1700/.test(steer) && /Saved MAX 1700\./.test(steer)),
      `${JSON.stringify(first?.calibration)} / ${steer}`);

    await page.click(`${tile('drive_speed')} [data-axis-reverse]`);
    await page.waitForFunction((sel) => document.querySelector(`${sel} [data-axis-reverse]`)?.getAttribute('aria-checked') === 'true',
      tile('drive_speed'), { timeout: 8000 }).catch(() => {});
    const reversed = await page.locator(`${tile('drive_speed')} [data-axis-reverse]`).getAttribute('aria-checked');
    report.add('e', 'The Reversed switch posts the other direction and reads back on',
      lib.verdict(state.posts.length === 2 && JSON.stringify(state.posts[1].calibration) === '{"drive_speed":{"reverse":true}}' && reversed === 'true'),
      `${JSON.stringify(state.posts[1]?.calibration)}, aria-checked ${reversed}`);

    await page.click(`${tile('drive_speed')} .cal-set[data-axis-set="max"]`);
    await page.waitForTimeout(300);
    const refused = (await page.locator(`${tile('drive_speed')} .cal-note`).textContent()).trim();
    report.add('f', 'MAX below CENTER is not sent, and the tile says why',
      lib.verdict(state.posts.length === 2 && refused === 'Not saved: MAX must read above CENTER.'), `"${refused}", ${state.posts.length} posts`);

    // Speed reads SBUS1: Steer is not offered on an SBUS2 channel, and is on
    // a free SBUS1 one (POST /api/rc/map refuses a split drive). The map
    // binds Steer already, so the check is of Speed against Steer's source.
    const offered = async (key) => {
      await page.click(`.rc-channel-item[data-chkey="${key}"]`);
      await page.waitForSelector('[data-action-search]', { timeout: 8000 });
      return page.locator('[data-action-select="drive_speed"]').count();
    };
    const onSbus2 = await offered('sbus2:3');
    const onSbus1 = await offered('sbus1:3');
    report.add('h', 'Speed is offered only on the receiver Steer reads',
      lib.verdict(onSbus2 === 0 && onSbus1 === 1), `SBUS2 CH3 offers Speed ${onSbus2}x, SBUS1 CH3 ${onSbus1}x`);

    const second = await openPage();
    const waiting = droid({ saved: 'single_sbus', running: 'dual_sbus' });
    await routeDroid(second.page, second.fixture, waiting);
    await openRc(second.page);
    const single = await second.page.evaluate(() => ({
      sbus1: document.querySelectorAll('.rc-channel-item[data-chkey^="sbus1:"]').length,
      sbus2: document.querySelectorAll('.rc-channel-item[data-chkey^="sbus2:"]').length,
      sub: document.getElementById('rc-mode-summary').textContent.trim(),
      line: getComputedStyle(document.getElementById('rc-mode-waiting')).display === 'none' ? '' : document.getElementById('rc-mode-waiting').textContent.trim(),
    }));
    report.add('g', 'Single SBUS saved, Dual SBUS running: SBUS1 alone, and the restart line',
      lib.verdict(single.sbus1 === 18 && single.sbus2 === 0 && /until you restart it/.test(single.line)),
      `SBUS1 ${single.sbus1}, SBUS2 ${single.sbus2}, "${single.sub}", "${single.line}"`);
    await second.page.locator('.card', { has: second.page.locator('#rc-receiver-card') }).screenshot({ path: `${ARTIFACTS}/receiver-waiting.png` });
  },
});
