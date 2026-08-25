// =============================================================================
// Feature Availability control interlock (issue #186).
//
// This issue-local host retains DOM listeners so it can drive the shipped
// shell.js -> setup.js resource-order contract and Setup change handlers. The
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

const loadInteractiveSetup = ({ identity = null, failIdentity = false } = {}) => {
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
  const config = { components: { arm1: { enabled: true } }, system: {} };
  const identityPayload = identity || {
    droidName: "artoo",
    mdnsUseName: true,
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
    location: { origin: "http://device", href: "http://device/setup.html" },
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
  documentMock.body.dataset.page = "setup";
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
  vm.runInNewContext(readFileSync("data/setup.js", "utf8"), context, { filename: "setup.js" });

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

test("shell identity delivery reaches Setup after both shipped resources load", async () => {
  const env = loadInteractiveSetup();
  await env.settle();

  assert.equal(env.element("profiler-card").dataset.featureState, "checking");
  await env.runSection("shell-identity");

  assert.equal(env.window.PAIdentity.droidName, "artoo", "the shell cache keeps the complete manifest");
  assert.equal(env.element("droid-name-input").value, "artoo", "Setup received the shell event");
  assert.equal(env.element("profiler-card").dataset.featureState, "not-in-this-build");
  assert.equal(env.element("profiler-availability-status").textContent, "Not included");
  assert.equal(
    env.requests.filter((request) => request.method === "GET" && request.path === "/api/identity").length,
    1,
    "the composed resource order still uses the shell's single identity request",
  );
});

test("shell identity failure reaches Setup and keeps profiler traffic fail-closed", async () => {
  const env = loadInteractiveSetup({ failIdentity: true });
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
    "Setup must not probe or poll while manifest availability is unknown",
  );
});

test("an unavailable component toggle ignores even a scripted change event", async () => {
  const env = loadInteractiveSetup();
  await env.settle();
  const arm1 = env.element("enable-arm1");
  arm1.dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.publishIdentity(false);

  assert.equal(arm1.disabled, true);
  assert.equal(env.element("status-arm1").textContent, "Not included");

  arm1.checked = false;
  await arm1.emit("change");
  await env.settle();

  assert.equal(env.timers.some((timer) => timer.ms === 300), false);
  assert.equal(env.requests.filter((request) => request.method === "POST").length, 0);
});

test("the same component toggle saves once its build requirement is present", async () => {
  const env = loadInteractiveSetup();
  await env.settle();
  const arm1 = env.element("enable-arm1");
  arm1.dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.publishIdentity(true);

  assert.equal(arm1.disabled, false);
  arm1.checked = false;
  await arm1.emit("change");
  await env.fireTimer(300);

  const saves = env.requests.filter((request) => request.method === "POST" && request.path === "/api/config");
  assert.equal(saves.length, 1);
  assert.equal(saves[0].body.get("enableArm1"), "false");
});
