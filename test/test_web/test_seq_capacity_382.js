// The Learned Sequence cap the editor shows is the one the controller enforces
// (#382). The operator lowered it from 16 to 10 on 2026-09-13 so saved
// sequences stop competing with the web UI for the artoo-esp32's filesystem
// blocks. The number lives twice - SEQ_STORE_MAX in include/seq_store_index.h
// refuses the save, data/seq.js tells the builder - and before this test the
// browser copy was a bare literal in two places that nothing held to the
// firmware's.
//
// Per test_web/README.md the display is executed, not pattern-matched: the
// shipped seq.js renders its list through window.__seqEditorForTesting and the
// test reads what it wrote. Only the firmware constant is read as text, because
// it is C.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");
const readData = (name) => fs.readFileSync(path.join(root, "data", name), "utf8");

// The firmware's cap, from the header that declares it.
const firmwareCap = (() => {
  const header = fs.readFileSync(path.join(root, "include", "seq_store_index.h"), "utf8");
  const match = /static const uint8_t SEQ_STORE_MAX = (\d+);/.exec(header);
  assert.ok(match, "include/seq_store_index.h no longer declares SEQ_STORE_MAX as this test reads it");
  return Number(match[1]);
})();

// The chain data/seq.html declares, minus the shell, transport and live
// renderer, which the list view never reaches.
const PAGE_MODULES = [
  "droid_parts.js",
  "droid_build.js",
  "dome_command_map.js",
  "dome_panel_model.js",
  "dome_layout.js",
  "seq_protocol_check.js",
  "seq.js",
];

const escapeHtml = (value) =>
  String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

function makeElement() {
  const element = {
    dataset: {},
    style: {},
    value: "",
    innerHTML: "",
    textContent: "",
    children: [],
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    setAttribute() {},
    getAttribute: () => null,
    addEventListener() {},
    removeEventListener() {},
    appendChild(child) {
      element.children.push(child);
      return child;
    },
    insertAdjacentHTML() {},
    remove() {},
    focus() {},
    closest: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
  };
  return element;
}

// Load the real chain into one context and hand back the element table and the
// module's test seam.
function newPage() {
  const elements = new Map();
  const elementById = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };
  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      get: () => Promise.resolve({ ok: true, status: 200, data: [] }),
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      postJson: () => Promise.resolve({ ok: true, data: {} }),
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
      getElementById: elementById,
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
  PAGE_MODULES.forEach((name) => vm.runInContext(readData(name), sandbox, { filename: name }));
  return { seam: sandbox.window.__seqEditorForTesting, elementById };
}

const learned = (count) =>
  Array.from({ length: count }, (_, i) => ({
    name: `DM:SEQ${i}`,
    suppressMs: 8000,
    toggleGroup: "none",
    steps: [{ t: 0, type: "end" }],
  }));

test("the firmware stores ten Learned Sequences", () => {
  assert.equal(firmwareCap, 10);
});

test("the list shows the cap the firmware enforces", () => {
  const { seam, elementById } = newPage();
  seam.renderListWithMocks(learned(3), []);
  assert.equal(elementById("seq-capacity-display").textContent, `3 / ${firmwareCap} saved`);
});

test("a full store reads as full, not as room for more", () => {
  const { seam, elementById } = newPage();
  seam.renderListWithMocks(learned(firmwareCap), []);
  assert.equal(
    elementById("seq-capacity-display").textContent,
    `${firmwareCap} / ${firmwareCap} saved`,
  );
});
