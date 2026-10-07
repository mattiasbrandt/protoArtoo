// An edit in the sequence editor reaches the sequence Save sends, and an edit
// the droid has not received is never let go without asking.
//
// The step cards this file once drove went with the card editor (9b71d736,
// #441); the timeline is the one editor, and its own suite is
// test_seq_timeline.js. What stays here is what Save, Duplicate, Retime to the
// grid and leaving do with the routine the editor holds, so an edit is made
// the way the timeline makes one: on the sequence the editor holds.
//
// Drives the shipped chain data/seq.html declares, in one vm context, through
// the seam the Playwright suites use (window.__seqEditorForTesting). Per
// test_web/README.md: executed, not pattern-matched.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const { shippedWords } = require("./helpers/shipped_words.cjs");
const { operatorShellUi } = require("./helpers/page_module_env.js");

const root = path.resolve(__dirname, "../..");
const read = (name) => fs.readFileSync(path.join(root, "data", name), "utf8");

const PAGE_MODULES = [
  // Escape and the question every surface asks with (#456).
  "overlay.js",
  // The status stream and the run watch data/seq.js reads from the Live
  // Reading (#451). Loaded, not started: starting it is the Operator Shell's
  // call, and no frame reaches these tests.
  "status_stream.js",
  "live_reading.js",
  "droid_parts.js",
  "droid_build.js",
  "dome_command_map.js",
  "dome_panel_model.js",
  "dome_layout.js",
  "dome_lights.js",
  "seq_protocol_check.js",
  "seq_tempo.js",
  "seq_gesture.js",
  "servo_motion.js",
  "seq_rehearsal.js",
  "outputs.js",
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

const fire = (element, name) => (element.listeners[name] || []).forEach((fn) => fn({ type: name, target: element }));

// `onDroid` is what the droid answers a read of one saved sequence with: the
// sequence, or an Error to refuse the read with.
function newPage({ onDroid = null } = {}) {
  const posts = [];
  // The shell's half of an unmount hold: what the surface registered, and what
  // it answered with. And every write to browser storage, which an unsaved
  // edit must never make.
  const surface = { decide: null, released: 0, stayed: 0 };
  const stored = [];
  const elements = new Map();
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };

  // The list's row acts, handed back the way the browser would find them in
  // the markup renderListView() just wrote: each a button with its row behind
  // it, and the row's own feedback line.
  const rowButtons = [];
  byId("seq-cards-container").querySelectorAll = (selector) => (selector === "[data-action]" ? rowButtons : []);

  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      ...shippedWords(),
      get: (url) => {
        if (onDroid && url.startsWith("/api/seq?name=")) {
          return onDroid instanceof Error
            ? Promise.reject(onDroid)
            : Promise.resolve({ ok: true, status: 200, data: JSON.parse(JSON.stringify(onDroid)) });
        }
        return Promise.resolve({ ok: true, status: 200, data: url.startsWith("/api/config") ? {} : [] });
      },
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      postJson: (url, body) => {
        posts.push({ url, body: JSON.parse(JSON.stringify(body)) });
        return Promise.resolve({ ok: true, data: {} });
      },
      messageFor: (error) => String(error && error.message),
    },
    // The act the pages draw their buttons with, from the shipped shell (#460).
    PAUi: operatorShellUi(),
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    PABootstrap: { registerSection() {}, setResourceLabels() {}, retryNow() {}, refreshSections() {} },
    PASurface: {
      holdUnmount: (decide) => {
        surface.decide = decide;
      },
      releaseUnmount: () => {
        surface.released += 1;
      },
      stayOnSurface: () => {
        surface.stayed += 1;
      },
    },
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem: (key) => stored.push(key), removeItem() {} },
    sessionStorage: { length: 0, key: () => null, getItem: () => null, setItem: (key) => stored.push(key), removeItem() {} },
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
    crypto: require("node:crypto").webcrypto,  // the browser's own, which the editor mints ids with
    AbortController, // the browser's own, which the editor cancels a read it leaves with
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
  return {
    posts,
    byId,
    surface,
    stored,
    // The sequence the editor holds, as Save would send it; null with no edit open.
    editing: () => seam.editorState.current,
    open(sequence) {
      seam.renderEditorView(sequence);
    },
    async save() {
      fire(byId("seq-editor-save"), "click");
      await settle();
    },
    // Press one of a list row's acts; hands back the row's feedback line.
    async rowAct(action, seqName) {
      const feedback = makeElement();
      const row = makeElement({ querySelector: (selector) => (selector === ".seq-item-feedback" ? feedback : null) });
      const button = makeElement({ dataset: { action, seqName }, closest: () => row });
      rowButtons.length = 0;
      rowButtons.push(button);
      seam.renderListWith([{ name: seqName }]);
      fire(button, "click");
      await settle();
      return feedback;
    },
    settle,
  };
}

