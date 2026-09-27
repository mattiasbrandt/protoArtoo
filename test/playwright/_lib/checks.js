// The harness every rule script under test/playwright/<surface>/ can run on:
// the browser, the write guard, the precondition, the PASS/FAIL table and the
// close. A script supplies its rule, its precondition and its checks; this
// supplies everything a script would otherwise copy, and the write guard in
// particular is kept in one place because it is the thing that stops a check
// from reaching the droid.
//
// Conventions (test/playwright/shell/stop-every-surface.js's): BASE_URL
// (default http://10.0.0.22), headed unless HEADLESS=true, STEP=1 the only
// thing that waits on stdin, 1440x900, FIXTURE=1 for the offline proof against
// tools/serve_editor_fixture.py with ./fixture_routes.js under it, and
// SELFTEST=<name> (FIXTURE=1 only) to force a script's own checks to fail.
//
// Exit codes: 0 every row PASS (NOT ASSESSED rows allowed beside a PASS);
// 1 any row FAIL, or the script could not finish; 2 the precondition did not
// hold, or nothing could be assessed.
const { chromium } = require('playwright');
const fs = require('node:fs');
const http = require('node:http');
const readline = require('node:readline');

const BASE_URL = (process.env.BASE_URL || 'http://10.0.0.22').replace(/\/$/, '');
const HEADLESS = process.env.HEADLESS === 'true';
const STEP = process.env.STEP === '1';
const FIXTURE = process.env.FIXTURE === '1';
const SELFTEST = process.env.SELFTEST || '';
const SETTLE_MS = Number(process.env.SETTLE_MS || 1500);

const PASS = 'PASS';
const FAIL = 'FAIL';
const NOT_ASSESSED = 'NOT ASSESSED';
const verdict = (ok) => (ok ? PASS : FAIL);

const pathOf = (url) => {
  const start = url.indexOf('/', url.indexOf('//') + 2);
  return (start < 0 ? '/' : url.slice(start)).split('?')[0];
};

const formOf = (body) => Object.fromEntries(new URLSearchParams(body || ''));

const step = async (prompt) => {
  if (!STEP) return;
  await new Promise((resolve) => {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
    rl.question(`${prompt} Enter to go on... `, () => {
      rl.close();
      resolve();
    });
  });
};

// ---------------------------------------------------------------------------
// The write guard
//
// A page route, so it runs before the fixture's context routes and hands
// everything it allows on to them. Every write the page attempts is RECORDED -
// allowed or not - with its body, content type and wall-clock time, so a check
// can assert on what the page tried to send and not only on what reached the
// droid. Anything `allow` does not return true for is aborted before it
// leaves the browser.
// ---------------------------------------------------------------------------
const installGuard = async (page, allow = () => false) => {
  const writes = [];
  await page.route('**/*', async (route) => {
    const request = route.request();
    const method = request.method();
    if (method === 'GET' || method === 'HEAD') return route.fallback();
    const entry = {
      method,
      path: pathOf(request.url()),
      body: request.postData() || '',
      contentType: request.headers()['content-type'] || '',
      at: Date.now(),
    };
    entry.allowed = Boolean(allow(entry));
    writes.push(entry);
    if (entry.allowed) return route.fallback();
    return route.abort('blockedbyclient');
  });
  return writes;
};

const describeWrite = (entry) => `${entry.allowed ? 'sent' : 'BLOCKED'} ${entry.method} ${entry.path} ${entry.body.slice(0, 80)}`.trim();

// ---------------------------------------------------------------------------
// Reading the droid
// ---------------------------------------------------------------------------

// A page on the droid's origin that runs none of the app: a precondition is
// read from here, through the same routes the app's reads take.
const landNeutral = async (page) => {
  await page.goto(`${BASE_URL}/fw-version.json`, { waitUntil: 'domcontentloaded' });
};

const readJson = (page, apiPath) =>
  page.evaluate(async (target) => {
    const response = await fetch(target, { cache: 'no-store' });
    const text = await response.text();
    let json = null;
    try {
      json = JSON.parse(text);
    } catch (_error) {
      json = null;
    }
    return { status: response.status, contentType: response.headers.get('content-type') || '', json, text: text.slice(0, 200) };
  }, apiPath);

const readEstop = async (page) => {
  const answer = await readJson(page, '/api/status');
  if (answer.status !== 200 || !answer.json || typeof answer.json.estop !== 'boolean') {
    return { known: false, why: `GET /api/status answered ${answer.status}: ${answer.text}` };
  }
  return { known: true, latched: answer.json.estop === true };
};

// Preconditions on the estop, as runCheck() takes them.
const estopMustBe = (latched) => async ({ page }) => {
  const estop = await readEstop(page);
  if (!estop.known) return `the estop state could not be read: ${estop.why}`;
  if (latched && !estop.latched) return 'the estop is CLEAR and this rule is about a latched one. Latch it (STOP on any surface), then run this again.';
  if (!latched && estop.latched) return 'the estop is LATCHED. Clear it on Foot Drive or the Dashboard, then run this again.';
  return null;
};

