/**
 * test/playwright/seq/seq-redesign-preview-and-groups.js
 *
 * Slice 6 coverage for the seq-editor redesign (issue #10): the two headline
 * features that lacked direct assertions -
 *   1. stepPreview(step) plain-English collapsed-card preview strings per type.
 *   2. Expanded-card field grouping (Target / Behavior / Timing) with the right
 *      fields under each group.
 *
 * Frontend-only; renders a known sequence through the editor and reads the DOM.
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

  // One step of each type with known field values -> known preview strings.
  const seq = {
    format: 1,
    name: 'DM:PREVGROUP',
    suppressMs: 8000,
    toggleGroup: 'none',
    meta: { source: 'test' },
    steps: [
      { t: 0, type: 'audio', cmd: '$H' },
      { t: 100, type: 'domeRotate', speedPct: 60, durationMs: 3000 },
      { t: 200, type: 'domeRotate', speedPct: -40, durationMs: 2000 },
      { t: 300, type: 'domeRotate', speedPct: 0, durationMs: 0 },
      { t: 400, type: 'loop', body: 14, periodMs: 6461, durationMs: 45000 },
      { t: 500, type: 'random', set: 'all', mode: 'flutter', moveMs: 300, jitterMs: 0, distinct: false },
      { t: 600, type: 'audioCat', category: 'happy', fallback: '$H' },
      { t: 700, type: 'end' },
    ],
    closeSteps: [],
  };

  const expectedPreview = [
    'Play sound ($H)',
    'Rotate right at 60% for 3000ms',
    'Rotate left at 40% for 2000ms',
    'Stop dome (neutral)',
    'Repeat next 14 steps every 6461ms for 45000ms',
    'Random flutter on all (move 300ms)',
    'Play a happy sound (fallback $H)',
    'End of sequence',
  ];

  // Expected field groups per step index (only groups that should appear).
  // Maps group label -> the data-field names expected inside it.
  const expectedGroups = {
    1: { Behavior: ['direction', 'speed'], Timing: ['durationMs'] },      // domeRotate
    4: { Behavior: ['body'], Timing: ['periodMs', 'durationMs'] },        // loop
    5: { Target: ['set'], Behavior: ['mode', 'distinct'], Timing: ['moveMs', 'jitterMs'] }, // random
  };

  const browser = await chromium.launch({ headless: process.env.HEADLESS === 'true' });
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  const page = await context.newPage();

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#seq-main-card', { timeout: 5000 });

    await test('Open editor via injection', async () => {
      await page.evaluate((s) => {
        window.__seqEditorForTesting.renderEditorView(s);
        document.getElementById('seq-editor-view').classList.remove('hidden');
      }, seq);
      await page.waitForSelector('.step-card', { state: 'attached', timeout: 5000 });
    });

    await test('Collapsed cards show correct plain-English preview per type', async () => {
      const previews = await page.locator('.step-card .step-card-preview').evaluateAll(
        (els) => els.map((e) => e.textContent.trim())
      );
      if (previews.length !== expectedPreview.length) {
        throw new Error(`expected ${expectedPreview.length} previews, got ${previews.length}`);
      }
      for (let i = 0; i < expectedPreview.length; i++) {
        if (previews[i] !== expectedPreview[i]) {
          throw new Error(`card #${i} preview "${previews[i]}" !== expected "${expectedPreview[i]}"`);
        }
      }
    });

    for (const idx of Object.keys(expectedGroups)) {
      const i = Number(idx);
      const type = seq.steps[i].type;
      await test(`Expanded ${type} card #${i} groups fields under correct labels`, async () => {
        const card = page.locator('.step-card').nth(i);
        await card.locator('.step-card-header').click();
        await page.waitForTimeout(80);

        const groups = await card.locator('.step-field-group').evaluateAll((nodes) =>
          nodes.map((g) => ({
            label: g.querySelector('.step-field-group-label')?.textContent.trim(),
            fields: [...g.querySelectorAll('[data-field]')].map((f) => f.getAttribute('data-field')),
          }))
        );
        const want = expectedGroups[i];
        for (const [label, wantFields] of Object.entries(want)) {
          const g = groups.find((x) => x.label === label);
          if (!g) {
            throw new Error(`${type} card missing group "${label}" (got ${groups.map((x) => x.label).join(', ')})`);
          }
          for (const wf of wantFields) {
            if (!g.fields.includes(wf)) {
              throw new Error(`${type} group "${label}" missing field "${wf}" (got ${g.fields.join(', ')})`);
            }
          }
        }
        await card.locator('.step-card-header').click(); // collapse for isolation
        await page.waitForTimeout(40);
      });
    }

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