// Retime to the grid is the most destructive edit on this surface (ADR 0060):
// it moves every step at once. So it says how many steps actually landed on a
// beat - a step inside a loop body cannot, and is not counted as if it had,
// which is the silence the reference's own test rewarded - and one press puts
// every step back where it was.
test("retiming to the grid counts only the steps that landed, and one undo puts them all back", async () => {
  const page = newPage();
  const original = [
    { t: 90, type: "audio", cmd: "$H" },
    { t: 600, type: "loop", body: 1, periodMs: 500, durationMs: 1000 },
    { t: 40, type: "audio", cmd: "$S" },
    { t: 2600, type: "end" },
  ];
  page.open({
    name: "DM:RETIME",
    suppressMs: 8000,
    toggleGroup: "none",
    tempo: { bpm: 120, phase: 0, barLen: 4, barPhase: 0, source: "typed", confidence: 1 },
    steps: JSON.parse(JSON.stringify(original)),
  });

  fire(page.byId("seq-editor-retime"), "click");
  assert.equal(page.byId("seq-editor-retime-receipt").textContent, "3 of 4 steps landed on a beat.");

  fire(page.byId("seq-editor-undo"), "click");
  await page.save();
  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  assert.deepEqual(saved[0].body.steps, original, "undo did not put every step back");
});

// A sequence inside another names it by a stable id (ADR 0046), so a save
// always carries one, and never replaces one it already has: a new id would
// orphan every sequence that holds this one.
test("a saved sequence always carries a stable id, and keeps the one it has", async () => {
  const minted = newPage();
  minted.open({ name: "DM:FRESH", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "end" }] });
  await minted.save();
  const first = minted.posts.filter((post) => post.url === "/api/seq");
  assert.equal(first.length, 1, "Save sent nothing");
  assert.match(first[0].body.id || "", /^[0-9a-f]{8}$/);

  const kept = newPage();
  kept.open({ name: "DM:HELD", id: "abcd1234", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "end" }] });
  await kept.save();
  const second = kept.posts.filter((post) => post.url === "/api/seq");
  assert.equal(second[0].body.id, "abcd1234", "the save replaced the sequence's id");
});

// Leaving the editor used to swap its state for a new object, so the next
// sequence opened from the list threw before it drew a single step (0022cf99,
// #441) - and the seam above went on holding the object that had been thrown
// away. The way out is All sequences, on the strip.
test("a sequence opens after another was closed with All sequences", () => {
  const page = newPage();
  const sequence = {
    name: "DM:AGAIN",
    suppressMs: 8000,
    toggleGroup: "none",
    steps: [
      { t: 0, type: "audio", cmd: "$H" },
      { t: 1000, type: "end" },
    ],
  };
  page.open(sequence);
  fire(page.byId("seq-editor-cancel"), "click");
  assert.equal(page.editing(), null, "the fixture: All sequences closed the clean edit");

  page.open(sequence);
  assert.equal(JSON.stringify(page.editing()?.steps), JSON.stringify(sequence.steps), "the sequence opened after another was closed is not the one the editor holds");
  assert.match(page.byId("seq-editor-view").innerHTML, /id="seq-editor-timeline"/, "the sequence opened after another was closed drew no stage");
});

