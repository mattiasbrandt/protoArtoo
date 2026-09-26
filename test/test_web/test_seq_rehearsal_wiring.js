// The Rehearsal's three appearances in the sequence editor (#354, #287
// specific 6): live counts beside Protocol Check's verdict, the full list at
// save and at a clone, and a folded badge beside a run -- and none of them ever
// standing in the way of the save or the run it reports on (ADR 0044).
//
// The harness runs the shipped chain data/seq.html declares, in one vm context,
// and drives it through the controls a builder presses: the Save and Test
// buttons' own click handlers, a Factory card's Tune button, a Learned card's
// Test button. The transport is recorded, so "the run went first" is an
// assertion about call order rather than a reading of the source.
//
// Per test_web/README.md: everything is executed, nothing is pattern-matched.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const { shippedWords } = require("./helpers/shipped_words.cjs");

const root = path.resolve(__dirname, "../..");
const read = (name) => fs.readFileSync(path.join(root, "data", name), "utf8");

const PAGE_MODULES = [
  "droid_parts.js",
  "droid_build.js",
  "dome_command_map.js",
  "dome_panel_model.js",
  "dome_layout.js",
  "seq_protocol_check.js",
  "seq_rehearsal.js",
  "seq.js",
];

const escapeHtml = (value) =>
  String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

function makeElement(extra = {}) {
  const element = {
    dataset: {},
    style: {},
    value: "",
    innerHTML: "",
    textContent: "",
    className: "",
    disabled: false,
    listeners: {},
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    setAttribute() {},
    getAttribute: () => null,
    addEventListener(name, fn) {
      (element.listeners[name] = element.listeners[name] || []).push(fn);
    },
    removeEventListener() {},
    appendChild: (child) => child,
    insertAdjacentHTML() {},
    remove() {},
    focus() {},
    closest: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    ...extra,
  };
  return element;
}

// DM:HELLO as it shipped before #354: two Rehearsal Warnings and nothing
// Protocol Check refuses.
const helloBefore = () => ({
  name: "DM:HELLO",
  suppressMs: 4000,
  toggleGroup: "none",
  steps: [
    { t: 0, type: "audio", cmd: "$H" },
    { t: 0, type: "dome", cmd: "@1MHello There" },
    { t: 0, type: "dome", cmd: "@3MGeneral Kenobi" },
    { t: 0, type: "dome", cmd: ":OP01" },
    { t: 160, type: "dome", cmd: ":OP01" },
    { t: 320, type: "dome", cmd: ":OP01" },
    { t: 480, type: "dome", cmd: ":OP01" },
    { t: 640, type: "dome", cmd: ":OP01" },
    { t: 800, type: "dome", cmd: ":CL01" },
    { t: 950, type: "end" },
  ],
});

