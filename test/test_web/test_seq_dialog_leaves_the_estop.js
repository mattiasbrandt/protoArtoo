// =============================================================================
// test/test_web/test_seq_dialog_leaves_the_estop.js
//
// A Sequences dialog covers the surface it belongs to and never the chrome
// around it (#359, ADR 0048).
//
// The Latching Estop rides the Operator Shell's chrome, and the whole reason
// it is there is that an operator must be able to stop the droid from wherever
// they are. A dialog that declares everything outside itself inert takes that
// away from a screen-reader user just as surely as a full-viewport scrim takes
// it away from a mouse, so these dialogs carry no aria-modal and what they
// mark inert is the surface.
//
// The shipped chain data/seq.html declares is run for real, over the real tree
// the shell mounts it into - the four shell regions, a .surface node inside
// #shell-content, seq.html's own body imported into it - and the dialog is
// opened and closed through the buttons a builder presses. Nothing here reads
// source text except the markup assertion, which is about markup.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";
import { shippedWords } from "./helpers/shipped_words.cjs";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

// The chain data/seq.html declares, minus the shell and the transport: this
// harness stands in for those, because the point here is what the surface's
// own scripts do to the tree the shell gave them.
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

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// The shell's four regions with the Sequences surface mounted in the work
// area, exactly as data/shell.js attaches one: a .surface node inside
// #shell-content holding the delegate document's body.
const mountSequences = async () => {
  const document = new MiniDocument();
  const region = (id, tag = "div") => {
    const node = document.createElement(tag);
    node.id = id;
    document.body.appendChild(node);
    return node;
  };
  const top = region("shell-top");
  const nav = region("shell-nav", "nav");
  const content = region("shell-content");
  const status = region("shell-status");

  // The estop the chrome carries, named the way shell.js names it, so a test
  // that says "STOP stays reachable" is talking about the real control's node
  // rather than about the region around it.
  const estop = document.createElement("button");
  estop.id = "shell-estop-button";
  top.appendChild(estop);

  const surface = document.createElement("div");
  surface.className = "surface";
  surface.dataset.surface = "seq";
  content.appendChild(surface);
  const parsed = new MiniDOMParser().parseFromString(readData("seq.html"));
  parsed.body.children.forEach((child) => surface.appendChild(document.importNode(child, true)));

  const windowMock = {
    PAApi: {
      // The shipped words table's lookups (helpers/shipped_words.cjs).
      ...shippedWords(),
      get: async () => ({ data: [] }),
      postJson: async () => ({ data: {} }),
      postForm: async () => ({ data: {} }),
      messageFor: (error) => String(error),
    },
    PAUtils: {
      showFeedback: () => {},
      escapeHtml: (value) => String(value ?? ""),
      escapeAttr: (value) => String(value ?? ""),
      debounce: (fn) => fn,
    },
    PABootstrap: { registerSection: () => {}, setResourceLabels: () => {}, refreshSections: () => {} },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    PASurface: { poll: () => ({ start() {}, stop() {} }), showing() {}, isStale: () => false },
    addEventListener: () => {},
    removeEventListener: () => {},
    setTimeout: (fn, ms) => { const t = setTimeout(fn, ms); t.unref?.(); return t; },
    clearTimeout,
    setInterval: (fn, ms) => { const t = setInterval(fn, ms); t.unref?.(); return t; },
    clearInterval,
    location: { origin: "http://device", hash: "#seq" },
    localStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
    matchMedia: () => ({ matches: false, addEventListener() {}, removeEventListener() {} }),
    requestAnimationFrame: (fn) => setTimeout(fn, 0),
  };

  const context = {
    window: windowMock,
    document,
    console: { log: () => {}, warn: () => {}, error: () => {}, info: () => {} },
    setTimeout: windowMock.setTimeout,
    clearTimeout,
    setInterval: windowMock.setInterval,
    clearInterval,
    AbortController,
    URLSearchParams,
    JSON, Math, Date, Number, String, Boolean, Object, Array, Set, Map, Promise, Error, RegExp,
    isNaN, parseInt, parseFloat,
    Event: class {},
    CustomEvent: class {},
    DOMParser: class {
      parseFromString(html) {
        return new MiniDOMParser().parseFromString(html);
      }
    },
    navigator: { userAgent: "node" },
    Blob: class {},
    URL: { createObjectURL: () => "blob:", revokeObjectURL() {} },
    FileReader: class {},
  };
  context.globalThis = context;
  vm.createContext(context);
  PAGE_MODULES.forEach((name) => vm.runInContext(readData(name), context, { filename: name }));
  await sleep(60);

  return {
    document,
    chrome: { top, nav, status, estop },
    surface,
    card: document.getElementById("seq-main-card"),
    importDialog: document.getElementById("seq-modal-import"),
    wipeDialog: document.getElementById("seq-modal-memory-wipe"),
    openImport: () => document.getElementById("seq-btn-import").fire("click", { type: "click" }),
    closeImport: () => document.getElementById("seq-modal-import-cancel").fire("click", { type: "click" }),
  };
};

// -----------------------------------------------------------------------------
// The markup half
// -----------------------------------------------------------------------------

test("neither Sequences dialog tells a screen reader the rest of the page is gone", (t) => {
  const html = readData("seq.html");

  assert.doesNotMatch(
    html,
    /aria-modal/,
    'aria-modal="true" means "everything outside this dialog is inert", and outside it is the'
      + " Latching Estop the Operator Shell keeps live for exactly this moment (ADR 0048)",
  );
  // The role is not what was wrong and must not go with it.
  assert.match(html, /id="seq-modal-import"[^>]*role="dialog"/);
  assert.match(html, /id="seq-modal-memory-wipe"[^>]*role="dialog"/);
});

// -----------------------------------------------------------------------------
// The behaviour half
// -----------------------------------------------------------------------------

test("opening a dialog takes the surface out of the tab order and leaves the chrome in it", async () => {
  const env = await mountSequences();

  assert.ok(!env.card.inert, "the fixture must start with a usable surface");

  env.openImport();

  assert.equal(env.importDialog.classList.contains("hidden"), false, "the dialog is open");
  assert.equal(env.card.inert, true, "the surface behind the dialog leaves the tab order");
  for (const [name, node] of [
    ["the topbar", env.chrome.top],
    ["the nav rail", env.chrome.nav],
    ["the Status Plate", env.chrome.status],
    ["the STOP button", env.chrome.estop],
  ]) {
    assert.ok(!node.inert, `${name} must stay reachable while a sequence dialog is open`);
  }
});

test("the dialog itself stays reachable, or the operator cannot answer it", async () => {
  const env = await mountSequences();
  env.openImport();

  assert.ok(!env.importDialog.inert, "a dialog that marked itself inert could not be answered");
  assert.ok(
    !env.wipeDialog.inert,
    "the other dialog is a sibling of this one and is not the surface behind it",
  );
});

test("closing the dialog gives the surface back", async () => {
  const env = await mountSequences();
  env.openImport();
  assert.equal(env.card.inert, true, "the fixture must start with the dialog open");

  env.closeImport();

  assert.equal(env.importDialog.classList.contains("hidden"), true, "the dialog is closed");
  assert.equal(env.card.inert, false, "and the surface is usable again");
});
