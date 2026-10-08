// bench-auto: droid estop=clear
// The Status Plate is mounted once, tells the truth about how fresh it is,
// and the droid's live state reaches every surface by one path.
//
// THE RULES IT HOLDS. Each is a standing rule of the Operator Shell and the
// Live Reading; the ticket that introduced it is history, in brackets. The
// letters name the checks in the output; the run order is A then B.
//
// A. In a browser with NO EventSource (deleted before any page script runs),
//    which is the one browser data/live_reading.js polls for: with a stream
//    there is no status poll at all, so "the stream blocked" is modelled as
//    the stream not existing rather than as a route that fails.
//   (e) One status poll for the whole shell, paused while the tab is hidden
//       (history: #419). GET /api/status is counted by its request timestamps
//       while the Dashboard, Foot Drive and Sound are each on screen in turn
//       (the surfaces that once polled on their own at 2, 3 and 5 s). Every
//       gap between two reads must be 4.0-6.5 s (POLL_MS = 5000,
//       data/live_reading.js:31) - a second poller shows as a short gap. Then
//       the tab is made hidden (document.visibilityState overridden and
//       visibilitychange dispatched, which is exactly what the poll and the
//       stream read): no read for 12 s. Made visible again, one read must
//       follow within 2 s (refreshOnReturn).
//   (f) Leaving a surface stops its polling (history: #360;
//       data/page_bootstrap.js createSurfacePoll). RC is opened, and it must be
//       seen asking for GET /api/rc at its 1 s cadence (at least 2 reads in
//       3 s - with no stream that is its cadence, data/rc.js); then the
//       Dashboard is opened and NO read of /api/rc may start in the next 3 s.
//       The same for Servos and its once-a-second GET /api/servo/outputs
//       (data/outputs.js follow).
//
// B. In a browser with the stream, carried from its first load by a relay
//    (below).
//   (a) The plate is chrome: one node for the whole session, never blank
//       (history: #346, #360). Walking every surface the served shell.js lists
//       in SURFACES through the nav, the plate (#status-plate-region), each of
//       its eight chips and the topbar's STOP are the SAME nodes from the
//       first load to the last surface: each is tagged with a JS property on
//       first load and the tag must still be on the node the document holds
//       after every navigation. A re-mounted plate loses the tag. And no chip
//       value is ever blank - checked after each navigation and, between
//       checks, by a MutationObserver that records any moment a chip's value
//       line was empty.
//   (d) Nothing is offered to move while contact with the droid is lost
//       (history: #419). Before the break, Servos' move acts (every row's
//       drive/open/close/stop, calibrate and let go, "back to centre", "find
//       by moving") are read enabled, and a Part on the Parts picture is
//       picked whose "Open it" / "Close it" is offered. The link is then
//       broken (b), and while it is down: that act on Parts is refused and
//       says "Waiting to hear if the droid is stopped. Open waits for the
//       answer." (data/parts.js), every Servos move act is refused
//       (data/servo.js gateActs), and the ESTOP chip shows the waiting dots - all
//       three read off reading.moveActsLive, which is true only on a heard,
//       clear estop (data/live_reading.js:126).
//   (b) A plate that cannot hear the droid says so (history: #346). The link
//       is broken FROM THE BROWSER SIDE ONLY: the open status stream is cut
//       and page.route aborts every new /api/events and /api/status request,
//       which are the two requests data/live_reading.js and
//       data/status_stream.js hear the droid through. The plate must stop
//       claiming fresh: data-freshness leaves "live", the freshness line says
//       "Reconnecting - these are the values it last sent." (or "The droid
//       could not report its status"), never "just now", and the estop state
//       line goes back to "Estop: " and the waiting dots (data/shell.js
//       renderPlateFreshness, ESTOP_STATE_TEXT; data/live_reading.js
//       loseContact).
//   (c) The plate recovers by itself: the routes are lifted and it must come
//       back to "live" with nothing pressed (history: #346).
//   (h) A surface shown again says its values are old until it has answered
//       (history: #360). From the Dashboard, Servos is opened again with its
//       Outputs read aborted by page.route. The note "Last reading from before
//       you left. Asking the droid again now." (data/shell.js) must be above
//       it, and still there after at least three refused refreshes; once the
//       route is lifted it must come down.
//   (g) A latch made anywhere reaches the plate without navigating (history:
//       #346). With the plate live, the script sends POST /api/estop itself
//       through Playwright's request API - not the button, not the page - and
//       the ESTOP chip must read LATCHED and the state line "Estop: latched"
//       within 5 s, with the address and the surface unchanged. The droid
//       pushes a status on the first trigger (src/failsafe_gate.cpp:121,
//       requestStatusBroadcastNow). Last, because it latches.
//
// PRECONDITION: the estop is CLEAR. The script reads GET /api/status first and
// refuses to run otherwise: (d) needs a clear droid to prove the move acts are
// refused only because contact is lost, and (g) is a latch. It LEAVES THE
// ESTOP LATCHED ((g), the last thing it does).
//
// HOW THE STREAM IS CUT. page.route only sees NEW requests, and the status
// stream is one long-lived request opened at boot; going offline in the browser
// (context.setOffline) was measured on 2026-09-27 NOT to end an open
// EventSource in Chromium. So /api/events is carried, from the first load, by a
// small relay this script runs on the host: the browser's stream goes to the
// relay, and the relay makes the one stream request to the droid. Cutting the
// relay's sockets is the browser's link dropping, the way a WiFi fade would
// drop it; the droid only sees its stream client go away. The relay holds the
// one stream slot the browser would have held, not a second one.
//
// WHY A REAL BROWSER. mini_dom (test/test_web/helpers/mini_dom.js) has no
// EventSource, no network, no timers against a real clock, no navigation and
// no layout: node identity across real hash navigations, a real stream error,
// a real poll cadence and a request that is or is not sent after a surface is
// left are only observable here.
//
// WHAT IT WRITES. One thing: the POST /api/estop of (g), sent by the
// script and not by a page. Everything a PAGE sends goes through the same
// browser-side guard as stop-every-surface.js, which aborts every write but
// RC's verbose-log toggle (POST /api/rc/debug, runtime only) and the Dashboard
// console's read-only `operations`/`help` (a plain visit to Configuration on a
// droid that is not set up would otherwise save guided Setup's visited list).
// It never presses STOP, any chip, or any act; picking a Part on the Parts
// picture only selects it (data/parts.js, "THE VIEW NEVER WRITES"). It never
// releases the estop. The "frame" branch (a frame that is not a reading) needs
// the droid to send a bad frame and is left to the web suite.
//
// RUN (operator watching; about 3 minutes):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/shell/status-plate-truth.js
//   BASE_URL=http://<board>   the controller (default http://10.0.0.22)
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:4173 HEADLESS=true against
// python3 tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js,
// its 'bench' droid; the outside latch goes to the fixture's own small server,
// which page.route never sees either).
// Self-tests, each must FAIL the check it names:
//   SELFTEST_REMOUNT=1   swaps the plate for a copy mid-walk (a);
//   SELFTEST_BLANK=1     empties one chip mid-walk (a);
//   SELFTEST_NOBREAK=1   installs the aborts but leaves the open stream up (b);
//   SELFTEST_UNGATE=1    re-enables every Servos move act once the link is
//                        down (d);
//   SELFTEST_DOUBLEPOLL=1 adds a second status reader every 2.5 s in the
//                        no-stream browser, hidden or not (e);
//   SELFTEST_KEEPPOLL=1  keeps asking /api/rc once a second after RC is
//                        left (f);
//   SELFTEST_NONOTE=1    takes the resumed note down as soon as Servos is
//                        back on screen (h);
//   SELFTEST_DEAFPLATE=1 breaks the browser's link before the outside latch,
//                        so the plate cannot hear it (g).
const assert = require('node:assert/strict');
const http = require('node:http');
const { mkdirSync } = require('node:fs');
const lib = require('../_lib/checks.js');

