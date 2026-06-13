const { chromium } = require('playwright');
const assert = require('assert');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/seq.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function runTest() {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  try {
    // =====================================================================
    // Test 1: Page loads and basic elements exist
    // =====================================================================
    console.log('Test 1: Loading seq.html and checking page structure...');
    await page.goto(TARGET_URL, { waitUntil: 'networkidle', timeout: 10000 });
    await page.waitForTimeout(500);

    // Take screenshot
    await page.screenshot({ path: '/tmp/seq-list-state.png', fullPage: true });

    const state = await page.evaluate(() => ({
      mainCardExists: !!document.querySelector('#seq-main-card'),
      emptyStateExists: !!document.querySelector('#seq-empty-state'),
      populatedStateExists: !!document.querySelector('#seq-populated-state'),
      cloneModalExists: !!document.querySelector('#seq-modal-clone-factory'),
      importModalExists: !!document.querySelector('#seq-modal-import'),
    }));

    assert.strictEqual(state.mainCardExists, true, 'Main card should exist');
    assert.strictEqual(state.emptyStateExists, true, 'Empty state should exist');
    assert.strictEqual(state.populatedStateExists, true, 'Populated state should exist');
    assert.strictEqual(state.cloneModalExists, true, 'Clone modal should exist');
    assert.strictEqual(state.importModalExists, true, 'Import modal should exist');

    console.log('✓ Page structure is correct');

    // =====================================================================
    // Test 2: Entry point buttons exist
    // =====================================================================
    console.log('Test 2: Checking entry point buttons...');

    const buttons = await page.evaluate(() => ({
      emptyCloneExists: !!document.querySelector('#seq-empty-clone'),
      emptyImportExists: !!document.querySelector('#seq-empty-import'),
      topCloneExists: !!document.querySelector('#seq-btn-clone-factory'),
      topImportExists: !!document.querySelector('#seq-btn-import'),
      emptyCloneDisabled: document.querySelector('#seq-empty-clone')?.disabled ?? true,
    }));

    assert.strictEqual(buttons.emptyCloneExists, true, 'Empty state clone button should exist');
    assert.strictEqual(buttons.emptyImportExists, true, 'Empty state import button should exist');
    assert.strictEqual(buttons.topCloneExists, true, 'Top clone button should exist');
    assert.strictEqual(buttons.topImportExists, true, 'Top import button should exist');
    assert.strictEqual(buttons.emptyCloneDisabled, false, 'Empty clone button should be enabled');

    console.log('✓ All entry point buttons are present and enabled');

    // =====================================================================
    // Test 3: Nav integration
    // =====================================================================
    console.log('Test 3: Checking navigation integration...');

    const navState = await page.evaluate(() => {
      const link = document.querySelector('a[href="/seq.html"]');
      return {
        exists: !!link,
        text: link?.textContent ?? '',
        hasEmoji: link?.textContent?.includes('🎭') ?? false,
        isActive: link?.classList.contains('active') ?? false,
      };
    });

    assert.strictEqual(navState.exists, true, 'Seq nav link should exist');
    assert.ok(navState.text.includes('Sequences'), 'Nav should show Sequences label');
    assert.strictEqual(navState.hasEmoji, true, 'Nav should include emoji');
    assert.strictEqual(navState.isActive, true, 'Seq nav link should be marked active');

    console.log('✓ Navigation integration correct');

    // =====================================================================
    // Test 4: Clone Factory modal opens
    // =====================================================================
    console.log('Test 4: Testing Clone Factory modal...');

    await page.click('#seq-empty-clone');
    await page.waitForTimeout(300);

    const cloneState = await page.evaluate(() => ({
      modalHidden: document.querySelector('#seq-modal-clone-factory')?.classList.contains('hidden') ?? true,
      title: document.querySelector('#seq-modal-clone-title')?.textContent ?? '',
    }));

    assert.strictEqual(cloneState.modalHidden, false, 'Clone modal should be visible after clicking');
    assert.ok(cloneState.title.includes('Clone'), 'Modal title should mention Clone');

    await page.screenshot({ path: '/tmp/seq-clone-modal.png', fullPage: true });

    console.log('✓ Clone modal opens correctly');

    // =====================================================================
    // Test 5: Clone modal can be closed
    // =====================================================================
    console.log('Test 5: Testing modal close...');

    await page.click('#seq-modal-clone-close');
    await page.waitForTimeout(200);

    const afterClose = await page.evaluate(() => ({
      modalHidden: document.querySelector('#seq-modal-clone-factory')?.classList.contains('hidden') ?? false,
    }));

    assert.strictEqual(afterClose.modalHidden, true, 'Clone modal should be hidden after close');

    console.log('✓ Modal closes correctly');

    // =====================================================================
    // Test 6: Import modal opens
    // =====================================================================
    console.log('Test 6: Testing Import modal...');

    await page.click('#seq-btn-import');
    await page.waitForTimeout(300);

    const importState = await page.evaluate(() => ({
      modalHidden: document.querySelector('#seq-modal-import')?.classList.contains('hidden') ?? true,
      title: document.querySelector('#seq-modal-import-title')?.textContent ?? '',
    }));

    assert.strictEqual(importState.modalHidden, false, 'Import modal should be visible');
    assert.ok(importState.title.includes('Import'), 'Import modal title should mention Import');

    await page.screenshot({ path: '/tmp/seq-import-modal.png', fullPage: true });

    console.log('✓ Import modal opens correctly');

    // =====================================================================
    // Test 7: Import modal can be closed via cancel button
    // =====================================================================
    console.log('Test 7: Testing import modal close...');

    await page.click('#seq-modal-import-cancel');
    await page.waitForTimeout(200);

    const importAfterClose = await page.evaluate(() => ({
      modalHidden: document.querySelector('#seq-modal-import')?.classList.contains('hidden') ?? false,
    }));

    assert.strictEqual(importAfterClose.modalHidden, true, 'Import modal should be hidden after cancel');

    console.log('✓ Import modal closes correctly');

    // =====================================================================
    // Test 8: Memory Wipe modal exists (for Slice E)
    // =====================================================================
    console.log('Test 8: Checking Memory Wipe modal structure...');

    const wipeState = await page.evaluate(() => ({
      modalExists: !!document.querySelector('#seq-modal-memory-wipe'),
      titleExists: !!document.querySelector('#seq-modal-wipe-title'),
    }));

    assert.strictEqual(wipeState.modalExists, true, 'Memory Wipe modal should exist');
    assert.strictEqual(wipeState.titleExists, true, 'Wipe modal title should exist');

    console.log('✓ Memory Wipe modal structure present');

    // =====================================================================
    // Test 9: Accessibility checks
    // =====================================================================
    console.log('Test 9: Accessibility checks...');

    const a11y = await page.evaluate(() => {
      const pageTitle = document.title;
      const cloneBtn = document.querySelector('#seq-empty-clone');
      const modal = document.querySelector('#seq-modal-clone-factory');
      return {
        pageTitle: pageTitle,
        cloneBtnHasLabel: !!(cloneBtn?.getAttribute('title') || cloneBtn?.textContent),
        modalRole: modal?.getAttribute('role'),
        modalAriaModal: modal?.getAttribute('aria-modal'),
        modalLabelledBy: modal?.getAttribute('aria-labelledby'),
      };
    });

    assert.ok(a11y.pageTitle.includes('Sequences'), 'Page title should mention Sequences');
    assert.strictEqual(a11y.cloneBtnHasLabel, true, 'Clone button should have label or title');
    assert.strictEqual(a11y.modalRole, 'dialog', 'Modal should have role=dialog');
    assert.strictEqual(a11y.modalAriaModal, 'true', 'Modal should have aria-modal=true');
    assert.ok(a11y.modalLabelledBy, 'Modal should have aria-labelledby');

    console.log('✓ Accessibility attributes correct');

    console.log('\n✅ All Slice A tests passed!');

  } catch (error) {
    console.error('Test failed:', error);
    await page.screenshot({ path: '/tmp/seq-error.png', fullPage: true });
    process.exit(1);
  } finally {
    await browser.close();
  }
}

runTest().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});
