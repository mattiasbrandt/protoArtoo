/**
 * test/playwright/seq/seq-visual-preset-step.js
 *
 * Test visual preset (DV:) step type: dropdown picker for named dome presets,
 * collapsed card preview, validation of preset names, round-trip through editorState.
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
    await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(() => window.__seqEditorForTesting, { timeout: 5000 });

    // Inject a test sequence
    const testSeq = {
      format: 1, name: 'DM:PRESETTEST', suppressMs: 8000, toggleGroup: 'none',
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

    // Test 1: visualPreset step appears in Common group picker
    await test('visualPreset step type appears in Common group', async () => {
      const addStepBtn = page.locator('#seq-editor-add-step');
      await addStepBtn.click();
      await page.waitForTimeout(100);

      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Expand the card
      const header = lastStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

      // Check for visualPreset chip in Common group
      const visualPresetChip = lastStep.locator('[data-type="visualPreset"]');
      const chipCount = await visualPresetChip.count();
      if (chipCount !== 1) throw new Error(`Expected 1 visualPreset chip, found ${chipCount}`);
    });

    // Test 2: Clicking visualPreset chip creates structured card with dropdown
    await test('visualPreset chip renders dropdown selector (not raw text box)', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Click visualPreset chip
      const visualPresetChip = lastStep.locator('[data-type="visualPreset"]');
      await visualPresetChip.click();
      await page.waitForTimeout(100);

      // Verify step type switched
      const activeChip = lastStep.locator('.step-type-chip.active');
      const activeType = await activeChip.getAttribute('data-type');
      if (activeType !== 'visualPreset') {
        throw new Error(`Expected active type 'visualPreset', got '${activeType}'`);
      }

      // Verify preset dropdown exists (not raw text box)
      const presetSelect = lastStep.locator('.step-field-preset');
      const selectCount = await presetSelect.count();
      if (selectCount !== 1) throw new Error(`Expected 1 preset select, found ${selectCount}`);

      // Verify raw text box does NOT exist for visualPreset
      const textBox = lastStep.locator('.step-field-cmd').filter({ hasNot: page.locator('[type="hidden"]') });
      const textBoxCount = await textBox.count();
      if (textBoxCount > 0) throw new Error(`Unexpected raw text cmd field for visualPreset`);
    });

    // Test 3: Default preset is ROCKMARCH
    await test('visualPreset defaults to ROCKMARCH', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      const presetSelect = lastStep.locator('.step-field-preset');
      const value = await presetSelect.inputValue();
      if (value !== 'ROCKMARCH') {
        throw new Error(`Expected default preset 'ROCKMARCH', got '${value}'`);
      }
    });

    // Test 4: Dropdown contains all 10 whitelisted presets
    await test('preset dropdown contains all 10 DV_PRESETS', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      const presetSelect = lastStep.locator('.step-field-preset');
      const options = presetSelect.locator('option');
      const optionCount = await options.count();

      if (optionCount !== 10) {
        throw new Error(`Expected 10 preset options, found ${optionCount}`);
      }

      // Verify all expected presets are present
      const expectedPresets = [
        'ROCKMARCH', 'VADER', 'ALARM', 'LEIA', 'HEART', 'CANTINA',
        'SCREAM', 'OVERLOAD', 'HELLO', 'RESET_VISUALS'
      ];
      for (const preset of expectedPresets) {
        const option = presetSelect.locator(`option[value="${preset}"]`);
        const count = await option.count();
        if (count !== 1) {
          throw new Error(`Expected 1 option for '${preset}', found ${count}`);
        }
      }
    });

    // Test 5: Preview text shows friendly name (e.g. "Visual preset: Rock March")
    await test('collapsed card preview shows friendly name', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Collapse the card to see preview
      const header = lastStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

      // Check preview text
      const preview = lastStep.locator('.step-card-preview');
      const text = await preview.textContent();
      if (!text.includes('Visual preset: Rock March')) {
        throw new Error(`Expected preview 'Visual preset: Rock March', got '${text}'`);
      }
    });

    // Test 6: Changing preset updates cmd field to DV:<NAME>
    await test('selecting a preset updates the cmd field', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Expand the card again
      const header = lastStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

      // Select VADER preset
      const presetSelect = lastStep.locator('.step-field-preset');
      await presetSelect.selectOption('VADER');
      await page.waitForTimeout(100);

      // Check the hidden cmd field
      const hiddenCmd = lastStep.locator('input[data-field="cmd"][type="hidden"]');
      const cmdValue = await hiddenCmd.inputValue();
      if (cmdValue !== 'DV:VADER') {
        throw new Error(`Expected cmd 'DV:VADER', got '${cmdValue}'`);
      }
    });

    // Test 7: Preview updates when preset changes
    await test('preview text updates when preset changes', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Collapse to see preview
      const header = lastStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

      // Check preview for VADER
      const preview = lastStep.locator('.step-card-preview');
      const text = await preview.textContent();
      if (!text.includes('Visual preset: Vader')) {
        throw new Error(`Expected preview 'Visual preset: Vader', got '${text}'`);
      }
    });

    // Test 8: visualPreset step appears in reference panel (What Each Step Type Does)
    await test('visualPreset appears in reference panel', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const lastStep = steps.nth(count - 1);

      // Ensure the card is expanded
      const header = lastStep.locator('.step-card-header');
      const isExpanded = await header.getAttribute('aria-expanded');
      if (isExpanded === 'false') {
        await header.click();
        await page.waitForTimeout(100);
      }

      // The reference panel HTML is only in the DOM when the card is expanded
      const stepTypeReference = lastStep.locator('.step-type-reference-item');
      const referenceItems = await stepTypeReference.count();

      if (referenceItems === 0) {
        throw new Error('No reference items found - card may not be expanded');
      }

      // Look for Visual Preset in the reference items
      let foundVisualPreset = false;
      for (let i = 0; i < referenceItems; i++) {
        const item = stepTypeReference.nth(i);
        const text = await item.textContent();
        if (text.includes('Visual Preset') && text.includes('Apply a named dome visual preset')) {
          foundVisualPreset = true;
          break;
        }
      }

      if (!foundVisualPreset) {
        throw new Error('Visual Preset not found in reference items');
      }
    });

    // Test 9: Step is stored as domeCmd in editorState
    await test('visualPreset step round-trips through editorState as domeCmd', async () => {
      const editorState = await page.evaluate(() => {
        return window.__seqEditorForTesting && window.__seqEditorForTesting.editorState
          ? window.__seqEditorForTesting.editorState
          : null;
      });

      if (!editorState) {
        throw new Error('editorState not available');
      }

      // Find the visualPreset step
      const domeStep = editorState && editorState.current && editorState.current.steps
        ? editorState.current.steps.find(s => s.type === 'visualPreset')
        : null;

      if (!domeStep) {
        throw new Error('visualPreset step not found in editorState');
      }

      if (!domeStep.cmd || !domeStep.cmd.startsWith('DV:')) {
        throw new Error(`Expected cmd starting with 'DV:', got '${domeStep.cmd}'`);
      }

      if (domeStep.cmd !== 'DV:VADER') {
        throw new Error(`Expected cmd 'DV:VADER', got '${domeStep.cmd}'`);
      }
    });

  } catch (error) {
    console.error('Fatal error:', error);
    failed++;
  } finally {
    await browser.close();
    console.log(`\n${passed} passed, ${failed} failed`);
    process.exit(failed > 0 ? 1 : 0);
  }
})();
