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

  const browser = await chromium.launch({ headless: process.env.HEADLESS === 'true' });
  const context = await browser.newContext();
  const page = await context.newPage();

  try {
    // Navigate to seq.html
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#seq-main-card', { timeout: 5000 });

    // Inject a test sequence directly — avoids needing the live /api/seq/builtins endpoint
    const testSeq = {
      format: 1, name: 'DM:TEST', suppressMs: 8000, toggleGroup: 'none',
      meta: { source: 'test', notes: '' },
      steps: [
        { t: 0, type: 'audio', cmd: '$H' },
        { t: 1000, type: 'end' },
      ],
      closeSteps: [],
    };
    await page.evaluate((seq) => {
      if (window.__seqEditorForTesting && window.__seqEditorForTesting.renderEditorView) {
        window.__seqEditorForTesting.renderEditorView(seq);
        document.getElementById('seq-editor-view').classList.remove('hidden');
      }
    }, testSeq);
    await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });

    // Test name validation
    await test('Name validation: valid DM:XXXX format passes', async () => {
      const nameInput = page.locator('#seq-editor-name');
      await nameInput.fill('DM:VALID');

      const summaryEl = page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('✓') && text.includes('valid')) {
        // Good, validation passed
      } else {
        throw new Error(`Expected validation to pass for DM:VALID, got: ${text}`);
      }
    });

    await test('Name validation: invalid format shows error', async () => {
      const nameInput = page.locator('#seq-editor-name');
      await nameInput.fill('dm:invalid'); // lowercase prefix
      await nameInput.blur();
      await page.waitForTimeout(100);

      const summaryEl = page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('⚠') || text.includes('error') || text.includes('must match')) {
        // Good, validation failed as expected
      } else {
        throw new Error(`Expected validation to fail for dm:invalid, got: ${text}`);
      }
    });

    // Test suppressMs validation
    await test('suppressMs validation: value < 1000ms shows error', async () => {
      const nameInput = page.locator('#seq-editor-name');
      await nameInput.fill('DM:TEST');

      // Range inputs clamp to [min, max], so values below min can't be set via DOM events.
      // Set editorState directly and call updateValidationSummary via the testing API.
      await page.evaluate(() => {
        window.__seqEditorForTesting.editorState.current.suppressMs = 500;
        window.__seqEditorForTesting.updateValidationSummary();
      });
      await page.waitForTimeout(100);

      const summaryEl = page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('⚠') || text.includes('1000')) {
        // Good, validation failed as expected
      } else {
        throw new Error(`Expected validation to fail for suppressMs=500, got: ${text}`);
      }
    });

    await test('suppressMs validation: valid value clears error', async () => {
      await page.evaluate(() => {
        window.__seqEditorForTesting.editorState.current.suppressMs = 8000;
        window.__seqEditorForTesting.updateValidationSummary();
      });
      await page.waitForTimeout(100);

      const summaryEl = page.locator('#seq-editor-validation-summary');
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
      const addStepBtn = page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Expand the last card to see the type chips
      const header = lastStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

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
      const suppressInput = page.locator('#seq-editor-suppress');
      await suppressInput.fill('8000');
      await suppressInput.blur();
      await page.waitForTimeout(100);

      const summaryEl = page.locator('#seq-editor-validation-summary');
      const text = await summaryEl.textContent();
      if (text.includes('⚠') || text.includes('>=')) {
        // Good, validation failed as expected
      } else {
        throw new Error(`Expected validation to fail for suppressMs=8000 with end-t=10000, got: ${text}`);
      }

      // Clean up: remove the end step and reset suppressMs
      page.once('dialog', async (dialog) => {
        await dialog.accept();
      });
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
