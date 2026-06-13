/**
 * test/playwright/seq/seq-protocol-check-mirror.js
 *
 * Test that client-side validation (SeqProtocolCheck) mirrors server rules.
 * Slice C: Protocol Check Tests
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

    // First, clone a factory entry to open the editor
    await test('Clone factory entry to open editor', async () => {
      const cloneBtn = await page.locator('#seq-btn-clone-factory');
      await cloneBtn.click();
      await page.waitForSelector('.seq-clone-builtins-list', { timeout: 5000 });

      const firstCloneBtn = await page.locator('.builtin-clone-btn').first();
      await firstCloneBtn.click();
      await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });
    });

    // Test name validation
    await test('Name validation: valid DM:XXXX format passes', async () => {
      const nameInput = await page.locator('#seq-editor-name');
      await nameInput.fill('DM:VALID');

      const summaryEl = await page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('✓') && text.includes('valid')) {
        // Good, validation passed
      } else {
        throw new Error(`Expected validation to pass for DM:VALID, got: ${text}`);
      }
    });

    await test('Name validation: invalid format shows error', async () => {
      const nameInput = await page.locator('#seq-editor-name');
      await nameInput.fill('dm:invalid'); // lowercase prefix
      await nameInput.blur();
      await page.waitForTimeout(100);

      const summaryEl = await page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('⚠') || text.includes('error') || text.includes('must match')) {
        // Good, validation failed as expected
      } else {
        throw new Error(`Expected validation to fail for dm:invalid, got: ${text}`);
      }
    });

    // Test suppressMs validation
    await test('suppressMs validation: value < 1000ms shows error', async () => {
      const nameInput = await page.locator('#seq-editor-name');
      await nameInput.fill('DM:TEST');

      const suppressInput = await page.locator('#seq-editor-suppress');
      await suppressInput.fill('500');
      await suppressInput.blur();
      await page.waitForTimeout(100);

      const summaryEl = await page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('⚠') || text.includes('1000')) {
        // Good, validation failed as expected
      } else {
        throw new Error(`Expected validation to fail for suppressMs=500, got: ${text}`);
      }
    });

    await test('suppressMs validation: valid value clears error', async () => {
      const suppressInput = await page.locator('#seq-editor-suppress');
      await suppressInput.fill('8000');
      await suppressInput.blur();
      await page.waitForTimeout(100);

      const summaryEl = await page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('✓') && text.includes('valid')) {
        // Good, validation passed
      } else {
        throw new Error(`Expected validation to pass for suppressMs=8000, got: ${text}`);
      }
    });

    // Test suppressMs vs end time constraint
    await test('suppressMs vs end time: shows error if suppressMs < end-t', async () => {
      // Add an end step with t=10000
      const addStepBtn = await page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const steps = await page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Set type to 'end'
      const typeChip = lastStep.locator('[data-type="end"]');
      await typeChip.click();
      await page.waitForTimeout(100);

      // Set time to 10000
      const tInput = lastStep.locator('.step-t');
      await tInput.fill('10000');
      await tInput.blur();
      await page.waitForTimeout(100);

      // Now set suppressMs to 8000 (less than 10000)
      const suppressInput = await page.locator('#seq-editor-suppress');
      await suppressInput.fill('8000');
      await suppressInput.blur();
      await page.waitForTimeout(100);

      const summaryEl = await page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('⚠') || text.includes('>=')) {
        // Good, validation failed as expected
      } else {
        throw new Error(`Expected validation to fail for suppressMs=8000 with end-t=10000, got: ${text}`);
      }

      // Clean up: remove the end step and reset suppressMs
      const removeBtn = lastStep.locator('.step-remove');
      await removeBtn.click();
      await page.waitForTimeout(100);

      await suppressInput.fill('8000');
      await suppressInput.blur();
      await page.waitForTimeout(100);
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
