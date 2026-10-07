// =============================================================================
// test/test_web/test_seq_timeline.js
//
// A sequence read as time, on the Sequences surface (data/seq_timeline.js,
// ADR 0062, #440), and edited there (ADR 0057, #441).
//
// The first invariant: moving the marker is silent. A press on the ruler, a drag and
// the arrow keys move it, and the picture and the readout follow it to that
// moment of the routine - and nothing reaches the droid. The droid moves only
// on a separate, deliberate press; nothing follows a dragging finger (ADR
// 0062: "it streams positions at the speed of a finger, and the first move on
// an uncalibrated part is a jump rather than a ramp"). And that press is one
// request carrying the name and the instant, never a pose the page worked out:
// the pose and its pace are the firmware's (include/sequence_pose.h).
//
// The harness runs the shipped chain data/seq.html declares, opens the
// timeline through the Factory row's own Timeline act, and records every
// request. The view is a real mini_dom tree, so the ruler it binds and the
// window listeners a drag adds are the ones the page really holds; the moment
// is checked in the readout, so a marker that did not move cannot pass.
//
// The second: a drag on the timeline edits the one sequence the editor holds.
// The block is written into that object as it moves, lands on a neighbour's
// edge inside a tolerance that is a time, is one entry in the editor's one
// history whatever it passed on the way, and what Save sends is in the time
// order Protocol Check accepts. A press that moves nothing is no edit.
//
// The third: what is dragged in from the library, and what is changed in the
// Picked block tab, are edits on that same history. A drop is one entry
// however many steps it makes, lands by the timeline's own tolerance and never
// outside the lanes; an inspector edit that changes nothing is no entry; and
// neither leaves the routine in a state the droid would refuse where the
// surface offered the choice (a flutter keeps the close it owes, Same pick
// needs a pick before it, a loop keeps the steps it takes at their moments).
//
// The fourth: a Factory sequence's stage is read-only. The builder has not made
// the sequence theirs, so nothing on its stage can be taken hold of and a drag
// across a block leaves the routine exactly as the droid sent it; Tune is the
// way into the editor.
// Per test/test_web/README.md: executed, not pattern-matched.
// =============================================================================
import test from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { createRequire } from "node:module";
import { webcrypto } from "node:crypto";

import { MiniDocument } from "./helpers/mini_dom.js";

const require = createRequire(import.meta.url);
const { shippedWords } = require("./helpers/shipped_words.cjs");
import { operatorShellUi } from "./helpers/page_module_env.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const read = (name) => readFileSync(join(__dirname, "../../data", name), "utf-8");

// PART 1 of data/page_bootstrap.js publishes window.PASurface, which the run
// watch and the editor hold the surface's unmount through (#441, #451).
const bootstrapSrc = read("page_bootstrap.js");
const bootstrapPart1 = bootstrapSrc.substring(
  bootstrapSrc.indexOf("(() => {"),
  bootstrapSrc.indexOf("// =========================== PART 2"),
);

// The script chain data/seq.html declares, from the page's own modules on.
const PAGE_MODULES = [
  // Escape and the question every surface asks with (#456).
  "overlay.js",
  // The status stream and the run watch data/seq.js reads from the Live
  // Reading (#451). Loaded, not started: starting it is the Operator Shell's
  // call, and no frame reaches these tests.
  "status_stream.js",
  "live_reading.js",
  "dome_lights.js",
  "seq_protocol_check.js",
  "seq_tempo.js",
  "seq_gesture.js",
  "servo_motion.js",
  "seq_rehearsal.js",
  "droid_parts.js",
  "droid_part_kind.js",
  "outputs.js",
  "droid_build.js",
  "dome_command_map.js",
  "dome_panel_model.js",
  "body_art.js",
  "body_view.js",
  "parts_mapping.js",
  "droid_picture.js",
  "seq_timeline.js",
  "seq.js",
];

const escapeHtml = (value) =>
  String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

// A loop that opens ring panel 1 at the start of each pass and closes it half
// way through, then a pie opened and never closed. Pass three opens P1 at
// 2100 ms and closes it at 2600 ms.
const ROUTINE = {
  name: "DM:PASSES",
  toggleGroup: "none",
  suppressMs: 4000,
  steps: [
    { t: 100, type: "loop", body: 2, periodMs: 1000, durationMs: 3000 },
    { t: 0, type: "dome", cmd: ":OP01" },
    { t: 500, type: "dome", cmd: ":CL01" },
    { t: 3200, type: "dome", cmd: ":OPP1" },
    { t: 3600, type: "end" },
  ],
};

