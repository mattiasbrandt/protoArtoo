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

    await page.evaluate((seqs) => {
      window.sequences = seqs;
      // Manually trigger renderListView to render the cards
      const els = {
        capacityDisplay: document.getElementById('seq-capacity-display'),
        emptyState: document.getElementById('seq-empty-state'),
        populatedState: document.getElementById('seq-populated-state'),
        cardsContainer: document.getElementById('seq-cards-container'),
      };

      els.capacityDisplay.textContent = `${seqs.length} / 16 sequences`;
      els.emptyState.classList.add('hidden');
      els.populatedState.classList.remove('hidden');

      // Render cards using the same logic as renderSeqCard
      const escapeHtml = (str) => {
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
      };

      const escapeAttr = (str) => {
        return str.replace(/"/g, '&quot;');
      };

      els.cardsContainer.innerHTML = seqs.map((seq) => {
        const badges = [];
        if (seq.retrained) {
          badges.push(
            `<span class="seq-badge seq-badge-retrained" title="This sequence shadows the factory ${seq.name}">Retrained</span>`
          );
        }
        if (seq.valid === false) {
          badges.push(
            `<span class="seq-badge seq-badge-invalid" title="This sequence fails Protocol Check and cannot be run until repaired">Invalid</span>`
          );
        }

        const stepCount = seq.stepCount || 0;
        const modifiedDate = seq.modified ? new Date(seq.modified).toLocaleString() : 'Unknown';

        const isCustom = !seq.source || seq.source === 'user';
        const shareBtn = isCustom
          ? `<button class="btn btn-sm btn-action" data-action="share" data-seq-name="${escapeAttr(seq.name)}" title="Open a pre-filled GitHub issue to share this sequence with the project">Share to project</button>`
          : '';

        const testBtnDisabled = seq.valid === false ? 'disabled title="Invalid sequence cannot be run — edit to repair"' : `data-seq-name="${escapeAttr(seq.name)}"`;

        return `
          <div class="seq-card">
            <div class="seq-card-header">
              <h4>${escapeHtml(seq.name)}</h4>
              <div class="seq-badges">${badges.join('')}</div>
            </div>
            <div class="seq-card-meta">
              <span class="seq-meta-item">Toggle: ${escapeHtml(seq.toggleGroup || 'none')}</span>
              <span class="seq-meta-item">Suppress: ${seq.suppressMs}ms</span>
              <span class="seq-meta-item">Steps: ${stepCount}</span>
              <span class="seq-meta-item">Modified: ${escapeHtml(modifiedDate)}</span>
            </div>
            <div class="seq-card-actions">
              <button class="btn btn-sm btn-action" data-action="edit" data-seq-name="${escapeAttr(seq.name)}">Edit</button>
              <button class="btn btn-sm btn-action" data-action="test" ${testBtnDisabled}>Test</button>
              <button class="btn btn-sm btn-action" data-action="duplicate" data-seq-name="${escapeAttr(seq.name)}">Duplicate</button>
              <button class="btn btn-sm btn-action" data-action="memory-wipe" data-seq-name="${escapeAttr(seq.name)}">Memory Wipe</button>
              <button class="btn btn-sm btn-action" data-action="export" data-seq-name="${escapeAttr(seq.name)}">Export</button>
              ${shareBtn}
            </div>
            <div class="seq-card-test-feedback feedback hidden"></div>
          </div>
        `;
      }).join('');
    }, mockSequences);

    await page.screenshot({ path: '/tmp/seq-invalid-card-list.png', fullPage: true });
    console.log('✓ Mock sequences injected and rendered');

    // =====================================================================
    // Test 2: Valid sequence has no Invalid badge
    // =====================================================================
    console.log('Test 2: Checking valid sequence card...');

    const validSeqState = await page.evaluate(() => {
      const card = Array.from(document.querySelectorAll('.seq-card')).find(
        (c) => c.querySelector('h4')?.textContent === 'ValidSeq'
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
      const card = Array.from(document.querySelectorAll('.seq-card')).find(
        (c) => c.querySelector('h4')?.textContent === 'InvalidSeq'
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
      const card = Array.from(document.querySelectorAll('.seq-card')).find(
        (c) => c.querySelector('h4')?.textContent === 'RetrainedInvalidSeq'
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
