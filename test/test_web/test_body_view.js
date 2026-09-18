// =============================================================================
// test/test_web/test_body_view.js
//
// The one renderer for a picture of the droid, and the selection panel beside
// it (data/body_view.js, ADR 0063, #352).
//
// What these hold is the contract the seam exists for, not the shape of the
// markup: the view never writes, a click only reports a pick, live state and a
// routine's moment draw identically, a part nothing is driving shows no
// position at all, a repaint keeps the selection, and one device on the droid
// is one marker offering its Parts.
//
// The module is executed as shipped against a mini_dom document, with a PAApi
// whose every method throws - so "the view never writes" is a fact the test can
// observe rather than a claim about the source text (test/test_web/README.md).
// =============================================================================
import test from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

// The real catalog, so the marker rules are checked against the Parts the droid
// actually has rather than against a fixture that agrees with them.
const catalogOf = () => {
  const context = { window: {} };
  context.globalThis = context;
  vm.runInNewContext(readData("droid_parts.js"), context, { filename: "droid_parts.js" });
  return context.window.DroidParts;
};

const boot = () => {
  const document = new MiniDocument();
  const refused = () => {
    throw new Error("the body view asked the droid for something");
  };
  const windowMock = {
    // Every route a surface module could reach the droid through, wired to
    // throw. The renderer's contract is that it never writes; this is what
    // makes a broken contract a failing test rather than an unnoticed request.
    PAApi: { get: refused, postForm: refused, estopPostForm: refused, gateControls: refused },
    PAUtils: { escapeHtml: (value) => String(value) },
  };
  const errors = [];
  const context = {
    window: windowMock,
    document,
    console: { error: (...args) => errors.push(args.join(" ")), warn: () => {}, log: () => {} },
    Object,
    Array,
    Map,
    Set,
    String,
    Number,
    Boolean,
    Math,
    JSON,
    Error,
  };
  context.globalThis = context;
  vm.runInNewContext(readData("body_view.js"), context, { filename: "body_view.js" });
  const host = document.createElement("div");
  document.body.appendChild(host);
  const panelHost = document.createElement("div");
  document.body.appendChild(panelHost);
  return { document, host, panelHost, BodyView: windowMock.BodyView, errors };
};

const bodyParts = (catalog) => catalog.parts.filter((part) => part.half === "body");
const markerOf = (host, id) =>
  host.querySelectorAll("[data-marker]").find((node) => node.dataset.marker === id);

// ---------------------------------------------------------------------------
// One device on the droid is one marker
// ---------------------------------------------------------------------------

test("a Part with no position word is reported unplaced, never dropped and never guessed", () => {
  const catalog = catalogOf();
  const { host, BodyView } = boot();
  const drawing = BodyView.mountDrawing(host, { parts: catalog.parts });

  const slots = catalog.parts.filter((part) => part.id.startsWith("other")).map((part) => part.id);
  assert.equal(slots.length, 10, "the catalog's ten escape-hatch slots");
  slots.forEach((id) => {
    assert.ok(drawing.unplaced.includes(id), `${id} is reported unplaced`);
    assert.equal(markerOf(host, id), undefined, `${id} is not placed somewhere plausible`);
  });
});

// ---------------------------------------------------------------------------
// It shows one kind of state at a time, and says which
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Nothing is presented as read back from a servo
// ---------------------------------------------------------------------------

test("a Part nothing is driving shows no position at all - absent, not dimmed", () => {
  const catalog = catalogOf();
  const { host, BodyView } = boot();
  const drawing = BodyView.mountDrawing(host, { parts: bodyParts(catalog) });

  ["undriven", "limp", "unmeasured", "unknown"].forEach((mark) => {
    drawing.update({ kind: "live", marks: { doorFL: { mark } } });
    const cell = markerOf(host, "doorFL");
    assert.ok(cell.classList.contains(`is-${mark}`), `${mark} takes its own treatment`);
    assert.equal(
      cell.classList.contains("has-position"),
      false,
      `${mark} makes no claim about where the part is`
    );
  });

  drawing.update({ kind: "live", marks: { doorFL: { mark: "driven", at: 0.5 } } });
  assert.ok(markerOf(host, "doorFL").classList.contains("has-position"));
});

