// bench-auto: fixture seq.html
// Analyze a track: the tempo it heard is stored as Analyzed, and its half, two
// thirds, three halves and double are offered beside it, so a folded reading
// is a choice on screen rather than a number that looks right (#14, operator
// 2026-10-08). Picking one stores the same measurement at that tempo, and one
// Undo puts the heard tempo back.
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence with a typed tempo of 100 BPM.
//
// WRITES IT ALLOWS: none. Analyzing is an edit in the page until Save.
//
// WHAT IT PROVES. The track is a click every 500 ms, 120 BPM, built here as a
// WAV and dropped into the real file input, so the browser's own decode runs:
//   1  the track stores about 120 BPM as Analyzed, and Heard as offers five
//      levels with the heard one pressed;
//   2  pressing the half stores 60 BPM, still Analyzed, with the track's
//      fingerprint and where beat 1 sits unchanged;
//   3  one Undo puts the heard tempo back;
//   4  a different track dropped in once leaves the tempo alone and takes
//      the levels off screen: they were the other track's (Codex review of
//      #14, 2026-10-08: the stale buttons stored the new track's tempo);
//   5  a level button pressed then, planted as a decoy, stores nothing;
//   6  the page sent nothing to the droid.
//
// WHY A REAL BROWSER. decodeAudioData and the file input are the browser's,
// and the row's layout is CSS the DOM stub cannot draw.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/analysed-track-offers-its-tempo-levels.js
// Self-test: SELFTEST=silent drops a silent track, which has no beat; rows 1
// and 2 must FAIL.
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const NAME = 'DM:LEVELS';

const SEQUENCE = {
  format: 1, name: NAME, id: 'levels01', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: '' },
  tempo: { bpm: 100, phase: 0, barLen: 4, barPhase: 0, source: 'typed', confidence: 1 },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: 700, type: 'dome', cmd: ':OP01' }, { t: 2400, type: 'end' }],
  closeSteps: [],
};

// 16-bit mono WAV: a short decaying burst every `gap` s from 0.25 s, or silence.
const clickWav = (silent, gap = 0.5) => {
  const rate = 22050;
  const n = rate * 12;
  const pcm = Buffer.alloc(n * 2);
  if (!silent) {
    for (let t = 0.25; t < 12; t += gap) {
      const start = Math.round(t * rate);
      for (let i = 0; i < 600 && start + i < n; i += 1) {
        pcm.writeInt16LE(Math.round(20000 * Math.sin(i * 0.3) * Math.exp(-i / 120)), (start + i) * 2);
      }
    }
  }
  const head = Buffer.alloc(44);
  head.write('RIFF', 0); head.writeUInt32LE(36 + pcm.length, 4); head.write('WAVE', 8);
  head.write('fmt ', 12); head.writeUInt32LE(16, 16); head.writeUInt16LE(1, 20); head.writeUInt16LE(1, 22);
  head.writeUInt32LE(rate, 24); head.writeUInt32LE(rate * 2, 28); head.writeUInt16LE(2, 32); head.writeUInt16LE(16, 34);
  head.write('data', 36); head.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([head, pcm]);
};

const tempoShown = async (page) => ({
  field: await page.locator('#seq-editor-bpm').inputValue(),
  source: await seq.textOf(page, '#seq-editor-tempo-source'),
  stored: (await seq.routine(page)).tempo || null,
});

const levelsShown = (page) => page.evaluate(() => {
  const row = document.getElementById('seq-editor-levels');
  const buttons = [...document.querySelectorAll('#seq-editor-levels-seg button')];
  return {
    visible: !!row && !row.classList.contains('hidden') && row.getBoundingClientRect().height > 0,
    buttons: buttons.map((b) => ({ label: b.getAttribute('aria-label'), pressed: b.getAttribute('aria-pressed') === 'true' })),
  };
});

