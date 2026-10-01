// bench-auto: fixture seq.html
const { chromium } = require('playwright');
const assert = require('assert');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/seq.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function runTest() {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  try {
    // =====================================================================
    // Setup: Load the page
    // =====================================================================
    console.log('Loading seq.html...');
    await page.goto(TARGET_URL, { waitUntil: 'networkidle', timeout: 10000 });
    await page.waitForTimeout(500);

    // =====================================================================
    // Test 1: Mock sequence list with an invalid sequence
    // =====================================================================
    console.log('Test 1: Injecting mock invalid sequence into the list...');

    const mockSequences = [
      {
        name: 'ValidSeq',
        toggleGroup: 'none',
        suppressMs: 300,
        stepCount: 5,
        modified: '2026-06-16T12:00:00Z',
        source: 'user',
        valid: true,
        retrained: false,
      },
      {
        name: 'InvalidSeq',
        toggleGroup: 'none',
        suppressMs: 300,
        stepCount: 3,
        modified: '2026-06-15T10:00:00Z',
        source: 'user',
        valid: false,
        retrained: false,
      },
      {
        name: 'RetrainedInvalidSeq',
        toggleGroup: 'group-a',
        suppressMs: 500,
        stepCount: 7,
        modified: '2026-06-14T08:00:00Z',
        source: 'factory',
        valid: false,
        retrained: true,
      },
    ];

    // The page's own list renderer draws the rows (data/seq.js renderSeqRow()).
    await page.evaluate((seqs) => {
      window.__seqEditorForTesting.renderListWithMocks(seqs, []);
    }, mockSequences);

    await page.screenshot({ path: '/tmp/seq-invalid-card-list.png', fullPage: true });
    console.log('✓ Mock sequences injected and rendered');

    // =====================================================================
    // Test 2: Valid sequence has no Invalid badge
    // =====================================================================
    console.log('Test 2: Checking valid sequence card...');

    const validSeqState = await page.evaluate(() => {
      const card = Array.from(document.querySelectorAll('.seq-item')).find(
        (c) => c.querySelector('th[scope="row"] .seq-name')?.textContent === 'ValidSeq'
      );
      if (!card) return { found: false };

      return {
        found: true,
        hasInvalidBadge: !!card.querySelector('.seq-badge-invalid'),
        testBtnDisabled: card.querySelector('[data-action="test"]')?.disabled ?? false,
        testBtnTitle: card.querySelector('[data-action="test"]')?.getAttribute('title') ?? '',
        editBtnDisabled: card.querySelector('[data-action="edit"]')?.disabled ?? false,
        exportBtnDisabled: card.querySelector('[data-action="export"]')?.disabled ?? false,
      };
    });

    assert.strictEqual(validSeqState.found, true, 'ValidSeq card should exist');
    assert.strictEqual(validSeqState.hasInvalidBadge, false, 'Valid sequence should not have Invalid badge');
    assert.strictEqual(validSeqState.testBtnDisabled, false, 'Valid sequence Test button should be enabled');
    assert.strictEqual(validSeqState.editBtnDisabled, false, 'Valid sequence Edit button should be enabled');
    assert.strictEqual(validSeqState.exportBtnDisabled, false, 'Valid sequence Export button should be enabled');

    console.log('✓ Valid sequence card is correct');

    // =====================================================================
    // Test 3: Invalid sequence has Invalid badge and disabled Test button
    // =====================================================================
    console.log('Test 3: Checking invalid sequence card...');

    const invalidSeqState = await page.evaluate(() => {
      const card = Array.from(document.querySelectorAll('.seq-item')).find(
        (c) => c.querySelector('th[scope="row"] .seq-name')?.textContent === 'InvalidSeq'
      );
      if (!card) return { found: false };

      return {
        found: true,
        hasInvalidBadge: !!card.querySelector('.seq-badge-invalid'),
        invalidBadgeTitle: card.querySelector('.seq-badge-invalid')?.getAttribute('title') ?? '',
        testBtnDisabled: card.querySelector('[data-action="test"]')?.disabled ?? false,
        testBtnTitle: card.querySelector('[data-action="test"]')?.getAttribute('title') ?? '',
        editBtnDisabled: card.querySelector('[data-action="edit"]')?.disabled ?? false,
        duplicateBtnDisabled: card.querySelector('[data-action="duplicate"]')?.disabled ?? false,
        memoryWipeBtnDisabled: card.querySelector('[data-action="memory-wipe"]')?.disabled ?? false,
        exportBtnDisabled: card.querySelector('[data-action="export"]')?.disabled ?? false,
        shareBtnDisabled: card.querySelector('[data-action="share"]')?.disabled ?? false,
      };
    });

    assert.strictEqual(invalidSeqState.found, true, 'InvalidSeq card should exist');
    assert.strictEqual(invalidSeqState.hasInvalidBadge, true, 'Invalid sequence should have Invalid badge');
    assert.ok(
      invalidSeqState.invalidBadgeTitle.includes('Protocol Check') && invalidSeqState.invalidBadgeTitle.includes('repaired'),
      'Invalid badge should have explanatory title'
    );
    assert.strictEqual(invalidSeqState.testBtnDisabled, true, 'Invalid sequence Test button should be disabled');
    assert.ok(
      invalidSeqState.testBtnTitle.includes('cannot be run'),
      'Test button should have disabled title explaining why'
    );
    assert.strictEqual(invalidSeqState.editBtnDisabled, false, 'Invalid sequence Edit button should be enabled');
    assert.strictEqual(invalidSeqState.duplicateBtnDisabled, false, 'Invalid sequence Duplicate button should be enabled');
    assert.strictEqual(invalidSeqState.memoryWipeBtnDisabled, false, 'Invalid sequence Memory Wipe button should be enabled');
    assert.strictEqual(invalidSeqState.exportBtnDisabled, false, 'Invalid sequence Export button should be enabled');
    assert.strictEqual(invalidSeqState.shareBtnDisabled, false, 'Invalid sequence Share button should be enabled (custom seq)');

    console.log('✓ Invalid sequence card is correct');

    // =====================================================================
    // Test 4: Retrained + Invalid sequence has both badges
    // =====================================================================
    console.log('Test 4: Checking retrained+invalid sequence card...');

    const retrainedInvalidState = await page.evaluate(() => {
      const card = Array.from(document.querySelectorAll('.seq-item')).find(
        (c) => c.querySelector('th[scope="row"] .seq-name')?.textContent === 'RetrainedInvalidSeq'
      );
      if (!card) return { found: false };

      return {
        found: true,
        hasRetrainedBadge: !!card.querySelector('.seq-badge-retrained'),
        hasInvalidBadge: !!card.querySelector('.seq-badge-invalid'),
        testBtnDisabled: card.querySelector('[data-action="test"]')?.disabled ?? false,
        editBtnDisabled: card.querySelector('[data-action="edit"]')?.disabled ?? false,
      };
    });

    assert.strictEqual(retrainedInvalidState.found, true, 'RetrainedInvalidSeq card should exist');
    assert.strictEqual(retrainedInvalidState.hasRetrainedBadge, true, 'Should have Retrained badge');
    assert.strictEqual(retrainedInvalidState.hasInvalidBadge, true, 'Should have Invalid badge');
    assert.strictEqual(retrainedInvalidState.testBtnDisabled, true, 'Test button should be disabled for invalid seq');
    assert.strictEqual(retrainedInvalidState.editBtnDisabled, false, 'Edit button should be enabled for repair');

    console.log('✓ Retrained+invalid sequence card shows both badges and disables Test');

    // =====================================================================
    // Test 5: Screenshot of all three states
    // =====================================================================
    console.log('Test 5: Capturing final screenshots...');
    await page.screenshot({ path: '/tmp/seq-invalid-final.png', fullPage: true });
    console.log('✓ Screenshots captured');

    console.log('\n✅ All invalid sequence card tests passed!');

  } catch (error) {
    console.error('Test failed:', error);
    await page.screenshot({ path: '/tmp/seq-invalid-error.png', fullPage: true });
    process.exit(1);
  } finally {
    await browser.close();
  }
}

runTest().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});
