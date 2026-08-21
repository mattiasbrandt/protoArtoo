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

    // Test 6: Pie panels have correct SVG structure
    await test('Pie panel SVG structure is correct', async () => {
      // All pie panels should have data-target attributes
      const pp1 = page.locator('#pp1');
      const pp4 = page.locator('#pp4');

      const pp1Target = await pp1.getAttribute('data-target');
      const pp4Target = await pp4.getAttribute('data-target');

      if (pp1Target !== 'P1') {
        throw new Error(`PP1 should have data-target="P1", got "${pp1Target}"`);
      }
      if (pp4Target !== 'P4') {
        throw new Error(`PP4 should have data-target="P4", got "${pp4Target}"`);
      }
    });

    // Test 7: Pie panel commits :OPPn directly (no gate)
    await test('Pie panel PP2 commits :OPP2 directly', async () => {
      const pp2 = page.locator('#pp2');
      await pp2.click();
      await page.waitForTimeout(100);

      const cmd = await page.locator('[data-field="cmd"]').first().inputValue();
      if (!cmd.includes(':OPP2')) {
        throw new Error(`Expected :OPP2, got ${cmd}`);
      }
    });

    // Test 8: PP3 and PP5 marked as unserviced (class pu from ported SVG)
    await test('PP3 and PP5 marked as unserviced (class pu)', async () => {
      const pp3 = page.locator('#pp3');
      const pp5 = page.locator('#pp5');

      const pp3Classes = await pp3.getAttribute('class');
      const pp5Classes = await pp5.getAttribute('class');

      if (!pp3Classes.includes('pu')) {
        throw new Error(`PP3 should have class pu, got "${pp3Classes}"`);
      }
      if (!pp5Classes.includes('pu')) {
        throw new Error(`PP5 should have class pu, got "${pp5Classes}"`);
      }
    });

    // Test 9: No pie safety gate exists; pie selects directly after a ring select
    await test('No pie gate; pie selectable directly after ring select', async () => {
      // Select a ring panel, then a pie panel — both commit with no gate
      await page.locator('#p4').click();
      await page.waitForTimeout(100);

      const gateCount = await page.locator('.dome-pie-gate').count();
      if (gateCount !== 0) {
        throw new Error('Pie safety gate should no longer exist in the DOM');
      }

      await page.locator('#pp1').click();
      await page.waitForTimeout(100);

      const cmd = await page.locator('[data-field="cmd"]').first().inputValue();
      if (!cmd.includes(':OPP1')) {
        throw new Error(`Expected :OPP1 after direct pie select, got ${cmd}`);
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

    // Test 12: SVG legend shows ring, pie, and fixed distinctions
    await test('SVG legend shows ring, pie, and fixed distinctions', async () => {
      const svg = page.locator('.dome-svg-picker');
      const svgText = await svg.textContent();
      // Ported SVG has "Ring servo", "Pie servo", "Fixed" in the legend
      if (!svgText.includes('Ring servo') || !svgText.includes('Pie servo') || !svgText.includes('Fixed')) {
        throw new Error(`SVG legend missing expected text. Got: "${svgText.substring(0, 200)}..."`);
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
