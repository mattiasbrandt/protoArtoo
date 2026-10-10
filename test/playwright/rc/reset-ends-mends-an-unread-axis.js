// bench-auto: fixture rc.html
// RC's Sticks card for an axis the droid keeps but does not read for its ends
// alone: a dead zone stored before the rule that swallows one side of the
// stick (ADR 0070). Its tile says why and offers Reset ends, which posts the
// map back with the axis in it and without its ends, so the droid starts it
// from the default ends. Introduced by #486.
//
// PRECONDITION: FIXTURE=1. The droid here is this script's own: a real droid
// refuses to store such ends, and the reset writes the RC Map; against a droid
// the script refuses to run (NOT ASSESSED).
//
// WHAT IT PROVES, at 1440x900:
//   a  Speed's tile says it is not read and why, in words, offers Reset ends
//      and no Set MIN/CENTER/MAX, and does not say "Map it again".
//   b  The tile sits in the row with the other two, the button shows its whole
//      word, and nothing leaves the tile.
//   c  Reset ends posts the map with Speed in it and Steer kept, without ends.
//   d  The tile is then drawn as one the droid reads: Set MIN/CENTER/MAX, the
//      default ends, and "Saved the default ends."
//
// WHY A REAL BROWSER. Layout and the click on the drawn button are what a DOM
// stub cannot see; the page module tests prove the words and the post.
//
// RUN (offline only):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true \
//     node test/playwright/rc/reset-ends-mends-an-unread-axis.js
// against tools/serve_editor_fixture.py, or `make pw-fixture DIR=rc`. For a
// look, leave HEADLESS unset and add STEP=1: it waits before and after the
// reset.
// Self-test: SELFTEST=stale has the droid keep Speed unread after the reset;
// row d must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/rc';
const RECEIVERS = { read: ['sbus1', 'sbus2'], drive: ['sbus1'], cues: ['sbus1', 'sbus2'] };
const DEFAULT_ENDS = { min: 172, center: 992, max: 1811, deadband: 0, reverse: false };

// One droid: Speed stored with a dead zone wider than CENTER sits from MIN.
const droid = ({ stale }) => {
  const state = {
    speedRead: false,
    posts: [],
    profile: {
      driveSpeed: { source: 'sbus1', channel: 1, min: 172, center: 300, max: 1811, deadband: 200, reverse: false },
      driveSteer: { source: 'sbus1', channel: 2, ...DEFAULT_ENDS },
      domeSpeed: { source: 'none', channel: 0, min: 0, center: 0, max: 0, deadband: 0, reverse: false },
    },
  };
  state.map = () => [
    state.speedRead
      ? { source: 'sbus1', channel: 1, action: 'drive_speed' }
      : { source: 'sbus1', channel: 1, action: 'drive_speed', read: false, field: 'calibration.deadband', reason: 'conflict' },
    { source: 'sbus1', channel: 2, action: 'drive_steer' },
  ];
  state.diag = () => ({
    mode: 'dual_sbus',
    updatedMs: 1,
    driveAwaitingCentre: false,
    sources: {
      sbus1: { enabled: true, linked: true, ageMs: 11, lostFrames: 0, failsafe: false, framesPerSecond: 71, decodeFails: 0 },
      sbus2: { enabled: true, linked: false, ageMs: 0, lostFrames: 0, failsafe: false, framesPerSecond: 0, decodeFails: 0 },
      pwm: { enabled: false, linked: false, ageMs: 0, lostFrames: 0, failsafe: false },
    },
    channels: [],
    rawDigital: { sbus1: [false, false], sbus2: [false, false] },
    digital: {},
    mappingProfile: { version: 1, channels: state.profile },
    raw: { sbus1: Array.from({ length: 16 }, () => 992), sbus2: Array.from({ length: 16 }, () => 992) },
    reactions: [],
  });
  // What the droid does with a save (assignRcMapEntryToSnapshot(), #486): an
  // axis mapped again on its own RC Channel keeps only ends the rules take,
  // so Speed starts from the defaults.
  state.save = (body) => {
    state.posts.push(body);
    if (stale) return;
    if ((body.map || []).some((entry) => entry.action === 'drive_speed' && entry.source === 'sbus1' && entry.channel === 1)) {
      state.speedRead = true;
      Object.assign(state.profile.driveSpeed, DEFAULT_ENDS);
    }
  };
  return state;
};

const routeDroid = async (page, fixture, state) => {
  const json = (route, body) => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
  await page.route('**/api/config*', (route) => {
    if (route.request().method() !== 'GET') return route.fallback();
    const config = fixture.state.config;
    return json(route, { ...config, rc: { ...(config.rc || {}), inputMode: 'dual_sbus', activeInputMode: 'dual_sbus', sbus: { recvCh2: false } } });
  });
  await page.route('**/api/rc', (route) => json(route, state.diag()));
  await page.route('**/api/rc/map', (route) => {
    if (route.request().method() !== 'POST') {
      const map = state.map();
      return json(route, { mode: 'dual_sbus', map, capacity: { total: 14, used: map.length }, receivers: RECEIVERS });
    }
    state.save(JSON.parse(new URLSearchParams(route.request().postData() || '').get('plain') || '{}'));
    return json(route, { ok: true });
  });
  await page.route('**/api/actions', (route) => json(route, [
    { token: 'drive_speed', display_name: 'Speed', domain: 'drive', description: '', rc_input: 'stick', reaction: false },
    { token: 'drive_steer', display_name: 'Steer', domain: 'drive', description: '', rc_input: 'stick', reaction: false },
    { token: 'dome_speed', display_name: 'Dome Speed', domain: 'dome', name: 'dome.action.set-speed', description: '', rc_input: 'stick', reaction: false },
  ]));
};

