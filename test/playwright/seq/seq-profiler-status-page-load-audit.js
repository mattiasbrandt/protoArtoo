// bench-auto: droid
/**
 * Page-load audit for the routes on the ADR 0021 seam: the sequence surface,
 * the profiler surface, and the status/helper routes.
 *
 * Asserts that the operator pages backed by those routes load against the
 * controller with zero 404s and render the payloads the routes return.
 *
 * Run against a droid, or offline against the fixture server with FIXTURE=1
 * (test/playwright/README.md). BASE_URL names the target and has no default,
 * so a run never reaches a droid nobody named:
 *   BASE_URL=http://<droid> node test/playwright/seq/seq-profiler-status-page-load-audit.js
 *
 * Pace matters. An unpaced multi-page sweep measures the connection admission
 * guard rather than the routes, so each page load is separated by a settle gap.
 */

const { chromium } = require('playwright');
const assert = require('assert');

const BASE_URL = (process.env.BASE_URL || '').replace(/\/$/, '');
const FIXTURE = process.env.FIXTURE === '1';
const HEADLESS = process.env.HEADLESS === 'true';
const SETTLE_MS = Number(process.env.SETTLE_MS || 3000);

/**
 * `GET /api/profiler` is compiled out unless the firmware was built from the
 * `protoArtoo_profiler` environment, so a 404 from it is the correct answer on
 * a normal build rather than a missing route. Every other 404 is a failure.
 */
const EXPECTED_404 = [/\/api\/profiler(\?|$)/];

function isExpected404(url) {
  return EXPECTED_404.some((pattern) => pattern.test(url));
}

async function loadPage(context, path) {
  const page = await context.newPage();
  const responses = [];
  const failures = [];
  // What the list routes answered, so the page can be held to rendering them.
  const payloads = {};

  page.on('response', (response) => {
    responses.push({ url: response.url(), status: response.status() });
    const route = new URL(response.url()).pathname;
    if ((route === '/api/seq/list' || route === '/api/seq/builtins') && response.status() === 200) {
      payloads[route] = response.json().catch(() => null);
    }
  });
  page.on('requestfailed', (request) => {
    failures.push({ url: request.url(), error: request.failure()?.errorText });
  });

  // Not `networkidle`: these pages hold an open SSE connection to /api/events,
  // so the network is never idle and the wait would always time out. Wait for
  // the document instead, then settle for the deferred fetches the page makes.
  await page.goto(`${BASE_URL}${path}`, { waitUntil: 'domcontentloaded', timeout: 30000 });
  await page.waitForTimeout(SETTLE_MS);

  return { page, responses, failures, payloads };
}

function reportPage(path, responses, failures) {
  const notFound = responses.filter((r) => r.status === 404 && !isExpected404(r.url));
  const tolerated = responses.filter((r) => r.status === 404 && isExpected404(r.url));
  const serverErrors = responses.filter((r) => r.status >= 500);

  console.log(`  ${responses.length} responses, ${failures.length} connection failures`);
  tolerated.forEach((r) => console.log(`  tolerated 404 (compiled out): ${r.url}`));
  notFound.forEach((r) => console.log(`  UNEXPECTED 404: ${r.url}`));
  serverErrors.forEach((r) => console.log(`  ${r.status}: ${r.url}`));
  failures.forEach((f) => console.log(`  CONNECTION FAILED: ${f.url} (${f.error})`));

  assert.strictEqual(notFound.length, 0, `${path} must load with zero unexpected 404s`);
  assert.strictEqual(failures.length, 0, `${path} must load with no failed requests`);
  assert.strictEqual(serverErrors.length, 0, `${path} must load with no 5xx responses`);
}

