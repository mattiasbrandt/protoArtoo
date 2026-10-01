// An edit to a step in the sequence editor reaches the sequence Save sends.
//
// A step's time offset input (.step-t) sits beside its fields rather than
// among them, and before #434 no listener was attached to it: the operator
// changed the time, the field showed it, and Save posted the old one. The
// same class of fault - an input the editor draws but never reads back - is
// what this file guards.
//
// Drives the shipped chain data/seq.html declares, in one vm context, through
// the seam the Playwright suites use (window.__seqEditorForTesting). Each
// step's inputs are derived from the markup the editor actually wrote - the
// card's time input from renderEditorView()'s markup, its fields from what
// renderStepFields() put in the card's .step-fields container - so an input
// the editor stopped drawing is found by nothing here. Per test_web/README.md:
// executed, not pattern-matched.

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

const unescapeAttr = (value) =>
  String(value).replace(/&quot;/g, '"').replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&");

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

  // Each rendered step card, rebuilt whenever the editor's markup changes.
  // Only an expanded card carries a time input and a fields container.
  let cardsFor = null;
  let cards = [];
  const stepCards = () => {
    const html = byId("seq-editor-view").innerHTML || "";
    if (html === cardsFor) return cards;
    cardsFor = html;
    const marks = [];
    const cardRe = /data-step-index="(\d+)"/g;
    let match;
    while ((match = cardRe.exec(html)) !== null) marks.push({ index: match[1], at: match.index });
    cards = [];
    marks.forEach((mark, i) => {
      const end = i + 1 < marks.length ? marks[i + 1].at : html.length;
      const slice = html.slice(mark.at, end);
      const time = /<input class="step-t"[^>]*value="(\d*)"/.exec(slice);
      if (!time || !slice.includes('class="step-fields"')) return;
      const card = { index: mark.index };
      card.timeInput = makeElement({ value: time[1] });
      card.fields = makeElement();
      // The type chip the card rendered lit.
      const lit = /class="step-type-chip[^"]*\bactive\b[^"]*"[^>]*data-type="([a-zA-Z]+)"/.exec(slice);
      card.chip = makeElement({
        dataset: { type: lit ? lit[1] : "" },
        classList: { add() {}, remove() {}, toggle() {}, contains: (name) => name === "active" },
      });
      // The fields renderStepFields() wrote, re-derived when it writes again.
      let inputsFor = null;
      let inputs = [];
      card.fieldInputs = () => {
        if (card.fields.innerHTML === inputsFor) return inputs;
        inputsFor = card.fields.innerHTML;
        inputs = [];
        const inputRe = /<input\b[^>]*>/g;
        let tag;
        while ((tag = inputRe.exec(inputsFor)) !== null) {
          const field = /data-field="([^"]+)"/.exec(tag[0]);
          if (!field) continue;
          const value = /value="([^"]*)"/.exec(tag[0]);
          inputs.push(makeElement({
            dataset: { field: field[1] },
            value: value ? unescapeAttr(value[1]) : "",
            type: "text",
            closest: (selector) => (selector === ".step-card" ? card.row : null),
          }));
        }
        return inputs;
      };
      // The panel step's two pickers, which are selects and not form fields:
      // each as the option its markup shows selected, re-derived when the
      // fields are written again.
      let pickersFor = null;
      let pickers = {};
      card.picker = (name) => {
        if (card.fields.innerHTML !== pickersFor) {
          pickersFor = card.fields.innerHTML;
          pickers = {};
        }
        if (!(name in pickers)) {
          const select = new RegExp(`<select class="[^"]*\\b${name}\\b[^"]*"[^>]*>([\\s\\S]*?)</select>`).exec(pickersFor);
          const chosen = select ? /<option value="([^"]*)"\s+selected/.exec(select[1]) : null;
          pickers[name] = select ? makeElement({ value: chosen ? chosen[1] : "" }) : null;
        }
        return pickers[name];
      };
      const PICKERS = ["dome-action-select", "dome-target-select"];
      card.fields.querySelector = (selector) => {
        if (selector === 'input[data-field="cmd"]') return card.fieldInputs().find((input) => input.dataset.field === "cmd") || null;
        return PICKERS.includes(selector.slice(1)) ? card.picker(selector.slice(1)) : null;
      };
      card.fields.querySelectorAll = (selector) => {
        if (selector === "[data-field]") return card.fieldInputs();
        if (selector === ".dome-action-select, .dome-target-select") return PICKERS.map(card.picker).filter(Boolean);
        return [];
      };
      card.row = makeElement({
        dataset: { stepIndex: mark.index },
        querySelector: (selector) =>
          selector === ".step-t" ? card.timeInput
            : selector === ".step-fields" ? card.fields
              : selector === ".step-type-chip.active" ? card.chip : null,
        querySelectorAll: (selector) => (selector === ".step-type-chip" ? [card.chip] : []),
      });
      card.fields.closest = (selector) => (selector === ".step-card" ? card.row : null);
      card.timeInput.closest = card.fields.closest;
      cards.push(card);
    });
    return cards;
  };

  // One selector part, `ancestor target` or `target`, answered from the cards.
  const matchPart = (part) => {
    const tokens = part.trim().split(/\s+/);
    const target = tokens[tokens.length - 1];
    const ancestor = tokens.length > 1 ? tokens[0] : null;
    if (target === ".step-t" && (!ancestor || ancestor === ".step-card")) return stepCards().map((c) => c.timeInput);
    if (target === ".step-card" && !ancestor) return stepCards().map((c) => c.row);
    if (target === ".step-fields" && !ancestor) return stepCards().map((c) => c.fields);
    if (target === "[data-field]" && ancestor === ".step-fields") return stepCards().flatMap((c) => c.fieldInputs());
    return [];
  };

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
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    PABootstrap: { registerSection() {}, setResourceLabels() {}, retryNow() {}, refreshSections() {} },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    PASurface: {
      // The poll handle the run watch (data/seq.js) takes as the page loads;
      // nothing here runs it.
      poll: () => ({ start() {}, stop() {} }),
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
      querySelector: (selector) => {
        const index = /^\[data-step-index="(\d+)"\]$/.exec(selector);
        return index ? stepCards().find((c) => c.index === index[1])?.row || null : null;
      },
      querySelectorAll: (selector) => selector.split(",").flatMap(matchPart),
      createElement: () => makeElement(),
      addEventListener() {},
      removeEventListener() {},
    },
    addEventListener() {},
    removeEventListener() {},
    alert() {},
    confirm: () => false,
    crypto: require("node:crypto").webcrypto,  // the browser's own, which the editor mints ids with
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
    card: (index) => stepCards().find((c) => c.index === String(index)),
    open(sequence, expanded) {
      seam.editorState.expanded = new Set(expanded);
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

test("a step's changed time is the time Save sends", async () => {
  const page = newPage();
  page.open(
    {
      name: "DM:TIMED",
      suppressMs: 8000,
      toggleGroup: "none",
      steps: [
        { t: 0, type: "audio", cmd: "$H" },
        { t: 1000, type: "end" },
      ],
    },
    [0],
  );

  const card = page.card(0);
  assert.ok(card, "the expanded step drew no time input");
  assert.equal(card.timeInput.value, "0");

  // The operator types a new time into the step, as the browser delivers it.
  card.timeInput.value = "400";
  fire(card.timeInput, "input");
  fire(card.timeInput, "change");

  await page.save();

  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  assert.equal(saved[0].body.steps[0].t, 400, "Save sent the step's old time");
  // The rest of the step came through the same read-back untouched.
  assert.equal(saved[0].body.steps[0].type, "audio");
  assert.equal(saved[0].body.steps[0].cmd, "$H");
});

// A step placed on a beat keeps the beat through an edit (ADR 0058, #438). The
// step is rebuilt from the form, which has no beat field, so without the carry
// the first edit to any step would quietly turn a beat back into a
// millisecond - and the next tempo change would leave that step behind.
test("a step on a beat keeps its beat through an edit, and a new tempo moves it", async () => {
  const page = newPage();
  page.open(
    {
      name: "DM:ONBEAT",
      suppressMs: 8000,
      toggleGroup: "none",
      tempo: { bpm: 130, phase: 0, barLen: 4, barPhase: 0, source: "typed", confidence: 1 },
      steps: [
        { t: 0, type: "audio", cmd: "$H" },
        { t: 923, beat: 2, type: "audio", cmd: "$S" },
        { t: 3000, type: "end" },
      ],
    },
    [1],
  );

  // An edit to the beat-placed step that leaves its time alone.
  const card = page.card(1);
  assert.ok(card, "the expanded step drew no time input");
  fire(card.timeInput, "change");

  // The builder types a new tempo.
  const bpm = page.byId("seq-editor-bpm");
  bpm.value = "120";
  fire(bpm, "change");

  await page.save();

  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  const steps = saved[0].body.steps;
  assert.equal(steps[1].beat, 2, "the edit dropped the step's beat");
  assert.equal(steps[1].t, 1000, "the step did not move to where beat 2 falls at 120 BPM");
  assert.equal(steps[0].t, 0);
  assert.equal(steps[2].t, 3000, "a step placed in milliseconds moved with the tempo");
  assert.equal(saved[0].body.tempo.source, "typed");
});

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
  page.open(
    {
      name: "DM:RETIME",
      suppressMs: 8000,
      toggleGroup: "none",
      tempo: { bpm: 120, phase: 0, barLen: 4, barPhase: 0, source: "typed", confidence: 1 },
      steps: JSON.parse(JSON.stringify(original)),
    },
    [],
  );

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
  minted.open({ name: "DM:FRESH", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "end" }] }, []);
  await minted.save();
  const first = minted.posts.filter((post) => post.url === "/api/seq");
  assert.equal(first.length, 1, "Save sent nothing");
  assert.match(first[0].body.id || "", /^[0-9a-f]{8}$/);

  const kept = newPage();
  kept.open({ name: "DM:HELD", id: "abcd1234", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "end" }] }, []);
  await kept.save();
  const second = kept.posts.filter((post) => post.url === "/api/seq");
  assert.equal(second[0].body.id, "abcd1234", "the save replaced the sequence's id");
});