// Whether guided Setup is drawn over Configuration on this droid, by
// data/setup.js's own rule (loadRun, configuredBeforeTheRecordExisted): the
// run is drawn until it has ended, except on a droid configured before the
// record existed - no record at all, and a Component Toggle on or a switchable
// Output ticked wired. `summaryOwed` is an ended run whose summary card has
// not been dismissed.
const guidedSetup = async (page) => {
  const config = await readJson(page, '/api/config');
  if (config.status !== 200 || !config.json) return { known: false, why: `GET /api/config answered ${config.status}: ${config.text}` };
  const guided = config.json.guidedSetup || {};
  const ended = guided.run === 'skipped' || guided.run === 'completed';
  let grandfathered = false;
  if (!ended && guided.recorded === false) {
    const components = config.json.components || {};
    grandfathered = Object.values(components).some((entry) => entry && entry.enabled === true);
    if (!grandfathered) {
      const outputs = await readJson(page, '/api/servo/outputs');
      grandfathered = (outputs.json?.outputs || []).some((row) => row.switchable === true && row.wired === true);
    }
  }
  return {
    known: true,
    record: guided,
    drawn: !ended && !grandfathered,
    summaryOwed: ended && guided.summaryDone !== true,
    why: ended ? `the run has ended (${guided.run})` : grandfathered ? 'no record, and the droid was configured before it existed' : `the run has not been made (${JSON.stringify(guided)})`,
  };
};

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------

// Every section the surface declared has settled, and every resource is in.
const waitSettled = async (page, timeoutMs = 20000) => {
  await page
    .waitForFunction(() => {
      const state = window.PABootstrap?.getState?.();
      return Boolean(state && state.resourcesReady && state.sectionsStable);
    }, null, { timeout: timeoutMs })
    .catch(() => {});
  await page.waitForTimeout(SETTLE_MS);
};

const waitMounted = (page, surface) =>
  page.waitForFunction(
    (target) => {
      const node = document.querySelector(`#shell-content > .surface[data-surface="${target}"]`);
      return Boolean(node && node.childElementCount > 0);
    },
    surface,
    { timeout: 20000 },
  );

// A page load straight onto a surface's address.
const loadSurface = async (page, surface) => {
  await page.goto(`${BASE_URL}/#${surface}`, { waitUntil: 'domcontentloaded' });
  await page.waitForSelector('#shell-estop-button', { timeout: 20000 });
  await waitMounted(page, surface);
  await waitSettled(page);
};

// A surface opened through the nav, as an operator opens it.
const openSurface = async (page, surface) => {
  const here = await page.evaluate((target) => document.body.dataset.page === target, surface);
  if (!here) {
    const link = page.locator(`#shell-nav [data-surface-link="${surface}"]`).first();
    if ((await link.count()) > 0) await link.click();
    else await page.evaluate((target) => { window.location.hash = target; }, surface);
  }
  await page.waitForSelector(`body[data-page="${surface}"]`, { timeout: 15000 });
  await waitMounted(page, surface);
  await waitSettled(page);
};

// The cards directly on a surface that are on screen, by their heading.
const visibleCards = (page, surface) =>
  page.evaluate((target) => {
    const root = document.querySelector(`#shell-content > .surface[data-surface="${target}"]`);
    return [...root.querySelectorAll('.card')]
      .filter((card) => !card.parentElement.closest('.card'))
      .filter((card) => card.checkVisibility({ checkVisibilityCSS: true }))
      .map((card) => card.querySelector('h2')?.textContent.trim() || card.id || '?');
  }, surface);

// A press where an operator's finger lands: the control's centre, whatever is
// hit-testable there - which, for a refused .btn (pointer-events: none), is
// the element beneath it.
const pressAt = async (page, locator) => {
  const box = await locator.boundingBox();
  if (!box) throw new Error('the control has no box on screen');
  await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
};

// ---------------------------------------------------------------------------
// Status frames without a write
// ---------------------------------------------------------------------------

// Counts the status frames the page hears from now on: the stream's own
// "status" events, never a replay of the one it already held.
const countFrames = (page) =>
  page.evaluate(() => {
    window.__checkFrames = 0;
    if (window.__checkFrameCounting) return;
    window.__checkFrameCounting = true;
    window.PAStatusStream.subscribe((eventType, _payload, meta) => {
      if (eventType === 'status' && !meta?.cached) window.__checkFrames += 1;
    });
  });