const { BASE_URL, FIXTURE, pathOf } = lib;
const SELFTEST_REMOUNT = process.env.SELFTEST_REMOUNT === '1';
const SELFTEST_BLANK = process.env.SELFTEST_BLANK === '1';
const SELFTEST_NOBREAK = process.env.SELFTEST_NOBREAK === '1';
const SELFTEST_UNGATE = process.env.SELFTEST_UNGATE === '1';
const SELFTEST_DOUBLEPOLL = process.env.SELFTEST_DOUBLEPOLL === '1';
const SELFTEST_KEEPPOLL = process.env.SELFTEST_KEEPPOLL === '1';
const SELFTEST_NONOTE = process.env.SELFTEST_NONOTE === '1';
const SELFTEST_DEAFPLATE = process.env.SELFTEST_DEAFPLATE === '1';
const SETTLE_MS = Number(process.env.SETTLE_MS || 1200);
const BREAK_MS = Number(process.env.BREAK_MS || 5000);
const ARTIFACT_DIR = 'output/playwright/issue-346';

const CHIPS = ['estop', 'drive', 'rclink', 'control', 'sleep', 'spd', 'domelink', 'soundlink'];
const RECONNECTING = 'Reconnecting - these are the values it last sent.';
const COULD_NOT_REPORT = 'The droid could not report its status';
// data/live_reading.js POLL_MS, and the window a gap may sit in around it.
const POLL_MS = 5000;
const GAP_MIN_MS = 4000;
const GAP_MAX_MS = 6500;
// data/shell.js, the Surface resumed note, word for word.
const RESUMED = 'Last reading from before you left. Asking the droid again now.';
// data/parts.js describePick, the refused Open it while the estop is not known.
const PARTS_WAITS = 'Waiting to hear if the droid is stopped. Open waits for the answer.';
// Servos' move acts (data/servo.js gateActs; the dial's buttons only exist
// while a dial is open, and none is opened here).
const SERVO_MOVE_ACTS =
  '#outputs-table .outputs-go, #outputs-table .outputs-calibrate, #outputs-table .outputs-off, ' +
  '#outputs-card .outputs-centre, #outputs-find .parts-find';