// Leaving the editor used to swap its state for a new object that had no set
// of expanded steps, so the next sequence opened from the list threw before it
// drew a single step (#441) - and the seam above went on holding the object
// that had been thrown away. The way out is All sequences, on the strip.
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
  page.open(sequence, []);
  fire(page.byId("seq-editor-cancel"), "click");

  page.open(sequence, [0]);
  assert.ok(page.card(0), "the sequence opened after another was closed drew no step");
});

// Every edit is one Undo and one Redo, on one history (ADR 0057, #441): a run
// of typing in a field is one edit however many keystrokes it took, a tempo
// change is one edit although it moves every step on a beat, and what comes
// back is exactly the routine Save would have sent - the tempo with the steps,
// because a step on a beat is only where its tempo puts it.
test("a run of typing and a tempo change are each one Undo, and Redo puts them back", async () => {
  const page = newPage();
  page.open(
    {
      name: "DM:HISTORY",
      suppressMs: 8000,
      toggleGroup: "none",
      tempo: { bpm: 130, phase: 0, barLen: 4, barPhase: 0, source: "typed", confidence: 1 },
      steps: [
        { t: 0, type: "audio", cmd: "$H" },
        { t: 923, beat: 2, type: "audio", cmd: "$S" },
        { t: 3000, type: "end" },
      ],
    },
    [0],
  );
  const undo = page.byId("seq-editor-undo");
  const redo = page.byId("seq-editor-redo");
  const times = () => Array.from(page.editing().steps, (step) => step.t);
  assert.equal(undo.disabled, true, "a sequence just opened has nothing to undo");

  // 4, 40, 400: three keystrokes, then the field is left.
  const time = page.card(0).timeInput;
  for (const typed of ["4", "40", "400"]) {
    time.value = typed;
    fire(time, "input");
  }
  fire(time, "change");
  assert.deepEqual(times(), [400, 923, 3000]);

  const bpm = page.byId("seq-editor-bpm");
  bpm.value = "120";
  fire(bpm, "change");
  assert.deepEqual(times(), [400, 1000, 3000], "the fixture: the step on beat 2 moved with the tempo");

  fire(undo, "click");
  assert.deepEqual(times(), [400, 923, 3000], "Undo did not take the step on a beat back with the tempo");
  assert.equal(page.editing().tempo.bpm, 130, "Undo left the new tempo in place");

  fire(undo, "click");
  assert.deepEqual(times(), [0, 923, 3000], "one Undo did not take back the whole typed time");
  assert.equal(undo.disabled, true, "three keystrokes left more than one entry behind");

  fire(redo, "click");
  fire(redo, "click");
  assert.equal(redo.disabled, true);
  await page.save();
  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  assert.deepEqual(saved[0].body.steps.map((step) => step.t), [400, 1000, 3000], "Redo did not put both edits back");
  assert.equal(saved[0].body.tempo.bpm, 120);
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
  const edit = () => {
    page.open(sequence, [0]);
    const time = page.card(0).timeInput;
    time.value = "250";
    fire(time, "change");
  };
  assert.equal(typeof page.surface.decide, "function", "Sequences registered no unmount hold with the shell");

  // With nothing unsaved, both ways out are free.
  page.open(sequence, []);
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
    { name: "DM:INFLIGHT", suppressMs: 8000, toggleGroup: "none", steps: [{ t: 0, type: "audio", cmd: "$H" }, { t: 1000, type: "end" }] },
    [0],
  );
  const time = page.card(0).timeInput;
  time.value = "250";
  fire(time, "change");

  // Save is pressed, and before the droid answers the time is changed again.
  fire(page.byId("seq-editor-save"), "click");
  time.value = "400";
  fire(time, "change");
  await page.settle();

  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  assert.equal(saved[0].body.steps[0].t, 250, "the fixture: the save went out before the second edit");
  assert.equal(page.surface.decide(), true, "an edit the droid never received was let go without asking");
});