// An unsaved edit is not kept, and it is never dropped without the builder
// being asked (operator decision 2026-09-30, #441). Leaving by All sequences
// and leaving the Sequences surface both ask first; the edit is still there until
// the answer is Discard; and nothing about it is written to browser storage.
test("an unsaved edit is dropped only on Discard, whichever way the builder was leaving", () => {
  const page = newPage();
  const sequence = {
    name: "DM:UNSAVED",
    suppressMs: 8000,
    toggleGroup: "none",
    steps: [
      { t: 0, type: "audio", cmd: "$H" },
      { t: 1000, type: "end" },
    ],
  };
  // The edit a drag of the first block makes, on the routine the editor holds.
  const edit = () => {
    page.open(sequence);
    page.editing().steps[0].t = 250;
  };
  assert.equal(typeof page.surface.decide, "function", "Sequences registered no unmount hold with the shell");

  // With nothing unsaved, both ways out are free.
  page.open(sequence);
  assert.equal(page.surface.decide(), false, "a clean edit held the surface");
  fire(page.byId("seq-editor-cancel"), "click");
  assert.equal(page.editing(), null, "All sequences on a clean edit did not close it");

  // All sequences, then keep editing: the edit is still there.
  edit();
  fire(page.byId("seq-editor-cancel"), "click");
  assert.equal(page.editing()?.steps[0].t, 250, "All sequences dropped an unsaved edit without asking");
  fire(page.byId("seq-modal-discard-keep"), "click");
  assert.equal(page.editing()?.steps[0].t, 250, "keeping the edit lost it");

  // Leaving the surface: held while it asks, and keeping puts the address back.
  assert.equal(page.surface.decide(), true, "the surface let go of an unsaved edit");
  assert.equal(page.editing()?.steps[0].t, 250);
  fire(page.byId("seq-modal-discard-keep"), "click");
  assert.deepEqual([page.surface.stayed, page.surface.released], [1, 0]);

  // Leaving again, and Discard: the edit goes and the navigation goes through.
  assert.equal(page.surface.decide(), true);
  fire(page.byId("seq-modal-discard-confirm"), "click");
  assert.equal(page.editing(), null, "Discard left the edit open");
  assert.deepEqual([page.surface.stayed, page.surface.released], [1, 1]);
  assert.equal(page.surface.decide(), false, "nothing is left to hold the surface for");

  assert.deepEqual(page.stored, [], "an unsaved edit was written to browser storage");
});

// What Save marks as saved is what it sent. An edit made while the droid is
// still answering never reached it, so it is still unsaved - and leaving is
// still asked about - when the answer lands (#441).
test("an edit made while a save is on its way is still unsaved when it lands", async () => {
  const page = newPage();
  page.open(
    { name: "DM:INFLIGHT", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "audio", cmd: "$H" }, { t: 1000, type: "end" }] });
  // The edit a drag of the first block makes, on the routine the editor holds.
  page.editing().steps[0].t = 250;

  // Save is pressed, and before the droid answers the block is moved again.
  fire(page.byId("seq-editor-save"), "click");
  page.editing().steps[0].t = 400;
  await page.settle();

  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  assert.equal(saved[0].body.steps[0].t, 250, "the fixture: the save went out before the second edit");
  assert.equal(page.surface.decide(), true, "an edit the droid never received was let go without asking");
});

// A sequence inside another is found by its id, and the droid takes the first
// sequence that carries it (src/seq_store.cpp). A duplicate is a new sequence:
// saved with the original's id, a routine that nests the original could play
// the copy instead.
test("a duplicate is saved under an id of its own, never the original's", async () => {
  const page = newPage({
    onDroid: { name: "DM:ORIGINAL", id: "abcd1234", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "end" }] },
  });
  await page.rowAct("duplicate", "DM:ORIGINAL");
  assert.equal(page.editing()?.name, "DM:ORIGINAL_COPY", "the fixture: Duplicate opened the copy to edit");

  await page.save();
  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  assert.match(saved[0].body.id || "", /^[0-9a-f]{8}$/, "the duplicate was saved with no id");
  assert.notEqual(saved[0].body.id, "abcd1234", "the duplicate was saved under the original's id");
});

// A name holds 18 characters after its DM:, and a duplicate adds five. The
// copy of a long-named sequence used to open under a name Protocol Check
// refuses, so it could not be saved until it was renamed.
test("a duplicate of the longest name a sequence can have can be saved as it opens", async () => {
  const longest = "DM:ABCDEFGHIJKLMNOPQR";
  const page = newPage({
    onDroid: { name: longest, id: "abcd1234", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "end" }] },
  });
  await page.rowAct("duplicate", longest);
  assert.notEqual(page.editing()?.name, longest, "the fixture: the copy has a name of its own");

  await page.save();
  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "the duplicate opened under a name the droid would refuse");
  assert.equal(saved[0].body.name, "DM:ABCDEFGHIJKLM_COPY");
});

// A row act that cannot read its sequence says so on the row. Duplicate used
// to say it only to the console, so the press did nothing a builder could see.
test("a Duplicate that cannot read the sequence says so on its row", async () => {
  const page = newPage({ onDroid: new Error("controller not reachable") });
  const feedback = await page.rowAct("duplicate", "DM:ORIGINAL");
  assert.equal(page.editing(), null, "a duplicate that was never read opened the editor");
  assert.match(feedback.textContent, /Could not read DM:ORIGINAL: controller not reachable/);
});
