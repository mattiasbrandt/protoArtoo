// bench-auto: fixture seq.html
// Tune on a Factory sequence's row opens the workspace with the page's own
// tuning notice, and the notice names the sequence being tuned. A builder's own
// sequence, opened through Edit, carries no such notice: the notice is what
// tells the two apart before a save replaces the factory one.
//
// The notice is the page's (data/seq.js renderEditorView(), the .seq-tuning
// note). This script builds none of it and sets no editor state: it presses
// the row's button and reads what the page drew. It asserts no copy words, so
// a copy pass on the notice does not break it; the name it looks for is the
// sequence's, which is data.
//
// PRECONDITION: FIXTURE=1. The droid is a stand-in (./_lib/sequences_droid.js)
// holding one saved sequence, and this script adds one Factory sequence to it
// in the two shapes GET /api/seq/builtins answers (src/web/api_seq.cpp
// handleSeqBuiltinsGet(): the list row, and with ?name= the whole sequence as
// src/seq_json.cpp seqJsonSerializeObject() writes it).
//
// WRITES IT ALLOWS: none. Nothing here saves.
//
// WHAT IT PROVES:
//   1  Tune on the Factory row opens the workspace with the tuning notice on
//      screen, and the notice carries the Factory sequence's name;
//   2  Edit on the builder's own sequence opens the workspace with no tuning
//      notice;
//   3  the page sent nothing to the droid.
//
// WHY A REAL BROWSER. The notice has to be on screen, not only in the markup:
// the workspace is drawn by the page's own render after a fetch the button
// starts.
//
// RUN:
//   PA_FIXTURE_PORT=<port> python3 tools/serve_editor_fixture.py &
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:<port> \
//     node test/playwright/seq/seq-tune-factory-banner.js
// Self-test: SELFTEST=edit opens the builder's own sequence through Edit and
// never presses Tune; row 1 must FAIL.
const lib = require('../_lib/checks.js');
const seq = require('./_lib/sequences_droid.js');

const ARTIFACTS = 'output/playwright/seq';
const FACTORY_NAME = 'DM:VADER';
const OWN_NAME = 'DM:KEEP';

const FACTORY = {
  format: 1, name: FACTORY_NAME, suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'factory', origin: '', license: '', notes: '', purpose: '', modified: false },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: 500, type: 'end' }],
  closeSteps: [],
};

const OWN = {
  format: 1, name: OWN_NAME, id: 'keep0001', suppressMs: 8000, toggleGroup: 'none',
  meta: { source: 'user', notes: '', purpose: '' },
  steps: [{ t: 0, type: 'audio', cmd: '$H' }, { t: 1500, type: 'end' }],
  closeSteps: [],
};

// The Factory sequence on the stand-in droid. Registered after seq.install(),
// so it is asked before the stand-in's own empty builtins answer.
const installFactory = (page) =>
  page.context().route('**/api/seq/builtins**', (route) => {
    const name = new URL(route.request().url()).searchParams.get('name');
    const json = (body, status = 200) => route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
    if (!name) {
      return json([{
        name: FACTORY.name, toggleGroup: FACTORY.toggleGroup, suppressMs: FACTORY.suppressMs,
        stepCount: FACTORY.steps.length, lengthMs: 500, purpose: '',
      }]);
    }
    return name === FACTORY.name ? json(FACTORY) : json({ error: 'not found' }, 404);
  });

// What is on screen: the workspace, and the tuning notice with what it says.
const seen = (page) =>
  page.evaluate(() => {
    const shown = (node) => Boolean(node) && node.checkVisibility({ checkVisibilityCSS: true });
    const notices = [...document.querySelectorAll('#seq-editor-view .seq-tuning')];
    return {
      workspace: shown(document.querySelector('#seq-editor-view .seq-strip')),
      notices: notices.length,
      noticeShown: notices.some(shown),
      noticeText: notices.map((node) => node.textContent.replace(/\s+/g, ' ').trim()).join(' | '),
    };
  });

const workspaceOpen = (page) =>
  page.waitForSelector('#seq-editor-view:not(.hidden) .seq-strip', { timeout: 10000 });

lib.runCheck({
  rule: 'Sequences: Tune opens a Factory sequence with the tuning notice, naming it; Edit on your own shows none',
  artifactDir: ARTIFACTS,
  selftests: ['edit'],
  precondition: seq.fixtureOnly,
  run: async ({ page, writes, report, selftest, openPage }) => {
    // Sequences on a page of its own, with both sequences in the list.
    const onSequences = async (target) => {
      await seq.install(target, { sequences: [OWN] });
      await installFactory(target);
      await seq.openSequences(target, OWN_NAME);
      await target.waitForSelector(`.seq-item-factory[data-seq-name="${FACTORY_NAME}"]`, { timeout: 15000 });
    };
    await onSequences(page);

    if (selftest === 'edit') {
      await seq.openInWorkspace(page, OWN_NAME);
    } else {
      await page.click(`.seq-item-factory[data-seq-name="${FACTORY_NAME}"] [data-action="tune"]`);
      await workspaceOpen(page);
    }
    let now = await seen(page);
    report.add('1', `Tune opens the workspace with the tuning notice on screen, carrying ${FACTORY_NAME}`,
      lib.verdict(now.workspace && now.notices === 1 && now.noticeShown && now.noticeText.includes(FACTORY_NAME)),
      JSON.stringify(now));
    await page.screenshot({ path: `${ARTIFACTS}/tune-factory-notice.png` });

    // A fresh page for the builder's own sequence, so no state rides over
    // from the tune.
    const own = await openPage();
    await onSequences(own.page);
    await seq.openInWorkspace(own.page, OWN_NAME);
    now = await seen(own.page);
    report.add('2', `Edit on ${OWN_NAME}, the builder's own, opens the workspace with no tuning notice`,
      lib.verdict(now.workspace && now.notices === 0), JSON.stringify(now));

    const sent = [...writes, ...own.writes];
    report.add('3', 'Nothing was sent to the droid',
      lib.verdict(sent.length === 0), sent.map(lib.describeWrite).join('; ') || 'no write');
  },
});