// One picker change is one edit, and it is its own step's (#441). A panel
// picker writes its step before it reads the form back; when that write came
// before the history looked at the step edited just before, the second
// step's change was filed under the first one's entry, and one Undo took
// back both.
test("a panel picked on one step and then on another is two edits, and Undo takes back only the last", () => {
  const page = newPage();
  page.open(
    {
      name: "DM:PICKED",
      suppressMs: 8000,
      toggleGroup: "none",
      steps: [
        { t: 0, type: "dome", cmd: ":OP01" },
        { t: 500, type: "dome", cmd: ":OP02" },
        { t: 1000, type: "end" },
      ],
    },
    [0, 1],
  );
  const commands = () => Array.from(page.editing().steps, (step) => step.cmd || step.type);
  const pick = (index, target) => {
    const select = page.card(index).picker("dome-target-select");
    assert.ok(select, `step ${index + 1} drew no panel list`);
    select.value = target;
    fire(select, "change");
  };

  pick(0, "03");
  pick(1, "04");
  assert.deepEqual(commands(), [":OP03", ":OP04", "end"], "the fixture: both pickers wrote their step");

  fire(page.byId("seq-editor-undo"), "click");
  assert.deepEqual(commands(), [":OP03", ":OP02", "end"], "one Undo took back more than the last pick");
  fire(page.byId("seq-editor-undo"), "click");
  assert.deepEqual(commands(), [":OP01", ":OP02", "end"]);
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

// A row act that cannot read its sequence says so on the row. Duplicate used
// to say it only to the console, so the press did nothing a builder could see.
test("a Duplicate that cannot read the sequence says so on its row", async () => {
  const page = newPage({ onDroid: new Error("controller not reachable") });
  const feedback = await page.rowAct("duplicate", "DM:ORIGINAL");
  assert.equal(page.editing(), null, "a duplicate that was never read opened the editor");
  assert.match(feedback.textContent, /Could not read DM:ORIGINAL: controller not reachable/);
});
