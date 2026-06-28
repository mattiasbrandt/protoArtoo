/**
 * test/playwright/seq/seq-dome-panel-picker.js
 *
 * Test the dome SVG panel picker (issue #12, rework).
 * Validates: real dome geometry, panel clicks, pie gating, dropdown sync, unserviced markers.
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
      name: 'DM:PICKER_TEST',
      suppressMs: 8000,
      toggleGroup: 'none',
      meta: { source: 'test', notes: '' },
      steps: [
        { t: 0, type: 'dome', cmd: ':OP01' },  // Start with ring P1
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

    // Test 1: SVG dome picker exists with correct viewBox
    await test('SVG dome picker exists with viewBox="0 0 480 480"', async () => {
      const svg = page.locator('.dome-svg-picker');
      const count = await svg.count();
      if (count === 0) {
        throw new Error('SVG dome picker not found');
      }
      const viewBox = await svg.getAttribute('viewBox');
      if (viewBox !== '0 0 480 480') {
        throw new Error(`Expected viewBox="0 0 480 480", got "${viewBox}"`);
      }
    });

    // Test 2: All pie panels present with correct IDs and classes
    await test('All pie panels present (pp1-pp6)', async () => {
      const panels = ['pp1', 'pp2', 'pp3', 'pp4', 'pp5', 'pp6'];
      for (const pid of panels) {
        const elem = page.locator(`#${pid}`);
        const count = await elem.count();
        if (count === 0) {
          throw new Error(`Pie panel ${pid} not found`);
        }
      }
    });

    // Test 3: All ring panels present with correct IDs
    await test('All ring panels present (p1-p4, p7, p11, p13)', async () => {
      const panels = ['p1', 'p2', 'p3', 'p4', 'p7', 'p11', 'p13'];
      for (const pid of panels) {
        const elem = page.locator(`#${pid}`);
        const count = await elem.count();
        if (count === 0) {
          throw new Error(`Ring panel ${pid} not found`);
        }
      }
    });

    // Test 4: Fixed feature labels present
    await test('Fixed feature labels present (P8, P9, P10, P12, P14)', async () => {
      const features = ['r_p8', 'r_p9', 'r_p10', 'r_p12', 'r_p14', 'r_merge'];
      for (const fid of features) {
        const elem = page.locator(`#${fid}`);
        const count = await elem.count();
        if (count === 0) {
          throw new Error(`Fixed feature ${fid} not found`);
        }
      }
    });

    // Test 5: Ring panel click sets correct command
    await test('Clicking ring panel P7 (target=07) sets :OP07', async () => {
      const p7 = page.locator('#p7');
      await p7.click();
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OP07')) {
        throw new Error(`Expected :OP07, got ${cmd}`);
      }
    });

    // Test 6: Pie panel click shows safety gate
    await test('Clicking pie panel PP1 (target=P1) shows safety gate', async () => {
      const pp1 = page.locator('#pp1');
      await pp1.click();
      await page.waitForTimeout(100);

      const gate = page.locator('.dome-pie-gate');
      const isVisible = await gate.isVisible();
      if (!isVisible) {
        throw new Error('Pie safety gate not visible after selecting pie panel');
      }
    });

    // Test 7: Pie panel generates correct command format (:OPP1, not :OPP1)
    await test('Pie panel PP1 generates :OPP1 command', async () => {
      const pp1 = page.locator('#pp1');

      // Uncheck and check the gate to allow selection
      const gateCheckbox = page.locator('.dome-pie-gate-confirm');
      await gateCheckbox.check();
      await page.waitForTimeout(100);

      // Click PP1
      await pp1.click();
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OPP1')) {
        throw new Error(`Expected :OPP1, got ${cmd}`);
      }
    });

    // Test 8: PP3 and PP5 marked as unserviced (class dome-pu)
    await test('PP3 and PP5 marked as unserviced (class dome-pu)', async () => {
      const pp3 = page.locator('#pp3');
      const pp5 = page.locator('#pp5');

      const pp3Classes = await pp3.getAttribute('class');
      const pp5Classes = await pp5.getAttribute('class');

      if (!pp3Classes.includes('dome-pu')) {
        throw new Error(`PP3 should have class dome-pu, got "${pp3Classes}"`);
      }
      if (!pp5Classes.includes('dome-pu')) {
        throw new Error(`PP5 should have class dome-pu, got "${pp5Classes}"`);
      }
    });

    // Test 9: Pie gate blocks selection until confirmed
    await test('Pie gate blocks pie panel selection until confirmed', async () => {
      // First, select a ring panel to clear the gate
      const p4 = page.locator('#p4');
      await p4.click();
      await page.waitForTimeout(100);

      // Gate should be hidden
      let gate = page.locator('.dome-pie-gate');
      let isVisible = await gate.isVisible();
      if (isVisible) {
        throw new Error('Gate should be hidden for ring target');
      }

      // Uncheck the gate (if it exists)
      const gateCheckbox = page.locator('.dome-pie-gate-confirm');
      const gateCheckboxCount = await gateCheckbox.count();
      if (gateCheckboxCount > 0) {
        const isChecked = await gateCheckbox.isChecked();
        if (isChecked) {
          await gateCheckbox.click();
          await page.waitForTimeout(50);
        }
      }

      // Click pie panel without gate checked
      const pp2 = page.locator('#pp2');
      const previousCmd = await page.locator('[data-field="cmd"]').first().inputValue();
      await pp2.click();
      await page.waitForTimeout(100);

      // Command should NOT have changed to :OPP2
      const currentCmd = await page.locator('[data-field="cmd"]').first().inputValue();
      if (currentCmd.includes(':OPP2')) {
        throw new Error('Pie panel should not be selectable without gate confirmation');
      }
    });

    // Test 10: Dropdown and SVG stay in sync
    await test('Dropdown and SVG stay in sync on dropdown change', async () => {
      const dropdown = page.locator('.dome-target-select').first();
      await dropdown.selectOption('02');
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.includes(':OP02')) {
        throw new Error(`Expected cmd to contain :OP02, got ${cmd}`);
      }

      // SVG should show P2 with selected class
      const p2 = page.locator('#p2');
      const hasSelected = await p2.evaluate((el) => el.classList.contains('selected'));
      if (!hasSelected) {
        throw new Error('SVG should mark P2 as selected after dropdown change');
      }
    });

    // Test 11: Action change updates command and preview
    await test('Changing action (OP->CL) updates command', async () => {
      const actionSelect = page.locator('.dome-action-select').first();
      await actionSelect.selectOption('CL');
      await page.waitForTimeout(100);

      const hiddenInput = page.locator('[data-field="cmd"]').first();
      const cmd = await hiddenInput.inputValue();
      if (!cmd.startsWith(':CL')) {
        throw new Error(`Expected cmd to start with :CL, got ${cmd}`);
      }

      const preview = page.locator('.dome-cmd-preview').first();
      const previewText = await preview.textContent();
      if (!previewText.includes(':CL')) {
        throw new Error(`Expected preview to show :CL, got ${previewText}`);
      }
    });

    // Test 12: Legend shows ring, pie, and unserviced
    await test('Legend shows ring, pie, and unserviced distinctions', async () => {
      const legend = page.locator('.dome-picker-legend');
      const legendText = await legend.textContent();
      if (!legendText.includes('Ring') || !legendText.includes('Pie') || !legendText.includes('Unserviced')) {
        throw new Error(`Legend missing expected text. Got: "${legendText}"`);
      }

      const swatches = page.locator('.dome-legend-swatch');
      const count = await swatches.count();
      if (count < 3) {
        throw new Error(`Expected at least 3 legend swatches (ring, pie, unserviced), got ${count}`);
      }
    });

    // Test 13: Ring and pie panel data-target attributes correct
    await test('Ring and pie panels have correct data-target attributes', async () => {
      const p7 = page.locator('#p7');
      const target7 = await p7.getAttribute('data-target');
      if (target7 !== '07') {
        throw new Error(`P7 should have data-target="07", got "${target7}"`);
      }

      const pp1 = page.locator('#pp1');
      const targetP1 = await pp1.getAttribute('data-target');
      if (targetP1 !== 'P1') {
        throw new Error(`PP1 should have data-target="P1", got "${targetP1}"`);
      }
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