// The stream relay (header). One upstream stream per browser stream.
const startRelay = async (upstreamUrl) => {
  const pairs = new Set();
  const server = http.createServer((req, res) => {
    const upstream = http.get(
      upstreamUrl,
      { headers: { accept: 'text/event-stream', 'cache-control': 'no-cache' } },
      (upRes) => {
        res.writeHead(upRes.statusCode, {
          'Content-Type': upRes.headers['content-type'] || 'text/event-stream',
          'Cache-Control': 'no-store',
        });
        upRes.pipe(res);
      },
    );
    const pair = { req: upstream, res };
    pairs.add(pair);
    upstream.on('error', () => res.destroy());
    res.on('close', () => {
      upstream.destroy();
      pairs.delete(pair);
    });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  return {
    port: server.address().port,
    cut: () => {
      const count = pairs.size;
      pairs.forEach(({ req, res }) => {
        res.destroy();
        req.destroy();
      });
      return count;
    },
    close: () =>
      new Promise((resolve) => {
        pairs.forEach(({ req, res }) => {
          res.destroy();
          req.destroy();
        });
        server.close(() => resolve());
        server.closeAllConnections();
      }),
  };
};

// What a slot shows: its text, or "..." when it is empty and its `waiting`
// class draws the dots (data/style.css), or '' when it shows nothing at all.
// Installed in the page before any load, since the ::after is only readable
// there.
const installShown = (page) =>
  page.addInitScript(() => {
    window.__shown = (slot) => {
      if (!slot) return '';
      const text = slot.textContent.trim();
      if (text) return text;
      return getComputedStyle(slot, '::after').content.includes('...') ? '...' : '';
    };
  });

const readPlate = (page) =>
  page.evaluate((chips) => {
    const region = document.getElementById('status-plate-region');
    return {
      freshness: region ? region.dataset.freshness : null,
      text: document.getElementById('status-plate-freshness')?.textContent || '',
      estopLine: `Estop: ${window.__shown(document.getElementById('shell-estop-value'))}`,
      values: Object.fromEntries(
        chips.map((id) => [id, window.__shown(document.querySelector(`#chip-${id} .status-chip-text`))]),
      ),
    };
  }, CHIPS);

// The browser-side write guard (header), for any page.
const allowWrite = (entry) => lib.isRcDebugToggle(entry) || lib.isConsoleCatalogLoad(entry);

// Opens a surface through the nav and waits for it to be on screen and mounted.
const goTo = async (page, surface) => {
  const here = await page.evaluate((target) => document.body.dataset.page === target, surface);
  if (!here) await page.locator(`#shell-nav [data-surface-link="${surface}"]`).first().click();
  await page.waitForSelector(`body[data-page="${surface}"]`, { timeout: 10000 });
  await page.waitForFunction(
    (target) => {
      const node = document.querySelector(`#shell-content > .surface[data-surface="${target}"]`);
      return Boolean(node && node.childElementCount > 0);
    },
    surface,
    { timeout: 20000 },
  );
};

// Hidden Tab Pause is read off document.visibilityState on every tick and on
// visibilitychange (data/page_bootstrap.js createBackgroundPoll); this sets
// both the way a browser tab going to the background does.
const setHidden = (page, hidden) =>
  page.evaluate((isHidden) => {
    if (isHidden) {
      Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
      Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
    } else {
      delete document.visibilityState;
      delete document.hidden;
    }
    document.dispatchEvent(new Event('visibilitychange'));
  }, hidden);

// The Servos move acts: how many, how many enabled.
const readServoActs = (page) =>
  page.evaluate((selector) => {
    const acts = [...document.querySelectorAll(selector)];
    return {
      total: acts.length,
      enabled: acts.filter((act) => !act.disabled && act.getAttribute('aria-disabled') !== 'true').length,
      visibleEnabled: acts.filter((act) => !act.disabled && act.checkVisibility()).length,
    };
  }, SERVO_MOVE_ACTS);

// The picked Part's act on the Parts picture.
const readPartsToggle = (page) =>
  page.evaluate(() => {
    const act = document.querySelector('#bodyview-panel [data-act="toggle"]');
    return {
      present: Boolean(act),
      hidden: act ? act.hidden : true,
      enabled: Boolean(act && !act.disabled && act.getAttribute('aria-disabled') === 'false'),
      label: act ? act.textContent : '',
      why: document.querySelector('#bodyview-panel .bodyview-panel-why')?.textContent || '',
      title: document.querySelector('#bodyview-panel .bodyview-panel-title')?.textContent || '',
    };
  });

(async () => {
  mkdirSync(ARTIFACT_DIR, { recursive: true });
  const browser = await lib.launchBrowser();
  let fixture = null;
  let relay = null;
  let refused = null;
  let latchedByScript = false;
  const walk = [];
  const report = lib.createReport('Status Plate truth');
  const check = (id, name, ok, detail) => report.add(id, name, lib.verdict(ok), detail);
  const guarded = [];
  try {
    // =======================================================================
    // A. The browser with no stream
    // =======================================================================
    const quiet = await browser.newContext({ viewport: lib.VIEWPORT });
    if (FIXTURE) fixture = await require('../_lib/fixture_routes.js').install(quiet, { droid: 'bench' });
    await quiet.addInitScript(() => {
      delete window.EventSource;
    });
    if (SELFTEST_DOUBLEPOLL) {
      await quiet.addInitScript(() => {
        // A second status reader that knows nothing of hidden tabs.
        window.setInterval(() => window.PALiveReading?.read().catch(() => {}), 2500);
      });
    }
    const q = await quiet.newPage();
    guarded.push(await lib.installGuard(q, allowWrite));
    const started = Date.now();
    const seen = [];
    q.on('request', (request) => {
      if (request.method() !== 'GET') return;
      seen.push({ at: Date.now() - started, path: pathOf(request.url()) });
    });
    await q.goto(`${BASE_URL}/#home`, { waitUntil: 'domcontentloaded' });
    await q.waitForSelector('#status-plate-region', { timeout: 20000 });

    // PRECONDITION (header).
    refused = await lib.estopMustBe(false)({ page: q });
    if (refused) throw new Error(refused);

    await q.waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness === 'live', null, { timeout: 30000 });
    const streamless = await q.evaluate(() => window.PAStatusStream.isSupported() === false);
    const reads = (from, to) => seen.filter((entry) => entry.path === '/api/status' && entry.at >= from && entry.at < to).map((entry) => entry.at);

    // (e) One poll for the whole shell, three surfaces in turn.
    {
      const from = Date.now() - started + 500;
      await q.waitForTimeout(3 * POLL_MS + 1000);
      await goTo(q, 'drive');
      await q.waitForTimeout(2 * POLL_MS + 1000);
      await goTo(q, 'sound');
      await q.waitForTimeout(2 * POLL_MS + 1000);
      const to = Date.now() - started;
      const at = reads(from, to);
      const gaps = at.slice(1).map((time, index) => time - at[index]);
      const bad = gaps.filter((gap) => gap < GAP_MIN_MS || gap > GAP_MAX_MS);
      const expected = Math.floor((to - from) / POLL_MS);
      check(
        'e',
        'one GET /api/status per ~5 s across Dashboard, Foot Drive, Sound',
        streamless && at.length >= expected - 1 && at.length <= expected + 1 && bad.length === 0,
        `${streamless ? '' : 'the browser still reports a stream, so nothing here was the fallback; '}` +
          `${at.length} reads in ${((to - from) / 1000).toFixed(1)} s (expected ${expected}±1), gaps ${gaps.map((gap) => (gap / 1000).toFixed(2)).join(', ')} s`,
      );
    }
    // (e) ...and none while the tab is hidden.
    {
      await setHidden(q, true);
      const from = Date.now() - started;
      await q.waitForTimeout(12000);
      const hiddenReads = reads(from, Date.now() - started);
      const back = Date.now() - started;
      await setHidden(q, false);
      await q.waitForTimeout(2000);
      const returnReads = reads(back, Date.now() - started);
      check(
        'e',
        'the status poll pauses while the tab is hidden, asks on return',
        hiddenReads.length === 0 && returnReads.length >= 1,
        `${hiddenReads.length} reads in 12 s hidden; ${returnReads.length} within 2 s of coming back` +
          (returnReads.length ? ` (after ${returnReads[0] - back} ms)` : ''),
      );
    }

    // (f) Leaving a surface stops its polling.
    for (const { surface, path, label } of [
      { surface: 'rc', path: '/api/rc', label: 'RC Control' },
      { surface: 'servo', path: '/api/servo/outputs', label: 'Servos' },
    ]) {
      const count = (from, to) => seen.filter((entry) => entry.path === path && entry.at >= from && entry.at < to).length;
      await goTo(q, surface);
      await q.waitForTimeout(1000);
      const onFrom = Date.now() - started;
      await q.waitForTimeout(3000);
      const onCount = count(onFrom, Date.now() - started);
      await q.locator('#shell-nav [data-surface-link="home"]').first().click();
      await q.waitForSelector('body[data-page="home"]', { timeout: 10000 });
      const leftAt = Date.now() - started;
      let keep = null;
      if (SELFTEST_KEEPPOLL && surface === 'rc') {
        keep = await q.evaluate((target) => window.setInterval(() => fetch(target, { cache: 'no-store' }).catch(() => {}), 1000), path);
      }
      await q.waitForTimeout(3000);
      if (keep !== null) await q.evaluate((id) => window.clearInterval(id), keep);
      const afterCount = count(leftAt, Date.now() - started);
      check(
        'f',
        `leaving ${label} stops GET ${path}`,
        onCount >= 2 && afterCount === 0,
        `${onCount} reads in 3 s on ${label}${onCount >= 2 ? '' : ' (its poll was never seen running, so leaving proves nothing)'}; ${afterCount} in the 3 s after leaving`,
      );
    }
    await q.screenshot({ path: `${ARTIFACT_DIR}/no-stream-browser.png` });
    await quiet.close();

    // =======================================================================
    // B. The browser with the stream
    // =======================================================================
    const context = await browser.newContext({ viewport: lib.VIEWPORT });
    if (fixture) await fixture.addContext(context);
    relay = await startRelay(FIXTURE ? `http://127.0.0.1:${fixture.ssePort}/events` : `${BASE_URL}/api/events`);
    const page = await context.newPage();
    await installShown(page);
    guarded.push(await lib.installGuard(page, allowWrite));
    // The stream goes through the relay from the very first load.
    await page.route('**/api/events*', (route) => route.continue({ url: `http://127.0.0.1:${relay.port}/events` }));

    await page.goto(`${BASE_URL}/#home`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('#status-plate-region', { timeout: 20000 });
    await page.waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness === 'live', null, {
      timeout: 30000,
    });

    // -----------------------------------------------------------------------
    // (a) One plate, never blank, across every surface
    // -----------------------------------------------------------------------
    // The shell's script as the droid serves it: inside its staged bundle on an
    // image that bundles (#461, tools/gzip_fsdata.py SCRIPT_BUNDLES), else /shell.js.
    const served = await page.evaluate(async () => {
      const chain = (document.documentElement.getAttribute('data-scripts') || '').split(',').map((name) => name.trim());
      const source = chain.includes('/bundle_shell.js') ? '/bundle_shell.js' : '/shell.js';
      return (await fetch(source, { cache: 'no-store' })).text();
    });
    const surfaces = [...served.matchAll(/page:\s*"([^"]+)"\s*,\s*doc:\s*"/g)].map((match) => match[1]);
    assert.ok(surfaces.length >= 10, `read only ${surfaces.length} surfaces out of the served shell.js`);
    console.log(`Surfaces in the served shell.js (${surfaces.length}): ${surfaces.join(', ')}`);

    await page.evaluate((chips) => {
      const region = document.getElementById('status-plate-region');
      region.__plateTag = 'first-load';
      document.getElementById('status-plate').__plateTag = 'first-load';
      document.getElementById('shell-estop-button').__plateTag = 'first-load';
      chips.forEach((id) => {
        document.getElementById(`chip-${id}`).__plateTag = 'first-load';
      });
      // Any moment a chip's value line is empty, between the checks below.
      window.__blankChips = [];
      const plate = document.getElementById('status-plate');
      new MutationObserver(() => {
        chips.forEach((id) => {
          if (window.__shown(document.querySelector(`#chip-${id} .status-chip-text`)) === '') {
            window.__blankChips.push(`${id} on ${document.body.dataset.page}`);
          }
        });
      }).observe(plate, { childList: true, characterData: true, subtree: true });
    }, CHIPS);

    for (const [index, surface] of surfaces.entries()) {
      const row = { surface, reasons: [] };
      walk.push(row);
      try {
        const here = await page.evaluate((target) => document.body.dataset.page === target, surface);
        const link = page.locator(`#shell-nav [data-surface-link="${surface}"]`).first();
        if (!here) {
          if (await link.count()) await link.click();
          else {
            row.reasons.push('no nav entry');
            await page.evaluate((target) => {
              window.location.hash = target;
            }, surface);
          }
        }
        await page.waitForSelector(`body[data-page="${surface}"]`, { timeout: 10000 });
        await page.waitForFunction(
          (target) => {
            const node = document.querySelector(`#shell-content > .surface[data-surface="${target}"]`);
            return Boolean(node && node.childElementCount > 0);
          },
          surface,
          { timeout: 20000 },
        );
        await page.waitForTimeout(SETTLE_MS);

        if (SELFTEST_REMOUNT && index === 3) {
          await page.evaluate(() => {
            const region = document.getElementById('status-plate-region');
            region.replaceWith(region.cloneNode(true));
          });
        }
        if (SELFTEST_BLANK && index === 2) {
          await page.evaluate(() => {
            document.querySelector('#chip-spd .status-chip-value').textContent = '';
          });
        }

        const identity = await page.evaluate((chips) => {
          const tagged = (el) => Boolean(el && el.__plateTag === 'first-load');
          return {
            region: tagged(document.getElementById('status-plate-region')),
            plate: tagged(document.getElementById('status-plate')),
            stop: tagged(document.getElementById('shell-estop-button')),
            chips: chips.filter((id) => !tagged(document.getElementById(`chip-${id}`))),
          };
        }, CHIPS);
        row.sameNode = identity.region && identity.plate && identity.chips.length === 0;
        if (!identity.region) row.reasons.push('#status-plate-region is a new node');
        if (!identity.plate) row.reasons.push('#status-plate is a new node');
        if (identity.chips.length) row.reasons.push(`chips re-made: ${identity.chips.join(', ')}`);
        if (!identity.stop) row.reasons.push('STOP is a new node');

        const plate = await readPlate(page);
        const blank = Object.entries(plate.values).filter(([, value]) => value === '').map(([id]) => id);
        row.filled = blank.length === 0;
        if (blank.length) row.reasons.push(`blank chips: ${blank.join(', ')}`);
        row.freshness = plate.freshness;
      } catch (error) {
        row.reasons.push(`could not run: ${String(error.message).split('\n')[0]}`);
      }
      row.result = row.reasons.length ? 'FAIL' : 'PASS';
      console.log(`${row.result} walk ${surface}${row.reasons.length ? ` - ${row.reasons.join('; ')}` : ''}`);
    }
    const blanks = await page.evaluate(() => window.__blankChips.slice());
    check('a', 'no chip was blank at any moment of the walk', blanks.length === 0, blanks.length ? [...new Set(blanks)].join(', ') : '');
    await page.screenshot({ path: `${ARTIFACT_DIR}/plate-after-walk.png` });

    // -----------------------------------------------------------------------
    // (d) before the break: what is offered while the droid is heard
    // -----------------------------------------------------------------------
    await goTo(page, 'servo');
    await page.waitForFunction(() => window.PAOutputs?.known().table, null, { timeout: 20000 });
    await page.waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness === 'live', null, { timeout: 30000 });
    const servoBefore = await readServoActs(page);
    console.log(`Servos before the break: ${servoBefore.enabled} of ${servoBefore.total} move acts enabled (${servoBefore.visibleEnabled} on screen)`);

    await goTo(page, 'parts');
    await page.waitForFunction(() => window.PAOutputs?.known().table, null, { timeout: 20000 });
    // A Part whose Open it / Close it is offered: each face in turn, each
    // marker on it picked (selection only) until one offers the act.
    let partsBefore = null;
    for (const face of await page.locator('#bodyview-drawing [data-face-tab]').evaluateAll((tabs) => tabs.map((tab) => tab.dataset.faceTab))) {
      await page.click(`#bodyview-drawing [data-face-tab="${face}"]`);
      const markers = await page.locator(`#bodyview-drawing .bv-face[data-face="${face}"] [data-marker]`).evaluateAll((cells) =>
        cells.filter((cell) => cell.getBoundingClientRect().width > 0).map((cell) => cell.dataset.marker),
      );
      for (const marker of markers) {
        await page.locator(`#bodyview-drawing [data-marker="${marker}"]`).first().click();
        const toggle = await readPartsToggle(page);
        if (toggle.enabled && !toggle.hidden) {
          partsBefore = { marker, ...toggle };
          break;
        }
      }
      if (partsBefore) break;
    }
    console.log(
      partsBefore
        ? `Parts before the break: "${partsBefore.label}" offered for ${partsBefore.title} (${partsBefore.marker})`
        : 'Parts before the break: no Part on the picture offered Open it / Close it',
    );

    // -----------------------------------------------------------------------
    // (b) Break the link from the browser side
    // -----------------------------------------------------------------------
    const before = await readPlate(page);
    console.log(`Before the break: freshness=${before.freshness}, "${before.text}", ${before.estopLine}`);

    const aborted = [];
    const abort = (route) => {
      aborted.push(route.request().url());
      return route.abort('internetdisconnected');
    };
    await page.route('**/api/events*', abort);
    await page.route('**/api/status*', abort);
    const cutAt = Date.now();
    const cutCount = SELFTEST_NOBREAK ? 0 : relay.cut();
    console.log(`Link broken in the browser: ${cutCount} open stream(s) cut; new /api/events and /api/status aborted.`);

    const left = await page
      .waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness !== 'live', null, { timeout: 10000 })
      .then(() => true, () => false);
    check('b', 'plate leaves "live" when the link breaks', left, left ? `after ${Date.now() - cutAt} ms` : 'still "live" 10 s after the break');

    // (d) while broken: Parts first (on screen), then Servos.
    await page.waitForTimeout(500);
    const partsBroken = await readPartsToggle(page);
    await page.screenshot({ path: `${ARTIFACT_DIR}/parts-contact-lost.png` });
    check(
      'd',
      'Parts refuses Open it while contact is lost, and says why',
      partsBefore !== null && !partsBroken.enabled && partsBroken.why === PARTS_WAITS,
      partsBefore === null
        ? 'no Part offered the act before the break, so its refusal proves nothing'
        : `"${partsBroken.label}" enabled=${partsBroken.enabled}; says "${partsBroken.why}"`,
    );
    await goTo(page, 'servo');
    if (SELFTEST_UNGATE) {
      await page.evaluate((selector) => {
        document.querySelectorAll(selector).forEach((act) => {
          act.disabled = false;
          act.setAttribute('aria-disabled', 'false');
        });
      }, SERVO_MOVE_ACTS);
    }
    const servoBroken = await readServoActs(page);
    const estopChip = (await readPlate(page)).values.estop;
    await page.screenshot({ path: `${ARTIFACT_DIR}/servo-contact-lost.png` });
    check(
      'd',
      'every Servos move act is refused while contact is lost',
      servoBefore.visibleEnabled > 0 && servoBroken.total > 0 && servoBroken.enabled === 0,
      `before: ${servoBefore.enabled}/${servoBefore.total} enabled; while lost: ${servoBroken.enabled}/${servoBroken.total} enabled` +
        (servoBefore.visibleEnabled > 0 ? '' : ' (none were enabled before, so the refusal proves nothing)'),
    );
    check('d', 'the ESTOP chip shows the waiting dots while contact is lost', estopChip === '...', `"${estopChip}"`);

    // Past the 1.5 s "just now" window, and long enough for a reconnect try
    // or two to be refused.
    await page.waitForTimeout(Math.max(BREAK_MS - (Date.now() - cutAt), 2500));
    const broken = await readPlate(page);
    await page.screenshot({ path: `${ARTIFACT_DIR}/plate-link-broken.png` });
    console.log(`While broken: freshness=${broken.freshness}, "${broken.text}", ${broken.estopLine}`);
    check('b', 'data-freshness reads "waiting" while broken', broken.freshness === 'waiting', `data-freshness="${broken.freshness}"`);
    check(
      'b',
      'freshness line says reconnecting or could not report',
      broken.text.includes(RECONNECTING) || broken.text.includes(COULD_NOT_REPORT),
      `"${broken.text}"`,
    );
    check('b', 'freshness line does not say "just now"', !broken.text.includes('just now'), `"${broken.text}"`);
    check('b', 'estop line goes back to the waiting dots', broken.estopLine === 'Estop: ...', `"${broken.estopLine}"`);
    const blankWhileBroken = Object.entries(broken.values).filter(([, value]) => value === '').map(([id]) => id);
    check('b', 'no chip is blank while broken', blankWhileBroken.length === 0, JSON.stringify(broken.values));
    console.log(`Requests refused during the break: ${aborted.length}`);

    // -----------------------------------------------------------------------
    // (c) Lift the routes; the plate must come back by itself
    // -----------------------------------------------------------------------
    await page.unroute('**/api/events*', abort);
    await page.unroute('**/api/status*', abort);
    const liftedAt = Date.now();
    const back = await page
      .waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness === 'live', null, { timeout: 45000 })
      .then(() => true, () => false);
    const after = await readPlate(page);
    await page.screenshot({ path: `${ARTIFACT_DIR}/plate-recovered.png` });
    console.log(`After the routes were lifted: freshness=${after.freshness}, "${after.text}", ${after.estopLine}`);
    check('c', 'plate returns to "live" on its own', back, back ? `after ${Date.now() - liftedAt} ms` : 'not live 45 s after the routes were lifted');
    check('c', 'freshness line stops saying reconnecting', !after.text.includes(RECONNECTING), `"${after.text}"`);
    check('c', 'estop line leaves the waiting dots', after.estopLine !== 'Estop: ...', `"${after.estopLine}"`);

    // -----------------------------------------------------------------------
    // (h) The Surface resumed note stays up while the refresh fails
    // -----------------------------------------------------------------------
    {
      await goTo(page, 'home');
      await page.waitForTimeout(1500);
      const refusedReads = [];
      const refuse = (route) => {
        refusedReads.push(Date.now());
        return route.abort('internetdisconnected');
      };
      await page.route('**/api/servo/outputs*', refuse);
      await page.locator('#shell-nav [data-surface-link="servo"]').first().click();
      await page.waitForSelector('body[data-page="servo"]', { timeout: 10000 });
      const noteNow = () =>
        page.evaluate(() => {
          const note = document.querySelector('#shell-content > .surface-resumed');
          return note ? note.textContent : null;
        });
      if (SELFTEST_NONOTE) await page.evaluate(() => document.querySelector('#shell-content > .surface-resumed')?.remove());
      const onReturn = await noteNow();
      const returnedAt = Date.now();
      await page.waitForTimeout(3500);
      const refusedSince = refusedReads.filter((at) => at >= returnedAt - 500).length;
      const whileFailing = await noteNow();
      await page.screenshot({ path: `${ARTIFACT_DIR}/servo-resumed-note.png` });
      await page.unroute('**/api/servo/outputs*', refuse);
      const cleared = await page
        .waitForFunction(() => !document.querySelector('#shell-content > .surface-resumed'), null, { timeout: 8000 })
        .then(() => true, () => false);
      check(
        'h',
        'resumed note stays up on Servos while its refresh fails',
        onReturn === RESUMED && whileFailing === RESUMED && refusedSince >= 3,
        `on return: ${onReturn === null ? 'no note' : `"${onReturn}"`}; after ${refusedSince} refused reads: ${whileFailing === null ? 'no note' : 'still up'}`,
      );
      check('h', 'resumed note comes down once Servos answers', cleared, cleared ? '' : 'still up 8 s after the Outputs read was let through');
    }

    // -----------------------------------------------------------------------
    // (g) A latch from outside the browser reaches the plate, no navigation
    // -----------------------------------------------------------------------
    {
      await page.waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness === 'live', null, { timeout: 30000 });
      const plateBefore = await readPlate(page);
      const where = await page.evaluate(() => ({ page: document.body.dataset.page, hash: window.location.hash }));
      if (SELFTEST_DEAFPLATE) {
        const deaf = (route) => route.abort('internetdisconnected');
        await page.route('**/api/events*', deaf);
        await page.route('**/api/status*', deaf);
        relay.cut();
      }
      const target = FIXTURE ? `http://127.0.0.1:${fixture.ssePort}` : BASE_URL;
      const sentAt = Date.now();
      const response = await context.request.post(`${target}/api/estop`, { timeout: 5000 });
      latchedByScript = response.ok();
      console.log(`Outside latch: POST ${target}/api/estop answered ${response.status()} (plate showed ESTOP ${plateBefore.values.estop})`);
      const reached = await page
        .waitForFunction(
          () =>
            document.querySelector('#chip-estop .status-chip-value')?.textContent.trim() === 'LATCHED' &&
            document.getElementById('shell-estop-state')?.textContent === 'Estop: latched',
          null,
          { timeout: 5000 },
        )
        .then(() => true, () => false);
      const tookMs = Date.now() - sentAt;
      const whereAfter = await page.evaluate(() => ({ page: document.body.dataset.page, hash: window.location.hash }));
      const plateAfter = await readPlate(page);
      await page.screenshot({ path: `${ARTIFACT_DIR}/plate-outside-latch.png` });
      check(
        'g',
        'a latch sent outside the browser reaches the plate without navigating',
        response.ok() && plateBefore.values.estop === 'CLEAR' && reached && whereAfter.page === where.page && whereAfter.hash === where.hash,
        `ESTOP ${plateBefore.values.estop} -> ${plateAfter.values.estop}, "${plateAfter.estopLine}"` +
          (reached ? ` after ${tookMs} ms` : ' - not within 5 s') +
          `; on ${where.page}${whereAfter.page === where.page && whereAfter.hash === where.hash ? ' throughout' : `, moved to ${whereAfter.page}`}`,
      );
    }

    const blocked = guarded.flatMap((writes) => lib.blockedWrites(writes));
    if (blocked.length) {
      console.log('\nWrites the guard stopped before they reached the droid:');
      [...new Set(blocked)].forEach((line) => console.log(`  ${line}`));
    }
  } catch (error) {
    if (!refused) {
      console.error('status-plate-truth could not complete:', error);
      process.exitCode = 1;
    }
  } finally {
    await lib.closeAll(browser, [relay, fixture]);
  }
  if (refused) {
    lib.notAssessed(refused);
    return;
  }

  const yes = (value) => (value === undefined ? '-' : value ? 'yes' : 'NO');
  console.log('\nsurface        same-node chips-filled freshness   result');
  console.log('-------------- --------- ------------ ----------- ------');
  for (const row of walk) {
    console.log(
      [row.surface.padEnd(14), yes(row.sameNode).padEnd(9), yes(row.filled).padEnd(12), String(row.freshness ?? '-').padEnd(11), row.result].join(' '),
    );
  }
  report.print();
  const failed = walk.filter((row) => row.result !== 'PASS').length + report.rows.filter((row) => row.result !== lib.PASS).length;
  const total = walk.length + report.rows.length;
  console.log(`\n=== Status Plate truth: ${total - failed}/${total} PASS ===`);
  console.log(`Screenshots under ${ARTIFACT_DIR}`);
  if (latchedByScript) {
    console.log('\nThe estop is LATCHED on the droid: this script latched it from outside the browser (check g). It does not release it: STOP (lit) releases it when you are ready.');
  }
  if (failed || walk.length === 0 || report.rows.length < 20) process.exitCode = 1;
})();