// A stub element for the parts of the page this test does not look at.
function stub(extra = {}) {
  const element = {
    dataset: {},
    style: {},
    value: "",
    innerHTML: "",
    textContent: "",
    className: "",
    listeners: {},
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    setAttribute() {},
    getAttribute: () => null,
    addEventListener(name, fn) {
      (element.listeners[name] = element.listeners[name] || []).push(fn);
    },
    removeEventListener() {},
    appendChild: (child) => child,
    insertAdjacentHTML() {},
    remove() {},
    focus() {},
    closest: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    ...extra,
  };
  return element;
}

function openPage() {
  const requests = [];
  const writes = [];
  const mini = new MiniDocument();
  const real = () => mini.body.appendChild(mini.createElement("div"));
  const timelineView = real();
  // Where the workspace has the timeline of the sequence being edited drawn -
  // the bar over it, its lanes, the droid beside it - and the drawer's Picked
  // block pane: real nodes, found by the ids the editor's own markup gives them.
  const editorBar = real();
  const editorTimeline = real();
  const editorDroid = real();
  const pickedPane = real();
  // The library: the Parts tab's list, which the pills are taken hold of on,
  // and the two groups its pills are written into.
  const partsList = real();
  const libraryParts = real();
  const libraryKinds = real();

  // The Factory row and its Timeline act, handed back the way the browser
  // would find them in the markup renderListView() just wrote.
  const cardFeedback = stub();
  const card = stub({ querySelector: (selector) => (selector === ".seq-item-feedback" ? cardFeedback : null) });
  const timelineButton = stub({ dataset: { action: "timeline", builtinName: ROUTINE.name }, closest: () => card });

  const elements = new Map([
    ["seq-timeline-view", timelineView],
    ["seq-editor-tlbar", editorBar],
    ["seq-editor-timeline", editorTimeline],
    ["seq-editor-droid", editorDroid],
    ["seq-picked", pickedPane],
    ["seq-editor-parts", partsList],
    ["seq-lib-parts", libraryParts],
    ["seq-lib-kinds", libraryKinds],
  ]);
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, stub());
    return elements.get(id);
  };
  byId("seq-cards-container").querySelectorAll = (selector) =>
    selector === "[data-action]" ? [timelineButton] : [];

  const answer = (url) => {
    requests.push(url);
    if (url === "/api/seq/list") return Promise.resolve({ ok: true, data: [] });
    if (url === "/api/seq/builtins") {
      return Promise.resolve({ ok: true, data: [{ name: ROUTINE.name, toggleGroup: "none", suppressMs: 4000, stepCount: 5 }] });
    }
    if (url.startsWith("/api/seq/builtins?name=")) return Promise.resolve({ ok: true, data: ROUTINE });
    if (url === "/api/servo/outputs") return Promise.resolve({ ok: true, status: 200, data: { outputs: [] } });
    if (url.startsWith("/api/config")) return Promise.resolve({ ok: true, status: 200, data: { components: {} } });
    return Promise.resolve({ ok: false, status: 404, data: null });
  };
  const write = (method) => (url, body) => {
    writes.push(`${method} ${url} ${JSON.stringify(body ?? null)}`);
    return Promise.resolve({ ok: true, data: {} });
  };

  const windowListeners = {};
  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      ...shippedWords(),
      get: answer,
      postJson: write("POST"),
      postForm: write("POST"),
      estopPostForm: write("POST"),
      request: (url, opts = {}) => write(opts.method || "GET")(url, opts.body),
      messageFor: (error) => String(error && error.message),
    },
    // The act the pages draw their buttons with, from the shipped shell (#460).
    PAUi: operatorShellUi(),
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {}, removeItem() {} },
    document: {
      readyState: "complete",
      body: mini.body,
      documentElement: mini.documentElement,
      getElementById: byId,
      querySelector: () => null,
      querySelectorAll: () => [],
      createElement: (tag) => mini.createElement(tag),
      addEventListener() {},
      removeEventListener() {},
    },
    addEventListener(type, fn) {
      (windowListeners[type] = windowListeners[type] || []).push(fn);
    },
    removeEventListener(type, fn) {
      windowListeners[type] = (windowListeners[type] || []).filter((each) => each !== fn);
    },
    crypto: webcrypto, // the browser's own, which the editor mints ids with
    AbortController, // the browser's own; the editor cancels a read it leaves
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    Promise,
    console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(bootstrapPart1, sandbox, { filename: "page_bootstrap.part1.js" });
  PAGE_MODULES.forEach((name) => vm.runInContext(read(name), sandbox, { filename: name }));

  const settle = async (turns = 12) => {
    for (let i = 0; i < turns; i += 1) await new Promise((resolve) => setImmediate(resolve));
  };
  return {
    requests,
    writes,
    timelineView,
    editorTimeline,
    editorBar,
    pickedPane,
    partsList,
    libraryParts,
    libraryKinds,
    timelineButton,
    byId,
    seam: sandbox.__seqEditorForTesting,
    settle,
    fireWindow: (type, event) => (windowListeners[type] || []).slice().forEach((fn) => fn(event)),
  };
}

