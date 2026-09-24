// Tier 3 of the Layout Fallback Hierarchy, once it consults the Dome Design
// (#343, ADR 0047, ADR 0009).
//
// Before this, tier 3 was "render the offline MK4 model", unconditionally: a
// builder on an MK3 dome, or on their own build, was shown a picture of
// somebody else's droid and given no hint it was not theirs. The Dome Design
// is the builder's statement, so tier 3 now asks what they said and shows the
// built-in drawing only where that drawing IS their dome.
//
// The variant is part of "their dome", not decoration: a simple MK4 dome
// cannot grow the complex pies, so the complex drawing promises panels that
// builder can never fit.
//
// The modules are executed rather than pattern-matched, per test_web/README.md.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");

function read(name) {
  return fs.readFileSync(path.join(root, "data", name), "utf8");
}

// A page with an unreachable dome - the state tier 3 exists for - and a Droid
// Build already applied. `droidBuild` is null for a page that never loaded the
// seam at all, which is how an older page reaches this code.
function newPage(droidBuild) {
  const context = {
    window: {
      PAAssetsReady: true,
      addEventListener() {},
      localStorage: {
        length: 0,
        key: () => null,
        getItem: () => null,
        setItem() {},
      },
      // The dome is not answering: that is what puts the hierarchy past tier 2.
      PAApi: { get: () => Promise.resolve({ ok: false, status: 503, data: null }) },
      // Read out of the vendored drawing itself rather than assumed here.
      DOME_PANEL_MAP_DESIGN: "mk4",
      DOME_PANEL_MAP_VARIANT: "complex",
      DomeCommandMap: { resolvePanelCommand: () => null },
      DroidBuild: droidBuild,
    },
    setTimeout,
    clearTimeout,
    Promise,
  };
  vm.runInNewContext(read("dome_layout.js"), context);
  return context.window;
}

// A stand-in for the seam carrying one stated answer. complementFor() answers
// the way data/droid_build.js does, including the distinction that matters:
// `known: false` is a complement nobody has read, not an empty one.
function seam(design, variant, complementKnown) {
  return {
    load: () => Promise.resolve(null),
    current: () => ({ dome: { design, variant }, body: { design, variant }, fitted: [] }),
    complementFor: () => ({ ids: [], known: complementKnown }),
  };
}

test("a dome the built-in drawing is not of is not drawn as theirs", async () => {
  const page = newPage(seam("own", "", true));
  await page.DomeLayout.load();

  assert.equal(page.DomeLayout.getSource(), "stated-design");
  const model = page.DomeLayout.getModel();
  assert.equal(model.usesVendoredDrawing, false);
  assert.equal(model.domeDesign, "own");
  // The complement IS known for `own` - it is deliberately empty - so what the
  // builder is told is that there is no picture, not that nothing is known.
  assert.equal(model.complementKnown, true);
  assert.match(model.warning, /not the design you stated/);
});

test("the variant decides it too: a basic dome is not the complex drawing", async () => {
  // An `mk4/basic` dome is unknown in the catalog - nobody has read what a
  // basic MK4 dome carries (#409) - so this is the case that must not be
  // answered with the complex picture OR with an empty one.
  const page = newPage(seam("mk4", "basic", false));
  await page.DomeLayout.load();

  assert.equal(page.DomeLayout.getSource(), "stated-design");
  const model = page.DomeLayout.getModel();
  assert.equal(model.usesVendoredDrawing, false);
  assert.equal(model.complementKnown, false);
  assert.match(model.warning, /does not record which panels/);
});

test("a page with no Droid Build keeps the behaviour it had before", async () => {
  // An older controller, or a page that does not load the seam. An unstated
  // design is not a statement that the drawing is wrong.
  const page = newPage(null);
  await page.DomeLayout.load();

  assert.equal(page.DomeLayout.getSource(), "vendored");
  const model = page.DomeLayout.getModel();
  assert.equal(model.usesVendoredDrawing, true);
  assert.equal(model.domeDesign, "");
  assert.equal(model.warning, null);
});

test("an unsupported schema is tier 3 plus its own warning", async () => {
  // ADR 0009: an unsupported schema's geometry is never trusted, so what can be
  // shown is what the stated design allows - including, for a design the
  // built-in drawing is not of, no drawing at all.
  const context = {
    window: {
      PAAssetsReady: true,
      addEventListener() {},
      localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {} },
      PAApi: {
        get: () => Promise.resolve({ ok: true, status: 200, data: { schema_revision: 99 } }),
      },
      DOME_PANEL_MAP_DESIGN: "mk4",
      DOME_PANEL_MAP_VARIANT: "complex",
      DomeCommandMap: { resolvePanelCommand: () => null },
      DroidBuild: seam("own", "", true),
    },
    setTimeout,
    clearTimeout,
    Promise,
  };
  vm.runInNewContext(read("dome_layout.js"), context);
  await context.window.DomeLayout.load();

  assert.equal(context.window.DomeLayout.getSource(), "unsupported");
  const model = context.window.DomeLayout.getModel();
  assert.match(model.warning, /schema 99 not supported/);
  // The schema warning wins, and the design still decides the drawing.
  assert.equal(model.usesVendoredDrawing, false);
});


// The dome's own layout is worth asking for again only when its link has just
// come up: the dome link's state is a status field, heard through the Live
// Reading like every other (#419), and a link that stays up is not asked again
// on every frame -- each ask is a request to a dome on a shared serial line.
test("a dome link coming up re-reads the layout once, and a link that stays up does not", async () => {
  const { statusFrame } = require("./helpers/fake_droid.js");
  const layoutReads = [];
  // `window` IS the global in a browser, so the sandbox is its own window and
  // the shipped files run unmodified.
  const sandbox = {
    PAAssetsReady: true,
    addEventListener() {},
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {} },
    PAApi: {
      get: (route) => {
        layoutReads.push(route);
        return Promise.resolve({ ok: false, status: 503, data: null });
      },
    },
    DOME_PANEL_MAP_DESIGN: "mk4",
    DOME_PANEL_MAP_VARIANT: "complex",
    DomeCommandMap: { resolvePanelCommand: () => null },
    DroidBuild: seam("mk4", "complex", true),
    document: { visibilityState: "visible", addEventListener() {} },
    // A browser has one; it never opens here, and frames reach the Live
    // Reading the way the Operator Shell's read hands them over.
    EventSource: class {
      addEventListener() {}
      close() {}
    },
    console: { warn() {}, error() {}, log() {} },
    setTimeout,
    clearTimeout,
    Promise,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(read("status_stream.js"), sandbox);
  vm.runInContext(read("live_reading.js"), sandbox);
  sandbox.PALiveReading.start();
  vm.runInContext(read("dome_layout.js"), sandbox);
  const settle = () => new Promise((resolve) => setImmediate(resolve));
  const push = (state) => sandbox.PAStatusStream.seed(statusFrame({ dome_link: { state } }));

  push("not_seen");
  await settle();
  const before = layoutReads.filter((route) => route === "/api/dome/layout").length;

  push("connected");
  await settle();
  push("connected");
  push("connected");
  await settle();

  assert.equal(
    layoutReads.filter((route) => route === "/api/dome/layout").length - before,
    1,
    "one layout read when the link came up, and none for the frames after it",
  );
});
