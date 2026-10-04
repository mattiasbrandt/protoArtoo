// bench-auto: fixture seq.html
// Leaving a sequence with unsaved edits asks first - "Discard your unsaved
// edits?" - whether the builder is going back to the list or to another
// surface, and Keep editing keeps the edits, the workspace and the surface.
// An unsaved edit is not kept anywhere else, so the question is the only
// thing between a stray press and a lost evening (#441, operator decision
// 2026-09-30).
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence in interrupt group None.
//
// WRITES IT ALLOWS: none. Nothing here saves.
//
// WHAT IT PROVES, with the interrupt group changed to Pies and not saved:
//   1  All sequences asks "Discard your unsaved edits?" and the workspace
//      stays behind the question;
//   2  Keep editing closes the question, and the workspace still holds Pies
//      and says Unsaved edits;
//   3  pressing Dashboard in the nav asks the same question, and Sequences
//      stays on screen;
//   4  Keep editing there keeps the builder on Sequences - the address too -
//      with the edit;
//   5  the page sent nothing to the droid, and the edit wrote nothing to
//      browser storage.
//
// WHY A REAL BROWSER. The shell's real navigation, held by the surface: the
// address, the nav and what is on screen have to agree after Keep editing.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/leaving-unsaved-edits-asks-first.js
// Self-test: SELFTEST=clean makes no edit, so nothing is asked and the
// builder is let go; rows 1 to 4 must FAIL (and 5: the Dashboard they are let
// go to keeps its own state in browser storage).
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const NAME = 'DM:KEEP';
const QUESTION = 'Discard your unsaved edits?';

const SEQUENCE = {
  format: 1, name: NAME, id: 'keep0001', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: '' },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: 1500, type: 'end' }],
  closeSteps: [],
};

// What is on screen: the question, the workspace, the surface, and the edit.
const seen = (page) =>
  page.evaluate(() => {
    const shown = (selector) => {
      const node = document.querySelector(selector);
      return Boolean(node) && node.checkVisibility({ checkVisibilityCSS: true });
    };
    const asking = shown('#seq-modal-discard');
    return {
      question: asking ? document.getElementById('seq-modal-discard-title').textContent.trim() : null,
      workspace: shown('#seq-editor-view .seq-strip'),
      surface: document.body.dataset.page,
      address: window.location.hash,
      group: window.__seqEditorForTesting?.editorState.current?.toggleGroup ?? null,
      state: (document.getElementById('seq-editor-state')?.textContent || '').trim(),
    };
  });

// Everything the page holds in browser storage, as one string. Opening the
// workspace caches the dome's layout there, once its read of the layout lands
// (data/seq.js renderEditorView() -> DomeLayout.load(); the cache entry is
// written before DomeLayout's onChange listeners are told). The edit must add
// nothing to it, so the snapshot is taken after that read has landed.
const stored = (page) =>
  page.evaluate(() => JSON.stringify([window.localStorage, window.sessionStorage].map((store) =>
    Object.keys(store).sort().map((key) => [key, store.getItem(key)]))));

lib.runCheck({
  rule: 'Sequences: leaving with unsaved edits asks first, and Keep editing keeps them',
  artifactDir: ARTIFACTS,
  selftests: ['clean'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest }) => {
    await seq.install(page, { sequences: [SEQUENCE] });
    await seq.openSequences(page, NAME);
    await page.evaluate(() => {
      window.__layoutLanded = 0;
      window.DomeLayout.onChange(() => { window.__layoutLanded += 1; });
    });
    await seq.openInWorkspace(page, NAME);
    await page.waitForFunction(() => window.__layoutLanded > 0, null, { timeout: 10000 });
    const storedBefore = await stored(page);

    if (selftest !== 'clean') {
      await page.click('#seq-editor-tab-sequence');
      await page.click('#seq-editor-toggle button[data-value="pies"]');
    }
    const keepEditing = async () => {
      if (await page.locator('#seq-modal-discard-keep').isVisible()) await page.click('#seq-modal-discard-keep');
      await page.waitForTimeout(400);
    };

    // The way back to the list.
    if (await page.locator('#seq-editor-cancel').isVisible()) await page.click('#seq-editor-cancel');
    await page.waitForTimeout(400);
    let now = await seen(page);
    report.add('1', `All sequences asks "${QUESTION}", with the workspace still behind it`,
      lib.verdict(now.question === QUESTION && now.workspace), JSON.stringify(now));
    await page.screenshot({ path: `${ARTIFACTS}/leave-asks.png` });

    await keepEditing();
    now = await seen(page);
    report.add('2', 'Keep editing closes the question; the workspace still holds Pies and says Unsaved edits',
      lib.verdict(now.question === null && now.workspace && now.group === 'pies' && now.state === 'Unsaved edits'), JSON.stringify(now));

    // The way to another surface.
    await page.click('#shell-nav [data-surface-link="home"]');
    await page.waitForTimeout(800);
    now = await seen(page);
    report.add('3', 'Dashboard in the nav asks the same question, and Sequences stays on screen',
      lib.verdict(now.question === QUESTION && now.surface === 'seq'), JSON.stringify(now));

    await keepEditing();
    now = await seen(page);
    report.add('4', 'Keep editing keeps the builder on Sequences, the address too, with the edit',
      lib.verdict(now.question === null && now.surface === 'seq' && now.address === '#seq' && now.workspace && now.group === 'pies'),
      JSON.stringify(now));

    const storedAfter = await stored(page);
    report.add('5', 'Nothing was sent to the droid, and the edit wrote nothing to browser storage',
      lib.verdict(writes.length === 0 && storedAfter === storedBefore),
      `${writes.map(lib.describeWrite).join('; ') || 'no write'}; browser storage ${storedAfter === storedBefore ? 'unchanged' : 'CHANGED'} since the sequence opened (${storedAfter.length} chars)`);
  },
});
