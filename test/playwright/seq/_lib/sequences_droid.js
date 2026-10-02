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
// What it models, from the firmware (read at 162d4472):
//   POST /api/seq/test   answers ok at once; the run's record is written
//                        `startDelayMs` later, when the dispatcher would take
//                        the run up (src/tasks/sequence_dispatcher.cpp). With
//                        `starts: false` it never is - the refused run.
//   GET /api/seq/last-run  the record (src/seq_last_run_json.cpp), the fields
//                        the page reads. With `silent` set on the droid it is
//                        a droid that has dropped off the network: the read
//                        gets no answer.
//   POST /api/seq/stop   ends a running record as `aborted`; ok either way.
//
// Not a bench-auto script: tools/bench_auto.py reads test/playwright/*/*.js,
// and this sits one folder deeper.
const lib = require('../../_lib/checks.js');

const install = async (page, { sequences, lastRun = { valid: false, note: 'no sequence run recorded since boot' }, startDelayMs = 1500, starts = true }) => {
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
      // first 40 characters of the purpose.
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
          droid.lastRun = { valid: true, name: run, source: 'web', outcome: 'running', running: true, startMs: droid.uptimeMs };
        }, startDelayMs);
      }
      return json({ ok: true });
    }
    if (method === 'POST' && apiPath === '/api/seq/stop') {
      if (droid.lastRun.running) {
        droid.lastRun = { ...droid.lastRun, outcome: 'aborted', running: false, endMs: droid.lastRun.startMs + 900 };
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
