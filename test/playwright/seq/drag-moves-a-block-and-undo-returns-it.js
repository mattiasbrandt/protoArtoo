// bench-auto: fixture seq.html
// Dragging a block on the timeline moves its step, and one Undo puts it back:
// a mis-drag costs one press, not the evening (ADR 0057). A press that only
// picks a block is not an edit and costs no undo slot. Introduced by #441.
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence with no tempo, so nothing snaps the block to a
// beat: a sound at 0 ms, a holo effect at 700 ms, the end at 3000 ms.
//
// WRITES IT ALLOWS: none.
//
// WHAT IT PROVES, on the holo block:
//   1  a press on it without moving changes nothing and leaves Undo off;
//   2  dragged to the right, its step starts later, the block is drawn
//      further right, and the workspace says Unsaved edits;
//   3  one Undo puts the step back at 700 ms and the block where it was;
//   4  the page sent nothing to the droid.
//
// WHY A REAL BROWSER. Pointer capture, hit testing and layout: the web suite's
// DOM has no boxes to drag across.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/drag-moves-a-block-and-undo-returns-it.js
// Self-test: SELFTEST=still lets the block go where it was pressed; row 2
// must FAIL (and row 3, which then has no edit to take back).
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const NAME = 'DM:DRAG';
const HOLO_AT = 700;
const DRAG_PX = 150;

const SEQUENCE = {
  format: 1, name: NAME, id: 'drag0001', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: '' },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: HOLO_AT, type: 'dome', cmd: 'DH:F:PULSE:BLUE' }, { t: 3000, type: 'end' }],
  closeSteps: [],
};

lib.runCheck({
  rule: 'Sequences: dragging a block moves its step, and one Undo puts it back',
  artifactDir: ARTIFACTS,
  selftests: ['still'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest }) => {
    await seq.install(page, { sequences: [SEQUENCE] });
    await seq.openSequences(page, NAME);
    await seq.openInWorkspace(page, NAME);

    // The holo step, and the block the timeline draws for it: the one block
    // on the Dome row (data/seq_timeline.js draws a DH: step there).
    const holoAt = async () => (await seq.routine(page)).steps.find((step) => step.cmd === 'DH:F:PULSE:BLUE').t;
    const block = () => page.locator('#seq-editor-timeline .tl-row[data-lane="dome"] .tl-item').first();
    const boxOf = async () => {
      const box = await block().boundingBox();
      if (!box) throw new Error('the timeline drew no block for the holo step');
      return box;
    };
    const undoOff = () => page.locator('#seq-editor-undo').isDisabled();

    const start = await boxOf();
    const x = start.x + start.width / 2;
    const y = start.y + start.height / 2;

    await page.mouse.click(x, y);
    report.add('1', 'A press on the block without moving changes nothing and leaves Undo off',
      lib.verdict((await holoAt()) === HOLO_AT && (await undoOff())), `step at ${await holoAt()} ms, Undo ${(await undoOff()) ? 'off' : 'on'}`);

    await page.mouse.move(x, y);
    await page.mouse.down();
    if (selftest !== 'still') {
      await page.mouse.move(x + DRAG_PX / 3, y, { steps: 4 });
      await page.mouse.move(x + DRAG_PX, y, { steps: 6 });
    }
    await page.mouse.up();
    const moved = await boxOf();
    const movedAt = await holoAt();
    const state = await seq.textOf(page, '#seq-editor-state');
    report.add('2', 'Dragged right: the step starts later, the block is drawn further right, and the workspace says Unsaved edits',
      lib.verdict(movedAt > HOLO_AT && moved.x > start.x + DRAG_PX / 2 && state === 'Unsaved edits'),
      `step ${HOLO_AT} -> ${movedAt} ms, block ${Math.round(start.x)} -> ${Math.round(moved.x)} px, "${state}"`);
    await page.screenshot({ path: `${ARTIFACTS}/drag-moved.png` });

    const hadEdit = !(await undoOff());
    if (hadEdit) await page.click('#seq-editor-undo');
    const back = await boxOf();
    report.add('3', `One Undo puts the step back at ${HOLO_AT} ms and the block where it was`,
      lib.verdict(hadEdit && (await holoAt()) === HOLO_AT && Math.abs(back.x - start.x) < 1),
      `${hadEdit ? 'Undo pressed' : 'Undo was off: no edit to take back'}; step at ${await holoAt()} ms, block at ${Math.round(back.x)} px`);

    report.add('4', 'The page sent nothing to the droid', lib.verdict(writes.length === 0), writes.map(lib.describeWrite).join('; ') || 'no write');
  },
});
