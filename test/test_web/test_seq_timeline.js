// =============================================================================
// test/test_web/test_seq_timeline.js
//
// A saved sequence read as time, on the Sequences surface (data/seq_timeline.js,
// ADR 0062, #440).
//
// The invariant: moving the marker is silent. A press on the ruler, a drag and
// the arrow keys move it, and the picture and the readout follow it to that
// moment of the routine - and nothing reaches the droid. The droid moves only
// on a separate, deliberate press; nothing follows a dragging finger (ADR
// 0062: "it streams positions at the speed of a finger, and the first move on
// an uncalibrated part is a jump rather than a ramp"). And that press is one
// request carrying the name and the instant, never a pose the page worked out:
// the pose and its pace are the firmware's (include/sequence_pose.h).
//
// The harness runs the shipped chain data/seq.html declares, opens the
// timeline through the Factory card's own Timeline button, and records every
// request. The view is a real mini_dom tree, so the ruler it binds and the
// window listeners a drag adds are the ones the page really holds; the moment
// is checked in the readout, so a marker that did not move cannot pass.
// Per test/test_web/README.md: executed, not pattern-matched.
// =============================================================================
import test from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { createRequire } from "node:module";

import { MiniDocument } from "./helpers/mini_dom.js";

const require = createRequire(import.meta.url);
const { shippedWords } = require("./helpers/shipped_words.cjs");

const __dirname = dirname(fileURLToPath(import.meta.url));
const read = (name) => readFileSync(join(__dirname, "../../data", name), "utf-8");

// The script chain data/seq.html declares, from the page's own modules on.
const PAGE_MODULES = [
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
  const timelineView = mini.createElement("div");
  mini.body.appendChild(timelineView);

  // The Factory card and its Timeline button, handed back the way the browser
  // would find them in the markup renderListView() just wrote.
  const cardFeedback = stub();
  const card = stub({ querySelector: (selector) => (selector === ".seq-card-test-feedback" ? cardFeedback : null) });
  const timelineButton = stub({ dataset: { action: "timeline", builtinName: ROUTINE.name }, closest: () => card });

  const elements = new Map([["seq-timeline-view", timelineView]]);
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, stub());
    return elements.get(id);
  };
  byId("seq-cards-container").querySelectorAll = (selector) =>
    selector === '[data-action="timeline"]' ? [timelineButton] : [];

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
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
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
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    Promise,
    console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  PAGE_MODULES.forEach((name) => vm.runInContext(read(name), sandbox, { filename: name }));

  const settle = async (turns = 12) => {
    for (let i = 0; i < turns; i += 1) await new Promise((resolve) => setImmediate(resolve));
  };
  return {
    requests,
    writes,
    timelineView,
    timelineButton,
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
  ["seq_protocol_check.js", "servo_motion.js", "seq_rehearsal.js", "droid_parts.js", "dome_command_map.js", "seq_timeline.js"]
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
