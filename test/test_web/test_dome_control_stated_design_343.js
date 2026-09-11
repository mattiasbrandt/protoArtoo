// The dashboard's dome card, when the dome is offline and the builder has said
// which dome they built (#343, ADR 0047).
//
// This is the surface tier 3 reaches. Before this ticket it answered an
// unreachable dome with the built-in MK4 picture and the sentence "showing MK4
// built-in layout" - to every builder, whatever they had built. A builder on
// their own design was shown fourteen ring panels and six pies their droid does
// not have, and told nothing that would make them doubt it.
//
// The module is executed rather than pattern-matched, per test_web/README.md:
// what is asserted is the markup the shipped code actually put in the card.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");

function read(name) {
  return fs.readFileSync(path.join(root, "data", name), "utf8");
}

// A DOM good enough to run the card for real: elements that remember what was
// written into them, a click listener the test can fire, and insertAdjacentHTML
// captured so the banner can be read back.
function makeElement(className) {
  const element = {
    className: className || "",
    dataset: {},
    innerHTML: "",
    textContent: "",
    inserted: [],
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
    insertAdjacentHTML(position, html) {
      element.inserted.push({ position, html });
    },
    remove() {},
    contains: () => false,
    querySelector(selector) {
      return element.query ? element.query(selector) : null;
    },
    querySelectorAll: () => [],
    focus() {},
    get firstElementChild() {
      return makeElement("banner");
    },
  };
  return element;
}

// Build the page the card lives on, run data/dome_control.js against it, and
// hand back the pieces a test drives it through.
function renderCard({ domeDesign, domeVariant, complementKnown }) {
  const card = makeElement("dome-control-card");
  const header = makeElement("dome-control-header");
  const body = makeElement("dome-control-body");
  const feedback = makeElement("dome-control-feedback");
  card.query = (selector) => {
    if (selector === ".dome-control-header") return header;
    if (selector === ".dome-control-body") return body;
    if (selector === ".dome-control-feedback") return feedback;
    return makeElement();
  };

  // Tier 3 with a dome that is offline: the model data/dome_layout.js resolves
  // for the stated design. Written out here rather than run through that module
  // so this test fails for one reason only - what the card does with it.
  const model = {
    source: complementKnown === undefined ? "vendored" : "stated-design",
    runtimeVerified: false,
    warning: null,
    viewBox: "0 0 480 480",
    elements: [],
    domeDesign,
    domeVariant,
    complementKnown: complementKnown === undefined ? true : complementKnown,
    usesVendoredDrawing: complementKnown === undefined,
  };

  // `window` IS the global in a browser, so the sandbox is its own window -
  // the module reaches PAApi and PAUtils bare as well as through window., and
  // a harness that split the two would answer a question the page never asks.
  const sandbox = {
    PAAssetsReady: true,
    addEventListener() {},
    DOME_PANEL_MAP_SVG: '<svg class="vendored-mk4"></svg>',
    DomeLayout: {
      load: () => Promise.resolve(),
      getModel: () => model,
      getSource: () => model.source,
      onChange() {},
    },
    DomeLayoutRender: { renderPicker: () => '<svg class="live"></svg>' },
    DomeCommandMap: { decodeCommandToElement: () => null },
    PAApi: {
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      get: () => Promise.resolve({ ok: true, data: [] }),
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml: (value) => String(value) },
    document: {
      body: { dataset: { page: "home" } },
      getElementById: (id) => (id === "dome-control-card" ? card : null),
      createElement: (tag) => makeElement(tag),
      addEventListener() {},
    },
    setTimeout,
    clearTimeout,
    Promise,
    console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(read("dome_control.js"), sandbox);

  return { card, header, body, expand: () => header.listeners.click[0]({ target: header }) };
}

// The card renders lazily on first expand, and every step of that is async.
async function expanded(options) {
  const page = renderCard(options);
  await page.expand();
  const picker = page.body.children[page.body.children.length - 1];
  const banner = page.body.inserted.map((entry) => entry.html).join("");
  return { picker: picker ? picker.innerHTML : "", banner };
}

test("the built-in drawing is shown to the builder whose dome it is", async () => {
  const view = await expanded({ domeDesign: "mk4", domeVariant: "complex" });
  assert.match(view.picker, /vendored-mk4/);
  assert.match(view.banner, /showing MK4 built-in layout/);
});

test("a builder on another design is not shown a drawing of somebody else's droid", async () => {
  const view = await expanded({ domeDesign: "own", domeVariant: "", complementKnown: true });
  assert.doesNotMatch(view.picker, /vendored-mk4/, "the MK4 drawing was shown as theirs");
  // And the card says why, rather than leaving an empty space.
  assert.match(view.banner, /no built-in map for the dome design you stated/);
});

test("a design whose complement nobody has read says that, not 'no map'", async () => {
  // `mk4/simple`: the simple/complex split is recorded nowhere in this
  // repository, so the two sentences send a builder to two different places -
  // one is "we have no picture", the other is "we do not know what you have".
  const view = await expanded({ domeDesign: "mk4", domeVariant: "simple", complementKnown: false });
  assert.doesNotMatch(view.picker, /vendored-mk4/);
  assert.match(view.banner, /does not know which panels that dome design carries/);
});
