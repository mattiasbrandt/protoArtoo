// A run started on the Sequences surface: when the page says it is running,
// and when it asks the droid about it.
//
// The droid answers POST /api/seq/test before the run has started and writes
// the run's record (GET /api/seq/last-run) only when its dispatcher takes the
// run up, so for a moment after every press the record is still the one from
// before it - and when the same sequence is run twice, that record carries
// this run's name and says it ended. A page that read it by name would end
// Running the instant it began. And the record is a multi-KB document the
// droid builds per request, so the page asks for it only while a run it
// started is under way, and not at all while another surface is on screen
// (ADR 0048).
//
// Runs the SHIPPED surface poll (PART 1 of data/page_bootstrap.js) in one
// context with the shipped data/seq.js, so the poll's start, stop and owner
// are the real ones. The once-a-second cadence is fired by hand: `tick()` runs
// what the page scheduled, and an interval the page cleared is never run. Per
// test_web/README.md: executed, not pattern-matched.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const { shippedWords } = require("./helpers/shipped_words.cjs");

const root = path.resolve(__dirname, "../..");
const read = (name) => fs.readFileSync(path.join(root, "data", name), "utf8");

const bootstrapFile = read("page_bootstrap.js");
const bootstrapPart1 = bootstrapFile.substring(
  bootstrapFile.indexOf("(() => {"),
  bootstrapFile.indexOf("// =========================== PART 2"),
);

// The script chain data/seq.html declares that data/seq.js leans on to open a
// sequence in the workspace.
const PAGE_MODULES = [
  "droid_parts.js",
  "droid_build.js",
  "dome_command_map.js",
  "dome_panel_model.js",
  "dome_layout.js",
  "seq_protocol_check.js",
  "seq_tempo.js",
  "seq_gesture.js",
  "servo_motion.js",
  "seq_rehearsal.js",
  "outputs.js",
  "seq.js",
];

const escapeHtml = (value) =>
  String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

// An element whose class list is real, so "hidden" means what it does in a
// browser.
function makeElement() {
  const classes = new Set();
  const element = {
    dataset: {},
    style: {},
    value: "",
    innerHTML: "",
    textContent: "",
    disabled: false,
    listeners: {},
    classList: {
      add: (...names) => names.forEach((name) => classes.add(name)),
      remove: (...names) => names.forEach((name) => classes.delete(name)),
      toggle: (name, force) => {
        const on = force === undefined ? !classes.has(name) : Boolean(force);
        if (on) classes.add(name);
        else classes.delete(name);
        return on;
      },
      contains: (name) => classes.has(name),
    },
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
  };
  return element;
}

const GREET = {
  name: "DM:GREET",
  suppressMs: 4000,
  toggleGroup: "none",
  steps: [
    { t: 0, type: "audio", cmd: "$H" },
    { t: 1500, type: "end" },
  ],
};

// A run's record, as src/seq_last_run_json.cpp writes the fields the page reads.
const record = (name, startMs, outcome) => ({
  valid: true,
  name,
  source: "web",
  outcome,
  running: outcome === "running",
  startMs,
  ...(outcome === "running" ? {} : { endMs: startMs + 1500 }),
});

// The Sequences surface with DM:GREET open in the workspace, on a droid whose
// last-run record is `droid.record` - the test changes it as the droid would.
function newSurface(initialRecord) {
  const droid = { record: initialRecord };
  const requests = []; // every request the page sent, in order: "GET /api/..."
  let clock = 1_000_000;
  const intervals = new Map();
  let nextInterval = 1;

  const elements = new Map();
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };
  // The markup the workspace writes hides these until a run is under way
  // (data/seq.js renderEditorView()).
  ["seq-editor-running", "seq-editor-stop", "seq-editor-test-hint"].forEach((id) => byId(id).classList.add("hidden"));

  class PageDate extends Date {
    static now() {
      return clock;
    }
  }

  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      ...shippedWords(),
      get: (url) => {
        requests.push(`GET ${url}`);
        if (url === "/api/seq/last-run") {
          return Promise.resolve({ ok: true, status: 200, data: JSON.parse(JSON.stringify(droid.record)) });
        }
        return Promise.resolve({ ok: true, status: 200, data: url.startsWith("/api/config") ? {} : [] });
      },
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      postJson: (url) => {
        requests.push(`POST ${url}`);
        return Promise.resolve({ ok: true, data: {} });
      },
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    PABootstrap: { registerSection() {}, setResourceLabels() {}, retryNow() {}, refreshSections() {} },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {}, removeItem() {} },
    location: { origin: "http://device" },
    document: {
      readyState: "complete",
      hidden: false,
      visibilityState: "visible",
      body: makeElement(),
      documentElement: makeElement(),
      getElementById: byId,
      querySelector: () => null,
      querySelectorAll: () => [],
      createElement: () => makeElement(),
      addEventListener() {},
      removeEventListener() {},
    },
    addEventListener() {},
    removeEventListener() {},
    dispatchEvent: () => true,
    CustomEvent: class { constructor(type, opts = {}) { this.type = type; this.detail = opts.detail; } },
    AbortController,
    alert() {},
    confirm: () => false,
    crypto: require("node:crypto").webcrypto,
    setTimeout: (fn, ms) => {
      const timer = setTimeout(fn, ms);
      timer.unref?.();
      return timer;
    },
    clearTimeout,
    // The cadence, held rather than run: tick() fires it.
    setInterval: (fn, ms) => {
      const id = nextInterval;
      nextInterval += 1;
      intervals.set(id, { fn, ms });
      return id;
    },
    clearInterval: (id) => intervals.delete(id),
    Promise,
    Date: PageDate,
    JSON,
    console: { log() {}, info() {}, warn() {}, error() {} },
  };
  sandbox.window = sandbox;
  sandbox.globalThis = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(bootstrapPart1, sandbox, { filename: "page_bootstrap.part1.js" });
  // The shell names the surface on screen before it runs the surface's
  // scripts, so what the surface creates in its script body is its own.
  sandbox.PASurface.showing("seq");
  PAGE_MODULES.forEach((name) => vm.runInContext(read(name), sandbox, { filename: name }));

  const settle = async (turns = 8) => {
    for (let i = 0; i < turns; i += 1) await new Promise((resolve) => setImmediate(resolve));
  };
  const press = async (id) => {
    (byId(id).listeners.click || []).forEach((fn) => fn());
    await settle();
  };

  sandbox.__seqEditorForTesting.renderEditorView(JSON.parse(JSON.stringify(GREET)));
  requests.length = 0;

  return {
    droid,
    requests,
    press,
    settle,
    shell: sandbox.PASurface,
    // A second of the page's clock, and whatever the page had scheduled for it.
    async tick(ms = 1000) {
      clock += ms;
      [...intervals.values()].forEach((interval) => interval.fn());
      await settle();
    },
    readsOfTheRecord: () => requests.filter((request) => request === "GET /api/seq/last-run").length,
    saysRunning: () => !byId("seq-editor-running").classList.contains("hidden"),
    offersTest: () => !byId("seq-editor-test").classList.contains("hidden"),
    feedback: () => byId("seq-editor-feedback").innerHTML,
  };
}

