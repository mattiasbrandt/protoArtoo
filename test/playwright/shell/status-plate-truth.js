// The Status Plate is mounted once and tells the truth about how fresh it is
// (#346, #360). Bench day, #355 section E.
//
// WHAT IT PROVES.
// (a) Walking every surface the served shell.js lists in SURFACES through the
//     nav, the plate (#status-plate-region), each of its eight chips and the
//     topbar's STOP are the SAME nodes from the first load to the last surface:
//     each is tagged with a JS property on first load and the tag must still be
//     on the node the document holds after every navigation. A re-mounted plate
//     loses the tag. And no chip value is ever blank - checked after each
//     navigation and, between checks, by a MutationObserver that records any
//     moment a chip's value line was empty.
// (b) The link to the droid is broken FROM THE BROWSER SIDE ONLY: the open
//     status stream is cut and page.route aborts every new /api/events and
//     /api/status request, which are the two requests data/live_reading.js and
//     data/status_stream.js hear the droid through. The plate must stop
//     claiming fresh: data-freshness leaves "live", the freshness line says
//     "Reconnecting - these are the values it last sent." (or "The droid could
//     not report its status"), never "just now", and the estop state line goes
//     back to "Estop: finding out" (data/shell.js renderPlateFreshness,
//     ESTOP_STATE_TEXT; data/live_reading.js loseContact).
// (c) The routes are lifted and the plate must come back to "live" by itself.
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
// EventSource, no network, no navigation and no layout: node identity across
// real hash navigations and a real stream error are only observable here.
//
// WHAT IT DOES NOT DO. It writes nothing to the droid: the same browser-side
// guard as stop-every-surface.js aborts every write but RC's verbose-log toggle
// and the Dashboard console's read-only `operations`/`help` (a plain visit to
// Configuration on a droid that is not set up would otherwise save guided
// Setup's visited list). It never presses STOP or any chip. The "frame" branch
// (a frame that is not a reading) needs the droid to send a bad frame and is
// left to the web suite.
//
// RUN (bench day, operator watching):
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/shell/status-plate-truth.js
//   BASE_URL=http://<board>   the controller (default http://10.0.0.22)
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:4173 HEADLESS=true against
// python3 tools/serve_editor_fixture.py (routes in ./_fixture_routes.js).
// Self-tests, each must FAIL its check: SELFTEST_REMOUNT=1 swaps the plate for
// a copy mid-walk; SELFTEST_BLANK=1 empties one chip mid-walk;
// SELFTEST_NOBREAK=1 installs the aborts but leaves the open stream up.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const http = require('node:http');
const { mkdirSync } = require('node:fs');

const BASE_URL = (process.env.BASE_URL || 'http://10.0.0.22').replace(/\/$/, '');
const HEADLESS = process.env.HEADLESS === 'true';
const FIXTURE = process.env.FIXTURE === '1';
const SELFTEST_REMOUNT = process.env.SELFTEST_REMOUNT === '1';
const SELFTEST_BLANK = process.env.SELFTEST_BLANK === '1';
const SELFTEST_NOBREAK = process.env.SELFTEST_NOBREAK === '1';
const SETTLE_MS = Number(process.env.SETTLE_MS || 1200);
const BREAK_MS = Number(process.env.BREAK_MS || 5000);
const ARTIFACT_DIR = 'output/playwright/issue-346';

const CHIPS = ['estop', 'drive', 'rclink', 'control', 'sleep', 'spd', 'domelink', 'soundlink'];
const RECONNECTING = 'Reconnecting - these are the values it last sent.';
const COULD_NOT_REPORT = 'The droid could not report its status';

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

const readPlate = (page) =>
  page.evaluate((chips) => {
    const region = document.getElementById('status-plate-region');
    return {
      freshness: region ? region.dataset.freshness : null,
      text: document.getElementById('status-plate-freshness')?.textContent || '',
      estopLine: document.getElementById('shell-estop-state')?.textContent || '',
      values: Object.fromEntries(
        chips.map((id) => [id, (document.querySelector(`#chip-${id} .status-chip-value`)?.textContent || '').trim()]),
      ),
    };
  }, CHIPS);

