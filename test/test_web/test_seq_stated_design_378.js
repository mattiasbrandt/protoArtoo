// The sequence editor's panel picker, once it draws the dome the builder
// actually stated (#378, ADR 0047, ADR 0009).
//
// Two surfaces already asked: #343 moved data/dome_layout.js's tier 3 onto the
// Droid Build seam and data/dome_control.js onto its answer. data/seq.js was
// left behind - not because the fix was hard, but because this 121 KB IIFE had
// no web coverage at all, so no mutation of it could be killed. The harness
// below is the half of this ticket that was missing.
//
// It drives the shipped module through the seam the Playwright suites already
// use - window.__seqEditorForTesting.renderEditorView - and derives the step
// containers from the markup renderStepRow() actually wrote, rather than
// asserting a hand-built page shape. The modules under it are the real ones, in
// the order data/seq.html declares them, so what is asserted here is the whole
// chain a builder's browser runs: DroidBuild.load() reads the stated design off
// /api/config, tier 3 decides whether the built-in drawing is that builder's
// dome, and the picker draws it or does not.
//
// Per test_web/README.md: everything is executed, nothing is pattern-matched.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

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
  const elements = new Map();
  const elementById = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };

  // The step-fields containers, derived from the editor markup the shipped
  // renderStepRow() wrote. Only an EXPANDED card carries one, and the step
  // index comes off the card the same way seq.js reads it - so a card that
  // stopped emitting either would be found by nothing here, which is the point.
  let fieldsByStep = new Map();
  const deriveStepFields = () => {
    const html = elementById("seq-editor-view").innerHTML || "";
    const marks = [];
    const cardRe = /data-step-index="(\d+)"/g;
    let match;
    while ((match = cardRe.exec(html)) !== null) {
      marks.push({ index: match[1], at: match.index });
    }
    const found = [];
    marks.forEach((mark, i) => {
      const end = i + 1 < marks.length ? marks[i + 1].at : html.length;
      if (!html.slice(mark.at, end).includes('class="step-fields"')) return;
      const container = fieldsByStep.get(mark.index) || makeElement();
      fieldsByStep.set(mark.index, container);
      const card = {
        dataset: { stepIndex: mark.index },
        querySelector: (selector) => (selector === ".step-fields" ? container : null),
      };
      container.closest = (selector) => (selector === ".step-card" ? card : null);
      found.push(container);
    });
    return found;
  };

  // The picker containers seq.js re-renders on a layout change, derived from
  // what renderStepFields() actually put in each step's fields container. A
  // picker rendered without one is unreachable from here exactly as it would be
  // in the browser.
  const derivePickerContainers = () => {
    const found = [];
    deriveStepFields().forEach((container) => {
      const html = container.innerHTML || "";
      if (!/class="dome-(?:svg-)?picker-container"/.test(html)) return;
      const picker = makeElement();
      picker.closest = container.closest;
      found.push(picker);
    });
    return found;
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
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {}, removeItem() {} },
    document: {
      readyState: "complete",
      body: makeElement(),
      documentElement: makeElement(),
      getElementById: elementById,
      querySelector: () => null,
      querySelectorAll: (selector) => {
        if (selector === ".step-fields") return deriveStepFields();
        if (selector === ".dome-svg-picker-container, .dome-picker-container") {
          return derivePickerContainers();
        }
        return [];
      },
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
  PAGE_MODULES.forEach((name) => {
    vm.runInContext(read(name), sandbox, { filename: name });
  });

  const seam = sandbox.window.__seqEditorForTesting;

  return {
    window: sandbox.window,
    requests,
    // Open a sequence in the editor with one step expanded, the way a builder
    // clicking a step card does, and return that step's fields markup.
    openEditor(sequence, expandedStep = 0) {
      seam.editorState.expanded = new Set([expandedStep]);
      seam.renderEditorView(sequence);
      return fieldsByStep.get(String(expandedStep));
    },
    // Lets the module's own async work settle: renderEditorView() kicks the
    // layout load off without awaiting it and re-renders on the answer.
    async settle(turns = 6) {
      for (let i = 0; i < turns; i += 1) {
        await new Promise((resolve) => setImmediate(resolve));
      }
    },
  };
}

// A Droid Build as GET /api/config carries it.
const build = (domeDesign, domeVariant) => ({
  droidBuild: {
    domeDesign,
    domeVariant,
    bodyDesign: domeDesign,
    bodyVariant: domeVariant,
    fitted: [],
  },
});

// One panel step and an end step - the sequence a builder is authoring when
// this picker is on screen.
const panelSequence = () => ({
  name: "DM:TEST",
  suppressMs: 8000,
  toggleGroup: "none",
  steps: [
    { t: 0, type: "dome", cmd: ":OP07" },
    { t: 1000, type: "end" },
  ],
});

// The drawing declares which design it is of; read it out of the module rather
// than repeating the answer here.
const drawing = (() => {
  const context = { window: {} };
  vm.runInNewContext(read("dome_panel_model.js"), context);
  return context.window;
})();

async function openWithBuild(config) {
  const page = newPage(config);
  // The hierarchy answers before the builder opens the editor: this is the
  // second and every later open of a page session. The first open is its own
  // test below.
  await page.window.DomeLayout.load();
  const fields = page.openEditor(panelSequence());
  return { page, html: fields.innerHTML };
}