test("moving the marker sends nothing; the press sends one request naming the sequence and the instant", async () => {
  const page = openPage();
  await page.settle();
  (page.timelineButton.listeners.click || []).forEach((fn) => fn());
  await page.settle();

  assert.ok(page.requests.includes(`/api/seq/builtins?name=${encodeURIComponent(ROUTINE.name)}`),
    "the Factory sequence was read from the Factory route");
  const ruler = page.timelineView.querySelector(".tl-ruler");
  assert.ok(ruler, "the timeline drew no ruler");
  const readout = page.timelineView.querySelector(".tl-readout");
  const openAt = () => /Open(.*?)(?:Sound|$)/.exec(readout.textContent)?.[1];

  // The ruler spans 1000 px; a press at x lands on x/1000 of the window.
  const windowMs = Number(ruler.getAttribute("aria-valuemax"));
  ruler.getBoundingClientRect = () => ({ left: 0, width: 1000, top: 0, height: 20 });
  const xAt = (ms) => (ms / windowMs) * 1000;
  const writesBefore = page.writes.length;

  // A press jumps: pass three has P1 open.
  ruler.fire("pointerdown", { clientX: xAt(2300), preventDefault() {} });
  assert.equal(openAt(), "P1");

  // A drag scrubs, through the window's own listeners: after pass three's close.
  page.fireWindow("pointermove", { clientX: xAt(2800) });
  assert.equal(openAt(), "none");
  page.fireWindow("pointerup", {});

  // The arrow keys step it: back into pass three, then on past the pie's open,
  // which nothing closes.
  for (let i = 0; i < 5; i += 1) ruler.fire("keydown", { key: "ArrowLeft", preventDefault() {} });
  assert.equal(openAt(), "P1");
  ruler.fire("keydown", { key: "End", preventDefault() {} });
  assert.equal(openAt(), "PP1");

  await page.settle();
  assert.deepEqual(page.writes.slice(writesBefore), [], "moving the marker sent something to the droid");
  assert.deepEqual(page.writes, [], "opening the timeline sent something to the droid");

  // The press, at the marker's instant: one request, name and instant only.
  const press = page.timelineView.querySelector('[data-tl-act="pose"]');
  assert.ok(press, "the timeline has no pose press");
  page.timelineView.querySelector(".tl-bar").fire("click", { target: press });
  await page.settle();
  assert.deepEqual(page.writes, [`POST /api/seq/pose ${JSON.stringify({ name: ROUTINE.name, t: 3600 })}`]);
});

// One reading of the routine: a step placed on a beat is drawn where the droid
// runs it, at the millisecond its beat resolves to, even when the `t` written
// beside it is stale (ADR 0058, #438). Otherwise the pose a builder sends from
// this view is a moment the run never reaches.
test("a step on a beat is drawn where its beat falls, not at a stale time", () => {
  const sandbox = { window: {}, console };
  sandbox.window.window = sandbox.window;
  vm.createContext(sandbox);
  ["dome_lights.js", "seq_protocol_check.js", "servo_motion.js", "seq_rehearsal.js", "droid_parts.js", "dome_command_map.js", "seq_timeline.js"]
    .forEach((name) => vm.runInContext(`(function(window){${read(name)}}).call(window, window)`, sandbox, { filename: name }));
  const model = sandbox.window.SeqTimeline.build({
    name: "DM:ONBEAT",
    toggleGroup: "none",
    suppressMs: 4000,
    tempo: { bpm: 120, phase: 0, barLen: 4, barPhase: 0, source: "typed", confidence: 1 },
    steps: [
      { t: 0, beat: 4, type: "dome", cmd: ":OP01" },
      { t: 3000, type: "end" },
    ],
  });
  const panel = model.parts.find((lane) => lane.part === "panel1");
  assert.ok(panel, "the panel the step opens has no lane");
  assert.equal(panel.changes[0].t, 2000);
});

