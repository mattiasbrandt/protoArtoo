// bench-auto: fixture seq.html
// A run started on Sequences shows Running and its own Stop, on the sequence's
// row and on the workspace's strip, until the droid's run record says it
// ended; Stop sends POST /api/seq/stop; and the run record document
// (GET /api/seq/last-run, multi-KB, built per request) is never asked for:
// what is running is the status frame's `seqRun`, which the droid sends when
// a run begins and when it ends (#451). Introduced by #441.
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence, DM:GREET, whose LAST RUN ENDED before the page
// opened - so the frame the page holds when Test is pressed still carries this
// run's name and says it is not under way, which is the record that must not
// end Running. Only another start time is the press's own run.
//
// WRITES IT ALLOWS, and nothing else: POST /api/seq/test and POST
// /api/seq/stop, to the stand-in.
//
// WHAT IT PROVES:
//   1  with nothing started here, the run record document is not read;
//   2  Test on the row, pressed twice in a row: one run is sent, the row
//      says Running, with the lamp, offers Stop DM:GREET in place of Test,
//      and its own line under it is clear;
//   3  it still does while the frame the page holds is the earlier run's
//      record, and once the run's own record is there;
//   4  the run's Stop is not the estop's red;
//   5  the workspace's strip shows the same run: Running DM:GREET and
//      Stop DM:GREET in place of Test on the droid;
//   6  Stop there sends exactly one POST /api/seq/stop;
//   7  when the record says the run ended, the strip offers Test on the droid
//      again, and the row Test;
//   8  Play on the droid, on the tap row, starts the same kind of run: the
//      strip says Running DM:GREET, with its Stop;
//   9  with both runs ended, the run record document was never read;
//   10 a run the droid accepts and never starts stops reading as running, and
//      the row says "The droid did not start DM:GREET.";
//   11 a run the estop ends: the row says "The estop stopped DM:GREET.";
//   12 a droid that stops answering mid-run: the row stops saying Running and
//      says "Lost touch with the droid; DM:GREET may still be running.".
//
// WHY A REAL BROWSER. The real request log against real timers, across the
// list and the workspace, and the colour a button actually takes.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/run-shows-running-and-its-stop.js
// Self-test: SELFTEST=peek reads the run record document from the page before
// the press and again after the runs have ended; rows 1, 3 and 9 must FAIL.
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const QUIET_MS = Number(process.env.QUIET_MS || 2500);
const NAME = 'DM:GREET';
const RECORD = '/api/seq/last-run';

const GREET = {
  format: 1, name: NAME, id: 'greet001', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: 'Says hello' },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: 700, type: 'dome', cmd: ':OP00' }, { t: 1500, type: 'end' }],
  closeSteps: [],
};

const ROW = `.seq-item[data-seq-name="${NAME}"]`;
const isRunWrite = (entry) => entry.method === 'POST' && (entry.path === '/api/seq/test' || entry.path === '/api/seq/stop');

// What the row shows of the run, as an operator sees it.
const rowState = (page) =>
  page.evaluate((selector) => {
    const row = document.querySelector(selector);
    const shown = (node) => Boolean(node) && node.checkVisibility({ checkVisibilityCSS: true });
    const says = row.querySelector('.seq-row-run');
    const stop = row.querySelector('[data-action="stop"]');
    return {
      running: shown(says) && says.textContent.trim() === 'Running' && shown(says.querySelector('.indicator.ok')),
      stop: shown(stop) ? stop.textContent.trim() : null,
      test: shown(row.querySelector('[data-action="test"]')),
      said: (row.querySelector('.seq-item-feedback')?.textContent || '').trim(),
    };
  }, ROW);

const stripState = (page) =>
  page.evaluate(() => {
    const shown = (id) => {
      const node = document.getElementById(id);
      return Boolean(node) && node.checkVisibility({ checkVisibilityCSS: true });
    };
    return {
      says: shown('seq-editor-running') ? document.getElementById('seq-editor-running').textContent.trim() : null,
      lamp: shown('seq-editor-running') && Boolean(document.querySelector('#seq-editor-running .indicator.ok')),
      stop: shown('seq-editor-stop') ? document.getElementById('seq-editor-stop').textContent.trim() : null,
      test: shown('seq-editor-test'),
    };
  });