async function auditSeqPage(context) {
  console.log('seq.html - sequence routes');
  const { page, responses, failures, payloads } = await loadPage(context, '/seq.html');

  try {
    reportPage('/seq.html', responses, failures);

    const seqCalls = responses.filter((r) => r.url.includes('/api/seq'));
    assert.ok(seqCalls.length > 0, 'seq.html must call the sequence routes');
    console.log(`  sequence route calls: ${seqCalls.map((r) => `${r.status}`).join(', ')}`);

    const learned = await payloads['/api/seq/list'];
    const factory = await payloads['/api/seq/builtins'];
    assert.ok(Array.isArray(learned), '/api/seq/list must answer a list');
    assert.ok(Array.isArray(factory), '/api/seq/builtins must answer a list');
    // A factory sequence the builder has retrained is listed as theirs.
    const learnedNames = new Set(learned.map((seq) => seq.name));
    const untuned = factory.filter((seq) => !learnedNames.has(seq.name));

    // The page must have rendered what the routes returned, not just received it.
    const rendered = await page.evaluate(() => {
      const populated = document.querySelector('#seq-populated-state');
      const empty = document.querySelector('#seq-empty-state');
      const visible = (el) => !!el && el.offsetParent !== null;
      return {
        mainCard: !!document.querySelector('#seq-main-card'),
        listResolved: visible(populated) || visible(empty),
        waiting: document.querySelectorAll('#seq-main-card .seq-section-waiting').length,
        // One Edit button per Learned card, one Tune button per factory card.
        learnedCards: document.querySelectorAll('[data-action="edit"][data-seq-name]').length,
        factoryCards: document.querySelectorAll('[data-action="tune"][data-builtin-name]').length,
      };
    });

    assert.strictEqual(rendered.mainCard, true, 'seq.html must render its main card');
    assert.strictEqual(
      rendered.listResolved,
      true,
      'seq.html must resolve the list into either its populated or its empty state',
    );
    console.log(`  rendered: ${rendered.learnedCards} of ${learned.length} learned, ${rendered.factoryCards} of ${untuned.length} factory`);
    assert.strictEqual(rendered.waiting, 0, 'seq.html must not still be waiting on a list that answered');
    assert.strictEqual(rendered.learnedCards, learned.length, 'seq.html must render a card per saved sequence');
    assert.strictEqual(rendered.factoryCards, untuned.length, 'seq.html must render a card per untuned factory sequence');

    await page.screenshot({ path: '/tmp/issue90-seq.png', fullPage: true });
  } finally {
    await page.close();
  }
}

async function auditSetupPage(context) {
  console.log('maintenance.html - profiler routes');
  const { page, responses, failures } = await loadPage(context, '/maintenance.html');

  try {
    reportPage('/maintenance.html', responses, failures);
    await page.screenshot({ path: '/tmp/issue90-setup.png', fullPage: true });
  } finally {
    await page.close();
  }
}

async function auditIndexPage(context) {
  console.log('index.html - status and shared helper routes');
  const { page, responses, failures } = await loadPage(context, '/index.html');

  try {
    reportPage('/index.html', responses, failures);

    const status = responses.filter((r) => r.url.includes('/api/status'));
    assert.ok(status.length > 0, 'index.html must call /api/status');
    status.forEach((r) => assert.strictEqual(r.status, 200, '/api/status must answer 200'));

    // Status is rendered, not merely fetched: the header carries the firmware
    // string the status/identity payloads supply, so an unresolved page shows
    // its loading placeholder instead.
    //
    // Deliberately not asserted here: the connection dot turning green. That is
    // driven by an /api/events push, which is a different route group's
    // concern, so gating this group's audit on it would misattribute a failure.
    const connText = (await page.locator('#conn-status').innerText()).trim();
    console.log(`  #conn-status: ${JSON.stringify(connText.slice(0, 90))}`);
    assert.ok(connText.length > 0, 'index.html must render a status header');
    assert.ok(
      !/Loading firmware info/.test(connText),
      'the firmware/status payload must have been applied, not left at its placeholder',
    );

    await page.screenshot({ path: '/tmp/issue90-index.png', fullPage: true });
  } finally {
    await page.close();
  }
}

async function main() {
  if (!BASE_URL) {
    console.error('BASE_URL is not set: name the droid, or the fixture server with FIXTURE=1.');
    process.exitCode = 2;
    return;
  }
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  // Offline, the controller routes the fixture server does not answer.
  const fixture = FIXTURE ? await require('../_lib/fixture_routes.js').install(context) : null;

  try {
    console.log(`Target: ${BASE_URL}${FIXTURE ? ' (fixture)' : ''}\n`);

    await auditSeqPage(context);
    await new Promise((resolve) => setTimeout(resolve, SETTLE_MS));

    await auditSetupPage(context);
    await new Promise((resolve) => setTimeout(resolve, SETTLE_MS));

    await auditIndexPage(context);

    console.log('\nPASS - all three pages loaded with zero unexpected 404s and rendered their payloads');
  } catch (err) {
    console.error(`\nFAIL - ${err.message}`);
    process.exitCode = 1;
  } finally {
    await browser.close();
    if (fixture) await fixture.close();
  }
}

main();
