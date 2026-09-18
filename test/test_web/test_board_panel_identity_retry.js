// =============================================================================
// test/test_web/test_board_panel_identity_retry.js
//
// The Setup board picture, which comes from the build's asset set (#382,
// ADR 0065), and its recovery after an identity retry (#202).
//
// The order is the one every product card follows: the line drawing when the
// page inlined one (the legacy set, built for artoo_esp32), else the photograph
// at /<registry id>.webp (the default set, built for firebeetle2), else the
// placeholder. The board panel and the sprite are the shipped files: setup.html
// is parsed with its _product_art.html include expanded from each set, so a
// renamed panel id or a lost symbol turns this suite red.
//
// The photograph keeps the deferred-asset gate from #202: identity can resolve
// after the one-shot data-deferred-src sweep has already run, so the src is set
// directly once PAAssetsReady is true, and deferred before. A drawing fetches
// nothing and never touches the gate.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { MiniDOMParser } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const setupSrc = readFileSync(join(dataDir, "setup.js"), "utf8");

const INCLUDE_RE = /<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+)\s*-->/g;

// setup.html as one asset set's build serves it, as far as the board picture
// is concerned: that set's sprite inlined. The recovery kernel is not what this
// suite is about and stays an unexpanded comment.
const setupDocument = (set) => {
  const page = readFileSync(join(dataDir, "setup.html"), "utf8").replace(INCLUDE_RE, (directive, target) =>
    target === "_product_art.html" ? readFileSync(join(dataDir, "asset-sets", set, target), "utf8") : directive
  );
  return new MiniDOMParser().parseFromString(page);
};

// The board panel is read from the real markup. Everything else setup.js wires
// up at load is not what these tests are about and gets a permissive stub.
const PANEL_IDS = new Set(["board-art", "board-art-use", "board-image", "board-image-placeholder", "board-placeholder-text"]);

const makeElement = () => ({
  id: "",
  dataset: {},
  style: {},
  className: "",
  classList: { add() {}, remove() {}, contains: () => false },
  textContent: "",
  innerHTML: "",
  value: "",
  checked: false,
  disabled: false,
  hidden: false,
  type: "checkbox",
  addEventListener() {},
  setAttribute() {},
  removeAttribute() {},
  querySelectorAll: () => [],
  querySelector: () => null,
  closest: () => null,
  appendChild() {},
  click() {},
});

const bootSetup = ({ set, assetsReady }) => {
  const parsed = setupDocument(set);
  const windowListeners = new Map();

  const documentMock = {
    getElementById: (id) => (PANEL_IDS.has(id) || id.startsWith("art-") ? parsed.getElementById(id) : makeElement()),
    querySelector: () => makeElement(),
    querySelectorAll: () => [],
    createElement: () => makeElement(),
    createTextNode: () => makeElement(),
    addEventListener() {},
    removeEventListener() {},
    body: makeElement(),
  };

  const windowMock = {
    document: documentMock,
    PAAssetsReady: assetsReady,
    PAIdentity: null,
    PABootstrap: { registerSection: () => {}, setResourceLabels() {} },
    PageBootstrap: { createBackgroundPoll: () => ({ start() {}, stop() {} }) },
    // data/page_bootstrap.js publishes window.PASurface in the browser; this
    // context hand-rolls its globals, so it has to carry it too (#360).
    PASurface: { poll: () => ({ start() {}, stop() {}, cancelRetry() {} }) },
    addEventListener(type, handler) {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(handler);
    },
    removeEventListener() {},
    setTimeout: () => 1,
    clearTimeout() {},
    setInterval: () => 1,
    clearInterval() {},
    location: { origin: "http://device", href: "http://device/setup.html" },
    localStorage: { getItem: () => null, setItem() {} },
    requestAnimationFrame: () => 1,
    confirm: () => true,
    CustomEvent: class { constructor(type, init = {}) { this.type = type; this.detail = init.detail; } },
    Event: class {},
  };

  const context = {
    window: windowMock,
    document: documentMock,
    console: { log() {}, warn() {}, error() {}, info() {} },
    setTimeout: windowMock.setTimeout,
    clearTimeout: windowMock.clearTimeout,
    setInterval: windowMock.setInterval,
    clearInterval: windowMock.clearInterval,
    fetch: async () => ({ json: async () => ({}) }),
    confirm: () => true,
    CustomEvent: windowMock.CustomEvent,
    Event: windowMock.Event,
    URLSearchParams,
    AbortController,
    JSON,
    Math,
    Date,
    Number,
    String,
    Boolean,
    Array,
    Object,
    Set,
    Map,
    Promise,
    Error,
    RegExp,
  };
  context.globalThis = context;
  for (const key of ["PABootstrap", "PageBootstrap"]) context[key] = windowMock[key];

  vm.runInNewContext(setupSrc, context, { filename: "setup.js" });

  const panel = {
    art: parsed.getElementById("board-art"),
    use: parsed.getElementById("board-art-use"),
    image: parsed.getElementById("board-image"),
    placeholder: parsed.getElementById("board-image-placeholder"),
    placeholderText: parsed.getElementById("board-placeholder-text"),
  };
  for (const [name, element] of Object.entries(panel)) {
    assert.ok(element, `setup.html must carry the board panel's ${name} element`);
  }

  const announceBoard = (board) => {
    const handlers = windowListeners.get("pa:identity-available") || [];
    assert.ok(handlers.length > 0, "setup.js should register a pa:identity-available handler");
    for (const handler of handlers) {
      handler({
        detail: {
          droidName: "artoo",
          board,
          board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
        },
      });
    }
  };

  // Which of the three the panel shows, by the same class the stylesheet hides.
  const showing = () => ["art", "image", "placeholder"].filter((name) => !panel[name].classList.contains("hidden"));

  return { panel, announceBoard, showing };
};

test("default set: before assets-ready, the photograph waits in data-deferred-src", () => {
  const { panel, announceBoard, showing } = bootSetup({ set: "default", assetsReady: false });

  announceBoard("firebeetle2");

  assert.strictEqual(panel.image.dataset.deferredSrc, "/firebeetle2.webp", "Before PAAssetsReady, should set data-deferred-src");
  assert.strictEqual(panel.image.src, undefined, "Before PAAssetsReady, should NOT set .src");
  assert.ok(!showing().includes("art"), "the default set carries no drawing to show");
});

test("default set: after assets-ready (a late identity retry), the photograph's src is set directly", () => {
  const { panel, announceBoard, showing } = bootSetup({ set: "default", assetsReady: true });

  announceBoard("artoo_esp32");

  assert.strictEqual(panel.image.src, "/artoo_pcb.webp", "After PAAssetsReady, should set .src to the registry id's photograph");
  assert.strictEqual(panel.image.dataset.deferredSrc, undefined, "After PAAssetsReady, should NOT set data-deferred-src");
  assert.strictEqual(panel.image.alt, "Artoo Controller PCB");

  panel.image.onload();
  assert.deepStrictEqual(showing(), ["image"], "a loaded photograph replaces the placeholder");
});

