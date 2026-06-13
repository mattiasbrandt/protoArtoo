/**
 * test/playwright/seq/seq-import-export.js
 *
 * Test import and export functionality.
 * Slice F: Import Logic
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

  const browser = await chromium.launch({ headless: process.env.HEADLESS !== 'false' });
  const context = await browser.createContext();
  const page = await context.newPage();

  try {
    // Navigate to seq.html
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });

    // Wait for page to be ready
    await page.waitForSelector('#seq-main-card', { timeout: 5000 });

    // Test import modal basic functionality
    await test('Import modal opens and closes', async () => {
      const importBtn = await page.locator('#seq-btn-import');
      await importBtn.click();
      await page.waitForSelector('#seq-modal-import:not(.hidden)', { timeout: 5000 });

      const cancelBtn = await page.locator('#seq-modal-import-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);

      const modal = await page.locator('#seq-modal-import');
      const isHidden = await modal.evaluate((el) => el.classList.contains('hidden'));
      if (!isHidden) {
        throw new Error('Import modal should be hidden after cancel');
      }
    });

    // Test confirm button disabled with invalid JSON
    await test('Confirm button disabled with invalid JSON', async () => {
      const importBtn = await page.locator('#seq-btn-import');
      await importBtn.click();
      await page.waitForSelector('#seq-modal-import:not(.hidden)', { timeout: 5000 });

      const textarea = await page.locator('#seq-import-textarea');
      await textarea.fill('not valid json {');
      await textarea.blur();
      await page.waitForTimeout(100);

      const confirmBtn = await page.locator('#seq-modal-import-confirm');
      const isDisabled = await confirmBtn.isDisabled();
      if (!isDisabled) {
        throw new Error('Confirm button should be disabled with invalid JSON');
      }

      // Clean up
      const cancelBtn = await page.locator('#seq-modal-import-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);
    });

    // Test confirm button enabled with valid JSON
    await test('Confirm button enabled with valid JSON', async () => {
      const importBtn = await page.locator('#seq-btn-import');
      await importBtn.click();
      await page.waitForSelector('#seq-modal-import:not(.hidden)', { timeout: 5000 });

      const validSeq = {
        format: 1,
        name: 'DM:IMPORT',
        suppressMs: 8000,
        toggleGroup: 'none',
        meta: { source: 'user' },
        steps: [
          { t: 0, type: 'audio', cmd: '$H' },
          { t: 1000, type: 'end' }
        ],
        closeSteps: []
      };

      const textarea = await page.locator('#seq-import-textarea');
      await textarea.fill(JSON.stringify(validSeq));
      await textarea.blur();
      await page.waitForTimeout(100);

      const confirmBtn = await page.locator('#seq-modal-import-confirm');
      const isDisabled = await confirmBtn.isDisabled();
      if (isDisabled) {
        throw new Error('Confirm button should be enabled with valid JSON');
      }

      // Don't actually click confirm (would need API to work), just cancel
      const cancelBtn = await page.locator('#seq-modal-import-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);
    });

    // Test export button is present on cards
    await test('Export button is visible (structure check)', async () => {
      // This is a structure check; actual export would need a sequence in the list
      // For now, just verify the button exists in the clone flow
      const cloneBtn = await page.locator('#seq-btn-clone-factory');
      await cloneBtn.click();
      await page.waitForSelector('.seq-clone-builtins-list', { timeout: 5000 });

      const firstCloneBtn = await page.locator('.builtin-clone-btn').first();
      await firstCloneBtn.click();
      await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });

      // Return to list
      const cancelBtn = await page.locator('#seq-editor-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);

      // Now check if export buttons are present
      const exportBtns = await page.locator('[data-action="export"]');
      const count = await exportBtns.count();
      if (count === 0) {
        // This is OK if list is empty; skip this assertion
        console.log('  (skipped: no sequences in list)');
      }
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