test("the record of an earlier run of the same sequence does not end Running, and this run's own ending does", async () => {
  // DM:GREET ran before, and ended: the record carries this run's name.
  const page = newSurface(record("DM:GREET", 40_000, "completed"));
  assert.equal(page.readsOfTheRecord(), 0, "the page asked about a run before any was started here");

  await page.press("seq-editor-test");
  assert.deepEqual(page.requests.slice(0, 2), ["GET /api/seq/last-run", "POST /api/seq/test"],
    "the record was not read before the run was sent");
  assert.ok(page.saysRunning(), "the page does not say the run it sent is running");

  // The dispatcher has not taken the run up yet: the droid still answers with
  // the earlier run's record.
  await page.tick();
  await page.tick();
  assert.ok(page.saysRunning(), "an earlier run's record ended this run");

  page.droid.record = record("DM:GREET", 91_000, "running");
  await page.tick();
  assert.ok(page.saysRunning());

  // Stop asks the droid; the run is over when its record says so.
  await page.press("seq-editor-stop");
  assert.equal(page.requests.at(-1), "POST /api/seq/stop");
  assert.ok(page.saysRunning(), "Running was taken down by the press, not by the droid's record");

  page.droid.record = record("DM:GREET", 91_000, "aborted");
  await page.tick();
  assert.equal(page.saysRunning(), false, "the run's own record ended and the page still says Running");
  assert.ok(page.offersTest());

  // And with nothing under way, the droid is not asked again.
  const asked = page.readsOfTheRecord();
  await page.tick();
  await page.tick();
  assert.equal(page.readsOfTheRecord(), asked, "the record is still being read after the run ended");
});

test("a run the droid accepted and never started stops reading as running, and the page says so", async () => {
  // Nothing has run since boot, and the droid refuses the run after its ok
  // (src/tasks/sequence_dispatcher.cpp: no room for the run's buffers).
  const page = newSurface({ valid: false, note: "no sequence run recorded since boot" });

  await page.press("seq-editor-test");
  assert.ok(page.saysRunning());
  await page.tick();
  assert.ok(page.saysRunning(), "the droid was given no time to take the run up");

  for (let second = 0; second < 6; second += 1) await page.tick();
  assert.equal(page.saysRunning(), false, "a run that never started still reads as running");
  assert.match(page.feedback(), /The droid did not start DM:GREET\./);

  const asked = page.readsOfTheRecord();
  await page.tick();
  assert.equal(page.readsOfTheRecord(), asked, "the record is still being read after the page gave up");
});

test("a run under way is not asked about while another surface is on screen, and is on the way back", async () => {
  const page = newSurface({ valid: false });
  await page.press("seq-editor-test");
  page.droid.record = record("DM:GREET", 5_000, "running");
  await page.tick();
  assert.ok(page.saysRunning());

  page.shell.showing("dashboard");
  const asked = page.readsOfTheRecord();
  await page.tick();
  await page.tick();
  assert.equal(page.readsOfTheRecord(), asked, "the droid was asked about the run from a surface nobody is reading");

  // It ended while the builder was elsewhere; the way back reads it at once.
  page.droid.record = record("DM:GREET", 5_000, "completed");
  page.shell.showing("seq");
  await page.settle();
  assert.equal(page.readsOfTheRecord(), asked + 1);
  assert.equal(page.saysRunning(), false);
});
