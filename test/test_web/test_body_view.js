// =============================================================================
// test/test_web/test_body_view.js
//
// The one renderer for a picture of the droid, and the selection panel beside
// it (data/body_view.js, ADR 0063 as amended 2026-09-19, #352, #372).
//
// What these hold is the contract the seam exists for, not the shape of the
// markup or the words on it: a body Part is on exactly one face, the body and
// the dome speak one state vocabulary, a holoprojector never draws an open
// state, the line art carries no color of its own, a mark the renderer does
// not know is reported rather than swallowed, and a click only reports a pick.
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
    Infinity,
  };
  context.globalThis = context;
  // The real catalog, art and dome map, so the placement rules are checked
  // against the droid protoArtoo actually draws rather than a fixture that
  // agrees with them.
  ["droid_parts.js", "dome_panel_model.js", "body_art.js", "body_view.js"].forEach((file) => {
    vm.runInNewContext(readData(file), context, { filename: file });
  });
  const host = document.createElement("div");
  document.body.appendChild(host);
  const panelHost = document.createElement("div");
  document.body.appendChild(panelHost);
  const mount = (options = {}) =>
    windowMock.BodyView.mountDrawing(host, {
      parts: windowMock.DroidParts.parts,
      art: windowMock.BodyArt,
      domeSvg: windowMock.DOME_PANEL_MAP_SVG,
      railHost: panelHost,
      ...options,
    });
  return { document, host, panelHost, window: windowMock, BodyView: windowMock.BodyView, errors, mount };
};

const markerOf = (host, id) =>
  host.querySelectorAll("[data-marker]").find((node) => node.dataset.marker === id);
const stateOf = (cell) =>
  (cell.getAttribute("class") || "")
    .split(" ")
    .filter((name) => name.startsWith("is-") && name !== "is-selected" && name !== "is-absent");

// ---------------------------------------------------------------------------
// Where a Part is
// ---------------------------------------------------------------------------

test("a body Part is drawn on exactly one face", () => {
  const { host, window, BodyView, errors, mount } = boot();
  mount();

  const bodyIds = window.DroidParts.parts.filter((part) => part.half === "body").map((part) => part.id);
  bodyIds.forEach((id) => {
    const inTable = ["front", "rear"].filter((face) => BodyView.GEOMETRY[face][id]);
    assert.ok(inTable.length <= 1, `${id} has a position on ${inTable.join(" and ")}`);
    const drawn = host.querySelectorAll(".bv-face").filter((face) => markerOf(face, id) !== undefined);
    assert.equal(drawn.length, inTable.length, `${id} is drawn on as many faces as it has a place on`);
  });
  assert.deepEqual(errors, [], "no Part was refused a second face");
});

// ---------------------------------------------------------------------------
// One vocabulary, body and dome alike
// ---------------------------------------------------------------------------

test("a body Part and a dome piece in the same state draw the same class and say the same word", () => {
  const { host, BodyView, mount } = boot();
  const drawing = mount();

  const marks = [
    { mark: "openable", at: 1 },
    { mark: "openable", at: 0 },
    { mark: "unmeasured" },
    { mark: "limp" },
    { mark: "unassigned" },
  ];
  marks.forEach((mark) => {
    drawing.update({ kind: "live", marks: { doorFL: mark, "dome-pp1": mark } });
    const body = markerOf(host, "doorFL");
    const dome = markerOf(host, "dome-pp1");
    assert.deepEqual(stateOf(dome), stateOf(body), `${JSON.stringify(mark)} draws one way on both`);
    const word = (cell) => cell.getAttribute("aria-label").split(" — ").pop();
    assert.equal(word(dome), word(body), `${JSON.stringify(mark)} is one word on both`);
    assert.ok(Object.values(BodyView.LEGEND_TEXT).includes(word(body)), "and it is a legend word");
  });
});

test("a holoprojector never draws an open or closed state, whatever it is handed", () => {
  const { host, mount } = boot();
  const drawing = mount();

  ["dome-hp1", "dome-hp2", "dome-hp3"].forEach((id) => {
    assert.equal(drawing.markerOf(id).panTilt, true, `${id} is a pan and tilt device`);
    drawing.update({ kind: "live", marks: { [id]: { mark: "openable", at: 1 } } });
    assert.deepEqual(stateOf(markerOf(host, id)), [], `${id} draws no state`);
  });
  // A pie beside them is an ordinary openable piece.
  assert.equal(drawing.markerOf("dome-pp3").panTilt, false);
});

