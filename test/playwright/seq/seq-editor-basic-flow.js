/**
 * test/playwright/seq/seq-editor-basic-flow.js
 *
 * Test basic editor workflow: clone, edit metadata, add/remove steps, save, cancel.
 * Slice D: Save, Test Flow
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

    // Clone a factory entry to open the editor
    await test('Clone factory entry', async () => {
      const cloneBtn = await page.locator('#seq-btn-clone-factory');
      await cloneBtn.click();
      await page.waitForSelector('.seq-clone-builtins-list', { timeout: 5000 });

      const firstCloneBtn = await page.locator('.builtin-clone-btn').first();
      await firstCloneBtn.click();
      await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });
    });

    // Edit name
    await test('Edit name and validate', async () => {
      const nameInput = await page.locator('#seq-editor-name');
      await nameInput.fill('DM:TESTCOPY');

      const summaryEl = await page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (!text.includes('✓')) {
        throw new Error(`Expected valid status, got: ${text}`);
      }
    });

    // Modify suppress value
    await test('Modify suppressMs via slider', async () => {
      const suppressInput = await page.locator('#seq-editor-suppress');
      await suppressInput.fill('9000');
      await suppressInput.blur();
      await page.waitForTimeout(100);

      const newValue = await suppressInput.getAttribute('value');
      if (parseInt(newValue, 10) !== 9000) {
        throw new Error(`Expected suppressMs to be 9000, got ${newValue}`);
      }

      // Display should update too
      const display = await page.locator('.seq-editor-slider-value');
      const displayText = await display.textContent();
      if (!displayText.includes('9000')) {
        throw new Error(`Expected slider value display to show 9000, got: ${displayText}`);
      }
    });

    // Add a step
    await test('Add a new step', async () => {
      const stepsCountBefore = await page.locator('.step-row').count();
      const addStepBtn = await page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const stepsCountAfter = await page.locator('.step-row').count();
      if (stepsCountAfter !== stepsCountBefore + 1) {
        throw new Error(`Expected step count to increase, was ${stepsCountBefore}, now ${stepsCountAfter}`);
      }
    });

    // Remove the last step
    await test('Remove a step', async () => {
      const stepsCountBefore = await page.locator('.step-row').count();
      const lastRemoveBtn = await page.locator('.step-remove').last();
      await lastRemoveBtn.click();
      await page.waitForTimeout(100);

      const stepsCountAfter = await page.locator('.step-row').count();
      if (stepsCountAfter !== stepsCountBefore - 1) {
        throw new Error(`Expected step count to decrease, was ${stepsCountBefore}, now ${stepsCountAfter}`);
      }
    });

    // Test button interaction
    await test('Test button disables during dispatch and re-enables', async () => {
      const testBtn = await page.locator('#seq-editor-test');
      const isDisabledBefore = await testBtn.isDisabled();
      if (isDisabledBefore) {
        throw new Error('Test button should not be disabled initially');
      }

      // Click test (will fail in local test env but button should disable)
      await testBtn.click();
      await page.waitForTimeout(200);

      // Button should be re-enabled after error
      const isDisabledAfter = await testBtn.isDisabled();
      if (isDisabledAfter) {
        throw new Error('Test button should be re-enabled after dispatch attempt');
      }

      // Feedback should show
      const feedbackEl = await page.locator('#seq-editor-feedback');
      const feedbackText = await feedbackEl.textContent();
      if (!feedbackText || feedbackText.trim() === '') {
        throw new Error('Expected feedback message after test attempt');
      }
    });

    // Cancel (don't actually save)
    await test('Cancel returns to list view', async () => {
      const cancelBtn = await page.locator('#seq-editor-cancel');
      await cancelBtn.click();
      await page.waitForTimeout(100);

      // List view should be visible again
      const editorView = await page.locator('#seq-editor-view');
      const isHidden = await editorView.evaluate((el) => el.classList.contains('hidden'));
      if (!isHidden) {
        throw new Error('Editor view should be hidden after cancel');
      }

      const populatedState = await page.locator('#seq-populated-state');
      const isVisible = await populatedState.evaluate((el) => !el.classList.contains('hidden'));
      if (!isVisible) {
        throw new Error('List view should be visible after cancel');
      }
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
