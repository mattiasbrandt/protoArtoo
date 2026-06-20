/**
 * test/playwright/seq/seq-dome-rotate-step.js
 *
 * Test domeRotate step type: ergonomic direction/speed/duration UI,
 * conversion to signed speedPct storage, and validation.
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

    // Inject a test sequence
    const testSeq = {
      format: 1, name: 'DM:ROTATETEST', suppressMs: 8000, toggleGroup: 'none',
      meta: { source: 'test', notes: '' },
      steps: [
        { t: 0, type: 'end' },
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

    // Test 1: Add domeRotate step and verify default state
    await test('domeRotate step renders with direction/speed/durationMs fields', async () => {
      const addStepBtn = page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const steps = page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Click domeRotate type
      const typeChip = lastStep.locator('[data-type="domeRotate"]');
      await typeChip.click();
      await page.waitForTimeout(100);

      // Verify fields exist
      const directionSelect = lastStep.locator('.step-field-direction');
      const speedInput = lastStep.locator('.step-field-speed');
      const durationInput = lastStep.locator('.step-field-durationMs');

      const directionCount = await directionSelect.count();
      const speedCount = await speedInput.count();
      const durationCount = await durationInput.count();

      if (directionCount !== 1) throw new Error(`Expected 1 direction select, found ${directionCount}`);
      if (speedCount !== 1) throw new Error(`Expected 1 speed input, found ${speedCount}`);
      if (durationCount !== 1) throw new Error(`Expected 1 duration input, found ${durationCount}`);

      // Verify default values
      const directionValue = await directionSelect.inputValue();
      const speedValue = await speedInput.inputValue();
      const durationValue = await durationInput.inputValue();

      if (directionValue !== 'stop') throw new Error(`Expected direction 'stop', got '${directionValue}'`);
      if (speedValue !== '0') throw new Error(`Expected speed '0', got '${speedValue}'`);
      if (durationValue !== '0') throw new Error(`Expected duration '0', got '${durationValue}'`);
    });

    // Test 2: Set direction to Right with speed 50 and duration 1000
    await test('Right direction with speed 50 converts to speedPct +50', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const targetStep = steps.nth(count - 1);

      const directionSelect = targetStep.locator('.step-field-direction');
      const speedInput = targetStep.locator('.step-field-speed');
      const durationInput = targetStep.locator('.step-field-durationMs');

      // Set Right direction
      await directionSelect.selectOption('right');
      await page.waitForTimeout(50);

      // Set speed to 50
      await speedInput.fill('50');
      await speedInput.blur();
      await page.waitForTimeout(50);

      // Set duration to 1000
      await durationInput.fill('1000');
      await durationInput.blur();
      await page.waitForTimeout(100);

      // Verify the step in editorState has speedPct = 50 (positive)
      const stepData = await page.evaluate(() => {
        if (window.__seqEditorForTesting && window.__seqEditorForTesting.editorState) {
          const state = window.__seqEditorForTesting.editorState;
          return state.current.steps[state.current.steps.length - 1];
        }
        return null;
      });

      if (!stepData) throw new Error('Could not retrieve step data from editor state');
      if (stepData.type !== 'domeRotate') throw new Error(`Expected type 'domeRotate', got '${stepData.type}'`);
      if (stepData.speedPct !== 50) throw new Error(`Expected speedPct 50, got ${stepData.speedPct}`);
      if (stepData.durationMs !== 1000) throw new Error(`Expected durationMs 1000, got ${stepData.durationMs}`);
    });

    // Test 3: Change to Left direction with speed 75
    await test('Left direction with speed 75 converts to speedPct -75', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const targetStep = steps.nth(count - 1);

      const directionSelect = targetStep.locator('.step-field-direction');
      const speedInput = targetStep.locator('.step-field-speed');

      // Change to Left
      await directionSelect.selectOption('left');
      await page.waitForTimeout(50);

      // Set speed to 75
      await speedInput.fill('75');
      await speedInput.blur();
      await page.waitForTimeout(100);

      // Verify speedPct = -75
      const stepData = await page.evaluate(() => {
        if (window.__seqEditorForTesting && window.__seqEditorForTesting.editorState) {
          const state = window.__seqEditorForTesting.editorState;
          return state.current.steps[state.current.steps.length - 1];
        }
        return null;
      });

      if (stepData.speedPct !== -75) throw new Error(`Expected speedPct -75, got ${stepData.speedPct}`);
      // Duration should remain from previous step
      if (stepData.durationMs !== 1000) throw new Error(`Expected durationMs 1000, got ${stepData.durationMs}`);
    });

    // Test 4: Change to Stop direction forces durationMs to 0
    await test('Stop direction forces durationMs to 0', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const targetStep = steps.nth(count - 1);

      const directionSelect = targetStep.locator('.step-field-direction');
      const durationInput = targetStep.locator('.step-field-durationMs');

      // Set duration to a non-zero value
      await durationInput.fill('500');
      await durationInput.blur();
      await page.waitForTimeout(50);

      // Change to Stop direction (this should trigger the change listener that forces duration to 0)
      await directionSelect.selectOption('stop');
      await page.waitForTimeout(150);  // Give event propagation time

      // Verify durationMs is forced to 0 by checking both the input value and the stored state
      const durationValue = await durationInput.inputValue();
      if (durationValue !== '0') throw new Error(`Expected duration input forced to '0', got '${durationValue}'`);

      // Verify speedPct = 0 and durationMs = 0 in stored state
      const stepData = await page.evaluate(() => {
        if (window.__seqEditorForTesting && window.__seqEditorForTesting.editorState) {
          const state = window.__seqEditorForTesting.editorState;
          return state.current.steps[state.current.steps.length - 1];
        }
        return null;
      });

      if (stepData.speedPct !== 0) throw new Error(`Expected speedPct 0, got ${stepData.speedPct}`);
      if (stepData.durationMs !== 0) throw new Error(`Expected durationMs 0 in stored state, got ${stepData.durationMs}`);
    });

    // Test 5: Maximum speed boundary (100%) works correctly
    await test('Maximum speed 100% with Right direction converts to speedPct +100', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const targetStep = steps.nth(count - 1);

      const speedInput = targetStep.locator('.step-field-speed');
      const directionSelect = targetStep.locator('.step-field-direction');
      const durationInput = targetStep.locator('.step-field-durationMs');

      // Set Right direction with max speed 100
      await directionSelect.selectOption('right');
      await page.waitForTimeout(50);

      await speedInput.fill('100');
      await speedInput.blur();
      await page.waitForTimeout(50);

      await durationInput.fill('1000');
      await durationInput.blur();
      await page.waitForTimeout(100);

      // Verify speedPct = 100
      const stepData = await page.evaluate(() => {
        if (window.__seqEditorForTesting && window.__seqEditorForTesting.editorState) {
          const state = window.__seqEditorForTesting.editorState;
          return state.current.steps[state.current.steps.length - 1];
        }
        return null;
      });

      if (stepData.speedPct !== 100) throw new Error(`Expected speedPct 100, got ${stepData.speedPct}`);
      if (stepData.durationMs !== 1000) throw new Error(`Expected durationMs 1000, got ${stepData.durationMs}`);
    });

    // Test 6: Non-zero speed with zero duration triggers validation error
    await test('Non-zero speed with zero durationMs shows validation error', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const targetStep = steps.nth(count - 1);

      const directionSelect = targetStep.locator('.step-field-direction');
      const speedInput = targetStep.locator('.step-field-speed');
      const durationInput = targetStep.locator('.step-field-durationMs');
      const stepRemoveBtn = targetStep.locator('.step-remove');

      // Set up: Right direction with speed 30 and valid duration
      await directionSelect.selectOption('right');
      await page.waitForTimeout(50);

      await speedInput.fill('30');
      await speedInput.blur();
      await page.waitForTimeout(50);

      await durationInput.fill('2000');
      await durationInput.blur();
      await page.waitForTimeout(100);

      // Now set duration to 0 (invalid for non-zero speed)
      await durationInput.fill('0');
      await durationInput.blur();
      await page.waitForTimeout(150);

      // Check for validation error - look at step row error state
      const hasError = await targetStep.evaluate((el) => el.classList.contains('step-row-error-state'));
      if (!hasError) throw new Error('Expected step-row-error-state class for non-zero speed with zero duration');

      // Verify the stored state shows the invalid condition
      const stepDataWithError = await page.evaluate(() => {
        if (window.__seqEditorForTesting && window.__seqEditorForTesting.editorState) {
          const state = window.__seqEditorForTesting.editorState;
          return state.current.steps[state.current.steps.length - 1];
        }
        return null;
      });
      if (stepDataWithError.speedPct !== 30) throw new Error(`Expected speedPct 30, got ${stepDataWithError.speedPct}`);
      if (stepDataWithError.durationMs !== 0) throw new Error(`Expected durationMs 0, got ${stepDataWithError.durationMs}`);

      // Clean up by removing the step
      await stepRemoveBtn.click();
      await page.waitForTimeout(100);
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