(async () => {
  mkdirSync(ARTIFACT_DIR, { recursive: true });
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  let fixture = null;
  let relay = null;
  const walk = [];
  const checks = [];
  const check = (name, ok, detail) => {
    checks.push({ name, ok, detail });
    console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? ` - ${detail}` : ''}`);
  };
  try {
    const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
    if (FIXTURE) fixture = await require('./_fixture_routes.js').install(context);
    relay = await startRelay(FIXTURE ? `http://127.0.0.1:${fixture.ssePort}/events` : `${BASE_URL}/api/events`);
    const page = await context.newPage();

    // The write guard, as in stop-every-surface.js, minus STOP: this script
    // presses nothing.
    const blocked = [];
    await page.route('**/*', async (route) => {
      const request = route.request();
      if (request.method() === 'GET' || request.method() === 'HEAD') return route.fallback();
      const url = request.url();
      const path = url.slice(url.indexOf('/', url.indexOf('//') + 2)).split('?')[0];
      const body = request.postData() || '';
      const allowed =
        (request.method() === 'POST' && path === '/api/rc/debug') ||
        (request.method() === 'POST' && path === '/api/console' && /^command=(operations|help)\b/.test(body));
      if (allowed) return route.fallback();
      blocked.push(`${request.method()} ${path} ${body.slice(0, 60)}`.trim());
      return route.abort('blockedbyclient');
    });
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
    const served = await page.evaluate(async () => (await fetch('/shell.js', { cache: 'no-store' })).text());
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
          const value = document.querySelector(`#chip-${id} .status-chip-value`);
          if (!value || value.textContent.trim() === '') {
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
    check('no chip was blank at any moment of the walk', blanks.length === 0, blanks.length ? [...new Set(blanks)].join(', ') : '');
    await page.screenshot({ path: `${ARTIFACT_DIR}/plate-after-walk.png` });

    // -----------------------------------------------------------------------
    // (b) Break the link from the browser side
    // -----------------------------------------------------------------------
    // Back to the Dashboard and a live plate, so the break starts from fresh.
    await page.locator('#shell-nav [data-surface-link="home"]').first().click();
    await page.waitForSelector('body[data-page="home"]');
    await page.waitForFunction(() => document.getElementById('status-plate-region').dataset.freshness === 'live', null, {
      timeout: 30000,
    });
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
    check('plate leaves "live" when the link breaks', left, left ? `after ${Date.now() - cutAt} ms` : 'still "live" 10 s after the break');
    // Past the 1.5 s "just now" window, and long enough for a reconnect try
    // or two to be refused.
    await page.waitForTimeout(Math.max(BREAK_MS - (Date.now() - cutAt), 2500));
    const broken = await readPlate(page);
    await page.screenshot({ path: `${ARTIFACT_DIR}/plate-link-broken.png` });
    console.log(`While broken: freshness=${broken.freshness}, "${broken.text}", ${broken.estopLine}`);
    check('data-freshness reads "finding-out" while broken', broken.freshness === 'finding-out', `data-freshness="${broken.freshness}"`);
    check(
      'freshness line says reconnecting or could not report',
      broken.text.includes(RECONNECTING) || broken.text.includes(COULD_NOT_REPORT),
      `"${broken.text}"`,
    );
    check('freshness line does not say "just now"', !broken.text.includes('just now'), `"${broken.text}"`);
    check('estop line goes back to finding out', broken.estopLine === 'Estop: finding out', `"${broken.estopLine}"`);
    const blankWhileBroken = Object.entries(broken.values).filter(([, value]) => value === '').map(([id]) => id);
    check('no chip is blank while broken', blankWhileBroken.length === 0, JSON.stringify(broken.values));
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
    check('plate returns to "live" on its own', back, back ? `after ${Date.now() - liftedAt} ms` : 'not live 45 s after the routes were lifted');
    check('freshness line stops saying reconnecting', !after.text.includes(RECONNECTING), `"${after.text}"`);
    check('estop line leaves finding out', after.estopLine !== 'Estop: finding out', `"${after.estopLine}"`);

    if (blocked.length) {
      console.log('\nWrites the guard stopped before they reached the droid:');
      [...new Set(blocked)].forEach((line) => console.log(`  ${line}`));
    }
  } catch (error) {
    console.error('status-plate-truth could not complete:', error);
    process.exitCode = 1;
  } finally {
    await browser.close();
    if (relay) await relay.close();
    if (fixture) await fixture.close();
  }

  const yes = (value) => (value === undefined ? '-' : value ? 'yes' : 'NO');
  console.log('\nsurface        same-node chips-filled freshness   result');
  console.log('-------------- --------- ------------ ----------- ------');
  for (const row of walk) {
    console.log(
      [row.surface.padEnd(14), yes(row.sameNode).padEnd(9), yes(row.filled).padEnd(12), String(row.freshness ?? '-').padEnd(11), row.result].join(' '),
    );
  }
  console.log('\ncheck                                                  result');
  console.log('------------------------------------------------------ ------');
  for (const item of checks) console.log(`${item.name.padEnd(54)} ${item.ok ? 'PASS' : 'FAIL'}`);
  const failed = walk.filter((row) => row.result !== 'PASS').length + checks.filter((item) => !item.ok).length;
  const total = walk.length + checks.length;
  console.log(`\n=== Status Plate truth: ${total - failed}/${total} PASS ===`);
  console.log(`Screenshots under ${ARTIFACT_DIR}`);
  if (failed || walk.length === 0 || checks.length === 0) process.exitCode = 1;
})();