test("the built-in drawing is shown to the builder whose dome it is", async () => {
  const { html } = await openWithBuild(build(drawing.DOME_PANEL_MAP_DESIGN, drawing.DOME_PANEL_MAP_VARIANT));

  assert.ok(html.includes(drawing.DOME_PANEL_MAP_SVG), "the MK4 drawing was withheld from an MK4 builder");
  assert.match(html, /Dome not reachable — showing built-in MK4 layout/);
});

test("a builder on their own build is not shown a drawing of somebody else's droid", async () => {
  const { html } = await openWithBuild(build("own", ""));

  assert.doesNotMatch(html, /dome-svg-picker/, "the MK4 drawing was shown as this builder's dome");
  // And the empty picker says why, rather than leaving a gap.
  assert.match(html, /Dome not reachable — no built-in map for the dome design you stated/);
  // The container itself stays: it is what a dome reconnect re-renders through.
  assert.match(html, /class="dome-picker-container"/);
});

test("a design whose complement nobody has read says that, not 'no map'", async () => {
  // mk4/simple carries `seeds: null` in the catalog - a simple MK4 dome cannot
  // grow the complex pies, and what it does carry is written down nowhere here.
  const { html } = await openWithBuild(build("mk4", "simple"));

  assert.doesNotMatch(html, /dome-svg-picker/);
  assert.match(html, /this build does not know which panels that dome design carries/);
});

test("the design comes off the Droid Build seam, not out of a second derivation", async () => {
  const { page, html } = await openWithBuild(build("own", ""));

  // DroidBuild.load() is the only thing on this page that reads /api/config,
  // and the picker's answer changed with what it found there.
  assert.ok(page.requests.some((url) => url.startsWith("/api/config")), "nothing asked the Droid Build seam");
  assert.equal(page.window.DroidBuild.current().dome.design, "own");
  assert.equal(page.window.DomeLayout.getModel().usesVendoredDrawing, false);
  assert.doesNotMatch(html, /dome-svg-picker/);
});

test("a controller that carries no Droid Build keeps the behaviour it had", async () => {
  // Older firmware: /api/config answers, but with no droidBuild in it. An
  // unstated design is not a statement that the drawing is wrong.
  const { html } = await openWithBuild({});

  assert.match(html, /dome-svg-picker/);
  assert.match(html, /Dome not reachable — showing built-in MK4 layout/);
});

test("the first open draws nobody's dome until the hierarchy has answered", async () => {
  // renderEditorView() starts the layout load without awaiting it, and an
  // unreachable dome takes the full fetch timeout to fall through to tier 3.
  // Drawing the built-in dome meanwhile shows a builder on their own design
  // somebody else's droid for as long as that takes.
  const page = newPage(build("own", ""));
  const fields = page.openEditor(panelSequence());

  assert.doesNotMatch(fields.innerHTML, /dome-svg-picker/, "a dome was drawn before anyone knew whose it was");
  assert.match(fields.innerHTML, /Checking which dome you built/);

  // And the picker fills itself in when the answer arrives, without the builder
  // reopening anything: DomeLayout.onChange() re-renders it.
  await page.settle();
  assert.doesNotMatch(fields.innerHTML, /Checking which dome you built/);
  assert.match(fields.innerHTML, /no built-in map for the dome design you stated/);
});

test("the same first open shows the MK4 builder their dome once it is known", async () => {
  const page = newPage(build(drawing.DOME_PANEL_MAP_DESIGN, drawing.DOME_PANEL_MAP_VARIANT));
  const fields = page.openEditor(panelSequence());

  assert.match(fields.innerHTML, /Checking which dome you built/);
  await page.settle();
  assert.ok(fields.innerHTML.includes(drawing.DOME_PANEL_MAP_SVG), "the MK4 builder never got their drawing");
});

test("authoring a panel the droid does not have stays legal", async () => {
  // Author before you wire: the Dome Design decides which PICTURE may be shown,
  // and nothing else. The step keeps its command, every panel target is still
  // offerable, and Protocol Check still passes the sequence.
  const { page, html } = await openWithBuild(build("own", ""));

  assert.match(html, /<option value="07" selected>/);
  assert.match(html, /<option value="P1" /);
  assert.match(html, /value=":OP07"/);

  const state = page.window.__seqEditorForTesting.editorState;
  assert.equal(state.current.steps[0].cmd, ":OP07");
  assert.equal(page.window.SeqProtocolCheck.validateSequence(state.current).ok, true);
});

test("an unsupported schema no longer claims a drawing it is not showing", async () => {
  // Tier 4 is tier 3 plus the schema warning: the geometry is not trusted, so
  // what may be drawn is what the stated design allows - and for a design the
  // built-in drawing is not of, that is nothing.
  const page = newPage(build("own", ""), { ok: true, status: 200, data: { schema_revision: 99 } });
  await page.window.DomeLayout.load();
  const html = page.openEditor(panelSequence()).innerHTML;

  assert.doesNotMatch(html, /dome-svg-picker/);
  assert.match(html, /schema 99 not supported/);
  assert.doesNotMatch(html, /showing built-in MK4 layout/);
});