lib.runCheck({
  rule: 'Sequences: an analyzed track offers its tempo levels, a picked level is stored as Analyzed, and one Undo restores the heard tempo',
  artifactDir: ARTIFACTS,
  selftests: ['silent'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest }) => {
    await seq.install(page, { sequences: [SEQUENCE] });
    await seq.openSequences(page, NAME);
    await seq.openInWorkspace(page, NAME);

    await page.locator('#seq-editor-track').setInputFiles({
      name: 'clicks-120.wav', mimeType: 'audio/wav', buffer: clickWav(selftest === 'silent'),
    });
    await page.waitForFunction(() => (document.getElementById('seq-editor-tempo-feedback')?.textContent || '') !== 'Reading the track...');
    const heard = await tempoShown(page);
    const levels = await levelsShown(page);
    const pressed = levels.buttons.filter((b) => b.pressed);
    report.add('1', 'The track stores about 120 BPM as Analyzed, and Heard as offers five levels with the heard one pressed',
      lib.verdict(Math.abs((heard.stored?.bpm || 0) - 120) <= 0.5 && heard.source === 'Analyzed' && heard.stored?.source === 'analysed'
        && levels.visible && levels.buttons.length === 5 && pressed.length === 1 && /heard/.test(pressed[0].label)),
      `${JSON.stringify(heard)} ${JSON.stringify(levels)}`);
    await page.screenshot({ path: `${ARTIFACTS}/analysed-track-tempo-levels.png` });

    const half = page.locator('#seq-editor-levels-seg button[data-level="0"]');
    if (await half.count()) await half.click();
    const picked = await tempoShown(page);
    const expectHalf = Math.round((heard.stored?.bpm || 0) * 5) / 10;
    report.add('2', 'Pressing the half stores half the tempo, still Analyzed, with the fingerprint and beat 1 unchanged',
      lib.verdict(!!heard.stored && picked.stored?.bpm === expectHalf && picked.stored?.source === 'analysed'
        && picked.stored?.hash === heard.stored?.hash && picked.stored?.phase === heard.stored?.phase && picked.field === String(expectHalf)),
      JSON.stringify(picked));

    if (await page.locator('#seq-editor-undo').isEnabled()) await page.click('#seq-editor-undo');
    const undone = await tempoShown(page);
    report.add('3', 'One Undo puts the heard tempo back',
      lib.verdict(JSON.stringify(undone.stored) === JSON.stringify(heard.stored)), JSON.stringify(undone));

    await page.locator('#seq-editor-track').setInputFiles({
      name: 'clicks-150.wav', mimeType: 'audio/wav', buffer: clickWav(false, 0.4),
    });
    await page.waitForFunction(() => /Not the track/.test(document.getElementById('seq-editor-tempo-feedback')?.textContent || ''));
    const other = await tempoShown(page);
    const otherLevels = await levelsShown(page);
    report.add('4', 'A different track dropped in once leaves the tempo alone and takes the levels off screen',
      lib.verdict(JSON.stringify(other.stored) === JSON.stringify(heard.stored) && !otherLevels.visible && otherLevels.buttons.length === 0),
      `${JSON.stringify(other)} ${JSON.stringify(otherLevels)}`);

    // The decoy: a level button where the row was, pressed while the held
    // tempo is still the first track's.
    await page.evaluate(() => {
      const seg = document.getElementById('seq-editor-levels-seg');
      seg.innerHTML = '<button type="button" data-level="0" id="decoy-level">decoy</button>';
      document.getElementById('decoy-level').click();
    });
    const decoyed = await tempoShown(page);
    report.add('5', 'A level pressed then stores nothing: it is not the held tempo\'s track',
      lib.verdict(JSON.stringify(decoyed.stored) === JSON.stringify(heard.stored)), JSON.stringify(decoyed));

    report.add('6', 'The page sent nothing to the droid', lib.verdict(writes.length === 0), writes.map(lib.describeWrite).join('; ') || 'no write');
  },
});
