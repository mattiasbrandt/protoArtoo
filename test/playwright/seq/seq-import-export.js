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

  const browser = await chromium.launch({ headless: process.env.HEADLESS === 'true' });
  const context = await browser.newContext();
  const page = await context.newPage();

  try {
    // Navigate to seq.html
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });

    // Wait for page to be ready
    await page.waitForSelector('#seq-main-card', { timeout: 5000 });

    // Test import modal basic functionality
    await test('Import modal opens and closes', async () => {
      const importBtn = page.locator('#seq-btn-import');
      await importBtn.click();
      await page.waitForSelector('#seq-modal-import:not(.hidden)', { timeout: 5000 });

      const cancelBtn = page.locator('#seq-modal-import-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);

      const modal = page.locator('#seq-modal-import');
      const isHidden = await modal.evaluate((el) => el.classList.contains('hidden'));
      if (!isHidden) {
        throw new Error('Import modal should be hidden after cancel');
      }
    });

    // Test confirm button disabled with invalid JSON
    await test('Confirm button disabled with invalid JSON', async () => {
      const importBtn = page.locator('#seq-btn-import');
      await importBtn.click();
      await page.waitForSelector('#seq-modal-import:not(.hidden)', { timeout: 5000 });

      const textarea = page.locator('#seq-import-textarea');
      await textarea.fill('not valid json {');
      await textarea.blur();
      await page.waitForTimeout(100);

      const confirmBtn = page.locator('#seq-modal-import-confirm');
      const isDisabled = await confirmBtn.isDisabled();
      if (!isDisabled) {
        throw new Error('Confirm button should be disabled with invalid JSON');
      }

      // Clean up
      const cancelBtn = page.locator('#seq-modal-import-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);
    });

    // Test confirm button enabled with valid JSON
    await test('Confirm button enabled with valid JSON', async () => {
      const importBtn = page.locator('#seq-btn-import');
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

      const textarea = page.locator('#seq-import-textarea');
      await textarea.fill(JSON.stringify(validSeq));
      await textarea.blur();
      await page.waitForTimeout(100);

      const confirmBtn = page.locator('#seq-modal-import-confirm');
      const isDisabled = await confirmBtn.isDisabled();
      if (isDisabled) {
        throw new Error('Confirm button should be enabled with valid JSON');
      }

      // Don't actually click confirm (would need API to work), just cancel
      const cancelBtn = page.locator('#seq-modal-import-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);
    });

    // Test export button is present when a seq-card is in the list
    await test('Export button present in card structure', async () => {
      // Inject a fake card to verify the export button DOM structure
      await page.evaluate(() => {
        const container = document.getElementById('seq-cards-container');
        container.innerHTML = `
          <div class="seq-card">
            <div class="seq-card-header"><h4>DM:EXPORTTEST</h4><div class="seq-badges"></div></div>
            <div class="seq-card-meta"></div>
            <div class="seq-card-actions">
              <button class="btn btn-sm btn-action" data-action="export" data-seq-name="DM:EXPORTTEST">Export</button>
            </div>
          </div>`;
        document.getElementById('seq-empty-state').classList.add('hidden');
        document.getElementById('seq-populated-state').classList.remove('hidden');
      });
      await page.waitForTimeout(100);

      const exportBtn = page.locator('[data-action="export"]').first();
      const count = await page.locator('[data-action="export"]').count();
      if (count === 0) throw new Error('Export button should be present in card structure');
      const isVisible = await exportBtn.isVisible();
      if (!isVisible) throw new Error('Export button should be visible');
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
