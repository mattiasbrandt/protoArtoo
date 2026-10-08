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
const { statusFrame } = require("./helpers/fake_droid.js");
const { operatorShellUi } = require("./helpers/page_module_env.js");

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
//
// underShell mounts it the way the Operator Shell does after the page's first
// surface: assets long since ready, and the bootstrap's section events still to
// come. The window listeners and the dome layout reads are kept so a test can
// see when the read starts.
function renderCard({ domeDesign, domeVariant, complementKnown, status, underShell = false }) {
  const card = makeElement("dome-control-card");
  const feedback = makeElement("dome-control-feedback");
  // The card's dome column: the banner and the dome drawing go here.
  const dome = makeElement("moving-parts-dome");
  card.query = (selector) => {
    if (selector === ".dome-control-feedback") return feedback;
    if (selector === ".moving-parts-dome") return dome;
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

  const posts = [];
  // The drawing the card renders its picker into. Its click listener is the
  // one a panel press reaches, so the test can press a panel for real.
  const svg = makeElement("svg");
  const document = {
    body: { dataset: { page: "home" } },
    getElementById: (id) => (id === "dome-control-card" ? card : null),
    createElement: (tag) => {
      const element = makeElement(tag);
      element.query = (selector) => (selector === "svg" ? svg : null);
      return element;
    },
    addEventListener() {},
  };

  // `window` IS the global in a browser, so the sandbox is its own window -
  // the module reaches PAApi and PAUtils bare as well as through window., and
  // a harness that split the two would answer a question the page never asks.
  const windowListeners = [];
  let layoutReads = 0;
  const sandbox = {
    PAAssetsReady: true,
    addEventListener(type, handler) {
      windowListeners.push({ type, handler });
    },
    removeEventListener(type, handler) {
      const at = windowListeners.findIndex((entry) => entry.type === type && entry.handler === handler);
      if (at >= 0) windowListeners.splice(at, 1);
    },
    DOME_PANEL_MAP_SVG: '<svg class="vendored-mk4"></svg>',
    DomeLayout: {
      load: () => {
        layoutReads += 1;
        return Promise.resolve();
      },
      getModel: () => model,
      getSource: () => model.source,
      onChange() {},
    },
    DomeLayoutRender: { renderPicker: () => '<svg class="live"></svg>' },
    DomeCommandMap: { decodeCommandToElement: () => null },
    // A browser has one; it never opens here, and the droid's status reaches
    // the Live Reading the way the Operator Shell's boot read hands it over.
    EventSource: class {
      addEventListener() {}
      close() {}
    },
    PAApi: {
      postForm: (route, form) => {
        posts.push({ route, form });
        return Promise.resolve({ ok: true, data: {} });
      },
      get: () => Promise.resolve({ ok: true, data: [] }),
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml: (value) => String(value) },
    document,
    setTimeout,
    clearTimeout,
    Promise,
    console,
  };
  if (underShell) sandbox.PABootstrap = {};
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  // The chain every document loads ahead of a surface: the stream and the
  // Live Reading, started as the shell starts it. Absent means the droid has
  // not said yet.
  vm.runInContext(read("status_stream.js"), sandbox);
  vm.runInContext(read("live_reading.js"), sandbox);
  sandbox.PALiveReading.start();
  if (status) sandbox.PAStatusStream.seed(status);
  // The estop hold both drawings on the card keep (data/droid_picture.js).
  vm.runInContext(read("droid_picture.js"), sandbox);
  vm.runInContext(read("dome_control.js"), sandbox);

  // A press on the built-in map's panel with this Panel Intent target, the
  // way a click reaches it: the picker's one delegated listener.
  const press = (target) => {
    const panel = { dataset: { target }, classList: { add() {}, remove() {} } };
    const event = {
      target: { closest: (selector) => (selector === "[data-target]" ? panel : null) },
    };
    return Promise.all(svg.listeners.click.map((listener) => listener(event)));
  };

  return {
    card,
    dome,
    feedback,
    posts,
    press,
    // The drawings are drawn when the module runs, and every step of that is
    // async: one turn of the event loop lets it finish.
    drawn: () => new Promise((resolve) => setTimeout(resolve, 0)),
    layoutReads: () => layoutReads,
    // Delivers a window event to the handlers registered for it right now.
    emit: (type, event) =>
      windowListeners.filter((entry) => entry.type === type).forEach(({ handler }) => handler(event)),
  };
}

async function drawn(options) {
  const page = renderCard(options);
  await page.drawn();
  const picker = page.dome.children[page.dome.children.length - 1];
  const banner = page.dome.inserted.map((entry) => entry.html).join("");
  return { picker: picker ? picker.innerHTML : "", banner };
}

test("the built-in drawing is shown to the builder whose dome it is", async () => {
  const view = await drawn({ domeDesign: "mk4", domeVariant: "complex" });
  assert.match(view.picker, /vendored-mk4/);
  assert.match(view.banner, /Showing the built-in MK4 map/);
});

test("a builder on another design is not shown a drawing of somebody else's droid", async () => {
  const view = await drawn({ domeDesign: "own", domeVariant: "", complementKnown: true });
  assert.doesNotMatch(view.picker, /vendored-mk4/, "the MK4 drawing was shown as theirs");
  // And the card says why, rather than leaving an empty space.
  assert.match(view.banner, /No built-in map for your dome design/);
});

// Under the Operator Shell the Dashboard's scripts run while it mounts, before
// app.js has read the config. The dome's layout read reads the Droid Build
// first (data/dome_layout.js -> data/droid_build.js load()), so started then it
// opened a second GET /api/config beside app.js's, on a controller that sheds
// connections under load (#399). It waits for the Dashboard's sections.
test("under the Operator Shell the dome's layout read waits until the Dashboard's sections settle", async () => {
  const page = renderCard({ domeDesign: "mk4", domeVariant: "complex", underShell: true });
  await page.drawn();
  assert.equal(page.layoutReads(), 0, "the dome read its layout before the Dashboard had read its config");

  page.emit("pa:bootstrap-change", { detail: { sectionsStable: false } });
  await page.drawn();
  assert.equal(page.layoutReads(), 0, "it read while the Dashboard's sections were still loading");

  page.emit("pa:bootstrap-change", { detail: { sectionsStable: true } });
  page.emit("pa:bootstrap-change", { detail: { sectionsStable: true } });
  await page.drawn();
  assert.equal(page.layoutReads(), 1, "the dome is drawn once, when the sections settle");
});

// The estop holds every servo move a picture of the droid can start (operator,
// 2026-09-19, #372). A panel press on this card went straight to the dome
// whatever the estop said.
test("a dome panel press sends nothing while the estop is latched or not yet known", async () => {
  const withoutEstop = statusFrame();
  delete withoutEstop.estop;
  // Latched; nothing heard yet; and a frame that never mentioned the estop,
  // which is not a droid saying it is clear (#419).
  for (const status of [statusFrame({ estop: true }), null, withoutEstop]) {
    const page = renderCard({ domeDesign: "mk4", domeVariant: "complex", status });
    await page.drawn();
    await page.press("07");
    assert.deepEqual(page.posts, [], `a press went out with the estop ${JSON.stringify(status)}`);
    assert.match(page.feedback.textContent, /estop latched|stopped/i, "and the card says why");
  }

  const clear = renderCard({ domeDesign: "mk4", domeVariant: "complex", status: statusFrame() });
  await clear.drawn();
  await clear.press("07");
  assert.deepEqual(
    clear.posts.map((post) => post.form.cmd),
    [":OP07"],
    "with the estop clear the same press opens the panel"
  );
});

// ---------------------------------------------------------------------------
// The body drawing on the same card (#372, operator 2026-09-28): a click on a
// drawn door opens or closes it, held by the same estop as Parts. Run on the
// shipped card markup (data/dashboard.html) with the real body view, Outputs,
// Droid Build and decision (data/droid_picture.js), so "sends nothing" is read
// off the requests the page actually tried.
// ---------------------------------------------------------------------------
async function bodyCard(status) {
  const { MiniDocument } = await import("./helpers/mini_dom.js");
  const { servoRow } = await import("./helpers/fake_droid.js");
  const document = new MiniDocument();
  const html = read("dashboard.html");
  const start = html.indexOf('<div class="moving-parts-bay" id="dome-control-card">');
  const end = html.indexOf("</section>", start);
  document.body.innerHTML = html.slice(start, end);
  document.body.dataset.page = "home";

  // ARM1 carries the right body door, its ends measured, standing closed.
  const outputs = [
    servoRow("ledc:0", "ARM1", { parts: ["doorFR"], calibrated: true, commandedUs: 1000, targetUs: 1000 }),
  ];
  const posts = [];
  const sandbox = {
    PAAssetsReady: true,
    addEventListener() {},
    EventSource: class {
      addEventListener() {}
      close() {}
    },
    // No dome on this bench: the dome half answers as unreachable and empty.
    DomeLayout: {
      load: () => Promise.resolve(),
      getModel: () => null,
      getSource: () => "vendored",
      onChange() {},
    },
    DomeCommandMap: { decodeCommandToElement: () => null },
    PAApi: {
      get: (route) =>
        Promise.resolve({ ok: true, data: route === "/api/servo/outputs" ? { outputs } : [] }),
      postForm: (route, form) => {
        posts.push({ route, form });
        return Promise.resolve({ ok: true, data: {} });
      },
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml: (value) => String(value), escapeAttr: (value) => String(value) },
    // The act the body view's panel is drawn with, from the shipped shell (#460).
    PAUi: operatorShellUi(),
    document,
    URLSearchParams,
    setTimeout,
    clearTimeout,
    Promise,
    console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  [
    "status_stream.js",
    "live_reading.js",
    "droid_parts.js",
    "droid_part_kind.js",
    "droid_build.js",
    "body_art.js",
    "body_view.js",
    "outputs.js",
    "parts_mapping.js",
    "droid_picture.js",
  ].forEach((file) => vm.runInContext(read(file), sandbox, { filename: file }));
  sandbox.PALiveReading.start();
  if (status) sandbox.PAStatusStream.seed(status);
  sandbox.DroidBuild.adopt({
    droidBuild: { domeDesign: "mk4", domeVariant: "complex", bodyDesign: "mk4", bodyVariant: "complex", fitted: ["doorFR"] },
  });
  await sandbox.PAOutputs.refresh();
  vm.runInContext(read("dome_control.js"), sandbox, { filename: "dome_control.js" });
  // The drawings are drawn when the module runs; let that finish first, as a
  // page does before anyone can click.
  await new Promise((resolve) => setTimeout(resolve, 0));

  const svg = document.querySelector(".moving-parts-body").querySelector(".bv-svg");
  const door = svg.querySelectorAll("[data-marker]").find((node) => node.dataset.marker === "doorFR");
  const settle = () => new Promise((resolve) => setTimeout(resolve, 0));
  return {
    posts,
    said: () => document.querySelector(".dome-control-feedback").textContent,
    click: async () => {
      svg.fire("click", { target: door });
      await settle();
    },
  };
}

test("a click on a body door on the card sends nothing while the estop is latched or not yet known", async () => {
  for (const status of [statusFrame({ estop: true }), null]) {
    const card = await bodyCard(status);
    await card.click();
    assert.deepEqual(card.posts, [], `a click went out with the estop ${JSON.stringify(status)}`);
    assert.match(card.said(), /estop latched|stopped/i, "and the card says why");
  }

  const clear = await bodyCard(statusFrame());
  await clear.click();
  assert.deepEqual(
    clear.posts.map((post) => [post.route, post.form.arm, post.form.action]),
    [["/api/servo", "ARM1", "open"]],
    "with the estop clear the same click opens the door"
  );
});
