// The Sequences list loading through the Page Recovery View's sections.
//
// Right after a boot or an upload the controller sheds connections in the
// opening burst by design (CONTEXT.md "Page Recovery View"). A list read that
// is shed must come back through the section machinery's retry and end on the
// page; before #434 the section loaders filled the lists without repainting,
// so the factory list showed only if something else repainted the page later.
//
// Runs the SHIPPED bootstrap reducer and browser host (PART 1 and PART 3 of
// data/page_bootstrap.js, as test_shell_mount.js does) in one context with the
// shipped data/seq.js, so the retry, the handle and the repaint are all the
// real ones. Per test_web/README.md: executed, not pattern-matched.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const { shippedWords } = require("./helpers/shipped_words.cjs");

const root = path.resolve(__dirname, "../..");
const read = (name) => fs.readFileSync(path.join(root, "data", name), "utf8");

const bootstrapFile = read("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const bootstrapPart1 = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const bootstrapPart3 = bootstrapFile.substring(part3Marker);

// The script chain data/seq.html declares that data/seq.js leans on at load.
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

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms).unref?.());

// Timers the code under test schedules must not hold the node:test process
// open (test_web/README.md, vm-hosting the browser host).
const unrefTimeout = (fn, ms) => {
  const timer = setTimeout(fn, ms);
  timer.unref?.();
  return timer;
};
const unrefInterval = (fn, ms) => {
  const timer = setInterval(fn, ms);
  timer.unref?.();
  return timer;
};

// An element whose class list is real, so "hidden" means what it does in a
// browser. `onClassChange` sees every change as it happens.
function makeElement(onClassChange = () => {}) {
  const classes = new Set();
  const element = {
    dataset: {},
    style: {},
    value: "",
    innerHTML: "",
    textContent: "",
    children: [],
    classList: {
      add: (...names) => { names.forEach((n) => classes.add(n)); onClassChange(); },
      remove: (...names) => { names.forEach((n) => classes.delete(n)); onClassChange(); },
      toggle: (name, force) => {
        const on = force === undefined ? !classes.has(name) : Boolean(force);
        if (on) classes.add(name); else classes.delete(name);
        onClassChange();
        return on;
      },
      contains: (name) => classes.has(name),
    },
    setAttribute() {},
    getAttribute: () => null,
    addEventListener() {},
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

// The page as the droid answers it: `answer(path, call)` returns a promise per
// request, `call` counting from 1 per path.
function newPage(answer) {
  const calls = new Map();
  const consoleErrors = [];
  let emptyStateShown = false;

  const elements = new Map();
  const elementById = (id) => {
    if (!elements.has(id)) {
      const element = makeElement(() => {
        if (id === "seq-empty-state" && !element.classList.contains("hidden")) emptyStateShown = true;
      });
      elements.set(id, element);
    }
    return elements.get(id);
  };
  // The markup's initial classes (data/seq.html): both states start hidden.
  elementById("seq-populated-state").classList.add("hidden");
  elementById("seq-empty-state").classList.add("hidden");
  emptyStateShown = false;

  const get = (url) => {
    const call = (calls.get(url) || 0) + 1;
    calls.set(url, call);
    return answer(url, call);
  };

  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      ...shippedWords(),
      get,
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      postJson: () => Promise.resolve({ ok: true, data: {} }),
      estopPostForm: () => Promise.resolve({ ok: true, data: {} }),
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml, escapeAttr: escapeHtml, showFeedback() {}, debounce: (fn) => fn },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {}, removeItem() {} },
    location: { origin: "http://device" },
    document: {
      readyState: "complete",
      hidden: false,
      visibilityState: "visible",
      currentScript: { dataset: { scripts: "" } },
      body: makeElement(),
      documentElement: makeElement(),
      getElementById: elementById,
      querySelector: () => null,
      querySelectorAll: () => [],
      createElement: () => makeElement(),
      addEventListener() {},
      removeEventListener() {},
    },
    addEventListener() {},
    removeEventListener() {},
    dispatchEvent: () => true,
    Event: class { constructor(type) { this.type = type; } },
    CustomEvent: class { constructor(type, opts = {}) { this.type = type; this.detail = opts.detail; } },
    AbortController,
    alert() {},
    confirm: () => false,
    setTimeout: unrefTimeout,
    clearTimeout,
    setInterval: unrefInterval,
    clearInterval,
    Promise,
    Date,
    JSON,
    console: {
      log() {},
      info() {},
      warn() {},
      error: (...args) => consoleErrors.push(args.map(String).join(" ")),
    },
  };
  sandbox.window = sandbox;
  sandbox.globalThis = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(bootstrapPart1, sandbox, { filename: "page_bootstrap.part1.js" });
  vm.runInContext(bootstrapPart3, sandbox, { filename: "page_bootstrap.part3.js" });
  PAGE_MODULES.forEach((name) => vm.runInContext(read(name), sandbox, { filename: name }));

  return {
    cards: () => elementById("seq-cards-container").innerHTML,
    emptyStateShown: () => emptyStateShown,
    consoleErrors,
    calls,
  };
}

const LEARNED = [{ name: "MY:WAVE", stepCount: 3, suppressMs: 4000, toggleGroup: "none" }];
const FACTORY = [{ name: "DM:HELLO", stepCount: 8, suppressMs: 5000, toggleGroup: "all" }];

// The controller turning a request away in the opening burst: 503 with a
// Retry-After, which the section machinery waits out and retries.
const shed = () => Object.assign(new Error("busy"), { kind: "http", status: 503, retryAfterMs: 60 });

// Right after a boot the page is painted from its markup for a second or more
// before data/seq.js runs its first render, so the markup itself must not say
// "Nothing learned yet" (measured on artoo at c2573944: shown +1.5 s, gone +2.5 s).
test("the served page starts with the empty state hidden, so a boot never shows it before the lists answer", () => {
  const tag = read("seq.html").match(/<[a-z]+\b[^>]*\bid="seq-empty-state"[^>]*>/);
  assert.ok(tag, "data/seq.html has no #seq-empty-state");
  assert.match(tag[0], /\bclass="[^"]*\bhidden\b[^"]*"/, "the empty state is visible before data/seq.js runs");
});

test("a factory read shed in the opening burst is retried and ends on the page, beside the Learned list", async () => {
  let duringTheWait = null;
  let page = null;
  page = newPage((url, call) => {
    if (url === "/api/seq/list") return Promise.resolve({ ok: true, status: 200, data: LEARNED });
    if (url === "/api/seq/builtins") {
      if (call === 1) return Promise.reject(shed());
      // The retry: what the page showed while the factory list was still owed.
      duringTheWait = page.cards();
      return Promise.resolve({ ok: true, status: 200, data: FACTORY });
    }
    return Promise.resolve({ ok: true, status: 200, data: [] });
  });

  for (let i = 0; i < 40 && !/DM:HELLO/.test(page.cards()); i += 1) await sleep(25);

  assert.equal(page.calls.get("/api/seq/builtins"), 2, "the shed factory read was not retried");
  assert.match(page.cards(), /DM:HELLO/, "the factory sequences never reached the page");
  assert.match(page.cards(), /MY:WAVE/);
  assert.ok(duringTheWait !== null, "the retry never happened");
  assert.match(duringTheWait, /MY:WAVE/, "the factory failure hid the Learned list");
  assert.equal(page.emptyStateShown(), false, "the empty state was shown while a list was still owed");
  assert.deepEqual(page.consoleErrors, []);
});
