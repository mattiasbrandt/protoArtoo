// =============================================================================
// Feature Availability control interlock (issue #186).
//
// This issue-local host retains DOM listeners so it can drive the shipped
// setup.js change handler. The shared page harness remains unchanged.
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

const loadInteractiveSetup = () => {
  const elements = new Map();
  const timers = [];
  const requests = [];
  const windowListeners = new Map();
  const element = (id) => {
    if (!elements.has(id)) {
      const created = makeElement();
      created.id = id;
      elements.set(id, created);
    }
    return elements.get(id);
  };
  const config = { components: { arm1: { enabled: true } }, system: {} };
  const call = async (method, path, body) => {
    requests.push({ method, path, body });
    return { data: path === "/api/config" ? config : {} };
  };

  const windowMock = {
    PAApi: {
      ApiError: class extends Error {},
      get: (path) => call("GET", path),
      postForm: (path, body) => call("POST", path, body),
      messageFor: (error) => error?.message || "Request failed",
    },
    PAUtils: { escapeHtml: String, escapeAttr: String, debounce: (fn) => fn },
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
  for (const key of ["PAApi", "PAUtils", "PAStatusStream", "PageBootstrap"]) {
    context[key] = windowMock[key];
  }
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
  return { element, timers, requests, settle, publishIdentity, fireTimer };
};

test("an unavailable component toggle ignores even a scripted change event", async () => {
  const env = loadInteractiveSetup();
  await env.settle();
  const arm1 = env.element("enable-arm1");
  arm1.dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.publishIdentity(false);

  assert.equal(arm1.disabled, true);
  assert.equal(env.element("status-arm1").textContent, "Not in this build");

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