test("how far open is drawn, and a fraction outside the travel cannot swing past the end", () => {
  const catalog = catalogOf();
  const { host, BodyView } = boot();
  const drawing = BodyView.mountDrawing(host, { parts: bodyParts(catalog) });
  const swing = () => {
    const transform = markerOf(host, "doorFL").querySelector(".bodyview-leaf").getAttribute("transform");
    return Number(/rotate\(([-\d.]+)/.exec(transform)[1]);
  };

  drawing.update({ kind: "live", marks: { doorFL: { mark: "driven", at: 0 } } });
  assert.equal(swing(), 0, "the close end does not swing");
  drawing.update({ kind: "live", marks: { doorFL: { mark: "driven", at: 1 } } });
  const wideOpen = swing();
  assert.ok(wideOpen > 0, "the open end swings");
  drawing.update({ kind: "live", marks: { doorFL: { mark: "driven", at: 0.5 } } });
  assert.ok(swing() > 0 && swing() < wideOpen, "half way through the travel swings half way");
  drawing.update({ kind: "live", marks: { doorFL: { mark: "driven", at: 9 } } });
  assert.equal(swing(), wideOpen, "nothing swings past the recorded end");
});

test("a mark the renderer does not draw is reported, never swallowed into 'not said yet'", () => {
  const catalog = catalogOf();
  const { host, BodyView, errors } = boot();
  const drawing = BodyView.mountDrawing(host, { parts: bodyParts(catalog) });

  drawing.update({ kind: "live", marks: { doorFL: { mark: "planned" } } });
  assert.equal(errors.length, 1, "the caller's defect reaches the console");
  assert.match(errors[0], /doorFL/);
  assert.match(errors[0], /planned/);
  // And nothing on the surface draws it: there is no `planned` state here.
  assert.equal(markerOf(host, "doorFL").classList.contains("is-planned"), false);
});

// ---------------------------------------------------------------------------
// A click only selects, and the view never writes
// ---------------------------------------------------------------------------

test("a click reports the pick and asks the droid for nothing", () => {
  const catalog = catalogOf();
  const { host, BodyView } = boot();
  const picks = [];
  const drawing = BodyView.mountDrawing(host, {
    parts: bodyParts(catalog),
    onPick: (id) => picks.push(id),
  });

  const svg = host.querySelector(".bodyview-svg");
  svg.fire("click", { target: markerOf(host, "chargebay") });
  assert.deepEqual(picks, ["chargebay"]);
  // Nothing was selected by the click itself: selecting is the caller's, which
  // is what makes a pick a report rather than a state the drawing keeps.
  assert.equal(drawing.selected(), null);
});

// ---------------------------------------------------------------------------
// The selection panel: named buttons, and a no that names the next move
// ---------------------------------------------------------------------------

const ACTS = [
  { id: "fit", label: "add it to the build" },
  { id: "wire", label: "give it an Output" },
  { id: "move", label: "move it" },
];

test("an act that cannot run is refused and says why, rather than vanishing", () => {
  const { panelHost, BodyView } = boot();
  const pressed = [];
  const panel = BodyView.mountPanel(panelHost, { acts: ACTS, onAct: (id) => pressed.push(id) });

  panel.show({
    title: "Left body door",
    facts: [{ term: "Driven by", value: "- not wired -" }],
    acts: { fit: { enabled: false }, wire: { enabled: true }, move: { enabled: false } },
    why: "Nothing drives it yet - give it an output first.",
  });

  const button = (id) => panelHost.querySelectorAll("[data-act]").find((node) => node.dataset.act === id);
  assert.equal(button("move").disabled, true);
  assert.equal(button("move").getAttribute("aria-disabled"), "true");
  assert.equal(button("wire").disabled, false);
  assert.equal(button("wire").getAttribute("aria-disabled"), "false");
  assert.match(panelHost.querySelector(".bodyview-panel-why").textContent, /give it an output first/i);

  // A refused button is still on the surface, so the next move is still named -
  // and pressing it does nothing, which is the rule rather than a repeat of it.
  panelHost.querySelector(".bodyview-panel-acts").fire("click", { target: button("move") });
  assert.deepEqual(pressed, []);
  panelHost.querySelector(".bodyview-panel-acts").fire("click", { target: button("wire") });
  assert.deepEqual(pressed, ["wire"]);
});

test("clearing the panel refuses every act, so nothing is offered with nothing picked", () => {
  const { panelHost, BodyView } = boot();
  const panel = BodyView.mountPanel(panelHost, { acts: ACTS });

  panel.show({ title: "Small long door", facts: [], acts: { move: { enabled: true } }, why: "" });
  panel.clear("Pick a part on the drawing.");

  panelHost.querySelectorAll("[data-act]").forEach((node) => {
    assert.equal(node.disabled, true, `${node.dataset.act} is refused with nothing picked`);
  });
  assert.equal(panelHost.querySelector(".bodyview-panel-title").textContent, "");
  assert.match(panelHost.querySelector(".bodyview-panel-why").textContent, /Pick a part/);
});