const SPEED = '.rc-axis[data-axis="drive_speed"]';

lib.runCheck({
  rule: 'RC mends an axis the droid does not read for its ends with Reset ends',
  artifactDir: ARTIFACTS,
  selftests: ['stale'],
  precondition: async () => (lib.FIXTURE ? null : 'writes the RC Map and needs ends a droid refuses to store: FIXTURE=1 only'),
  run: async ({ page, fixture, report, selftest }) => {
    const state = droid({ stale: selftest === 'stale' });
    await routeDroid(page, fixture, state);
    await page.goto(`${lib.BASE_URL}/#rc`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector(`${SPEED} [data-axis-reset]`, { timeout: 20000 });
    await page.waitForTimeout(lib.SETTLE_MS || 500);

    const before = await page.evaluate((sel) => {
      const tile = document.querySelector(sel);
      const right = tile.getBoundingClientRect().right;
      const tiles = [...document.querySelectorAll('#rc-axes .rc-axis')];
      const button = tile.querySelector('[data-axis-reset]');
      return {
        warn: tile.querySelector('.rc-axis-warn')?.textContent.trim() || '',
        text: tile.textContent.replace(/\s+/g, ' ').trim(),
        reset: button?.textContent.trim() || '',
        disabled: Boolean(button?.disabled),
        sets: tile.querySelectorAll('[data-axis-set]').length,
        rows: new Set(tiles.map((el) => Math.round(el.getBoundingClientRect().top))).size,
        clipped: button ? button.scrollWidth > button.clientWidth + 1 : true,
        spill: [...tile.querySelectorAll('*')].filter((child) => child.getBoundingClientRect().right > right + 0.5).length,
      };
    }, SPEED);
    report.add('a', 'Speed says it is not read and why, and offers Reset ends instead of ends to set',
      lib.verdict(before.warn === 'Not read: Dead zone is wider than CENTER sits from an end.' && before.reset === 'Reset ends'
        && !before.disabled && before.sets === 0 && !/Map it again/.test(before.text)),
      `"${before.warn}" / "${before.reset}"${before.disabled ? ' (disabled)' : ''} / Set buttons ${before.sets}`);
    report.add('b', 'The tile sits in the row, the button shows its whole word, nothing leaves the tile',
      lib.verdict(before.rows === 1 && !before.clipped && before.spill === 0),
      `rows ${before.rows}, clipped ${before.clipped}, spilling ${before.spill}`);
    await page.locator('.card', { has: page.locator('#rc-axes') }).screenshot({ path: `${ARTIFACTS}/reset-ends-before.png` });
    await lib.step('Speed is not read for its ends.');

    await page.click(`${SPEED} [data-axis-reset]`);
    await page.waitForFunction((sel) => /Saved the default ends/.test(document.querySelector(`${sel} .cal-note`)?.textContent || ''),
      SPEED, { timeout: 8000 }).catch(() => {});
    await page.waitForTimeout(300);
    const posted = state.posts[0] || {};
    const entry = (action) => JSON.stringify((posted.map || []).find((each) => each.action === action) || null);
    report.add('c', 'Reset ends posts the map with Speed in it and Steer kept, without ends',
      lib.verdict(state.posts.length === 1 && posted.calibration === undefined
        && entry('drive_speed') === '{"source":"sbus1","channel":1,"action":"drive_speed"}'
        && entry('drive_steer') === '{"source":"sbus1","channel":2,"action":"drive_steer"}'),
      `${state.posts.length} post(s): ${JSON.stringify(posted)}`);

    const after = await page.evaluate((sel) => {
      const tile = document.querySelector(sel);
      return {
        sets: tile.querySelectorAll('[data-axis-set]').length,
        ends: tile.querySelector('.cal-ends')?.textContent.trim() || '',
        note: tile.querySelector('.cal-note')?.textContent.trim() || '',
      };
    }, SPEED);
    report.add('d', 'Speed is drawn as read again, on the default ends, and says it saved them',
      lib.verdict(after.sets === 3 && after.ends === 'MIN 172 · CENTER 992 · MAX 1811' && after.note === 'Saved the default ends.'),
      `Set buttons ${after.sets} / "${after.ends}" / "${after.note}"`);
    await page.locator('.card', { has: page.locator('#rc-axes') }).screenshot({ path: `${ARTIFACTS}/reset-ends-after.png` });
    await lib.step('Speed is read again on the default ends.');
  },
});
