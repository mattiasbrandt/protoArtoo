// The Sequences list against what the droid states about itself (#378): the
// cap on Learned Sequences it reports, and nothing the page carries of its own.
// The card editor's panel picker, which the rest of this file drove, retired
// with the card editor (#441).
//
// It drives the shipped module through window.__seqEditorForTesting, over the
// modules data/seq.html declares, in that order.
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

// The script chain data/seq.html declares, minus the ones this behaviour never
// reaches (the shell, the transport, the live renderer): tier 3 has no live
// elements by definition, so data/dome_layout_render.js is never called.
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

// An element that remembers what was written into it. Reads it does not know
// about answer null/[] rather than a fresh stub, so a selector this harness has
// not taught it about cannot silently read as "found something".
function makeElement() {
  const element = {
    dataset: {},
    style: {},
    value: "",
    innerHTML: "",
    textContent: "",
    children: [],
    listeners: {},
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    setAttribute() {},
    getAttribute: () => null,
    addEventListener(name, fn) {
      (element.listeners[name] = element.listeners[name] || []).push(fn);
    },
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

// Build the page data/seq.js runs on, load the real chain into one context, and
// hand back the handles a test drives it through.
//
// `config` is what GET /api/config answers - the Droid Build seam's only input.
// The dome itself answers 503, which is the state tier 3 exists for.
function newPage(config, domeResponse) {
  const requests = [];
  // Window events, so a page that listens for the shell's identity outcome
  // hears it the way it would in a browser.
  const windowListeners = {};
  const elements = new Map();
  const elementById = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };

  const respond = (url) => {
    requests.push(url);
    if (url.startsWith("/api/config")) {
      return Promise.resolve({ ok: true, status: 200, data: config });
    }
    if (url.startsWith("/api/dome/layout")) {
      return Promise.resolve(domeResponse || { ok: false, status: 503, data: null });
    }
    return Promise.resolve({ ok: true, status: 200, data: [] });
  };

  // `window` IS the global in a browser: these modules reach PAApi and PAUtils
  // both bare and through window., so the sandbox is its own window.
  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      // The shipped words table's lookups (helpers/shipped_words.cjs).
      ...shippedWords(),
      get: respond,
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      postJson: () => Promise.resolve({ ok: true, data: {} }),
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: {
      escapeHtml,
      escapeAttr: escapeHtml,
      showFeedback() {},
      debounce: (fn) => fn,
    },
    PABootstrap: {
      registerSection() {},
      setResourceLabels() {},
      declareSections() {},
      retryNow() {},
      refreshSections() {},
    },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    // The surface's poll handle, as data/page_bootstrap.js hands it out. The
    // run watch (data/seq.js) takes one as the page loads; nothing here runs it.
    PASurface: { poll: () => ({ start() {}, stop() {} }) },
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
    addEventListener(name, fn) {
      (windowListeners[name] = windowListeners[name] || []).push(fn);
    },
    removeEventListener() {},
    dispatchEvent(event) {
      (windowListeners[event.type] || []).forEach((fn) => fn(event));
      return true;
    },
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
  PAGE_MODULES.forEach((name) => {
    vm.runInContext(read(name), sandbox, { filename: name });
  });

  const seam = sandbox.window.__seqEditorForTesting;

  return {
    window: sandbox.window,
    seam,
    elementById,
    requests,
    // What the shell does once GET /api/identity answers: publish it, then
    // tell every surface (data/shell.js publishIdentity()).
    reportIdentity(identity) {
      sandbox.window.PAIdentity = identity;
      sandbox.window.dispatchEvent({ type: "pa:identity-available", detail: identity });
    },
  };
}

// How many Learned Sequences a droid stores is a board fact the droid reports:
// five on the artoo-esp32, ten elsewhere (ADR 0065, amended 2026-09-25). The
// page used to carry its own ten, which an artoo-esp32 would have contradicted
// at the sixth save. It must say whatever the droid says, and follow it when
// the droid says something else - never a number of its own.
const held = (count) =>
  Array.from({ length: count }, (_, i) => ({
    name: `DM:HELD${i}`,
    suppressMs: 8000,
    toggleGroup: "none",
    valid: true,
  }));

test("the Sequences list names the cap the droid reports, and follows it", () => {
  const page = newPage({});
  const capacity = () => page.elementById("seq-capacity-display").textContent;
  const list = () => page.elementById("seq-cards-container").innerHTML;

  // A droid reporting five, holding seven after a firmware-only update: all
  // seven are still listed, and the page does not claim room it does not have.
  page.reportIdentity({ board: "artoo_esp32", learned_sequence_cap: 5 });
  page.seam.renderListWithMocks(held(7), []);
  assert.match(capacity(), /\/ 5 /);
  for (let i = 0; i < 7; i += 1) assert.match(list(), new RegExp(`DM:HELD${i}<`));
  assert.match(list(), /seq-over-cap/, "an over-cap droid did not say new saves wait");

  // The same page, told ten: the droid's word replaces the old one on the
  // screen without a reload, and seven of ten is not over anything.
  page.reportIdentity({ board: "firebeetle2", learned_sequence_cap: 10 });
  assert.match(capacity(), /\/ 10 /);
  assert.doesNotMatch(list(), /seq-over-cap/);
});

test("a droid that has not reported its cap is promised none", () => {
  const page = newPage({});
  page.seam.renderListWithMocks(held(3), []);
  assert.doesNotMatch(page.elementById("seq-capacity-display").textContent, /\//);
});