// ---------------------------------------------------------------------------
// Color lives in the stylesheet's palette
// ---------------------------------------------------------------------------

test("the droid's line art carries no color, stroke or fill of its own", () => {
  const { window } = boot();

  ["front", "rear"].forEach((face) => {
    const art = window.BodyArt[face];
    assert.ok(art.includes("<path"), `the ${face} art is there`);
    assert.doesNotMatch(art, /#[0-9a-f]{3,8}\b|rgba?\(/i, `the ${face} art spends a color literal`);
    assert.doesNotMatch(art, /\s(stroke|fill|style|class)=/, `the ${face} art paints itself`);
  });
});

// ---------------------------------------------------------------------------
// A mark it does not know is a caller's defect
// ---------------------------------------------------------------------------

test("a mark the renderer does not draw is reported, never swallowed into 'not said yet'", () => {
  const { host, errors, mount } = boot();
  const drawing = mount();

  drawing.update({ kind: "live", marks: { doorFL: { mark: "planned" } } });
  assert.equal(errors.length, 1, "the caller's defect reaches the console");
  assert.match(errors[0], /doorFL/);
  assert.match(errors[0], /planned/);
  assert.equal(markerOf(host, "doorFL").classList.contains("is-planned"), false);
});

// ---------------------------------------------------------------------------
// A click only selects, and the view never writes
// ---------------------------------------------------------------------------

test("a click reports the pick and asks the droid for nothing", () => {
  const { host, mount } = boot();
  const picks = [];
  const drawing = mount({ onPick: (id) => picks.push(id) });

  const svgOf = (id) => host.querySelectorAll(".bv-svg").find((svg) => markerOf(svg, id) !== undefined);
  svgOf("chargebay").fire("click", { target: markerOf(host, "chargebay") });
  svgOf("dome-pp4").fire("click", { target: markerOf(host, "dome-pp4") });
  assert.deepEqual(picks, ["chargebay", "dome-pp4"]);
  // Nothing was selected by the click itself: selecting is the caller's, which
  // is what makes a pick a report rather than a state the drawing keeps.
  assert.equal(drawing.selected(), null);
});

test("a repaint keeps the pick a builder is holding", () => {
  const { host, mount } = boot();
  const drawing = mount();

  drawing.select("doorFR");
  drawing.update({ kind: "live", marks: { doorFR: { mark: "openable", at: 1 } } });
  assert.equal(drawing.selected(), "doorFR");
  assert.ok(markerOf(host, "doorFR").classList.contains("is-selected"));
});

// ---------------------------------------------------------------------------
// The selection panel: named buttons, and a no that names the next move
// ---------------------------------------------------------------------------

const ACTS = [
  { id: "toggle", label: "Open it" },
  { id: "wire", label: "Give it an output" },
];

test("an act that cannot run is refused and says why, rather than vanishing", () => {
  const { panelHost, BodyView } = boot();
  const pressed = [];
  const panel = BodyView.mountPanel(panelHost, { acts: ACTS, onAct: (id) => pressed.push(id) });

  panel.show({
    title: "Small long door",
    facts: [{ term: "Servo", value: "No output mapped" }],
    acts: { toggle: { enabled: false }, wire: { enabled: true } },
    why: "No output mapped. Give it one first.",
  });

  const button = (id) => panelHost.querySelectorAll("[data-act]").find((node) => node.dataset.act === id);
  assert.equal(button("toggle").disabled, true);
  assert.equal(button("toggle").hidden, false, "a refused act stays on the surface");
  assert.equal(button("toggle").getAttribute("aria-disabled"), "true");
  assert.match(panelHost.querySelector(".bodyview-panel-why").textContent, /Give it one first/);

  panelHost.querySelector(".bodyview-panel-acts").fire("click", { target: button("toggle") });
  assert.deepEqual(pressed, []);
  panelHost.querySelector(".bodyview-panel-acts").fire("click", { target: button("wire") });
  assert.deepEqual(pressed, ["wire"]);
});

test("clearing the panel refuses every act, so nothing is offered with nothing picked", () => {
  const { panelHost, BodyView } = boot();
  const panel = BodyView.mountPanel(panelHost, { acts: ACTS });

  panel.show({ title: "Left body door", facts: [], acts: { toggle: { enabled: true } }, why: "" });
  panel.clear();

  panelHost.querySelectorAll("[data-act]").forEach((node) => {
    assert.equal(node.disabled, true, `${node.dataset.act} is refused with nothing picked`);
  });
  assert.equal(panelHost.querySelector(".bodyview-panel-title").textContent, "");
});
