/**
 * test/playwright/seq/seq-visual-preset-step.js
 *
 * Test visual preset (DV:) step: now implemented as a dome step sub-mode (not a separate type).
 * A visual preset step is a dome type with cmd starting with "DV:".
 * Validates that:
 * - Visual Preset step is stored as type="dome" with cmd="DV:..."
 * - SeqProtocolCheck.validateStep returns ok=true for DV: steps
 * - Picker shows preset dropdown when in preset mode
 * - Loading a dome step with cmd:"DV:VADER" renders as preset card (not panel/advanced)
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

    // Test 1: Protocol validation passes for DV: steps (dome type)
    await test('SeqProtocolCheck.validateStep passes for DV: preset steps', async () => {
      const validationResult = await page.evaluate(() => {
        const step = { t: 0, type: 'dome', cmd: 'DV:VADER' };
        return window.SeqProtocolCheck && window.SeqProtocolCheck.validateStep
          ? window.SeqProtocolCheck.validateStep(step, 0, [step])
          : null;
      });

      if (!validationResult) {
        throw new Error('SeqProtocolCheck not available or validateStep not found');
      }

      if (!validationResult.ok) {
        throw new Error(`Expected validation ok=true, got error: ${validationResult.error}`);
      }
    });

    // Test 2: Protocol validation rejects invalid DV preset names
    await test('SeqProtocolCheck rejects invalid DV preset names', async () => {
      const validationResult = await page.evaluate(() => {
        const step = { t: 0, type: 'dome', cmd: 'DV:INVALID_PRESET' };
        return window.SeqProtocolCheck && window.SeqProtocolCheck.validateStep
          ? window.SeqProtocolCheck.validateStep(step, 0, [step])
          : null;
      });

      if (!validationResult) {
        throw new Error('SeqProtocolCheck not available or validateStep not found');
      }

      if (validationResult.ok) {
        throw new Error(`Expected validation to fail for invalid preset, but it passed`);
      }

      if (!validationResult.error || !validationResult.error.includes('not a known dome visual preset')) {
        throw new Error(`Expected error about unknown preset, got: ${validationResult.error}`);
      }
    });

    // Test 3: Load a dome step with DV: cmd renders as preset mode (not panel/advanced)
    await test('loading a dome step with cmd="DV:LEIA" renders as preset picker', async () => {
      // Create and inject a sequence with a visual preset step
      const preloadSeq = {
        format: 1, name: 'DM:PRELOADTEST', suppressMs: 8000, toggleGroup: 'none',
        meta: { source: 'test', notes: '' },
        steps: [
          { t: 0, type: 'dome', cmd: 'DV:LEIA' },
          { t: 100, type: 'end' },
        ],
        closeSteps: [],
      };
      await page.evaluate((seq) => {
        if (window.__seqEditorForTesting && window.__seqEditorForTesting.renderEditorView) {
          window.__seqEditorForTesting.renderEditorView(seq);
          document.getElementById('seq-editor-view').classList.remove('hidden');
        }
      }, preloadSeq);
      await page.waitForSelector('#seq-editor-view:not(.hidden)', { timeout: 5000 });
      await page.waitForTimeout(200);

      const steps = page.locator('.step-card');
      const count = await steps.count();
      const firstStep = steps.nth(0);

      // Expand the first step
      const header = firstStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

      // Check for preset select (indicates preset mode)
      const presetSelect = firstStep.locator('.step-field-preset');
      const selectCount = await presetSelect.count();
      if (selectCount !== 1) throw new Error(`Expected 1 preset select for DV:LEIA step, found ${selectCount}`);

      // Verify the preset select is set to LEIA
      const value = await presetSelect.inputValue();
      if (value !== 'LEIA') {
        throw new Error(`Expected preset 'LEIA', got '${value}'`);
      }

      // Verify no panel action select (not panel mode)
      const domeActionSelect = firstStep.locator('.dome-action-select');
      const actionCount = await domeActionSelect.count();
      if (actionCount > 0) throw new Error(`Expected 0 dome-action-select for preset, found ${actionCount}`);

      // Verify no raw text box (not advanced mode)
      const textBox = firstStep.locator('.step-field-cmd').filter({ hasNot: page.locator('[type="hidden"]') });
      const textBoxCount = await textBox.count();
      if (textBoxCount > 0) throw new Error(`Unexpected raw text cmd field for preset`);
    });

    // Test 4: Collapsed card preview shows friendly preset name
    await test('collapsed card preview shows friendly preset name', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const firstStep = steps.nth(0);

      // Collapse the card to see preview
      const header = firstStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

      // Check preview text
      const preview = firstStep.locator('.step-card-preview');
      const text = await preview.textContent();
      if (!text.includes('Visual preset: Leia')) {
        throw new Error(`Expected preview 'Visual preset: Leia', got '${text}'`);
      }
    });

    // Test 5: Selecting a different preset updates cmd to DV:<NAME>
    await test('selecting a different preset updates cmd field', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const firstStep = steps.nth(0);

      // Expand
      const header = firstStep.locator('.step-card-header');
      await header.click();
      await page.waitForTimeout(100);

      // Select VADER preset
      const presetSelect = firstStep.locator('.step-field-preset');
      await presetSelect.selectOption('VADER');
      await page.waitForTimeout(100);

      // Check the hidden cmd field
      const hiddenCmd = firstStep.locator('input[data-field="cmd"][type="hidden"]');
      const cmdValue = await hiddenCmd.inputValue();
      if (cmdValue !== 'DV:VADER') {
        throw new Error(`Expected cmd 'DV:VADER', got '${cmdValue}'`);
      }
    });

    // Test 6: Preset dropdown contains all 10 whitelisted presets
    await test('preset dropdown contains all 10 DV_PRESETS', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const firstStep = steps.nth(0);

      const presetSelect = firstStep.locator('.step-field-preset');
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
        const optCount = await option.count();
        if (optCount !== 1) {
          throw new Error(`Expected 1 option for '${preset}', found ${optCount}`);
        }
      }
    });

    // Test 7: Verify step is stored as type="dome" with cmd="DV:..." in editorState
    await test('visual preset step stored as type="dome" with cmd="DV:..." in editorState', async () => {
      const editorState = await page.evaluate(() => {
        return window.__seqEditorForTesting && window.__seqEditorForTesting.editorState
          ? window.__seqEditorForTesting.editorState
          : null;
      });

      if (!editorState) {
        throw new Error('editorState not available');
      }

      // Find the dome step with DV: cmd
      const domeStep = editorState && editorState.current && editorState.current.steps
        ? editorState.current.steps.find(s => s.type === 'dome' && (s.cmd || '').startsWith('DV:'))
        : null;

      if (!domeStep) {
        throw new Error('dome step with DV: cmd not found in editorState');
      }

      // Verify type is "dome", not "visualPreset"
      if (domeStep.type !== 'dome') {
        throw new Error(`Expected type 'dome', got '${domeStep.type}'`);
      }

      // Verify cmd is DV:VADER (after the previous test change)
      if (!domeStep.cmd.startsWith('DV:')) {
        throw new Error(`Expected cmd starting with 'DV:', got '${domeStep.cmd}'`);
      }
    });

    // Test 8: Toggle from preset to panel mode changes UI (and updates cmd if needed)
    await test('dome mode toggle cycles preset → advanced → panel', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const firstStep = steps.nth(0);

      // Expand first
      const header = firstStep.locator('.step-card-header');
      const isExpanded = await header.getAttribute('aria-expanded');
      if (isExpanded === 'false') {
        await header.click();
        await page.waitForTimeout(100);
      }

      // Currently in preset mode, toggle to advanced
      const toggleBtn = firstStep.locator('.dome-mode-toggle');
      await toggleBtn.click();
      await page.waitForTimeout(100);

      // Should now be in advanced mode (raw text input)
      const textInput = firstStep.locator('.step-field-cmd').filter({ hasNot: page.locator('[type="hidden"]') });
      const textCount = await textInput.count();
      if (textCount !== 1) throw new Error(`Expected 1 raw text input in advanced mode, found ${textCount}`);

      // Toggle again to panel mode
      await toggleBtn.click();
      await page.waitForTimeout(100);

      // Should convert DV:VADER to a panel cmd (default :OP00) and render panel UI
      const actionSelect = firstStep.locator('.dome-action-select');
      const actionCount = await actionSelect.count();
      if (actionCount !== 1) throw new Error(`Expected 1 dome-action-select in panel mode, found ${actionCount}`);

      // Verify cmd was converted to :OP00
      const hiddenCmd = firstStep.locator('input[data-field="cmd"][type="hidden"]');
      const cmdValue = await hiddenCmd.inputValue();
      if (cmdValue !== ':OP00') {
        throw new Error(`Expected cmd ':OP00' after toggle to panel, got '${cmdValue}'`);
      }
    });

    // Test 9: Toggle back to preset mode and verify it converts panel cmd to DV:ROCKMARCH
    await test('toggle from panel back to preset converts cmd to DV:ROCKMARCH', async () => {
      const steps = page.locator('.step-card');
      const count = await steps.count();
      const firstStep = steps.nth(0);

      // Currently in panel mode, toggle to preset
      const toggleBtn = firstStep.locator('.dome-mode-toggle');
      await toggleBtn.click();
      await page.waitForTimeout(100);

      // Should be in preset mode
      const presetSelect = firstStep.locator('.step-field-preset');
      const selectCount = await presetSelect.count();
      if (selectCount !== 1) throw new Error(`Expected 1 preset select, found ${selectCount}`);

      // Verify cmd was set to DV:ROCKMARCH (default)
      const hiddenCmd = firstStep.locator('input[data-field="cmd"][type="hidden"]');
      const cmdValue = await hiddenCmd.inputValue();
      if (cmdValue !== 'DV:ROCKMARCH') {
        throw new Error(`Expected cmd 'DV:ROCKMARCH' after toggle to preset, got '${cmdValue}'`);
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