// A dome turn, then a ring panel opened and closed, with room either side.
const EDITED = {
  name: "DM:DRAGGED",
  toggleGroup: "none",
  suppressMs: 8000,
  steps: [
    { t: 0, type: "audio", cmd: "$H" },
    { t: 500, type: "domeRotate", speedPct: 40, durationMs: 1000 },
    { t: 2000, type: "dome", cmd: ":OP01" },
    { t: 3000, type: "dome", cmd: ":CL01" },
    { t: 4000, type: "end" },
  ],
};

test("a drag on the timeline is one edit to the sequence the editor saves", async () => {
  const page = openPage();
  await page.settle();
  page.seam.renderEditorView(JSON.parse(JSON.stringify(EDITED)));
  (page.byId("seq-editor-show-timeline").listeners.click || []).forEach((fn) => fn());

  const view = page.editorTimeline;
  assert.ok(view.querySelector(".tl-grid"), "the editor drew no timeline");
  const steps = () => Array.from(page.seam.editorState.current.steps, (step) => [step.type, step.t]);
  const turn = () => page.seam.editorState.current.steps.find((step) => step.type === "domeRotate");

  // The track is 1000 px wide. The drawing is rebuilt as a block moves, so the
  // ruler and the block are looked up, and given their boxes, at each press.
  const press = (clientX, extra = {}) => {
    const ruler = view.querySelector(".tl-ruler");
    const windowMs = Number(ruler.getAttribute("aria-valuemax"));
    ruler.getBoundingClientRect = () => ({ left: 0, width: 1000 });
    const block = view.querySelector('[data-lane="spin"]').querySelector(".tl-item");
    const left = (turn().t / windowMs) * 1000;
    const width = (turn().durationMs / windowMs) * 1000;
    block.getBoundingClientRect = () => ({ left, right: left + width, width });
    const at = left + width / 2;
    view.querySelector(".tl-grid").fire("pointerdown", { target: block, clientX: at + (clientX || 0), preventDefault() {}, ...extra });
    return { at, pxPerMs: 1000 / windowMs };
  };

  // Dragged 1560 ms: its start comes within 60 ms of the panel's open at
  // 2000 ms and lands on it, and the sequence the editor holds has moved
  // before the pointer is let go.
  let held = press();
  page.fireWindow("pointermove", { clientX: held.at + 1560 * held.pxPerMs });
  assert.equal(turn().t, 2000, "the block did not land on the edge it came within the tolerance of");
  assert.ok(view.querySelector(".tl-snap"), "nothing shows where the drag will land");
  page.fireWindow("pointerup", {});
  assert.equal(page.byId("seq-editor-undo").disabled, false);

  // A press that moves nothing only selects.
  press();
  page.fireWindow("pointerup", {});

  // An Undo asked for with a block still held does nothing: the drag's writes
  // are in the routine and not yet an entry, and the steps a restore would put
  // back are not the ones the drag is holding. Taking the view down under a
  // drag puts the block back where the press found it.
  held = press();
  page.fireWindow("pointermove", { clientX: held.at + 450 * held.pxPerMs });
  (page.byId("seq-editor-undo").listeners.click || []).forEach((fn) => fn());
  assert.equal(turn().t, 2450, "Undo ran under a drag");
  (page.byId("seq-editor-show-steps").listeners.click || []).forEach((fn) => fn());
  assert.equal(turn().t, 2000, "a drag torn down mid-gesture left its half-made move in the routine");
  (page.byId("seq-editor-show-timeline").listeners.click || []).forEach((fn) => fn());

  // Dragged on past the open, out of reach of any edge: it stays where it is put.
  held = press();
  page.fireWindow("pointermove", { clientX: held.at + 450 * held.pxPerMs });
  page.fireWindow("pointerup", {});
  assert.equal(turn().t, 2450);

  // One Undo is the last drag and nothing else: the press between the two
  // drags left no entry behind.
  (page.byId("seq-editor-undo").listeners.click || []).forEach((fn) => fn());
  assert.equal(turn().t, 2000, "one Undo did not take back exactly the last drag");
  (page.byId("seq-editor-redo").listeners.click || []).forEach((fn) => fn());

  // The turn now starts after the open, so it follows it in the step list:
  // Protocol Check refuses a step timed before the one above it.
  assert.deepEqual(steps(), [["audio", 0], ["dome", 2000], ["domeRotate", 2450], ["dome", 3000], ["end", 4000]]);
  (page.byId("seq-editor-save").listeners.click || []).forEach((fn) => fn());
  await page.settle();
  const saved = page.writes.filter((write) => write.startsWith("POST /api/seq {"));
  assert.equal(saved.length, 1, "Save did not send the sequence the timeline edited");
  assert.deepEqual(
    JSON.parse(saved[0].slice("POST /api/seq ".length)).steps.map((step) => [step.type, step.t]),
    [["audio", 0], ["dome", 2000], ["domeRotate", 2450], ["dome", 3000], ["end", 4000]],
  );
});

