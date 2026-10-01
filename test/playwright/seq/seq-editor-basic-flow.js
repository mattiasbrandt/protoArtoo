// bench-auto: fixture seq.html
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

  const browser = await chromium.launch({ headless: process.env.HEADLESS === 'true' });
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const page = await context.newPage();

  try {
    // Navigate to seq.html
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#seq-main-card', { timeout: 5000 });

    // Inject a test sequence directly — avoids needing the live /api/seq/builtins endpoint
    const testSeq = {
      format: 1, name: 'DM:FLOWTEST', suppressMs: 8000, toggleGroup: 'none',
      meta: { source: 'test', notes: '' },
      steps: [
        { t: 0, type: 'audio', cmd: '$H' },
        { t: 0, type: 'dome', cmd: ':OP00' },
        { t: 500, type: 'end' },
      ],
      closeSteps: [],
    };
    await test('Open editor via injection', async () => {
      await page.evaluate((seq) => {
        if (window.__seqEditorForTesting && window.__seqEditorForTesting.renderEditorView) {
          window.__seqEditorForTesting.renderEditorView(seq);
          document.getElementById('seq-editor-view').classList.remove('hidden');
        }
      }, testSeq);
      await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });
      // The workspace opens on the timeline; the step cards are in the step list.
      await page.click('#seq-editor-show-steps');
    });

    // Edit name
    await test('Edit name and validate', async () => {
      // The name is in the drawer's Sequence pane.
      await page.click('#seq-editor-tab-sequence');
      const nameInput = page.locator('#seq-editor-name');
      await nameInput.fill('DM:TESTCOPY');

      // The verdict carries no glyph; its status class says which it is.
      const valid = await page.locator('#seq-editor-validation-summary .seq-validation-valid').count();
      if (valid !== 1) {
        const text = await page.locator('#seq-editor-validation-summary').textContent();
        throw new Error(`Expected valid status, got: ${text.trim()}`);
      }
    });

    // Modify suppress value
    await test('Modify suppressMs via slider', async () => {
      // The Mute period slider is folded under the Sequence pane's More
      // settings. Moving it fires the slider's own input handler, which writes
      // editorState and the value beside the slider.
      await page.click('#seq-editor-tab-sequence');
      await page.click('details.seq-settings-more summary');
      await page.locator('#seq-editor-suppress').fill('9000');
      await page.waitForTimeout(100);

      const newValue = await page.evaluate(() =>
        window.__seqEditorForTesting.editorState.current.suppressMs
      );
      if (newValue !== 9000) {
        throw new Error(`Expected suppressMs to be 9000, got ${newValue}`);
      }

      // Display should update too, in seconds
      const display = page.locator('.seq-editor-slider-value');
      const displayText = await display.textContent();
      if (displayText.trim() !== '9.0 s') {
        throw new Error(`Expected slider value display to show 9.0 s, got: ${displayText}`);
      }
    });

    // Add a step
    await test('Add a new step', async () => {
      const stepsCountBefore = await page.locator('.step-card').count();
      const addStepBtn = page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const stepsCountAfter = await page.locator('.step-card').count();
      if (stepsCountAfter !== stepsCountBefore + 1) {
        throw new Error(`Expected step count to increase, was ${stepsCountBefore}, now ${stepsCountAfter}`);
      }
    });

    // Remove the last step
    await test('Remove a step', async () => {
      const stepsCountBefore = await page.locator('.step-card').count();

      // Handle confirm dialogs by accepting them
      page.once('dialog', async (dialog) => {
        await dialog.accept();
      });

      const lastRemoveBtn = page.locator('.step-remove').last();
      await lastRemoveBtn.click();
      await page.waitForTimeout(100);

      const stepsCountAfter = await page.locator('.step-card').count();
      if (stepsCountAfter !== stepsCountBefore - 1) {
        throw new Error(`Expected step count to decrease, was ${stepsCountBefore}, now ${stepsCountAfter}`);
      }
    });

    // Test button interaction
    await test('Test button disables during dispatch and re-enables', async () => {
      const testBtn = page.locator('#seq-editor-test');
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
      const feedbackEl = page.locator('#seq-editor-feedback');
      const feedbackText = await feedbackEl.textContent();
      if (!feedbackText || feedbackText.trim() === '') {
        throw new Error('Expected feedback message after test attempt');
      }
    });

    // Cancel (don't actually save). The edits above are unsaved, so Cancel
    // asks before it drops them (#441): the editor stays until Discard.
    await test('Cancel asks, and Discard returns to list view', async () => {
      const cancelBtn = page.locator('#seq-editor-cancel');
      await cancelBtn.click();
      await page.waitForSelector('#seq-modal-discard:not(.hidden)', { timeout: 2000 });
      const stillOpen = await page.locator('#seq-editor-view').evaluate((el) => !el.classList.contains('hidden'));
      if (!stillOpen) {
        throw new Error('Cancel closed an editor with unsaved edits without asking');
      }
      await page.locator('#seq-modal-discard-confirm').click();
      // Cancel reloads the list; without a live API the empty state shows.
      // Wait for the reload to settle then check editor is hidden.
      await page.waitForTimeout(300);

      const editorView = page.locator('#seq-editor-view');
      const isHidden = await editorView.evaluate((el) => el.classList.contains('hidden'));
      if (!isHidden) {
        throw new Error('Editor view should be hidden after cancel');
      }

      // Either empty or populated state should be visible (no live API -> empty state)
      const mainCard = page.locator('#seq-main-card');
      const mainVisible = await mainCard.isVisible();
      if (!mainVisible) {
        throw new Error('Main card should be visible after cancel');
      }
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
