// =============================================================================
// Feature Availability control interlock (issue #186).
//
// This issue-local host retains DOM listeners so it can drive the shipped
// resource-order contract - shell.js, then the Feature Availability module both
// surfaces load, then the surfaces - and Configuration's change handlers. It
// loads Configuration and Maintenance into one session, the way a builder who
// has opened both has them: the component rows are Configuration's and the
// Memory Profiler is Maintenance's, and one manifest reaches both (#404). The
// shared page harness remains unchanged.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "node:fs";

const makeElement = () => {
  const listeners = new Map();
  const attributes = new Map();
  const row = {
    dataset: {},
    classList: { add() {}, remove() {} },
    querySelectorAll: () => [],
    appendChild() {},
  };
  const element = {
    id: "",
    dataset: {},
    style: {},
    className: "",
    classList: { add() {}, remove() {}, toggle() {} },
    textContent: "",
    innerHTML: "",
    value: "",
    checked: false,
    disabled: false,
    hidden: false,
    type: "checkbox",
    onload: null,
    onerror: null,
    addEventListener(type, handler) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type).push(handler);
    },
    setAttribute(name, value) { attributes.set(name, String(value)); },
    removeAttribute(name) { attributes.delete(name); },
    querySelectorAll: () => [],
    querySelector: () => null,
    closest: () => row,
    appendChild() {},
    click() {},
    async emit(type, event = {}) {
      for (const handler of listeners.get(type) || []) {
        await handler({ target: element, preventDefault() {}, ...event });
      }
    },
  };
  return element;
};

