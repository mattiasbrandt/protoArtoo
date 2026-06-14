/**
 * test/playwright/seq/seq-share-to-project.js
 *
 * Test the "Share to project" contribution funnel (ADR 0007):
 * - the button renders on a custom (source=user) sequence card
 * - clicking opens a pre-filled GitHub contribution issue
 * - the sequence is copied for pasting (or a fallback message is shown)
 * - no Guild badge remains
 */

const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/seq.html';

(async () => {
  let passed = 0;
  let failed = 0;

  const test = async (name, fn) => {
    try {
      await fn();
      console.log(`✓ ${name}`);
      passed++;
    } catch (error) {
      console.error(`✗ ${name}`);
      console.error(`  ${error.message}`);
      failed++;
    }
  };

  const assert = (cond, msg) => {
    if (!cond) throw new Error(msg);
  };

  const browser = await chromium.launch({ headless: process.env.HEADLESS !== 'false' });
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  const page = await context.newPage();
  await page.goto(TARGET_URL);
  await page.waitForFunction(
    () => window.__seqEditorForTesting && window.__seqEditorForTesting.renderListWith
  );

  const SAMPLE = {
    name: 'DM:MYDANCE',
    source: 'user',
    retrained: false,
    toggleGroup: 'none',
    suppressMs: 8000,
    stepCount: 4,
    modified: Date.now(),
  };

  // Stub popup + network + render the custom sequence into the list.
  await page.evaluate((seq) => {
    window.__openCalls = [];
    window.open = (url) => {
      window.__openCalls.push(url);
      return null;
    };
    window.PAApi = window.PAApi || {};
    window.PAApi.get = async () => ({
      data: { format: 1, name: seq.name, suppressMs: seq.suppressMs, toggleGroup: 'none', steps: [{ t: 0, type: 'end' }] },
    });
    window.PAApi.messageFor = (e) => String(e);
    window.__seqEditorForTesting.renderListWith([seq]);
  }, SAMPLE);

  await test('Share button renders on a custom sequence', async () => {
    const btn = await page.$('.seq-card-actions button[data-action="share"]');
    assert(btn, 'Share to project button not found');
    const label = (await btn.textContent()).trim();
    assert(/share to project/i.test(label), `unexpected label: ${label}`);
  });

  await test('No Guild badge remains', async () => {
    const guild = await page.$('.seq-badge-guild');
    assert(!guild, 'Guild badge should be gone');
  });

  await test('Share opens a pre-filled contribution issue', async () => {
    await page.click('.seq-card-actions button[data-action="share"]');
    await page.waitForFunction(() => window.__openCalls && window.__openCalls.length > 0);
    const url = await page.evaluate(() => window.__openCalls[0]);
    assert(
      url.includes('github.com/mattiasbrandt/protoArtoo/issues/new'),
      `wrong issue host: ${url}`
    );
    assert(url.includes('template=sequence-contribution.md'), `missing template: ${url}`);
    assert(url.includes('DM%3AMYDANCE'), `missing sequence name in title: ${url}`);
    assert(url.toLowerCase().includes('labels='), `missing labels: ${url}`);
  });

  await test('Share reports clipboard copy or fallback', async () => {
    // showFeedback() rewrites the element's className to "feedback <level>",
    // so match by the .feedback class and its text rather than the original one.
    await page.waitForFunction(
      () => {
        const card = [...document.querySelectorAll('.seq-card')].find(
          (c) => c.querySelector('h4')?.textContent === 'DM:MYDANCE'
        );
        const fb = card?.querySelector('.feedback');
        return !!fb && !fb.classList.contains('hidden') && /clipboard|copy|attach/i.test(fb.textContent || '');
      },
      { timeout: 3000 }
    );
  });

  await browser.close();
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
