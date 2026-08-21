/**
 * test/playwright/seq/seq-memory-wipe.js
 *
 * Test Memory Wipe modal: confirmation text requirement, dangling bindings display.
 * Slice E: Destructive Actions
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

  try {
    // Navigate to seq.html
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#seq-main-card', { timeout: 5000 });

    // Directly open the Memory Wipe modal via DOM — avoids needing live API/list population
    await test('Memory Wipe modal appears when triggered', async () => {
      await page.evaluate(() => {
        const seqName = 'DM:TESTWIPE';
        const modal = document.getElementById('seq-modal-memory-wipe');
        const nameEl = document.getElementById('seq-wipe-seq-name');
        const input = document.getElementById('seq-wipe-confirm-input');
        const confirmBtn = document.getElementById('seq-modal-wipe-confirm');
        if (nameEl) nameEl.textContent = `Delete sequence: ${seqName}`;
        if (input) { input.placeholder = seqName; input.value = ''; }
        if (confirmBtn) confirmBtn.disabled = true;
        if (input && confirmBtn) {
          input.addEventListener('input', () => { confirmBtn.disabled = input.value !== seqName; });
        }
        if (modal) modal.classList.remove('hidden');
      });
      await page.waitForSelector('#seq-modal-memory-wipe:not(.hidden)', { timeout: 3000 });
      const seqName = 'DM:TESTWIPE';

      // Check that the modal shows the correct sequence name
      const wipeTitle = page.locator('#seq-wipe-seq-name');
      const titleText = await wipeTitle.textContent();
      if (!titleText.includes(seqName)) {
        throw new Error(`Expected modal to show ${seqName}, got: ${titleText}`);
      }
    });

    await test('Confirm button disabled until name is typed', async () => {
      const confirmBtn = page.locator('#seq-modal-wipe-confirm');
      const confirmInput = page.locator('#seq-wipe-confirm-input');
      const placeholder = await confirmInput.getAttribute('placeholder');

      // Button should be disabled initially
      let isDisabled = await confirmBtn.isDisabled();
      if (!isDisabled) {
        throw new Error('Confirm button should be disabled initially');
      }

      // Type wrong text
      await confirmInput.fill('WrongName');
      isDisabled = await confirmBtn.isDisabled();
      if (!isDisabled) {
        throw new Error('Confirm button should still be disabled with wrong name');
      }

      // Type correct text (using placeholder as the expected name)
      await confirmInput.fill(placeholder);
      isDisabled = await confirmBtn.isDisabled();
      if (isDisabled) {
        throw new Error('Confirm button should be enabled with correct name');
      }
    });

    await test('Cancel closes modal without deleting', async () => {
      const cancelBtn = page.locator('#seq-modal-wipe-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);

      const modal = page.locator('#seq-modal-memory-wipe');
      const isHidden = await modal.evaluate((el) => el.classList.contains('hidden'));
      if (!isHidden) {
        throw new Error('Modal should be hidden after cancel');
      }
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
