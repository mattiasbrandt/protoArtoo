/**
 * test/playwright/seq/seq-expand-middle-card.js
 *
 * Regression: expanding a single MIDDLE card of a multi-step sequence must
 * render that card's OWN step fields/help, not another step's.
 *
 * Bug history: the conditional-field population loop zipped .step-fields
 * containers (which exist only on expanded cards) against steps[] by
 * enumeration order, so the Nth expanded card got steps[N] instead of the
 * step at its real data-step-index. Expanding a middle card of a loaded
 * factory sequence (the operator's core action) rendered the wrong fields.
 * Fixed by deriving the step index from the card's data-step-index.
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

  const browser = await chromium.launch({ headless: process.env.HEADLESS === 'true' });
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const page = await context.newPage();

  // Distinct types so a mismatch is unambiguous: audio / loop / dome / domeRotate.
  const testSeq = {
    format: 1,
    name: 'DM:EXPANDMID',
    suppressMs: 8000,
    toggleGroup: 'none',
    meta: { source: 'test', notes: '' },
    steps: [
      { t: 0, type: 'audio', cmd: '$H' },
      { t: 100, type: 'loop', body: 2, periodMs: 1000, durationMs: 5000 },
      { t: 200, type: 'dome', cmd: ':OP00' },
      { t: 300, type: 'domeRotate', speedPct: 60, durationMs: 3000 },
      { t: 400, type: 'end' },
    ],
    closeSteps: [],
  };

  // Map each step type to a data-field that is UNIQUE to it, so finding that
  // field on the expanded card proves the card rendered its own step.
  const uniqueFieldFor = {
    loop: 'body',
    dome: 'cmd',
    domeRotate: 'direction',
  };

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#seq-main-card', { timeout: 5000 });

    await test('Open editor via injection', async () => {
      await page.evaluate((seq) => {
        window.__seqEditorForTesting.renderEditorView(seq);
        document.getElementById('seq-editor-view').classList.remove('hidden');
      }, testSeq);
      await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });
    });

    // Expand each non-trivial middle card ONE AT A TIME (collapse it again after)
    // and assert the expanded fields belong to that card's own step.
    for (const idx of [1, 2, 3]) {
      const expectedType = testSeq.steps[idx].type;
      const expectedField = uniqueFieldFor[expectedType];
      await test(`Expanding middle card #${idx} (${expectedType}) renders its own fields`, async () => {
        const card = page.locator('.step-card').nth(idx);
        // Sanity: the card header reflects the right step type.
        const cardType = await card.getAttribute('data-step-type');
        if (cardType !== expectedType) {
          throw new Error(`card #${idx} data-step-type is ${cardType}, expected ${expectedType}`);
        }
        await card.locator('.step-card-header').click();
        await page.waitForTimeout(100);

        const fields = await card.locator('.step-fields [data-field]').evaluateAll(
          (els) => els.map((e) => e.getAttribute('data-field'))
        );
        if (!fields.includes(expectedField)) {
          throw new Error(
            `card #${idx} (${expectedType}) expanded fields [${fields.join(', ')}] ` +
            `missing expected field "${expectedField}" - rendered the wrong step`
          );
        }
        // Collapse again to keep the next iteration's expand isolated.
        await card.locator('.step-card-header').click();
        await page.waitForTimeout(50);
      });
    }

    console.log(`\n✓ Passed: ${passed}`);
    console.log(`✗ Failed: ${failed}`);
    process.exit(failed > 0 ? 1 : 0);
  } catch (error) {
    console.error('Fatal error:', error);
    process.exit(1);
  } finally {
    await browser.close();
  }
})();
