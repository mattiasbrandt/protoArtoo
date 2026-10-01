// bench-auto: fixture seq.html
// Tap along: taps, then Use, store the tempo the taps meant, as Tapped, and
// one Undo restores the tempo that was there before. The tempo is what every
// step on a beat is timed from (ADR 0058), so a tap that stored another number
// would move the whole routine. Asked for by the operator on #441 (2026-10-01:
// does tap-to-set-BPM do what it shows).
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence with a typed tempo of 100 BPM.
//
// WRITES IT ALLOWS: none. Tapping is an edit in the page until Save.
//
// WHAT IT PROVES. The page's clock is held by the script, so the taps are
// exactly 500 ms apart - 120 BPM:
//   1  five taps read "5 taps, 120 BPM", and Use is offered;
//   2  Use stores 120 BPM as Tapped: the tempo field, the source word beside
//      it, and the sequence Save would send;
//   3  one Undo puts 100 BPM, Typed, back in all three;
//   4  the page sent nothing to the droid.
//
// WHY A REAL BROWSER. Real presses on the real controls, through the listeners
// the workspace bound when it opened.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/tap-along-stores-the-tapped-tempo.js
// Self-test: SELFTEST=slow taps 600 ms apart, which is 100 BPM and not the
// 120 the rows expect; rows 1 and 2 must FAIL.
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const NAME = 'DM:TAPPED';
const TAPS = 5;

const SEQUENCE = {
  format: 1, name: NAME, id: 'tapped01', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: '' },
  tempo: { bpm: 100, phase: 0, barLen: 4, barPhase: 0, source: 'typed', confidence: 1 },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: 700, type: 'dome', cmd: ':OP01' }, { t: 2400, type: 'end' }],
  closeSteps: [],
};

// The tempo, in the three places the page holds it.
const tempoShown = async (page) => ({
  field: await page.locator('#seq-editor-bpm').inputValue(),
  source: await seq.textOf(page, '#seq-editor-tempo-source'),
  stored: (await seq.routine(page)).tempo || null,
});

lib.runCheck({
  rule: 'Sequences: tap along stores the tempo the taps meant, as Tapped, and one Undo restores the tempo before',
  artifactDir: ARTIFACTS,
  selftests: ['slow'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest }) => {
    await seq.install(page, { sequences: [SEQUENCE] });
    await seq.openSequences(page, NAME);
    await seq.openInWorkspace(page, NAME);
    const gapMs = selftest === 'slow' ? 600 : 500;

    // The page reads the time of a tap from performance.now(); hold it.
    await page.evaluate(() => {
      window.__tapClock = 50000;
      window.performance.now = () => window.__tapClock;
    });
    await page.click('#seq-editor-tap-open');
    for (let tap = 0; tap < TAPS; tap += 1) {
      if (tap > 0) await page.evaluate((gap) => { window.__tapClock += gap; }, gapMs);
      await page.click('#seq-editor-tap-beat');
    }
    const count = await seq.textOf(page, '#seq-editor-tap-count');
    const useOffered = await page.locator('#seq-editor-tap-use').isEnabled();
    report.add('1', `${TAPS} taps 500 ms apart read "${TAPS} taps, 120 BPM", and Use is offered`,
      lib.verdict(count === `${TAPS} taps, 120 BPM` && useOffered), `"${count}", Use ${useOffered ? 'offered' : 'disabled'}`);
    await page.screenshot({ path: `${ARTIFACTS}/tap-along-tapped.png` });

    if (useOffered) await page.click('#seq-editor-tap-use');
    const used = await tempoShown(page);
    report.add('2', 'Use stores 120 BPM as Tapped: the field, the source word, and the sequence Save would send',
      lib.verdict(used.field === '120' && used.source === 'Tapped' && used.stored?.bpm === 120 && used.stored?.source === 'tapped'),
      JSON.stringify(used));

    if (await page.locator('#seq-editor-undo').isEnabled()) await page.click('#seq-editor-undo');
    const undone = await tempoShown(page);
    report.add('3', 'One Undo puts 100 BPM, Typed, back in all three',
      lib.verdict(undone.field === '100' && undone.source === 'Typed' && undone.stored?.bpm === 100 && undone.stored?.source === 'typed'),
      JSON.stringify(undone));

    report.add('4', 'The page sent nothing to the droid', lib.verdict(writes.length === 0), writes.map(lib.describeWrite).join('; ') || 'no write');
  },
});