// The Picked block tab draws where the block starts as a number a builder can
// type over. A field the editor draws and never reads back is the fault #434
// was (test_seq_step_edits.js): the number on screen changes and Save sends the
// old one. Typed, it is the same edit a drag of the block makes, on the same
// history.
test("a start typed for the picked block moves it, as one edit that Undo takes back", async () => {
  const page = openPage();
  await page.settle();
  page.seam.renderEditorView(JSON.parse(JSON.stringify(EDITED)));

  const view = page.editorTimeline;
  const turn = () => page.seam.editorState.current.steps.find((step) => step.type === "domeRotate");
  const startField = () => page.pickedPane.querySelector('[data-picked="start"]');
  assert.equal(startField(), null, "the fixture: nothing is picked when a sequence opens");

  // A press on the dome turn, let go where it is: it is picked, and no edit.
  const pick = () => {
    view.querySelector(".tl-ruler").getBoundingClientRect = () => ({ left: 0, width: 1000 });
    const block = view.querySelector('[data-lane="spin"]').querySelector(".tl-item");
    block.getBoundingClientRect = () => ({ left: 100, right: 300, width: 200 });
    view.querySelector(".tl-grid").fire("pointerdown", { target: block, clientX: 200, preventDefault() {} });
    page.fireWindow("pointerup", {});
  };
  pick();
  assert.ok(startField(), "the picked block shows no start to type over");
  assert.equal(startField().getAttribute("value"), "500");
  assert.equal(page.byId("seq-editor-undo").disabled, true, "picking a block was recorded as an edit");

  // The builder types a new start, as the browser delivers it.
  const typed = startField();
  typed.value = "1200";
  page.pickedPane.fire("change", { target: typed });
  assert.equal(turn().t, 1200, "the typed start never reached the routine");
  assert.equal(startField().getAttribute("value"), "1200", "the tab still shows the old start");

  (page.byId("seq-editor-undo").listeners.click || []).forEach((fn) => fn());
  assert.equal(turn().t, 500, "one Undo did not take the typed start back");

  // A start past where the block may go is held at its limit, the end of the
  // routine - and typed again there it moves nothing. The field still shows
  // where the block is, never a number that was not applied. (The Undo put
  // other steps in the routine, so the block is picked again first.)
  pick();
  for (let i = 0; i < 2; i += 1) {
    const past = startField();
    past.value = "99999";
    page.pickedPane.fire("change", { target: past });
    assert.equal(turn().t, 4000, "the block went past the end of the routine");
    assert.notEqual(startField(), past, "the field was left holding a start that was not applied");
    assert.equal(startField().getAttribute("value"), "4000");
  }
});

// The timeline holds what is picked as the steps themselves, and putting a
// step on or off a beat writes a new step in its place (the time is resolved
// again from the tempo). An edit made from the Picked block tab must leave
// the tab on the block it edited, or the one press empties it.
test("taking the picked block off its beat leaves it picked", async () => {
  const page = openPage();
  await page.settle();
  page.seam.renderEditorView({
    ...JSON.parse(JSON.stringify(EDITED)),
    tempo: { bpm: 120, phase: 0, barLen: 4, barPhase: 0, source: "typed", confidence: 1 },
    steps: [
      { t: 0, type: "audio", cmd: "$H" },
      { t: 500, beat: 1, type: "domeRotate", speedPct: 40, durationMs: 1000 },
      { t: 4000, type: "end" },
    ],
  });

  const view = page.editorTimeline;
  const turn = () => page.seam.editorState.current.steps.find((step) => step.type === "domeRotate");
  view.querySelector(".tl-ruler").getBoundingClientRect = () => ({ left: 0, width: 1000 });
  const block = view.querySelector('[data-lane="spin"]').querySelector(".tl-item");
  block.getBoundingClientRect = () => ({ left: 100, right: 300, width: 200 });
  view.querySelector(".tl-grid").fire("pointerdown", { target: block, clientX: 200, preventDefault() {} });
  page.fireWindow("pointerup", {});

  const offBeat = page.pickedPane.querySelector('[data-picked="off-beat"]');
  assert.ok(offBeat, "a block on a beat offers no way off it");
  page.pickedPane.fire("click", { target: offBeat });

  assert.equal(turn().beat, undefined, "the step is still on its beat");
  assert.equal(turn().t, 500, "taking a step off its beat moved it");
  assert.ok(page.pickedPane.querySelector('[data-picked="start"]'), "the edit emptied the Picked block tab");
  assert.equal(page.pickedPane.querySelector('[data-picked="off-beat"]'), null, "the tab still offers Off the beat");
  assert.ok(
    view.querySelectorAll("[data-edit]").some((node) => node.getAttribute("data-edit").includes("is-selected")),
    "the block is no longer drawn as picked",
  );
});