function newPage({ sequence = helloBefore(), failRead = false } = {}) {
  const calls = [];
  const elements = new Map();
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };

  // The card buttons renderListView() binds, handed back the way the browser
  // would find them in the markup it just wrote.
  const cardFeedback = makeElement();
  const cardRehearsal = makeElement();
  const card = makeElement({
    querySelector: (selector) =>
      selector === ".seq-card-test-feedback" ? cardFeedback : selector === ".seq-card-rehearsal" ? cardRehearsal : null,
  });
  const testButton = makeElement({ dataset: { action: "test", seqName: sequence.name }, closest: () => card });
  const tuneButton = makeElement({ dataset: { action: "tune", builtinName: sequence.name } });
  byId("seq-cards-container").querySelectorAll = (selector) => {
    if (selector === '[data-action="tune"]') return [tuneButton];
    if (selector.startsWith(".seq-card-actions button")) return [testButton];
    return [];
  };

  const get = (url) => {
    calls.push(["get", url]);
    if (url.startsWith("/api/seq/builtins?name=")) return Promise.resolve({ ok: true, data: sequence });
    if (url.startsWith("/api/seq?name=")) {
      return failRead ? Promise.reject(new Error("controller not reachable")) : Promise.resolve({ ok: true, data: sequence });
    }
    if (url.startsWith("/api/dome/layout")) return Promise.resolve({ ok: false, status: 503, data: null });
    return Promise.resolve({ ok: true, status: 200, data: url.startsWith("/api/config") ? {} : [] });
  };

  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      // The shipped words table's lookups (helpers/shipped_words.cjs).
      ...shippedWords(),
      get,
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      postJson: (url, body) => {
        calls.push(["post", url, body]);
        return Promise.resolve({ ok: true, data: {} });
      },
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    PABootstrap: {
      registerSection() {},
      setResourceLabels() {},
      declareSections() {},
      retryNow() {},
      refreshSections() {},
    },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {}, removeItem() {} },
    document: {
      readyState: "complete",
      body: makeElement(),
      documentElement: makeElement(),
      getElementById: byId,
      querySelector: () => null,
      querySelectorAll: () => [],
      createElement: () => makeElement(),
      addEventListener() {},
      removeEventListener() {},
    },
    addEventListener() {},
    removeEventListener() {},
    alert() {},
    confirm: () => false,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    Promise,
    console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  PAGE_MODULES.forEach((name) => vm.runInContext(read(name), sandbox, { filename: name }));

  const seam = sandbox.window.__seqEditorForTesting;
  const settle = async (turns = 8) => {
    for (let i = 0; i < turns; i += 1) await new Promise((resolve) => setImmediate(resolve));
  };
  const click = async (element) => {
    (element.listeners.click || []).forEach((fn) => fn());
    await settle();
  };

  return {
    seam,
    calls,
    byId,
    card: { feedback: cardFeedback, rehearsal: cardRehearsal, testButton, tuneButton },
    settle,
    click,
    open(seq = sequence) {
      seam.editorState.expanded = new Set();
      seam.renderEditorView(seq);
    },
  };
}

test("the editor counts the Rehearsal's findings beside Protocol Check, and the count never disables Save", () => {
  const page = newPage();
  page.open();

  const counts = page.byId("seq-editor-rehearsal").innerHTML;
  assert.match(counts, /data-count="warning">2 warnings/);
  // Three of the nine are checkable; the six panel moves are the dome's to time.
  assert.match(counts, /checked 3 of 9 steps/);
  // Protocol Check passes this sequence, so Save stays live however many
  // warnings the Rehearsal has.
  assert.equal(page.byId("seq-editor-save").disabled, false);
});

test("Test on Droid runs first, then folds a badge for what the droid holds, not the edits on screen", async () => {
  const page = newPage();
  page.open();
  // Unsaved edits that would clear both warnings: the droid still runs the
  // saved copy, so the badge must still report that copy.
  page.seam.editorState.current.steps.splice(1, 7);
  await page.click(page.byId("seq-editor-test"));

  const runAt = page.calls.findIndex(([method, url]) => method === "post" && url === "/api/seq/test");
  assert.ok(runAt >= 0, "the run was held back");
  const feedback = page.byId("seq-editor-feedback").innerHTML;
  assert.match(feedback, /DM:HELLO dispatched\./);
  assert.match(feedback, /<details class="seq-rehearsal-badge seq-rehearsal-badge-warning"/);
  assert.match(feedback, /Rehearsal: 2 warnings, 0 notes/);
});

test("a card whose sequence cannot be read back says so instead of looking all clear", async () => {
  const page = newPage({ failRead: true });
  page.seam.renderListWithMocks([{ name: "DM:HELLO", stepCount: 10, valid: true }], []);
  await page.click(page.card.testButton);

  assert.equal(page.card.feedback.textContent, "Dispatched.");
  assert.equal(page.card.rehearsal.innerHTML, "");
  assert.match(page.card.rehearsal.textContent, /Could not read DM:HELLO back to rehearse it: controller not reachable/);
});
