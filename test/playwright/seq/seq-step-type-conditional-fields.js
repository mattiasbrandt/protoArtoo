/**
 * test/playwright/seq/seq-step-type-conditional-fields.js
 *
 * Test that each step type renders the correct conditional fields and validates them.
 * Slice C: Conditional Fields + Validation
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
      if (!(await cloneBtn.isVisible())) {
        throw new Error('Clone button not visible');
      }
      await cloneBtn.click();

      // Wait for modal to appear
      await page.waitForSelector('.seq-clone-builtins-list', { timeout: 5000 });

      // Click first Clone button in the list
      const firstCloneBtn = await page.locator('.builtin-clone-btn').first();
      if (!(await firstCloneBtn.isVisible())) {
        throw new Error('First clone button not visible');
      }
      await firstCloneBtn.click();

      // Wait for editor to appear
      await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });
    });

    // Test each step type's conditional fields
    const testStepType = async (type, expectedFields) => {
      await test(`Step type '${type}' renders correct fields`, async () => {
        // Click "Add Step" to create a new step
        const addStepBtn = await page.locator('#seq-editor-add-step');
        await addStepBtn.click();
        await page.waitForTimeout(100);

        // Get the last step row
        const steps = await page.locator('.step-row');
        const count = await steps.count();
        const lastStep = steps.nth(count - 1);

        // Click the type chip for this type
        const typeChip = lastStep.locator(`[data-type="${type}"]`);
        await typeChip.click();
        await page.waitForTimeout(100);

        // Check that the right fields are present
        const fieldsContainer = lastStep.locator('.step-fields');
        const fieldElems = fieldsContainer.locator('[data-field]');
        const fieldCount = await fieldElems.count();
        const fieldNames = [];
        for (let i = 0; i < fieldCount; i++) {
          const attr = await fieldElems.nth(i).getAttribute('data-field');
          fieldNames.push(attr);
        }

        // Verify expected fields
        for (const expectedField of expectedFields) {
          if (!fieldNames.includes(expectedField)) {
            throw new Error(`Missing expected field: ${expectedField}. Found: ${fieldNames.join(', ')}`);
          }
        }

        // Verify no extra fields
        if (fieldNames.length !== expectedFields.length) {
          throw new Error(`Expected ${expectedFields.length} fields, got ${fieldNames.length}: ${fieldNames.join(', ')}`);
        }

        // Clean up: remove the step
        const removeBtn = lastStep.locator('.step-remove');
        await removeBtn.click();
        await page.waitForTimeout(100);
      });
    };

    // Test field specs for each type
    await testStepType('audio', ['cmd']);
    await testStepType('dome', ['cmd']);
    await testStepType('loop', ['body', 'periodMs', 'durationMs']);
    await testStepType('random', ['set', 'pulseMin', 'pulseMax', 'moveMs', 'jitterMs', 'distinct']);
    await testStepType('audioCat', ['category', 'fallback']);
    await testStepType('end', []); // end has no fields (except empty span)

    // Test that out-of-range values trigger error highlighting
    await test('Out-of-range value shows .field-error class', async () => {
      const addStepBtn = await page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const steps = await page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Set type to 'loop' (has numeric fields with bounds)
      const typeChip = lastStep.locator('[data-type="loop"]');
      await typeChip.click();
      await page.waitForTimeout(100);

      // Find the body field and set it to an out-of-range value (e.g., 150, max is 96)
      const bodyField = lastStep.locator('[data-field="body"]');
      await bodyField.fill('150');
      await bodyField.blur(); // Trigger validation
      await page.waitForTimeout(100);

      // Check for .field-error class
      const hasError = await bodyField.evaluate((el) => el.classList.contains('field-error'));
      if (!hasError) {
        throw new Error('Expected .field-error class on out-of-range field');
      }

      // Fix the value and verify error clears
      await bodyField.fill('5');
      await bodyField.blur();
      await page.waitForTimeout(100);

      const hasErrorAfterFix = await bodyField.evaluate((el) => el.classList.contains('field-error'));
      if (hasErrorAfterFix) {
        throw new Error('Expected .field-error to be cleared after fixing value');
      }

      // Clean up
      const removeBtn = lastStep.locator('.step-remove');
      await removeBtn.click();
      await page.waitForTimeout(100);
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
