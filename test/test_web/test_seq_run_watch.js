// A run started on the Sequences surface: when the page says it is running,
// and when it stops saying so.
//
// The droid answers POST /api/seq/test before the run has started and records
// the run only when its dispatcher takes it up, so for a moment after every
// press the status frame's run (`seqRun`) is still the one from before it -
// and when the same sequence is run twice, it carries this run's name and
// says it ended. A page that judged the press by name would end Running the
// instant it began. The run watch is the Live Reading's (data/live_reading.js,
// 3e8c085c, #451): it judges a press by the run's start time and reads only
// the frames the session already gets, never GET /api/seq/last-run.
//
// Runs the SHIPPED surface poll (PART 1 of data/page_bootstrap.js), status
// stream and Live Reading in one context with the shipped data/seq.js. With
// no EventSource the Live Reading's fallback poll brings the frames, so the
// droid here is what GET /api/status answers. The page's clock is fired by
// hand: `tick()` moves it a second and runs what the page scheduled for it,
// and a timer the page cleared is never run. Per test_web/README.md: executed,
// not pattern-matched.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const { shippedWords } = require("./helpers/shipped_words.cjs");
const { operatorShellUi } = require("./helpers/page_module_env.js");
const { MiniDocument } = require("./helpers/mini_dom.js");
const { statusFrame } = require("./helpers/fake_droid.js");

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
  // Escape and the question every surface asks with (#456).
  "overlay.js",
  // The status stream and the run watch data/seq.js reads from the Live
  // Reading (#451). Loaded, not started: starting it is the Operator Shell's
  // call, and no frame reaches these tests.
  "status_stream.js",
  "live_reading.js",
  "droid_parts.js",
  "droid_build.js",
  "dome_command_map.js",
  "dome_panel_model.js",
  "dome_layout.js",
  "dome_lights.js",
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

// The run a status frame carries, as src/web/status_json.cpp writes it
// (docs/api.md, `seqRun`).
const run = (name, startMs, running) => ({ name, running, startMs });

// The Sequences surface with DM:GREET open in the workspace, on a droid whose
// last recorded run is `droid.run` (null before any since boot) - the test
// changes it as the droid would.
function newSurface(initialRun) {
  // `answers: false` is a droid that has dropped off the network: a read of
  // its status gets no answer.
  const droid = { run: initialRun, answers: true };
  const requests = []; // every request the page sent, in order: "GET /api/..."
  let clock = 1_000_000;
  const intervals = new Map();
  let nextInterval = 1;
  // Timeouts on the page's clock: each runs once the clock reaches it.
  const timeouts = new Map();
  let nextTimeout = 1;
  const runDue = () => {
    [...timeouts.entries()]
      .filter(([, timer]) => timer.due <= clock)
      .forEach(([id, timer]) => {
        timeouts.delete(id);
        timer.fn();
      });
  };

  const elements = new Map();
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, makeElement());
    return elements.get(id);
  };
  // The markup the workspace writes hides these until a run is under way
  // (data/seq.js renderEditorView()).
  ["seq-editor-running", "seq-editor-stop", "seq-editor-test-hint"].forEach((id) => byId(id).classList.add("hidden"));
  // Stop is a real act: a run under way renames it through PAUi.setAct(),
  // which reads the act's own markup.
  const ui = operatorShellUi();
  const mini = new MiniDocument();
  const stop = mini.body.appendChild(mini.createElement("button"));
  stop.innerHTML = ui.actFace("stop", "Stop");
  stop.classList.add("hidden");
  stop.listeners = {};
  stop.addEventListener = (name, fn) => (stop.listeners[name] = stop.listeners[name] || []).push(fn);
  elements.set("seq-editor-stop", stop);

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
        if (url === "/api/status") {
          if (!droid.answers) return Promise.reject(Object.assign(new Error("no response"), { kind: "network" }));
          return Promise.resolve({ ok: true, status: 200, data: statusFrame({ seqRun: droid.run && { ...droid.run } }) });
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
    // The act the pages draw their buttons with, from the shipped shell (#460).
    PAUi: ui,
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    PABootstrap: { registerSection() {}, setResourceLabels() {}, retryNow() {}, refreshSections() {} },
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
    setTimeout: (fn, ms = 0) => {
      const id = nextTimeout;
      nextTimeout += 1;
      timeouts.set(id, { fn, due: clock + ms });
      return id;
    },
    clearTimeout: (id) => timeouts.delete(id),
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
  // As the Operator Shell starts it, before any surface reads it.
  sandbox.PALiveReading.start();

  const settle = async (turns = 8) => {
    for (let i = 0; i < turns; i += 1) {
      runDue();
      await new Promise((resolve) => setImmediate(resolve));
    }
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
    saysRunning: () => !byId("seq-editor-running").classList.contains("hidden"),
    offersTest: () => !byId("seq-editor-test").classList.contains("hidden"),
    feedback: () => byId("seq-editor-feedback").innerHTML,
  };
}