// ---------------------------------------------------------------------------
// The library and the inspector (#441). The track is 1000 px wide and the
// lanes 400 px tall; the timeline draws its ruler again whenever the routine
// changes, so the boxes are given again before each pointer event that reads
// them.
// ---------------------------------------------------------------------------
function workspace(sequence) {
  const page = openPage();
  page.seam.renderEditorView(JSON.parse(JSON.stringify(sequence)));
  const view = page.editorTimeline;
  const boxes = () => {
    view.querySelector(".tl-ruler").getBoundingClientRect = () => ({ left: 0, right: 1000, width: 1000 });
    view.querySelector(".tl-grid").getBoundingClientRect = () => ({ top: 0, bottom: 400 });
  };
  const xAt = (ms) => (ms / Number(view.querySelector(".tl-ruler").getAttribute("aria-valuemax"))) * 1000;
  const steps = () => page.seam.editorState.current.steps;
  const press = (id) => (page.byId(id).listeners.click || []).forEach((fn) => fn());
  return {
    page,
    view,
    steps,
    // The routine in short. Array.from: the steps are the page's own array,
    // from another realm than the one these assertions compare in.
    brief: () => Array.from(steps(), (step) => [step.t, step.cmd || step.type]),
    undo: () => press("seq-editor-undo"),
    undoOff: () => page.byId("seq-editor-undo").disabled,
    saveOff: () => page.byId("seq-editor-save").disabled,
    said: () => page.editorBar.querySelector(".tl-said").textContent,
    // Take hold of a library pill and let it go at `ms` on the timeline, `y`
    // px down from the top of the lanes; with no `ms`, let it go where it was
    // pressed.
    drag(lib, ms = null, y = 200) {
      const group = lib.startsWith("kind:") ? page.libraryKinds : page.libraryParts;
      const pill = group.querySelector(`[data-lib="${lib}"]`);
      assert.ok(pill, `the library offers no ${lib}`);
      page.partsList.fire("pointerdown", { target: pill, clientX: 300, clientY: 700, pointerId: 1 });
      if (ms !== null) {
        boxes();
        page.fireWindow("pointermove", { clientX: xAt(ms), clientY: y, pointerId: 1 });
        boxes();
      }
      page.fireWindow("pointerup", { clientX: ms === null ? 300 : xAt(ms), clientY: ms === null ? 700 : y, pointerId: 1 });
    },
    // Press a block and let it go where it is: the `nth` item of `kind` on a lane.
    pick(lane, kind, nth = 0) {
      boxes();
      const block = view.querySelector(`[data-lane="${lane}"]`).querySelectorAll(`.tl-${kind}`)[nth];
      assert.ok(block, `no ${kind} block on the ${lane} lane`);
      block.getBoundingClientRect = () => ({ left: 100, right: 300, width: 200 });
      view.querySelector(".tl-grid").fire("pointerdown", { target: block, clientX: 200, preventDefault() {} });
      page.fireWindow("pointerup", {});
    },
    field: (name) => page.pickedPane.querySelector(`[data-picked="${name}"]`),
    choice: (name, value) =>
      page.pickedPane.querySelectorAll(`[data-picked="${name}"]`).find((button) => button.getAttribute("data-value") === value) || null,
    // Type into an inspector field, or press one of its choices, as the
    // browser delivers each.
    type(name, value) {
      const input = page.pickedPane.querySelector(`[data-picked="${name}"]`);
      input.value = String(value);
      page.pickedPane.fire("change", { target: input });
    },
    choose(name, value) {
      const button = page.pickedPane.querySelectorAll(`[data-picked="${name}"]`).find((each) => each.getAttribute("data-value") === value);
      assert.ok(button, `the inspector offers no ${name} "${value}"`);
      page.pickedPane.fire("click", { target: button });
    },
    // Move a fader through `values` and let it go.
    slide(name, values) {
      const fader = page.pickedPane.querySelector(`[data-picked="${name}"]`);
      values.forEach((value) => {
        fader.value = String(value);
        page.pickedPane.fire("input", { target: fader });
      });
      page.pickedPane.fire("change", { target: fader });
    },
  };
}