const loadInteractiveSurfaces = ({ identity = null, failIdentity = false } = {}) => {
  const elements = new Map();
  const timers = [];
  const requests = [];
  const windowListeners = new Map();
  const sections = new Map();
  const element = (id) => {
    if (!elements.has(id)) {
      const created = makeElement();
      created.id = id;
      elements.set(id, created);
    }
    return elements.get(id);
  };
  const config = { components: { drive: { enabled: true } }, system: {} };
  const identityPayload = identity || {
    droidName: "artoo",
    mdnsUseName: true,
    board: "artoo_esp32",
    board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
    build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
  };
  class ApiError extends Error {
    constructor(message, { kind = "network", status = 0 } = {}) {
      super(message);
      this.name = "ApiError";
      this.kind = kind;
      this.status = status;
    }
  }
  const call = async (method, path, body) => {
    requests.push({ method, path, body });
    if (path === "/api/config") return { data: config };
    if (path === "/api/identity" && failIdentity) {
      throw new ApiError("Network request failed", { kind: "network" });
    }
    if (path === "/api/identity") return { data: identityPayload };
    return { data: {} };
  };

  const windowMock = {
    PAApi: {
      ApiError,
      get: (path) => call("GET", path),
      postForm: (path, body) => call("POST", path, body),
      messageFor: (error) => error?.message || "Request failed",
    },
    PAUtils: { escapeHtml: String, escapeAttr: String, debounce: (fn) => fn },
    PABootstrap: {
      registerSection: (name, load, opts = {}) => sections.set(name, { load, opts }),
      setResourceLabels() {},
    },
    PAStatusStream: { isSupported: () => false, getLastStatus: () => null, subscribe() {} },
    PageBootstrap: { createBackgroundPoll: () => ({ start() {}, stop() {} }) },
    // data/page_bootstrap.js publishes window.PASurface in the browser; this
    // context hand-rolls its globals, so it has to carry it too (#360).
    PASurface: { poll: () => ({ start() {}, stop() {}, cancelRetry() {} }) },
    setTimeout(fn, ms) {
      const id = timers.length + 1;
      timers.push({ id, fn, ms, cleared: false });
      return id;
    },
    clearTimeout(id) {
      const timer = timers.find((entry) => entry.id === id);
      if (timer) timer.cleared = true;
    },
    setInterval: () => 1,
    clearInterval() {},
    addEventListener(type, handler) {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(handler);
    },
    removeEventListener() {},
    dispatchEvent(event) {
      for (const handler of windowListeners.get(event.type) || []) handler(event);
    },
    location: { origin: "http://device", href: "http://device/configuration.html" },
    localStorage: { getItem: () => null, setItem() {} },
    requestAnimationFrame: () => 1,
  };
  const documentMock = {
    body: makeElement(),
    visibilityState: "visible",
    getElementById: element,
    querySelector: () => makeElement(),
    querySelectorAll: () => [],
    createElement: () => makeElement(),
    createTextNode: () => makeElement(),
    addEventListener() {},
    removeEventListener() {},
  };
  documentMock.body.dataset.page = "configuration";
  const context = {
    window: windowMock,
    document: documentMock,
    console: { log() {}, warn() {}, error() {}, info() {} },
    setTimeout: windowMock.setTimeout,
    clearTimeout: windowMock.clearTimeout,
    setInterval: windowMock.setInterval,
    clearInterval: windowMock.clearInterval,
    fetch: async () => ({ json: async () => ({}) }),
    confirm: () => true,
    Event: class {},
    CustomEvent: class {
      constructor(type, init = {}) { this.type = type; this.detail = init.detail; }
    },
    URLSearchParams,
    AbortController,
    JSON,
    Math,
    Date,
    Number,
    String,
    Boolean,
    Array,
    Object,
    Set,
    Map,
    Promise,
    Error,
    RegExp,
  };
  context.globalThis = context;
  for (const key of ["PAApi", "PAUtils", "PABootstrap", "PAStatusStream", "PageBootstrap"]) {
    context[key] = windowMock[key];
  }
  vm.runInNewContext(readFileSync("data/shell.js", "utf8"), context, { filename: "shell.js" });
  element("profiler-card").dataset.buildFlag = "PA_HEAP_PROFILE";
  for (const file of ["feature_availability.js", "configuration.js", "maintenance.js"]) {
    vm.runInNewContext(readFileSync(`data/${file}`, "utf8"), context, { filename: file });
  }

  const settle = async () => {
    for (let turn = 0; turn < 4; turn += 1) await new Promise((resolve) => setImmediate(resolve));
  };
  const publishIdentity = async (profiler) => {
    for (const handler of windowListeners.get("pa:identity-available") || []) {
      handler({ detail: {
        board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
        build_flags: { PA_HEAP_PROFILE: profiler, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
      } });
    }
    await settle();
  };
  const fireTimer = async (ms) => {
    const timer = timers.findLast((entry) => entry.ms === ms && !entry.cleared);
    assert.ok(timer, `expected an active ${ms}ms timer`);
    timer.cleared = true;
    await timer.fn();
    await settle();
  };
  const runSection = async (name) => {
    const section = sections.get(name);
    assert.ok(section, `expected registered ${name} section`);
    await section.load();
    await settle();
  };
  return { element, timers, requests, settle, publishIdentity, fireTimer, runSection, window: windowMock };
};

test("shell identity delivery reaches Configuration and Maintenance after their shipped resources load", async () => {
  const env = loadInteractiveSurfaces();
  await env.settle();

  assert.equal(env.element("profiler-card").dataset.featureState, "checking");
  await env.runSection("shell-identity");

  assert.equal(env.window.PAIdentity.droidName, "artoo", "the shell cache keeps the complete manifest");
  assert.equal(env.element("droid-name-input").value, "artoo", "Configuration received the shell event");
  assert.equal(env.element("profiler-card").dataset.featureState, "not-in-this-build");
  assert.equal(env.element("profiler-availability-status").textContent, "Not included");
  assert.equal(
    env.requests.filter((request) => request.method === "GET" && request.path === "/api/identity").length,
    1,
    "the composed resource order still uses the shell's single identity request",
  );
});

test("shell identity failure reaches Maintenance and keeps profiler traffic fail-closed", async () => {
  const env = loadInteractiveSurfaces({ failIdentity: true });
  await env.settle();

  assert.equal(env.element("profiler-card").dataset.featureState, "checking");
  await assert.rejects(
    env.runSection("shell-identity"),
    (error) => error.name === "ApiError" && error.kind === "network",
  );

  assert.equal(env.window.PAIdentity, undefined, "a failed request must not seed the shell cache");
  assert.equal(env.element("profiler-card").dataset.featureState, "identity-unavailable");
  assert.equal(env.element("profiler-availability-status").textContent, "Availability unknown");
  assert.equal(
    env.requests.filter((request) => request.path === "/api/profiler").length,
    0,
    "Maintenance must not probe or poll while manifest availability is unknown",
  );
});

test("an unavailable component toggle ignores even a scripted change event", async () => {
  const env = loadInteractiveSurfaces();
  await env.settle();
  const drive = env.element("enable-drive");
  drive.dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.publishIdentity(false);

  assert.equal(drive.disabled, true);
  assert.equal(env.element("status-drive").textContent, "Not included");

  drive.checked = false;
  await drive.emit("change");
  await env.settle();

  assert.equal(env.timers.some((timer) => timer.ms === 300), false);
  assert.equal(env.requests.filter((request) => request.method === "POST").length, 0);
});

test("the same component toggle saves once its build requirement is present", async () => {
  const env = loadInteractiveSurfaces();
  await env.settle();
  const drive = env.element("enable-drive");
  drive.dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.publishIdentity(true);

  assert.equal(drive.disabled, false);
  drive.checked = false;
  await drive.emit("change");
  await env.fireTimer(300);

  const saves = env.requests.filter((request) => request.method === "POST" && request.path === "/api/config");
  assert.equal(saves.length, 1);
  assert.equal(saves[0].body.get("enableDrive"), "false");
});

// B1's fourth answer (#341, applied on #369). An identity that failed is two
// different answers: a controller that did not respond may yet, and one that
// answered with a manifest this page cannot read never will. The copy always
// told them apart; the paint said "still finding out" for both, so a settled
// no breathed as if an answer were coming. Both paint sites - a Configuration
// row and the Maintenance profiler card - must carry the family.
const tracked = (element) => {
  const classes = new Set();
  element.classList = {
    add: (...names) => names.forEach((name) => classes.add(name)),
    remove: (...names) => names.forEach((name) => classes.delete(name)),
    contains: (name) => classes.has(name),
    toggle: (name, on) => (on ? classes.add(name) : classes.delete(name)),
  };
  return classes;
};

const paintFor = async (reason) => {
  const env = loadInteractiveSurfaces();
  await env.settle();
  const drive = env.element("enable-drive");
  drive.dataset.buildFlag = "PA_HEAP_PROFILE";
  const row = { dataset: {}, querySelectorAll: () => [], appendChild() {} };
  const rowClasses = tracked(row);
  drive.closest = () => row;
  const cardClasses = tracked(env.element("profiler-card"));
  env.window.dispatchEvent({ type: "pa:identity-unavailable", detail: { reason } });
  await env.settle();
  return { rowClasses, cardClasses };
};

test("an identity that will never be read is painted settled, and one still connecting is still finding out", async () => {
  const terminal = await paintFor("incompatible");
  const retrying = await paintFor("no-response");

  for (const [where, classes] of [["row", terminal.rowClasses], ["card", terminal.cardClasses]]) {
    assert.ok(classes.has("availability-settled-no"), `a terminal failure's ${where} is settled no`);
    assert.ok(!classes.has("availability-finding-out"), `and its ${where} is not still finding out`);
  }
  for (const [where, classes] of [["row", retrying.rowClasses], ["card", retrying.cardClasses]]) {
    assert.ok(classes.has("availability-finding-out"), `a retryable failure's ${where} is still finding out`);
    assert.ok(!classes.has("availability-settled-no"), `and its ${where} is not settled`);
  }
});
