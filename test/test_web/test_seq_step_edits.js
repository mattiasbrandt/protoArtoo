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

function newPage() {
  const posts = [];
  const elements = new Map();
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };

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
      card.fields.querySelectorAll = (selector) => (selector === "[data-field]" ? card.fieldInputs() : []);
      card.row = makeElement({
        dataset: { stepIndex: mark.index },
        querySelector: (selector) =>
          selector === ".step-t" ? card.timeInput : selector === ".step-fields" ? card.fields : null,
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
    if (target === ".step-fields" && !ancestor) return stepCards().map((c) => c.fields);
    if (target === "[data-field]" && ancestor === ".step-fields") return stepCards().flatMap((c) => c.fieldInputs());
    return [];
  };

  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      ...shippedWords(),
      get: (url) => Promise.resolve({ ok: true, status: 200, data: url.startsWith("/api/config") ? {} : [] }),
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
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {}, removeItem() {} },
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
    card: (index) => stepCards().find((c) => c.index === String(index)),
    open(sequence, expanded) {
      seam.editorState.expanded = new Set(expanded);
      seam.renderEditorView(sequence);
    },
    async save() {
      fire(byId("seq-editor-save"), "click");
      await settle();
    },
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

  fire(page.byId("seq-editor-retime-undo"), "click");
  await page.save();
  const saved = page.posts.filter((post) => post.url === "/api/seq");
  assert.equal(saved.length, 1, "Save sent nothing");
  assert.deepEqual(saved[0].body.steps, original, "undo did not put every step back");
});