// Makes the droid send every open stream a status frame. A client admitted to
// GET /api/events is an edge the controller answers by broadcasting to every
// client (src/web/api_events.cpp), so this opens one stream from here, reads
// its first frame and lets it go - a read, not a write. The fixture's SSE
// stand-in is told to push instead.
const nudgeFrames = async (fixture, count) => {
  for (let index = 0; index < count; index += 1) {
    if (fixture) {
      fixture.push();
    } else {
      await new Promise((resolve) => {
        const request = http.get(`${BASE_URL}/api/events`, (response) => {
          const done = () => {
            request.destroy();
            resolve();
          };
          response.once('data', done);
          setTimeout(done, 4000);
        });
        request.on('error', () => resolve());
      });
    }
    await new Promise((resolve) => setTimeout(resolve, 700));
  }
};

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------
const createReport = (title) => {
  const rows = [];
  const add = (id, name, result, detail = '') => {
    rows.push({ id: String(id), name, result, detail });
    console.log(`${result} [${id}] ${name}${detail ? ` - ${detail}` : ''}`);
  };
  const print = () => {
    console.log(`\n${title}`);
    console.log('id   result        check');
    console.log('---- ------------- ---------------------------------------------------------------');
    rows.forEach((row) => console.log(`${row.id.padEnd(4)} ${row.result.padEnd(13)} ${row.name}`));
    const count = (result) => rows.filter((row) => row.result === result).length;
    console.log(`\n=== ${count(PASS)} PASS, ${count(FAIL)} FAIL, ${count(NOT_ASSESSED)} NOT ASSESSED ===`);
  };
  return { rows, add, print };
};

// ---------------------------------------------------------------------------
// runCheck: one rule script, start to close
//
//   rule          the standing rule, printed as the table's title
//   artifactDir   where screenshots go (created)
//   allow(entry)  which writes the guard lets through; default none
//   fixture       options for ./fixture_routes.js when FIXTURE=1
//   selftests     the SELFTEST names this script knows; anything else is refused
//   precondition  async (ctx) => null, or the reason it does not hold; run on a
//                 neutral page of the droid's origin before any surface opens
//   run           async (ctx) => void; adds rows with ctx.report.add()
//
// ctx: { page, writes, fixture, report, browser, selftest, pageErrors,
//        openPage(fixtureOptions) } - openPage() gives a fresh context and page
// with the same guard, closed with the rest.
// ---------------------------------------------------------------------------
const runCheck = async ({ rule, artifactDir, allow = () => false, fixture: fixtureOptions = {}, selftests = [], precondition = null, run }) => {
  if (SELFTEST && !FIXTURE) {
    console.error('SELFTEST breaks the check on purpose and is for the offline proof only: needs FIXTURE=1');
    process.exit(2);
  }
  if (SELFTEST && !selftests.includes(SELFTEST)) {
    console.error(`SELFTEST=${SELFTEST} is not one this script knows (${selftests.join(', ') || 'none'})`);
    process.exit(2);
  }
  fs.mkdirSync(artifactDir, { recursive: true });
  const report = createReport(rule);
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const holders = [];
  const pageErrors = [];
  const allWrites = [];
  let refused = null;

  const openPage = async (options = fixtureOptions) => {
    const context = await browser.newContext({ viewport: { width: 1440, height: 900 }, acceptDownloads: true });
    const fixture = FIXTURE ? await require('./fixture_routes.js').install(context, options) : null;
    const holder = {
      context,
      fixture,
      close: async () => {
        await context.close().catch(() => {});
        if (fixture) await fixture.close();
      },
    };
    holders.push(holder);
    const page = await context.newPage();
    page.on('pageerror', (error) => pageErrors.push(String(error).split('\n')[0]));
    const writes = await installGuard(page, allow);
    allWrites.push(writes);
    return { page, writes, fixture, close: holder.close };
  };

  try {
    const first = await openPage(fixtureOptions);
    const ctx = { ...first, report, browser, selftest: SELFTEST, pageErrors, openPage };
    if (precondition) {
      await landNeutral(first.page);
      refused = await precondition(ctx);
    }
    if (refused) {
      console.error(`\nNOT ASSESSED - precondition does not hold: ${refused}`);
    } else {
      await run(ctx);
    }
  } catch (error) {
    console.error(`${rule}: could not complete:`, error);
    report.add('x', 'the script ran to the end', FAIL, String(error.message).split('\n')[0]);
  } finally {
    for (const holder of holders) await holder.close();
    await browser.close();
  }

  if (refused) {
    process.exitCode = 2;
    return;
  }
  const writes = allWrites.flat();
  if (writes.length) {
    console.log('\nWrites the page attempted:');
    [...new Set(writes.map(describeWrite))].forEach((line) => console.log(`  ${line}`));
  }
  if (pageErrors.length) {
    console.log('\nPage errors (a finding only where a row above says so):');
    [...new Set(pageErrors)].forEach((line) => console.log(`  ${line}`));
  }
  report.print();
  console.log(`Screenshots under ${artifactDir}`);
  const results = report.rows.map((row) => row.result);
  if (results.includes(FAIL)) process.exitCode = 1;
  else if (!results.includes(PASS)) process.exitCode = 2;
};

module.exports = {
  BASE_URL,
  HEADLESS,
  STEP,
  FIXTURE,
  SELFTEST,
  SETTLE_MS,
  PASS,
  FAIL,
  NOT_ASSESSED,
  verdict,
  pathOf,
  formOf,
  step,
  installGuard,
  describeWrite,
  landNeutral,
  readJson,
  readEstop,
  estopMustBe,
  guidedSetup,
  waitSettled,
  loadSurface,
  openSurface,
  visibleCards,
  pressAt,
  countFrames,
  nudgeFrames,
  createReport,
  runCheck,
};
