// The droid's Sequences routes, for the rule scripts in the folder above: the
// fixture server and ../../_lib/fixture_routes.js answer only an empty
// GET /api/seq/list and /api/seq/builtins, so a script that needs a sequence
// on the droid stands the rest in here. FIXTURE=1 only.
//
// Registered on the browser CONTEXT, after the fixture's own routes, so it is
// asked before them - and after the script's write guard, which is a page
// route (../../_lib/checks.js installGuard): every write the page attempts is
// still recorded and still refused unless the script allows it.
//
// What it models, from the firmware (read at 162d4472; the status frame's
// run at a67570cf):
//   POST /api/seq/test   answers ok at once; the run's record is written
//                        `startDelayMs` later, when the dispatcher would take
//                        the run up (src/tasks/sequence_dispatcher.cpp). With
//                        `starts: false` it never is - the refused run.
//   GET /api/seq/last-run  the record (src/seq_last_run_json.cpp), the fields
//                        the page reads.
//   POST /api/seq/stop   ends a running record as `aborted`; ok either way.
//   the status frame     carries the record as `seqRun` - its name, whether
//                        it is under way, and when it began
//                        (src/web/status_json.cpp) - and the droid sends a
//                        status when a run begins and when it ends
//                        (src/sequence_run_evidence.cpp, #451). So every
//                        record() pushes one on the fixture's stream. Pass
//                        the fixture (runCheck's ctx.fixture) for this; a
//                        script that starts no run need not.
//   dropOff()            a droid that has dropped off the network: the open
//                        status stream ends, and every read of the status, of
//                        the stream and of the record goes unanswered.
//
// Not a bench-auto script: tools/bench_auto.py reads test/playwright/*/*.js,
// and this sits one folder deeper.
const lib = require('../../_lib/checks.js');

// The record as the status frame carries it (src/web/status_json.cpp).
const seqRunOf = (record) =>
  (record.valid ? { name: record.name, running: record.running === true, startMs: record.startMs } : null);

const install = async (page, { sequences, lastRun = { valid: false, note: 'no sequence run recorded since boot' }, startDelayMs = 1500, starts = true, fixture = null }) => {
  const droid = {
    sequences: new Map(sequences.map((seq) => [seq.name, JSON.parse(JSON.stringify(seq))])),
    lastRun,
    starts,
    silent: false,
    // Every request to /api/seq*, in order.
    requests: [],
    uptimeMs: 100000,
  };
  droid.reads = (apiPath) => droid.requests.filter((entry) => entry.method === 'GET' && entry.path === apiPath);
  // Writes the run record, and says so on the status stream as the droid
  // does at a run's begin and end. Only those edges write it here.
  droid.record = (next) => {
    droid.lastRun = next;
    if (!fixture) return;
    fixture.state.seqRun = seqRunOf(next);
    fixture.push();
  };
  droid.dropOff = async () => {
    if (!fixture) throw new Error('sequences_droid: dropOff() needs the fixture (runCheck ctx.fixture)');
    droid.silent = true;
    // Ends the fixture's stream server, and with it the stream the page holds
    // open: page.route only sees new requests, so cutting the one long-lived
    // stream has to happen at its server. The routes below refuse the
    // reconnect and the status reads that follow.
    await fixture.close();
  };
  // The page reads what the frame says before it sends: the record the droid
  // already holds is in its first status.
  if (fixture) fixture.state.seqRun = seqRunOf(lastRun);

  // Registered after the fixture's own routes, so asked before them.
  const deafWhenSilent = (route) => (droid.silent ? route.abort('internetdisconnected') : route.fallback());
  await page.context().route('**/api/status*', deafWhenSilent);
  await page.context().route('**/api/events*', deafWhenSilent);

  await page.context().route('**/api/seq**', async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const method = request.method();
    const apiPath = url.pathname;
    const name = url.searchParams.get('name');
    droid.requests.push({ method, path: apiPath, name, body: request.postData() || '', at: Date.now() });
    const json = (body, status = 200) => route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });

    if (method === 'GET' && apiPath === '/api/seq/list') {
      // What the list row says without the file (handleSeqListGet(),
      // src/web/api_seq.cpp): the step count, the end step's time and the
      // start of the purpose. The droid keeps 40 bytes, cut on a whole
      // character; 40 characters here is the same for the plain text a
      // fixture carries.
      return json([...droid.sequences.values()].map((seq) => {
        const steps = Array.isArray(seq.steps) ? seq.steps : [];
        const last = steps[steps.length - 1];
        const purpose = seq.meta?.purpose || '';
        return {
          name: seq.name, id: seq.id, source: seq.meta?.source || 'user', valid: true, toggleGroup: seq.toggleGroup || 'none',
          stepCount: steps.length, lengthMs: last && last.type === 'end' ? last.t : 0,
          purpose: purpose.slice(0, 40), purposeCut: purpose.length > 40,
        };
      }));
    }
    if (method === 'GET' && apiPath === '/api/seq/builtins') return json([]);
    if (method === 'GET' && apiPath === '/api/seq' && name) {
      return droid.sequences.has(name) ? json(droid.sequences.get(name)) : json({ error: 'sequence not found' }, 404);
    }
    if (method === 'GET' && apiPath === '/api/seq/last-run') return droid.silent ? route.abort('failed') : json(droid.lastRun);
    if (method === 'POST' && apiPath === '/api/seq/test') {
      const run = JSON.parse(request.postData() || '{}').name;
      if (droid.starts) {
        setTimeout(() => {
          droid.uptimeMs += 60000;
          droid.record({ valid: true, name: run, source: 'web', outcome: 'running', running: true, startMs: droid.uptimeMs });
        }, startDelayMs);
      }
      return json({ ok: true });
    }
    if (method === 'POST' && apiPath === '/api/seq/stop') {
      if (droid.lastRun.running) {
        droid.record({ ...droid.lastRun, outcome: 'aborted', running: false, endMs: droid.lastRun.startMs + 900 });
      }
      return json({ ok: true });
    }
    if (method === 'POST' && apiPath === '/api/seq') {
      const saved = JSON.parse(request.postData() || '{}');
      droid.sequences.set(saved.name, saved);
      return json({ ok: true });
    }
    return json({ error: `the script's droid has no ${method} ${apiPath}` }, 404);
  });

  return droid;
};

// These rules are proven against the stand-in above, never against a droid:
// one of them runs a sequence, and all of them need a known sequence saved.
const fixtureOnly = async () =>
  (lib.FIXTURE ? null : 'this rule is checked offline against the fixture server: run it with FIXTURE=1 (test/playwright/README.md)');

// Sequences, with the list on screen.
const openSequences = async (page, name) => {
  await lib.loadSurface(page, 'seq');
  await page.waitForSelector(`.seq-item[data-seq-name="${name}"]`, { timeout: 15000 });
};

// The builder's own sequence, opened in the workspace through its row's Edit.
const openInWorkspace = async (page, name) => {
  await page.click(`.seq-item[data-seq-name="${name}"] [data-action="edit"]`);
  await page.waitForSelector('#seq-editor-view:not(.hidden) .seq-strip', { timeout: 10000 });
  await page.waitForSelector('#seq-editor-timeline .tl-item', { timeout: 10000 });
};

// The routine as Save would send it.
const routine = (page) =>
  page.evaluate(() => JSON.parse(JSON.stringify(window.__seqEditorForTesting.editorState.current)));

const textOf = async (page, selector) => ((await page.locator(selector).first().textContent()) || '').trim();

module.exports = { install, fixtureOnly, openSequences, openInWorkspace, routine, textOf };
