// bench-auto: fixture seq.html
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

  const browser = await chromium.launch({ headless: process.env.HEADLESS !== 'false' });
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
    // The name is in the drawer's Sequence pane.
    await page.click('#seq-editor-tab-sequence');

    // Protocol Check's verdict carries no glyph: its status class says which
    // it is (seq-validation-valid / seq-validation-error), and the sentence
    // says why.
    const verdict = () => page.evaluate(() => {
      const el = document.querySelector('#seq-editor-validation-summary .seq-validation-status');
      return {
        valid: !!el && el.classList.contains('seq-validation-valid'),
        error: !!el && el.classList.contains('seq-validation-error'),
        text: (el?.textContent || '').trim(),
      };
    });

    // Test name validation
    await test('Name validation: valid DM:XXXX format passes', async () => {
      const nameInput = page.locator('#seq-editor-name');
      await nameInput.fill('DM:VALID');

      const v = await verdict();
      if (!v.valid) throw new Error(`Expected validation to pass for DM:VALID, got: ${v.text}`);
    });

    await test('Name validation: invalid format shows error', async () => {
      const nameInput = page.locator('#seq-editor-name');
      await nameInput.fill('dm:invalid'); // lowercase prefix
      await nameInput.blur();
      await page.waitForTimeout(100);

      const v = await verdict();
      if (!v.error) throw new Error(`Expected validation to fail for dm:invalid, got: ${v.text}`);
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

      const v = await verdict();
      if (!v.error) throw new Error(`Expected validation to fail for suppressMs=500, got: ${v.text}`);
    });

    await test('suppressMs validation: valid value clears error', async () => {
      await page.evaluate(() => {
        window.__seqEditorForTesting.editorState.current.suppressMs = 8000;
        window.__seqEditorForTesting.updateValidationSummary();
      });
      await page.waitForTimeout(100);

      const v = await verdict();
      if (!v.valid) throw new Error(`Expected validation to pass for suppressMs=8000, got: ${v.text}`);
    });

    // Test suppressMs vs end time constraint
    await test('suppressMs vs end time: shows error if suppressMs < end-t', async () => {
      // Move the sequence's own end step to t=10000. The timeline draws the
      // end as a line, not a block (data/seq_timeline.js, .tl-end): a press on
      // it picks the end step, and the drawer's Picked block pane offers its
      // Starts at. The inspector writes itself again after every edit, so the
      // field is found afresh each time it is used.
      const endType = await page.evaluate(() => {
        const list = window.__seqEditorForTesting.editorState.current.steps;
        return list[list.length - 1].type;
      });
      if (endType !== 'end') throw new Error(`Expected the last step to be the end step, got ${endType}`);

      await page.locator('#seq-editor-timeline .tl-end').click();
      const startsAt = () => page.locator('#seq-picked input[data-picked="start"]');
      const setEnd = async (ms) => {
        await startsAt().fill(String(ms));
        await startsAt().press('Enter');
        await page.waitForTimeout(100);
      };
      const endAt = () => page.evaluate(() => {
        const list = window.__seqEditorForTesting.editorState.current.steps;
        return list[list.length - 1].t;
      });

      await setEnd(10000);
      if ((await endAt()) !== 10000) throw new Error(`Expected the end step at 10000 ms, got ${await endAt()}`);

      // suppressMs stays 8000, less than the 10000 ms the sequence now runs.
      const v = await verdict();
      if (!v.error) {
        throw new Error(`Expected validation to fail for suppressMs=8000 with end-t=10000, got: ${v.text}`);
      }

      // Clean up: put the end step back.
      await setEnd(1000);
      const after = await verdict();
      if (!after.valid) throw new Error(`Expected the sequence to be valid again, got: ${after.text}`);
    });

    // Regression: audioCat "fallback" is a NAMED SLOT, not a "$" sound. Factory
    // sequences ALARM/HEART/SCREAM/OVERLOAD serialize fallback as e.g. "scream",
    // which previously tripped the client validator (it demanded a "$" prefix) and
    // painted those factory cards red. The validator must accept known slots
    // (incl. "none") and reject "$"-prefixed or unknown values.
    await test('audioCat fallback: named slot passes, $-sound and unknown rejected', async () => {
      const r = await page.evaluate(() => {
        const V = window.SeqProtocolCheck;
        const mk = (fallback) => ({ t: 0, type: 'audioCat', category: 'alert', fallback });
        return {
          slotOk:   V.validateStep(mk('scream'), 0, [], true).ok,
          noneOk:   V.validateStep(mk('none'),   0, [], true).ok,
          dollarOk: V.validateStep(mk('$H'),     0, [], true).ok,
          bogusOk:  V.validateStep(mk('nope'),   0, [], true).ok,
        };
      });
      if (!r.slotOk) throw new Error('expected named slot "scream" to pass');
      if (!r.noneOk) throw new Error('expected "none" to pass');
      if (r.dollarOk) throw new Error('expected "$H" to be rejected (not a slot)');
      if (r.bogusOk) throw new Error('expected unknown slot "nope" to be rejected');
    });

  } finally {
    await browser.close();
  }

  // Summary
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
})();