lib.runCheck({
  rule: 'Sequences: a run started here shows Running and its Stop, read off the status frame, and the run record document is never read',
  artifactDir: ARTIFACTS,
  allow: isRunWrite,
  selftests: ['peek'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, fixture, report, selftest }) => {
    const EARLIER_START_MS = 40000;
    const droid = await seq.install(page, {
      sequences: [GREET],
      lastRun: { valid: true, name: NAME, source: 'web', outcome: 'completed', running: false, startMs: EARLIER_START_MS, endMs: 41500 },
      fixture,
    });
    // The run record in the frame the page holds now.
    const heldRun = () => page.evaluate(() => window.PALiveReading.current().status?.seqRun ?? null);
    const peek = () => page.evaluate((target) => fetch(target, { cache: 'no-store' }).then((response) => response.text()), RECORD);

    await seq.openSequences(page, NAME);
    if (selftest === 'peek') await peek();
    await page.waitForTimeout(QUIET_MS);
    report.add('1', 'Nothing started here: the run record document is not read', lib.verdict(droid.reads(RECORD).length === 0),
      `${droid.reads(RECORD).length} read(s) of ${RECORD} in the first ${QUIET_MS} ms`);

    // The row's Test, pressed twice as a hurried hand does.
    await page.dblclick(`${ROW} [data-action="test"]`);
    await page.waitForSelector(`${ROW}.is-running`, { timeout: 5000 });
    let row = await rowState(page);
    const heldAtPress = await heldRun();
    const sent = writes.filter((entry) => entry.path === '/api/seq/test').length;
    report.add('2', `Test on the row, pressed twice: one run sent; it says Running, with the lamp, offers Stop ${NAME}, and its own line is clear`,
      lib.verdict(sent === 1 && row.running && row.stop === `Stop ${NAME}` && !row.test && row.said === ''), `${sent} run(s) sent; ${JSON.stringify(row)}`);

    // Row 2 was read while the frame the page held was still the earlier
    // run's record: the same name, not under way. The run's own arrives on
    // the stream after the stand-in's start delay, under another start time.
    const earlierHeld = heldAtPress !== null && heldAtPress.name === NAME && heldAtPress.running === false && heldAtPress.startMs === EARLIER_START_MS;
    await page.waitForFunction((earlier) => {
      const run = window.PALiveReading.current().status?.seqRun;
      return Boolean(run) && run.running === true && run.startMs !== earlier;
    }, EARLIER_START_MS, { timeout: 5000 }).catch(() => {});
    const heldOwn = await heldRun();
    row = await rowState(page);
    report.add('3', "It still does while the page held the earlier run's record, and with the run's own",
      lib.verdict(earlierHeld && heldOwn?.running === true && heldOwn.startMs !== EARLIER_START_MS && row.running && droid.reads(RECORD).length === 0),
      `at the press the page held ${JSON.stringify(heldAtPress)}; now ${JSON.stringify(heldOwn)}; row ${JSON.stringify(row)}; ${droid.reads(RECORD).length} read(s) of ${RECORD}`);

    const colours = await page.evaluate((selector) => {
      const stop = document.querySelector(`${selector} [data-action="stop"]`);
      const style = getComputedStyle(stop);
      const probe = document.createElement('span');
      probe.style.color = 'var(--danger)';
      document.body.appendChild(probe);
      const danger = getComputedStyle(probe).color;
      probe.remove();
      return { danger, classes: stop.className, background: style.backgroundColor, border: style.borderTopColor, text: style.color };
    }, ROW);
    report.add('4', "The run's Stop is not the estop's red",
      lib.verdict(!/\bdanger\b/.test(colours.classes) && ![colours.background, colours.border, colours.text].includes(colours.danger)),
      JSON.stringify(colours));
    await page.screenshot({ path: `${ARTIFACTS}/run-row-running.png` });

    // The same run, on the workspace's strip.
    await seq.openInWorkspace(page, NAME);
    let strip = await stripState(page);
    report.add('5', `The strip shows the same run: Running ${NAME} and Stop ${NAME} in place of Test on the droid`,
      lib.verdict(strip.says === `Running ${NAME}` && strip.lamp && strip.stop === `Stop ${NAME}` && !strip.test), JSON.stringify(strip));
    await page.screenshot({ path: `${ARTIFACTS}/run-strip-running.png` });

    const since = writes.length;
    await page.click('#seq-editor-stop');
    await page.waitForFunction(() => !document.getElementById('seq-editor-test').classList.contains('hidden'), null, { timeout: 5000 }).catch(() => {});
    const stops = writes.slice(since).filter((entry) => entry.path === '/api/seq/stop');
    report.add('6', 'Stop sends exactly one POST /api/seq/stop', lib.verdict(stops.length === 1 && stops[0].allowed && writes.length === since + 1),
      writes.slice(since).map(lib.describeWrite).join('; ') || 'no write');

    strip = await stripState(page);

    // The third way a run starts here: the tap row's Play on the droid.
    const testOffered = () => page.waitForFunction(() => !document.getElementById('seq-editor-test').classList.contains('hidden'), null, { timeout: 5000 }).catch(() => {});
    await testOffered();
    await page.click('#seq-editor-tap-open');
    await page.click('#seq-editor-tap-play');
    await page.waitForFunction(() => !document.getElementById('seq-editor-running').classList.contains('hidden'), null, { timeout: 5000 }).catch(() => {});
    const played = await stripState(page);
    // Stopped once the stand-in has started it, so its record ends.
    for (let waited = 0; waited < 5000 && !droid.lastRun.running; waited += 100) await page.waitForTimeout(100);
    if (played.stop) await page.click('#seq-editor-stop');
    await testOffered();

    await page.click('#seq-editor-cancel');
    await page.waitForSelector(ROW, { timeout: 5000 });
    row = await rowState(page);
    report.add('7', 'The record says the run ended: the strip offers Test on the droid again, and the row Test',
      lib.verdict(strip.test && strip.says === null && strip.stop === null && row.test && !row.running && row.stop === null),
      `record ${droid.lastRun.outcome}; strip ${JSON.stringify(strip)}; row ${JSON.stringify(row)}`);

    report.add('8', `Play on the droid, on the tap row, starts the same kind of run: the strip says Running ${NAME}, with its Stop`,
      lib.verdict(played.says === `Running ${NAME}` && played.lamp && played.stop === `Stop ${NAME}` && !played.test), JSON.stringify(played));

    const readsAtEnd = droid.reads(RECORD).length;
    if (selftest === 'peek') await peek();
    await page.waitForTimeout(QUIET_MS);
    report.add('9', 'With both runs ended, the run record document was never read',
      lib.verdict(droid.lastRun.running === false && droid.reads(RECORD).length === 0),
      `record ${droid.lastRun.outcome}; ${droid.reads(RECORD).length} read(s) of ${RECORD} in the whole run, ${droid.reads(RECORD).length - readsAtEnd} of them in the ${QUIET_MS} ms after`);

    // The droid says ok and never starts the run.
    droid.starts = false;
    await page.click(`${ROW} [data-action="test"]`);
    await page.waitForSelector(`${ROW}.is-running`, { timeout: 5000 });
    await page.waitForSelector(`${ROW}:not(.is-running)`, { timeout: 12000 }).catch(() => {});
    row = await rowState(page);
    report.add('10', 'A run the droid never starts stops reading as running, and the row says so',
      lib.verdict(!row.running && row.test && row.said === `The droid did not start ${NAME}.`), JSON.stringify(row));
    await page.screenshot({ path: `${ARTIFACTS}/run-not-started.png` });

    // A run the stand-in starts, for the two endings that are not the run's own.
    const startRun = async () => {
      droid.starts = true;
      await page.click(`${ROW} [data-action="test"]`);
      await page.waitForSelector(`${ROW}.is-running`, { timeout: 5000 });
      for (let waited = 0; waited < 5000 && !droid.lastRun.running; waited += 100) await page.waitForTimeout(100);
    };
    const ended = () => page.waitForSelector(`${ROW}:not(.is-running)`, { timeout: 15000 }).catch(() => {});

    // The estop latches and the run ends. Both are set before the one push,
    // so the frame that ends the run is a latched one: a run that ends while
    // the reading is latched is the estop's (data/live_reading.js RUN_ENDINGS).
    await startRun();
    fixture.state.estop = true;
    droid.record({ ...droid.lastRun, outcome: 'estop', running: false, endMs: droid.lastRun.startMs + 300 });
    await ended();
    row = await rowState(page);
    report.add('11', 'A run the estop ends: the row says so', lib.verdict(!row.running && row.said === `The estop stopped ${NAME}.`), JSON.stringify(row));
    // Released as another window's STOP would, so the next run is the droid's
    // to take up.
    fixture.clearEstop();

    await startRun();
    await droid.dropOff();
    await ended();
    row = await rowState(page);
    report.add('12', 'A droid that stops answering mid-run: the row stops saying Running, and says it lost touch',
      lib.verdict(!row.running && row.test && row.said === `Lost touch with the droid; ${NAME} may still be running.`), JSON.stringify(row));
    await page.screenshot({ path: `${ARTIFACTS}/run-lost-touch.png` });
  },
});