test("a Part dragged from the library lands as one edit, and one Undo takes the whole drop away", async () => {
  const w = workspace(EDITED);
  const before = w.brief();
  assert.equal(w.view.querySelector('[data-lane="pie1"]'), null, "the fixture: no step names the pie yet");

  // A press that goes nowhere adds nothing, and neither does a pill let go
  // off the lanes: the routine is as it was and there is nothing to undo.
  w.drag("part:pie1");
  w.drag("part:pie1", 3460, 900);
  assert.deepEqual(w.brief(), before, "a pill that was not dropped on the lanes changed the routine");
  assert.equal(w.undoOff(), true, "a drop that landed nothing was recorded as an edit");

  // Dropped on the lanes at 3460 ms, out of reach of any edge: the pie opens
  // there and closes a second on - held at the end, which is at 4000 ms - on
  // a lane of its own, and the droid would accept the routine.
  w.drag("part:pie1", 3460);
  assert.deepEqual(w.brief(), [[0, "$H"], [500, "domeRotate"], [2000, ":OP01"], [3000, ":CL01"], [3460, ":OPP1"], [4000, ":CLP1"], [4000, "end"]]);
  assert.ok(w.view.querySelector('[data-lane="pie1"]'), "the dropped Part has no lane");
  assert.equal(w.saveOff(), false, "the drop left a routine the droid would refuse");
  assert.ok(w.field("runs"), "the dropped block is not the one the inspector is on");

  w.undo();
  assert.deepEqual(w.brief(), before, "one Undo did not take the whole drop away");
  assert.equal(w.undoOff(), true, "the drop was more than one entry");

  // Within the tolerance of a block's edge it lands on the edge, as a dragged
  // block does: 60 ms past the panel's open at 2000 ms.
  w.drag("part:pie1", 2060);
  assert.equal(w.steps().find((step) => step.cmd === ":OPP1").t, 2000, "the drop did not land on the edge it came within the tolerance of");
});

test("an inspector edit that changes nothing records nothing, and one that does is one Undo", async () => {
  const w = workspace(EDITED);
  w.pick("panel1", "open");
  const close = () => w.steps().find((step) => step.cmd === ":CL01");
  const open = () => w.steps().find((step) => step.cmd === ":OP01");
  assert.equal(w.field("runs").getAttribute("value"), "1000");

  // The number it already has, typed again.
  w.type("runs", 1000);
  assert.equal(w.undoOff(), true, "typing the value a field already had was recorded as an edit");

  // Runs for moves the close: the open stays where it is.
  w.type("runs", 400);
  assert.deepEqual([open().t, close().t], [2000, 2400]);
  w.undo();
  assert.equal(close().t, 3000, "one Undo did not take the typed length back");
  assert.equal(w.undoOff(), true);

  // A fader held and moved is one edit, written as it moves. The whole throw
  // is stored as absence, so a routine that never said how far saves back as
  // it was read.
  w.pick("panel1", "open");
  w.slide("howFar", [80, 60]);
  assert.equal(open().howFar, 60, "the fader never reached the routine");
  w.slide("howFar", [100]);
  assert.equal("howFar" in open(), false, "the whole throw was written into the step");
  w.undo();
  assert.equal(open().howFar, 60);
  w.undo();
  assert.equal("howFar" in open(), false, "a fader moved through two values took two Undos");
  assert.equal(w.undoOff(), true);
});

test("Flutter on a Part standing open keeps the close it owes, and is not offered where there is none", async () => {
  const w = workspace(EDITED);
  w.pick("panel1", "open");
  w.choose("motion", "flutter");
  assert.deepEqual(w.brief().slice(2, 4), [[2000, ":OF01"], [3000, ":CL01"]], "the flutter did not take the open's place and keep its close");
  assert.equal(w.saveOff(), false, "the flutter left a routine the droid would refuse");
  assert.equal(w.choice("motion", "flutter")?.getAttribute("aria-pressed"), "true", "the inspector left the block it changed");

  // Open on the flutter makes it the pair again, with its length to type.
  w.choose("motion", "open");
  assert.deepEqual(w.brief().slice(2, 4), [[2000, ":OP01"], [3000, ":CL01"]]);
  assert.ok(w.field("runs"), "the pair came back without its length");

  // An open no step closes (the droid closes a ring panel itself after the
  // end): a flutter there would never be closed, which Protocol Check refuses.
  const left = workspace({ ...EDITED, steps: EDITED.steps.filter((step) => step.cmd !== ":CL01") });
  left.pick("panel1", "open");
  assert.ok(left.choice("motion", "open"), "the fixture: the open's Motion row is drawn");
  assert.equal(left.choice("motion", "flutter"), null, "Flutter is offered on an open with no close to keep");
});

