// bench-auto: fixture seq.html
// Retime to the grid moves every step onto its nearest beat and says how many
// landed, and one Undo puts every step back where it was. A bulk edit that
// could not be taken back in one press, or a receipt that counted steps that
// did not move, is the fault ADR 0060 wrote the receipt and its undo against.
// Asked for by the operator on #441 (2026-10-01).
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence at 120 BPM - a beat every 500 ms - whose steps
// sit off the beat, at 0, 430, 1080 and 1900 ms.
//
// WRITES IT ALLOWS: none.
//
// WHAT IT PROVES:
//   1  Retime to the grid puts every step on a beat: 0, 500, 1000 and 2000 ms,
//      each now placed by its beat;
//   2  the receipt counts them: "4 of 4 steps landed on a beat.";
//   3  one Undo puts all four back at the times they had, off the beat;
//   4  the page sent nothing to the droid.
//
// WHY A REAL BROWSER. Real presses, through the listeners the workspace bound,
// and the receipt as it is drawn.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/retime-puts-steps-on-beats.js
// Self-test: SELFTEST=unpressed leaves Retime to the grid alone; rows 1 and 2
// must FAIL.
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const NAME = 'DM:OFFBEAT';
const OFF_BEAT = [0, 430, 1080, 1900];
const ON_BEAT = [0, 500, 1000, 2000];

const SEQUENCE = {
  format: 1, name: NAME, id: 'offbeat1', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: '' },
  tempo: { bpm: 120, phase: 0, barLen: 4, barPhase: 0, source: 'typed', confidence: 1 },
  steps: [
    { t: OFF_BEAT[0], type: 'audio', cmd: '$H' },
    { t: OFF_BEAT[1], type: 'dome', cmd: ':OP01' },
    { t: OFF_BEAT[2], type: 'dome', cmd: ':CL01' },
    { t: OFF_BEAT[3], type: 'end' },
  ],
  closeSteps: [],
};

const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

lib.runCheck({
  rule: 'Sequences: Retime to the grid puts the steps on beats and says how many landed, and one Undo puts them back',
  artifactDir: ARTIFACTS,
  selftests: ['unpressed'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest }) => {
    await seq.install(page, { sequences: [SEQUENCE] });
    await seq.openSequences(page, NAME);
    await seq.openInWorkspace(page, NAME);
    const steps = async () => (await seq.routine(page)).steps.map((step) => ({ t: step.t, beat: step.beat ?? null }));

    if (selftest !== 'unpressed') await page.click('#seq-editor-retime');
    const retimed = await steps();
    report.add('1', `Every step is on a beat: ${ON_BEAT.join(', ')} ms, each placed by its beat`,
      lib.verdict(same(retimed.map((step) => step.t), ON_BEAT) && retimed.every((step) => Number.isInteger(step.beat))), JSON.stringify(retimed));
    const receipt = await seq.textOf(page, '#seq-editor-retime-receipt');
    report.add('2', 'The receipt counts them: "4 of 4 steps landed on a beat."', lib.verdict(receipt === '4 of 4 steps landed on a beat.'), `"${receipt}"`);
    await page.screenshot({ path: `${ARTIFACTS}/retime-receipt.png` });

    if (await page.locator('#seq-editor-undo').isEnabled()) await page.click('#seq-editor-undo');
    const undone = await steps();
    report.add('3', `One Undo puts all four back: ${OFF_BEAT.join(', ')} ms, off the beat`,
      lib.verdict(same(undone.map((step) => step.t), OFF_BEAT) && undone.every((step) => step.beat === null)), JSON.stringify(undone));

    report.add('4', 'The page sent nothing to the droid', lib.verdict(writes.length === 0), writes.map(lib.describeWrite).join('; ') || 'no write');
  },
});
