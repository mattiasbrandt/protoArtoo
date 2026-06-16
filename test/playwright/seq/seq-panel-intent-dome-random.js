/**
 * test/playwright/seq/seq-panel-intent-dome-random.js
 *
 * Test panel intent controls for dome steps and random step field updates.
 * Verifies:
 * - New dome steps default to :OP00
 * - Panel mode UI with action/target selects
 * - Advanced mode toggle
 * - Random step uses mode instead of pulseMin/pulseMax
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

    // Inject a test sequence
    const testSeq = {
      format: 1,
      name: 'DM:PANELTEST',
      suppressMs: 8000,
      toggleGroup: 'none',
      meta: { source: 'test', notes: '' },
      steps: [{ t: 0, type: 'audio', cmd: '$H' }],
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
    });

    // Test 1: Add a dome step and verify default
    await test('New dome step defaults to :OP00', async () => {
      const addStepBtn = page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const steps = page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Click dome type chip
      const domeChip = lastStep.locator('[data-type="dome"]');
      await domeChip.click();
      await page.waitForTimeout(100);

      // Check the hidden cmd input has :OP00
      const fieldsContainer = lastStep.locator('.step-fields');
      const cmdInput = fieldsContainer.locator('input[data-field="cmd"]');
      const cmdValue = await cmdInput.inputValue();
      if (cmdValue !== ':OP00') {
        throw new Error(`Expected :OP00, got ${cmdValue}`);
      }
    });

    // Test 2: Panel mode UI renders with action and target selects
    await test('Panel mode renders action and target selects', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);
      const fieldsContainer = lastStep.locator('.step-fields');

      // Check for action select
      const actionSelect = fieldsContainer.locator('.dome-action-select');
      const actionCount = await actionSelect.count();
      if (actionCount === 0) {
        throw new Error('Expected .dome-action-select but found none');
      }

      // Check for target select
      const targetSelect = fieldsContainer.locator('.dome-target-select');
      const targetCount = await targetSelect.count();
      if (targetCount === 0) {
        throw new Error('Expected .dome-target-select but found none');
      }

      // Check for preview span
      const preview = fieldsContainer.locator('.dome-cmd-preview');
      const previewCount = await preview.count();
      if (previewCount === 0) {
        throw new Error('Expected .dome-cmd-preview but found none');
      }
    });

    // Test 3: Changing action/target updates the command
    await test('Panel mode action/target selects update command', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);
      const fieldsContainer = lastStep.locator('.step-fields');

      // Select Close action
      const actionSelect = fieldsContainer.locator('.dome-action-select');
      await actionSelect.selectOption('CL');
      await page.waitForTimeout(100);

      // Select target P1 (01)
      const targetSelect = fieldsContainer.locator('.dome-target-select');
      await targetSelect.selectOption('01');
      await page.waitForTimeout(100);

      // Check hidden cmd input is now :CL01
      const cmdInput = fieldsContainer.locator('input[data-field="cmd"]');
      const cmdValue = await cmdInput.inputValue();
      if (cmdValue !== ':CL01') {
        throw new Error(`Expected :CL01, got ${cmdValue}`);
      }

      // Check preview also updated
      const preview = fieldsContainer.locator('.dome-cmd-preview');
      const previewText = await preview.textContent();
      if (previewText !== ':CL01') {
        throw new Error(`Expected preview :CL01, got ${previewText}`);
      }
    });

    // Test 4: Toggle to advanced mode shows text input
    await test('Dome advanced mode toggle works', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);
      const fieldsContainer = lastStep.locator('.step-fields');

      // Find and click the advanced mode toggle
      const toggleBtn = fieldsContainer.locator('.dome-mode-toggle');
      const toggleCount = await toggleBtn.count();
      if (toggleCount === 0) {
        throw new Error('No .dome-mode-toggle button found in fields container');
      }

      await toggleBtn.click();
      await page.waitForTimeout(200);

      // After toggle to advanced, should see text input with placeholder @0T6, *HP0, :SE07
      // Re-query the fieldsContainer since it may have been re-rendered
      const fieldsContainerAfter = lastStep.locator('.step-fields');
      const textInput = fieldsContainerAfter.locator('input[type="text"][data-field="cmd"]');
      const textInputCount = await textInput.count();
      if (textInputCount === 0) {
        throw new Error('Expected text input in advanced mode after toggle');
      }

      const placeholder = await textInput.getAttribute('placeholder');
      if (!placeholder || !placeholder.includes('@0T6')) {
        throw new Error(`Expected advanced placeholder with @0T6, got: ${placeholder}`);
      }
    });

    // Test 5: Random step has mode field, not pulseMin/pulseMax
    await test('Random step renders mode select, not pulseMin/pulseMax', async () => {
      const addStepBtn = page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const steps = page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Click random type chip
      const randomChip = lastStep.locator('[data-type="random"]');
      await randomChip.click();
      await page.waitForTimeout(100);

      const fieldsContainer = lastStep.locator('.step-fields');

      // Check for mode select
      const modeSelect = fieldsContainer.locator('[data-field="mode"]');
      const modeCount = await modeSelect.count();
      if (modeCount === 0) {
        throw new Error('Expected [data-field="mode"] but found none');
      }

      // Check for NO pulseMin
      const pulseMinInput = fieldsContainer.locator('[data-field="pulseMin"]');
      const pulseMinCount = await pulseMinInput.count();
      if (pulseMinCount > 0) {
        throw new Error('Random step should not have pulseMin field');
      }

      // Check for NO pulseMax
      const pulseMaxInput = fieldsContainer.locator('[data-field="pulseMax"]');
      const pulseMaxCount = await pulseMaxInput.count();
      if (pulseMaxCount > 0) {
        throw new Error('Random step should not have pulseMax field');
      }

      // Check mode default is flutter
      const modeValue = await modeSelect.inputValue();
      if (modeValue !== 'flutter') {
        throw new Error(`Expected mode default 'flutter', got ${modeValue}`);
      }
    });

    // Test 6: Random step has new set options (all, hold)
    await test('Random step set select has all, hold options', async () => {
      const steps = page.locator('.step-row');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);
      const fieldsContainer = lastStep.locator('.step-fields');

      const setSelect = fieldsContainer.locator('[data-field="set"]');
      const allOption = setSelect.locator('option[value="all"]');
      const holdOption = setSelect.locator('option[value="hold"]');

      const allCount = await allOption.count();
      const holdCount = await holdOption.count();

      if (allCount === 0) {
        throw new Error('Expected option[value="all"] in random set select');
      }
      if (holdCount === 0) {
        throw new Error('Expected option[value="hold"] in random set select');
      }
    });

    console.log(`\n✓ Passed: ${passed}`);
    console.log(`✗ Failed: ${failed}`);
    process.exit(failed > 0 ? 1 : 0);
  } catch (error) {
    console.error('Fatal error:', error);
    process.exit(1);
  } finally {
    await browser.close();
  }
})();