test("an earlier run of the same sequence does not end Running, and this run's own ending does", async () => {
  // DM:GREET ran before, and ended: the frame's run carries this run's name.
  const page = newSurface(run("DM:GREET", 40_000, false));

  await page.press("seq-editor-test");
  const readAndRun = page.requests.filter((request) => request === "GET /api/status" || request.startsWith("POST "));
  assert.deepEqual(readAndRun.slice(0, 2), ["GET /api/status", "POST /api/seq/test"],
    "a page with no frame yet did not read the droid's run before it sent this one");
  assert.ok(page.saysRunning(), "the page does not say the run it sent is running");

  // The dispatcher has not taken the run up yet: the droid still reports the
  // earlier run.
  await page.tick();
  await page.tick();
  assert.ok(page.saysRunning(), "an earlier run of the same sequence ended this run");

  page.droid.run = run("DM:GREET", 91_000, true);
  await page.tick();
  assert.ok(page.saysRunning());

  // Stop asks the droid; the run is over when its frame says so.
  await page.press("seq-editor-stop");
  assert.equal(page.requests.at(-1), "POST /api/seq/stop");
  assert.ok(page.saysRunning(), "Running was taken down by the press, not by the droid's frame");

  page.droid.run = run("DM:GREET", 91_000, false);
  await page.tick();
  assert.equal(page.saysRunning(), false, "the run's own frame ended it and the page still says Running");
  assert.ok(page.offersTest());
});

test("a run the droid accepted and never started stops reading as running, and the page says so", async () => {
  // Nothing has run since boot, and the droid refuses the run after its ok
  // (src/tasks/sequence_dispatcher.cpp: no room for the run's buffers).
  const page = newSurface(null);

  await page.press("seq-editor-test");
  assert.ok(page.saysRunning());
  await page.tick();
  assert.ok(page.saysRunning(), "the droid was given no time to take the run up");

  for (let second = 0; second < 6; second += 1) await page.tick();
  assert.equal(page.saysRunning(), false, "a run that never started still reads as running");
  assert.match(page.feedback(), /The droid did not start DM:GREET\./);
});

test("a droid that stops answering mid-run does not read as running, and the page says it lost touch", async () => {
  const page = newSurface(null);
  await page.press("seq-editor-test");
  page.droid.run = run("DM:GREET", 5_000, true);
  await page.tick();
  assert.ok(page.saysRunning());

  // The droid drops off the network: out of touch, the last frame cannot say
  // a run is still under way.
  page.droid.answers = false;
  await page.tick();
  assert.equal(page.saysRunning(), false, "a run on a droid that no longer answers still reads as running");
  assert.match(page.feedback(), /Lost touch with the droid; DM:GREET may still be running\./);
});

test("another run landing just after the press does not end this run before its own is there", async () => {
  // A run sent a moment before this one - from another row, the radio, the
  // Dashboard - is recorded first; this run preempts it and is recorded next.
  const page = newSurface(null);
  await page.press("seq-editor-test");
  page.droid.run = run("DM:OTHER", 7_000, true);
  await page.tick();
  assert.ok(page.saysRunning(), "the run before this one ended it");

  page.droid.run = run("DM:GREET", 7_050, true);
  await page.tick();
  assert.ok(page.saysRunning());

  page.droid.run = run("DM:GREET", 7_050, false);
  await page.tick();
  assert.equal(page.saysRunning(), false);
});