test("Same pick is offered only to a Random Flutter that has a pick before it", async () => {
  const random = (t) => ({ t, type: "random", set: "ring", mode: "flutter", moveMs: 300, jitterMs: 0, distinct: true });
  const w = workspace({ ...EDITED, steps: [random(500), random(1500), { t: 4000, type: "end" }] });

  w.pick("panel1", "maybe", 0);
  assert.ok(w.choice("set", "ring"), "the fixture: the first Random Flutter's set is drawn");
  assert.equal(w.choice("set", "hold"), null, "the first Random Flutter is offered a pick nothing made");

  w.pick("panel1", "maybe", 1);
  w.choose("set", "hold");
  assert.equal(w.steps()[1].set, "hold");
  assert.equal(w.saveOff(), false);
});

test("a dropped Servo Loop takes the steps that start in its first pass and leaves them at their moments", async () => {
  const w = workspace(EDITED);
  const before = w.brief();

  // At 1750 ms its first pass (1846 ms) reaches the panel's open and close.
  // They are timed from the start of a pass once repeated, so each is
  // rewritten from there and still happens when it did.
  w.drag("kind:loop", 1750);
  const loopAt = w.steps().findIndex((step) => step.type === "loop");
  const loop = w.steps()[loopAt];
  assert.equal(loop?.t, 1750, "the loop did not land where it was dropped");
  assert.equal(loop.body, 2, "the loop does not repeat the steps that start in its first pass");
  assert.deepEqual(
    Array.from(w.steps().slice(loopAt + 1, loopAt + 3), (step) => [loop.t + step.t, step.cmd]),
    [[2000, ":OP01"], [3000, ":CL01"]],
    "a step the loop took was moved by being taken",
  );
  assert.equal(w.saveOff(), false, "the dropped loop left a routine the droid would refuse");

  w.undo();
  assert.deepEqual(w.brief(), before, "one Undo did not take the loop off and put its steps back");

  // With only the end after it there is nothing to repeat: it lands nothing,
  // says so, and is no edit.
  w.drag("kind:loop", 3500);
  assert.deepEqual(w.brief(), before, "a loop with nothing to repeat changed the routine");
  assert.equal(w.undoOff(), true);
  assert.equal(w.said(), "Servo Loop needs a step after it to repeat.");
});

test("a Factory sequence's stage offers nothing that edits it", async () => {
  const page = openPage();
  await page.settle();
  const asSent = JSON.parse(JSON.stringify(ROUTINE));
  (page.timelineButton.listeners.click || []).forEach((fn) => fn());
  await page.settle();

  const view = page.timelineView;
  const blocks = view.querySelectorAll(".tl-item");
  assert.ok(blocks.length > 0, "the Factory sequence drew no blocks");
  assert.deepEqual(view.querySelectorAll("[data-edit]"), [], "a block of a Factory sequence can be taken hold of");

  // A press on a block and a drag across the stage, as a builder would try it.
  const ruler = view.querySelector(".tl-ruler");
  ruler.getBoundingClientRect = () => ({ left: 0, width: 1000 });
  const block = blocks[0];
  block.getBoundingClientRect = () => ({ left: 100, right: 300, width: 200 });
  view.querySelector(".tl-grid").fire("pointerdown", { target: block, clientX: 200, preventDefault() {} });
  page.fireWindow("pointermove", { clientX: 600 });
  page.fireWindow("pointerup", {});
  assert.deepEqual(ROUTINE, asSent, "a drag on a Factory sequence's stage changed the routine");

  // The strip carries the way back and Tune, and nothing that keeps an edit.
  const strip = view.querySelector(".seq-strip");
  assert.ok(strip.querySelector('[data-stage-act="back"]'), "the stage has no way back to the list");
  assert.equal(view.querySelector("#seq-editor-save"), null, "a read-only stage offers Save");
  await page.settle();
  assert.deepEqual(page.writes, [], "looking at a Factory sequence sent something to the droid");

  // Tune is its way into the editor: the Factory sequence is read to be edited.
  const before = page.requests.length;
  strip.fire("click", { target: strip.querySelector('[data-stage-act="tune"]') });
  await page.settle();
  assert.deepEqual(page.requests.slice(before), [`/api/seq/builtins?name=${encodeURIComponent(ROUTINE.name)}`]);
  assert.equal(page.seam.editorState.current?.name, ROUTINE.name, "Tune did not open the sequence to edit");
});
