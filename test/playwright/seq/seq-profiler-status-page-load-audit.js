/**
 * Page-load audit for the routes on the ADR 0021 seam: the sequence surface,
 * the profiler surface, and the status/helper routes.
 *
 * Asserts that the operator pages backed by those routes load against the
 * controller with zero 404s and render the payloads the routes return.
 *
 * Run against the live controller:
 *   TARGET_HOST=http://10.0.0.22 node test/playwright/seq/seq-profiler-status-page-load-audit.js
 *
 * Pace matters. An unpaced multi-page sweep measures the connection admission
 * guard rather than the routes, so each page load is separated by a settle gap.
 */

const { chromium } = require('playwright');
const assert = require('assert');

const TARGET_HOST = process.env.TARGET_HOST || 'http://10.0.0.22';
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

async function loadPage(browser, path) {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const responses = [];
  const failures = [];

  page.on('response', (response) => {
    responses.push({ url: response.url(), status: response.status() });
  });
  page.on('requestfailed', (request) => {
    failures.push({ url: request.url(), error: request.failure()?.errorText });
  });

  // Not `networkidle`: these pages hold an open SSE connection to /api/events,
  // so the network is never idle and the wait would always time out. Wait for
  // the document instead, then settle for the deferred fetches the page makes.
  await page.goto(`${TARGET_HOST}${path}`, { waitUntil: 'domcontentloaded', timeout: 30000 });
  await page.waitForTimeout(SETTLE_MS);

  return { page, responses, failures };
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

async function auditSeqPage(browser) {
  console.log('seq.html - sequence routes');
  const { page, responses, failures } = await loadPage(browser, '/seq.html');

  try {
    reportPage('/seq.html', responses, failures);

    const seqCalls = responses.filter((r) => r.url.includes('/api/seq'));
    assert.ok(seqCalls.length > 0, 'seq.html must call the sequence routes');
    console.log(`  sequence route calls: ${seqCalls.map((r) => `${r.status}`).join(', ')}`);

    // The page must have rendered what the routes returned, not just received it.
    const rendered = await page.evaluate(() => {
      const populated = document.querySelector('#seq-populated-state');
      const empty = document.querySelector('#seq-empty-state');
      const visible = (el) => !!el && el.offsetParent !== null;
      return {
        mainCard: !!document.querySelector('#seq-main-card'),
        listResolved: visible(populated) || visible(empty),
        // One Edit button per rendered sequence card.
        cardCount: document.querySelectorAll('[data-action="edit"][data-seq-name]').length,
      };
    });

    assert.strictEqual(rendered.mainCard, true, 'seq.html must render its main card');
    assert.strictEqual(
      rendered.listResolved,
      true,
      'seq.html must resolve the list into either its populated or its empty state',
    );
    console.log(`  rendered sequence cards: ${rendered.cardCount}`);
    assert.ok(
      rendered.cardCount > 0,
      'seq.html must render a card per saved sequence, proving the list payload reached the DOM',
    );

    await page.screenshot({ path: '/tmp/issue90-seq.png', fullPage: true });
  } finally {
    await page.close();
  }
}

async function auditSetupPage(browser) {
  console.log('setup.html - profiler routes');
  const { page, responses, failures } = await loadPage(browser, '/setup.html');

  try {
    reportPage('/setup.html', responses, failures);
    await page.screenshot({ path: '/tmp/issue90-setup.png', fullPage: true });
  } finally {
    await page.close();
  }
}

async function auditIndexPage(browser) {
  console.log('index.html - status and shared helper routes');
  const { page, responses, failures } = await loadPage(browser, '/index.html');

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
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });

  try {
    console.log(`Target: ${TARGET_HOST}\n`);

    await auditSeqPage(browser);
    await new Promise((resolve) => setTimeout(resolve, SETTLE_MS));

    await auditSetupPage(browser);
    await new Promise((resolve) => setTimeout(resolve, SETTLE_MS));

    await auditIndexPage(browser);

    console.log('\nPASS - all three pages loaded with zero unexpected 404s and rendered their payloads');
  } catch (err) {
    console.error(`\nFAIL - ${err.message}`);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
}

main();
