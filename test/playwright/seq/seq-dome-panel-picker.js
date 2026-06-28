/**
 * test/playwright/seq/seq-dome-panel-picker.js
 *
 * Test the dome SVG panel picker (issue #12, slice 1).
 * Validates: SVG rendering, panel clicks, pie gating, dropdown sync.
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

    // Inject a test sequence with a dome panel step
    const testSeq = {
      format: 1,
      name: 'DM:PANEL_TEST',
      suppressMs: 8000,
      toggleGroup: 'none',
      meta: { source: 'test', notes: '' },
      steps: [
        { t: 0, type: 'dome', cmd: ':OP01' },  // Start with P1 (ring)
        { t: 500, type: 'end' },
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

    // Expand the dome panel step
    const stepCard = page.locator('.step-card').first();
    await stepCard.locator('.step-card-header').click();
    await page.waitForTimeout(100);

    // Test 1: SVG picker is rendered
    await test('SVG dome picker is rendered', async () => {
      const svg = await page.locator('.dome-svg-picker').count();
      if (svg === 0) {
        throw new Error('SVG dome picker not found');
      }
    });

    // Test 2: Ring panels are clickable
    await test('Ring panel click sets target', async () => {
      // Click ring panel P2 (target="02")
      const p2Panel = page.locator('[data-target="02"]');
      await p2Panel.click();
      await page.waitForTimeout(100);

      // Check that the hidden cmd field was updated
      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OP02')) {
        throw new Error(`Expected cmd to contain :OP02, got ${cmd}`);
      }

      // Check that the preview was updated
      const preview = page.locator('.dome-cmd-preview').first();
      const previewText = await preview.textContent();
      if (!previewText.includes(':OP02')) {
        throw new Error(`Expected preview to show :OP02, got ${previewText}`);
      }
    });

    // Test 3: Ring target group is selectable
    await test('Ring group (15) target is selectable', async () => {
      // Click ring group circle
      const ringGroup = page.locator('[data-group="15"]');
      await ringGroup.click();
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OP15')) {
        throw new Error(`Expected cmd to contain :OP15 for ring group, got ${cmd}`);
      }
    });

    // Test 4: All panels group (00) is selectable
    await test('All panels group (00) is selectable', async () => {
      // Click all panels center dot
      const allGroup = page.locator('[data-group="00"]');
      await allGroup.click();
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OP00')) {
        throw new Error(`Expected cmd to contain :OP00, got ${cmd}`);
      }
    });

    // Test 5: Pie panel click shows safety gate
    await test('Pie panel click reveals safety gate', async () => {
      // Click pie panel P1
      const p1PiePanel = page.locator('[data-target="P1"]');
      await p1PiePanel.click();
      await page.waitForTimeout(100);

      // Check that the gate is displayed
      const gate = page.locator('.dome-pie-gate');
      const isVisible = await gate.isVisible();
      if (!isVisible) {
        throw new Error('Pie safety gate not visible after selecting pie panel');
      }
    });

    // Test 6: Pie panel requires gate confirmation before committing
    await test('Pie panel gate must be checked to select', async () => {
      // Gate should already be visible from previous test
      // Try clicking another pie panel without checking the gate
      const p2PiePanel = page.locator('[data-target="P2"]');

      // The click should not change the target if gate is not checked
      const gateCheckbox = page.locator('.dome-pie-gate-confirm');
      const isChecked = await gateCheckbox.isChecked();
      if (isChecked) {
        // Uncheck it
        await gateCheckbox.click();
        await page.waitForTimeout(50);
      }

      // Now try to click another pie panel
      const previousCmd = await page.locator('[data-field="cmd"]').first().inputValue();
      await p2PiePanel.click();
      await page.waitForTimeout(100);

      // The gate checkbox should be focused instead (indicating gate block)
      const focusedEl = await page.evaluate(() => document.activeElement?.className || '');
      const currentCmd = await page.locator('[data-field="cmd"]').first().inputValue();

      // Even if the gate isn't focused, the command should not have changed to P2
      if (currentCmd.includes(':OPP2')) {
        throw new Error('Pie panel target was set without gate confirmation');
      }
    });

    // Test 7: Checking gate allows pie panel selection
    await test('Checking pie gate allows selection', async () => {
      const gateCheckbox = page.locator('.dome-pie-gate-confirm');
      await gateCheckbox.check();
      await page.waitForTimeout(100);

      // Now click a pie panel
      const p3PiePanel = page.locator('[data-target="P3"]');
      await p3PiePanel.click();
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OPP3')) {
        throw new Error(`Expected cmd to contain :OPP3, got ${cmd}`);
      }
    });

    // Test 8: Pie group (14) shows gate when selected
    await test('Pie group (14) shows gate and requires confirmation', async () => {
      // Click ring target first to hide gate
      const ringGroup = page.locator('[data-group="15"]');
      await ringGroup.click();
      await page.waitForTimeout(100);

      let gate = page.locator('.dome-pie-gate');
      let isVisible = await gate.isVisible();
      if (isVisible) {
        throw new Error('Gate should be hidden for ring group');
      }

      // Now click pie group
      const pieGroup = page.locator('[data-group="14"]');
      await pieGroup.click();
      await page.waitForTimeout(100);

      gate = page.locator('.dome-pie-gate');
      isVisible = await gate.isVisible();
      if (!isVisible) {
        throw new Error('Gate should be visible for pie group');
      }
    });

    // Test 9: Dropdown and SVG stay in sync
    await test('Dropdown and SVG stay in sync on dropdown change', async () => {
      // Change dropdown to a ring target
      const dropdown = page.locator('.dome-target-select').first();
      await dropdown.selectOption('04');
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OP04')) {
        throw new Error(`Expected cmd to contain :OP04 after dropdown change, got ${cmd}`);
      }

      // SVG should show P4 selected
      const p4Panel = page.locator('[data-target="04"]');
      const hasSelected = await p4Panel.evaluate((el) => el.classList.contains('selected'));
      if (!hasSelected) {
        throw new Error('SVG should mark P4 as selected after dropdown change');
      }
    });

    // Test 10: Action change updates preview
    await test('Changing action updates command preview', async () => {
      const actionSelect = page.locator('.dome-action-select').first();
      await actionSelect.selectOption('CL');
      await page.waitForTimeout(100);

      const preview = page.locator('.dome-cmd-preview').first();
      const previewText = await preview.textContent();
      if (!previewText.includes(':CL')) {
        throw new Error(`Expected preview to show :CL, got ${previewText}`);
      }

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.startsWith(':CL')) {
        throw new Error(`Expected cmd to start with :CL, got ${cmd}`);
      }
    });

    // Test 11: Legend shows ring vs pie distinction
    await test('Legend displays ring vs pie visual distinction', async () => {
      const legend = page.locator('.dome-picker-legend');
      const legendText = await legend.textContent();
      if (!legendText.includes('Ring') || !legendText.includes('Pie')) {
        throw new Error('Legend should show Ring and Pie labels');
      }

      const ringLegend = page.locator('.dome-legend-swatch.ring');
      const pieLegend = page.locator('.dome-legend-swatch.pie');
      const ringVisible = await ringLegend.count();
      const pieVisible = await pieLegend.count();
      if (ringVisible === 0 || pieVisible === 0) {
        throw new Error('Legend swatches not found');
      }
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
