// bench-auto: fixture seq.html
// A Part dragged from the library onto the timeline makes its own lane, and
// one Undo takes it away: adding to a routine is one gesture and one press
// back (ADR 0057). A press on a Part that goes nowhere adds nothing and moves
// nothing. Introduced by #441.
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence that names no dome pie: a sound at 0 ms and the
// end at 4000 ms.
//
// WRITES IT ALLOWS: none.
//
// WHAT IT PROVES, on Dome pie 1 in the Parts tab:
//   1  a press on it without dragging changes nothing, leaves Undo off, and
//      says how to add it;
//   2  dragged onto the lanes, the pie has a lane, the routine gains its open
//      and its close, and the workspace says Unsaved edits;
//   3  the dropped block is the one Picked block is on;
//   4  one Undo takes the lane and both steps away;
//   5  the page sent nothing to the droid.
//
// WHY A REAL BROWSER. The drop is found by where the pointer is over the
// lanes: hit testing and layout, which the web suite's DOM has no boxes for.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/a-part-dragged-from-the-library-makes-its-lane.js
// Self-test: SELFTEST=short lets the pill go before it reaches the lanes;
// row 2 must FAIL (and rows 3 and 4, which then have no drop to speak of).
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const NAME = 'DM:DROP';
const PILL = '#seq-lib-parts [data-lib="part:pie1"]';
const LANE = '#seq-editor-timeline .tl-row[data-lane="pie1"]';
const DROP_AT_MS = 1500;

const SEQUENCE = {
  format: 1, name: NAME, id: 'drop0001', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: '' },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: 4000, type: 'end' }],
  closeSteps: [],
};

lib.runCheck({
  rule: 'Sequences: a Part dragged from the library makes its lane, and one Undo takes it away',
  artifactDir: ARTIFACTS,
  selftests: ['short'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest }) => {
    await seq.install(page, { sequences: [SEQUENCE] });
    await seq.openSequences(page, NAME);
    await seq.openInWorkspace(page, NAME);
    await page.click('#seq-editor-tab-parts');

    const pieSteps = async () => (await seq.routine(page)).steps.filter((step) => /^:(OP|CL)P1$/.test(step.cmd || ''));
    const lanes = () => page.locator(LANE).count();
    const undoOff = () => page.locator('#seq-editor-undo').isDisabled();
    // The pill, brought clear of the shell's status bar, and where it is then.
    const pillAt = async () => {
      await page.locator(PILL).evaluate((node) => node.scrollIntoView({ block: 'center' }));
      const box = await page.locator(PILL).boundingBox();
      if (!box) throw new Error('the Parts tab drew no pill for Dome pie 1');
      return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
    };

    let from = await pillAt();
    await page.mouse.click(from.x, from.y);
    const said = await seq.textOf(page, '#seq-editor-tlbar .tl-said');
    report.add('1', 'A press on the Part without dragging changes nothing, leaves Undo off, and says how to add it',
      lib.verdict((await pieSteps()).length === 0 && (await lanes()) === 0 && (await undoOff()) && said === 'Dome pie 1: drag it onto the timeline to add it.'),
      `${(await pieSteps()).length} pie step(s), ${await lanes()} lane(s), Undo ${(await undoOff()) ? 'off' : 'on'}, said "${said}"`);

    from = await pillAt();
    await page.mouse.move(from.x, from.y);
    await page.mouse.down();
    await page.mouse.move(from.x + 12, from.y - 24, { steps: 3 });
    // Where the lanes are once the pill is in hand: the ruler's own box, and
    // a little below it, inside the lanes.
    const ruler = page.locator('#seq-editor-timeline .tl-ruler');
    const track = await ruler.boundingBox();
    const windowMs = Number(await ruler.getAttribute('aria-valuemax'));
    const toX = track.x + (DROP_AT_MS / windowMs) * track.width;
    const toY = selftest === 'short' ? track.y - 60 : track.y + track.height + 10;
    await page.mouse.move(toX, toY, { steps: 8 });
    await page.screenshot({ path: `${ARTIFACTS}/library-drag-held.png` });
    await page.mouse.up();

    const dropped = await pieSteps();
    const state = await seq.textOf(page, '#seq-editor-state');
    report.add('2', 'Dragged onto the lanes: the pie has a lane, the routine has its open and its close, and the workspace says Unsaved edits',
      lib.verdict((await lanes()) === 1 && dropped.length === 2 && dropped[0].cmd === ':OPP1' && dropped[1].cmd === ':CLP1' && dropped[1].t > dropped[0].t && state === 'Unsaved edits'),
      `${await lanes()} lane(s); steps ${JSON.stringify(dropped)}; "${state}"`);
    await page.screenshot({ path: `${ARTIFACTS}/library-dropped.png` });

    const heading = await seq.textOf(page, '#seq-picked h3');
    const tabForward = (await page.locator('#seq-editor-tab-block').getAttribute('aria-pressed')) === 'true';
    report.add('3', 'The dropped block is the one Picked block is on',
      lib.verdict(tabForward && heading === 'Dome pie 1'), `Picked block ${tabForward ? 'forward' : 'not forward'}, heading "${heading}"`);

    const hadEdit = !(await undoOff());
    if (hadEdit) await page.click('#seq-editor-undo');
    report.add('4', 'One Undo takes the lane and both steps away',
      lib.verdict(hadEdit && (await lanes()) === 0 && (await pieSteps()).length === 0 && (await undoOff())),
      `${hadEdit ? 'Undo pressed' : 'Undo was off: no drop to take back'}; ${await lanes()} lane(s), ${(await pieSteps()).length} pie step(s), Undo ${(await undoOff()) ? 'off' : 'on'}`);

    report.add('5', 'The page sent nothing to the droid', lib.verdict(writes.length === 0), writes.map(lib.describeWrite).join('; ') || 'no write');
  },
});
