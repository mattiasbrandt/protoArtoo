// bench-auto: fixture seq.html
// A run started on Sequences shows Running and its own Stop, on the sequence's
// row and on the workspace's strip, until the droid's run record says it
// ended; Stop sends POST /api/seq/stop; and the run record
// (GET /api/seq/last-run, a multi-KB document the droid builds per request) is
// asked for only while a run started here is under way. Introduced by #441.
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence, DM:GREET, whose LAST RUN ENDED before the page
// opened - so the first answers after the press still carry this run's name
// and say "completed", which is the record that must not end Running.
//
// WRITES IT ALLOWS, and nothing else: POST /api/seq/test and POST
// /api/seq/stop, to the stand-in.
//
// WHAT IT PROVES:
//   1  with nothing started here, the run record is not read at all;
//   2  Test on the row: the row says Running, with the lamp, and offers
//      Stop DM:GREET in place of Test;
//   3  it still does after the droid has answered with the earlier run's
//      record, and once the run's own record is there;
//   4  the run's Stop is not the estop's red;
//   5  the workspace's strip shows the same run: Running DM:GREET and
//      Stop DM:GREET in place of Test on the droid;
//   6  Stop there sends exactly one POST /api/seq/stop;
//   7  when the record says the run ended, the strip offers Test on the droid
//      again, and the row Test;
//   8  Play on the droid, on the tap row, starts the same kind of run: the
//      strip says Running DM:GREET, with its Stop;
//   9  once that run has ended too, the run record is not read again;
//   10 a run the droid accepts and never starts stops reading as running, and
//      the row says "The droid did not start DM:GREET.".
//
// WHY A REAL BROWSER. The real request log against real timers, across the
// list and the workspace, and the colour a button actually takes.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/run-shows-running-and-its-stop.js
// Self-test: SELFTEST=peek reads the run record from the page before the
// press and again after the runs have ended; rows 1 and 9 must FAIL.
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
  rule: 'Sequences: a run started here shows Running and its Stop, and its record is read only while it is under way',
  artifactDir: ARTIFACTS,
  allow: isRunWrite,
  selftests: ['peek'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest }) => {
    const droid = await seq.install(page, {
      sequences: [GREET],
      lastRun: { valid: true, name: NAME, source: 'web', outcome: 'completed', running: false, startMs: 40000, endMs: 41500 },
    });
    const peek = () => page.evaluate((target) => fetch(target, { cache: 'no-store' }).then((response) => response.text()), RECORD);

    await seq.openSequences(page, NAME);
    if (selftest === 'peek') await peek();
    await page.waitForTimeout(QUIET_MS);
    report.add('1', 'Nothing started here: the run record is not read', lib.verdict(droid.reads(RECORD).length === 0),
      `${droid.reads(RECORD).length} read(s) of ${RECORD} in the first ${QUIET_MS} ms`);

    // The row's Test.
    await page.click(`${ROW} [data-action="test"]`);
    await page.waitForSelector(`${ROW}.is-running`, { timeout: 5000 });
    let row = await rowState(page);
    report.add('2', `Test on the row: it says Running, with the lamp, and offers Stop ${NAME} in place of Test`,
      lib.verdict(row.running && row.stop === `Stop ${NAME}` && !row.test), JSON.stringify(row));

    // The earlier run's record has been answered at least once by now, and
    // the run's own arrives after the stand-in's start delay.
    const earlier = droid.reads(RECORD).length;
    await page.waitForTimeout(3000);
    row = await rowState(page);
    report.add('3', "It still does after the earlier run's record was answered, and with the run's own",
      lib.verdict(row.running && earlier >= 2 && droid.lastRun.running === true),
      `${earlier} read(s) while the record was still the earlier run's; record now ${droid.lastRun.outcome}; row ${JSON.stringify(row)}`);

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

    const ended = droid.reads(RECORD).length;
    if (selftest === 'peek') await peek();
    await page.waitForTimeout(QUIET_MS);
    report.add('9', 'With both runs ended, the run record is not read', lib.verdict(droid.lastRun.running === false && droid.reads(RECORD).length === ended),
      `record ${droid.lastRun.outcome}; ${droid.reads(RECORD).length - ended} read(s) of ${RECORD} in the ${QUIET_MS} ms after`);

    // The droid says ok and never starts the run.
    droid.starts = false;
    await page.click(`${ROW} [data-action="test"]`);
    await page.waitForSelector(`${ROW}.is-running`, { timeout: 5000 });
    await page.waitForSelector(`${ROW}:not(.is-running)`, { timeout: 12000 }).catch(() => {});
    row = await rowState(page);
    report.add('10', 'A run the droid never starts stops reading as running, and the row says so',
      lib.verdict(!row.running && row.test && row.said === `The droid did not start ${NAME}.`), JSON.stringify(row));
    await page.screenshot({ path: `${ARTIFACTS}/run-not-started.png` });
  },
});
