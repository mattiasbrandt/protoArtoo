// =============================================================================
// Identity manifest validation — Layer 1 shape validation, Layer 2 per-key
// completeness, and resolver split (#200, slice 1).
//
// Tests the five contract scenarios:
// 1. {PA_HEAP_PROFILE: false} → "not-in-this-build" (state) with phase="ready"
// 2. {} → "identity-unavailable" (state) with phase="failed" (missing key)
// 3. {PA_HEAP_PROFILE: "0"} → invalid type, caught by Layer 1 validation
// 4. Non-object response (e.g., string) → invalid, caught by Layer 1 validation
// 5. 204 No Content (null/empty response) → invalid, caught by Layer 1 validation
//
// This test harness loads shell.js and setup.js into a vm context with mocked
// window/document, validating that Layer 1 validation prevents invalid manifests
// from reaching the resolver, and Layer 2 uses Object.hasOwn to distinguish
// missing keys from false values.
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
  return {
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
    inert: false,
    addEventListener(type, handler) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type).push(handler);
    },
    setAttribute(name, value) { attributes.set(name, String(value)); },
    getAttribute(name) { return attributes.get(name); },
    removeAttribute(name) { attributes.delete(name); },
    querySelectorAll: () => [],
    querySelector: () => null,
    closest: () => row,
    appendChild() {},
    click() {},
    async emit(type, event = {}) {
      for (const handler of listeners.get(type) || []) {
        await handler({ target: this, preventDefault() {}, ...event });
      }
    },
  };
};

const loadContextWithIdentity = ({ identity = null } = {}) => {
  const windowListeners = new Map();
  const dispatchedEvents = [];
  const timers = [];
  const intervals = [];
  const element = (id) => {
    const el = makeElement();
    el.id = id;
    return el;
  };

  const windowMock = {
    PAIdentity: null,
    document: {
      body: makeElement(),
      visibilityState: "visible",
      getElementById: element,
      querySelector: () => makeElement(),
      querySelectorAll: () => [],
      createElement: () => makeElement(),
      createTextNode: () => makeElement(),
      addEventListener() {},
      removeEventListener() {},
    },
    addEventListener(type, handler) {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(handler);
    },
    removeEventListener() {},
    dispatchEvent(event) {
      dispatchedEvents.push(event);
      for (const handler of windowListeners.get(event.type) || []) handler(event);
    },
    location: { origin: "http://device", href: "http://device/setup.html" },
    localStorage: { getItem: () => null, setItem() {} },
    requestAnimationFrame: () => 1,
    setTimeout(fn, ms) {
      const id = timers.length + 1;
      timers.push({ id, fn, ms });
      return id;
    },
    clearTimeout(id) {
      // mark as cleared
    },
    setInterval(fn, ms) {
      const id = intervals.length + 1;
      intervals.push({ id, fn, ms });
      return id;
    },
    clearInterval(id) {
      // mark as cleared
    },
    PAUtils: { escapeHtml: String, escapeAttr: String, debounce: (fn) => fn },
    PABootstrap: {
      registerSection: () => {},
      setResourceLabels() {},
    },
    PAStatusStream: { isSupported: () => false, getLastStatus: () => null, subscribe() {} },
    PageBootstrap: { createBackgroundPoll: () => ({ start() {}, stop() {} }) },
    PAApi: null,
    PAFeatureAvailability: null,
  };

  const context = {
    window: windowMock,
    document: windowMock.document,
    console: { log() {}, warn() {}, error() {}, info() {} },
    setTimeout: windowMock.setTimeout.bind(windowMock),
    clearTimeout: windowMock.clearTimeout.bind(windowMock),
    setInterval: windowMock.setInterval.bind(windowMock),
    clearInterval: windowMock.clearInterval.bind(windowMock),
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

  // Set up document mocks
  windowMock.document.body.dataset.page = "setup";

  // Load shell.js first
  vm.runInNewContext(readFileSync("data/shell.js", "utf8"), context, { filename: "shell.js" });

  // Load setup.js
  vm.runInNewContext(readFileSync("data/setup.js", "utf8"), context, { filename: "setup.js" });

  return { context, windowMock, dispatchedEvents, element };
};

test("Layer 1 validation: valid manifest passes and publishes pa:identity-available", () => {
  const { context, windowMock, dispatchedEvents } = loadContextWithIdentity();
  const identity = {
    droidName: "artoo",
    board: "artoo_esp32",
    board_capabilities: { PA_CAP_NATIVE_WIFI: true },
    build_flags: { PA_HEAP_PROFILE: false },
  };

  // Simulate identity being received via pa:identity-updated (goes through publishIdentity)
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-updated", { detail: identity }));

  // Should have published the valid identity
  const availableEvents = dispatchedEvents.filter(e => e.type === "pa:identity-available");
  assert(availableEvents.length > 0, "Should dispatch pa:identity-available");
});

test("Layer 1 validation: null identity is rejected", () => {
  const { context, windowMock, dispatchedEvents } = loadContextWithIdentity();

  // Simulate receiving null via pa:identity-updated (goes through publishIdentity)
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-updated", { detail: null }));

  // Should dispatch pa:identity-unavailable instead
  const unavailableEvents = dispatchedEvents.filter(e => e.type === "pa:identity-unavailable");
  assert(unavailableEvents.length > 0, "Should dispatch pa:identity-unavailable for null");
});

test("Layer 1 validation: non-object response is rejected", () => {
  const { context, windowMock, dispatchedEvents } = loadContextWithIdentity();

  // Simulate receiving a string instead of an object via pa:identity-updated (goes through publishIdentity)
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-updated", { detail: "not an object" }));

  // Should dispatch pa:identity-unavailable instead
  const unavailableEvents = dispatchedEvents.filter(e => e.type === "pa:identity-unavailable");
  assert(unavailableEvents.length > 0, "Should dispatch pa:identity-unavailable for non-object");
});

test("Layer 1 validation: invalid build_flags type is rejected", () => {
  const { context, windowMock, dispatchedEvents } = loadContextWithIdentity();
  const identity = {
    droidName: "artoo",
    build_flags: { PA_HEAP_PROFILE: "0" }, // Should be boolean, not string
  };

  // Simulate receiving invalid manifest via pa:identity-updated (goes through publishIdentity)
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-updated", { detail: identity }));

  // Should dispatch pa:identity-unavailable instead
  const unavailableEvents = dispatchedEvents.filter(e => e.type === "pa:identity-unavailable");
  assert(unavailableEvents.length > 0, "Should dispatch pa:identity-unavailable for invalid type");
});

test("Layer 2 validation: false value is distinguished from missing key", () => {
  const { context } = loadContextWithIdentity();

  // Manually set identity with explicit false value
  context.window.PAIdentity = {
    droidName: "artoo",
    build_flags: { PA_HEAP_PROFILE: false }, // Explicit false
  };

  // Manually trigger setIdentity to put resolve into ready phase
  context.window.PAFeatureAvailability.setIdentity(context.window.PAIdentity);

  const resolve = context.window.PAFeatureAvailability.resolve;

  // With an explicit false value, the feature should be "not-in-this-build"
  const result = resolve({ buildFlag: "PA_HEAP_PROFILE" });
  assert.strictEqual(result.state, "not-in-this-build");
  assert.strictEqual(result.phase, "ready");
});

test("Layer 2 validation: missing key returns availability unknown (checking phase)", () => {
  const { context } = loadContextWithIdentity();

  // Manually set identity with empty build_flags
  context.window.PAIdentity = {
    droidName: "artoo",
    build_flags: {}, // Empty, PA_HEAP_PROFILE is missing
  };

  // Manually trigger setIdentity to put resolve into ready phase
  context.window.PAFeatureAvailability.setIdentity(context.window.PAIdentity);

  const resolve = context.window.PAFeatureAvailability.resolve;

  // Missing key should resolve to "failed" phase (availability unknown)
  const result = resolve({ buildFlag: "PA_HEAP_PROFILE" });
  assert.strictEqual(result.state, "identity-unavailable");
  assert.strictEqual(result.phase, "failed");
});

test("Layer 2 validation: missing board_capabilities object returns availability unknown", () => {
  const { context } = loadContextWithIdentity();

  // Manually set identity without board_capabilities
  context.window.PAIdentity = {
    droidName: "artoo",
    build_flags: { PA_HEAP_PROFILE: false },
    // No board_capabilities at all
  };

  // Manually trigger setIdentity
  context.window.PAFeatureAvailability.setIdentity(context.window.PAIdentity);

  const resolve = context.window.PAFeatureAvailability.resolve;

  // Missing board_capabilities should resolve to "failed" phase
  const result = resolve({ boardCapability: "PA_CAP_NATIVE_WIFI" });
  assert.strictEqual(result.state, "identity-unavailable");
  assert.strictEqual(result.phase, "failed");
});

test("Resolver split: resolve() returns { phase, state }, not { state, available }", () => {
  const { context } = loadContextWithIdentity();

  context.window.PAIdentity = {
    droidName: "artoo",
    build_flags: { PA_HEAP_PROFILE: false },
  };

  context.window.PAFeatureAvailability.setIdentity(context.window.PAIdentity);

  const resolve = context.window.PAFeatureAvailability.resolve;

  // Check that the return shape includes phase and state
  const result = resolve({ buildFlag: "PA_HEAP_PROFILE" });
  assert(Object.hasOwn(result, "phase"), "Return object should have phase property");
  assert(Object.hasOwn(result, "state"), "Return object should have state property");
  assert.strictEqual(result.phase, "ready");
  assert.strictEqual(result.state, "not-in-this-build");
  // Should not have available property in the new return shape
  assert(!Object.hasOwn(result, "available"), "Return object should not have available property");
});

test("Resolver split: checking phase returns phase='checking' not phase='ready'", () => {
  const { context } = loadContextWithIdentity();

  // Don't set identity, so manifest is not ready
  const resolve = context.window.PAFeatureAvailability.resolve;

  // When manifest is not ready, resolve should return phase="checking"
  const result = resolve({ buildFlag: "PA_HEAP_PROFILE" });
  assert.strictEqual(result.phase, "checking");
  assert.strictEqual(result.state, "checking");
});

test("Layer 2: board_capabilities false value is distinguished from missing key", () => {
  const { context } = loadContextWithIdentity();

  context.window.PAIdentity = {
    droidName: "artoo",
    board_capabilities: { PA_CAP_NATIVE_WIFI: false }, // Explicit false
  };

  context.window.PAFeatureAvailability.setIdentity(context.window.PAIdentity);

  const resolve = context.window.PAFeatureAvailability.resolve;

  // With an explicit false value, the feature should be "not-on-this-board"
  const result = resolve({ boardCapability: "PA_CAP_NATIVE_WIFI" });
  assert.strictEqual(result.state, "not-on-this-board");
  assert.strictEqual(result.phase, "ready");
});

test("Layer 2: missing board capability key returns availability unknown", () => {
  const { context } = loadContextWithIdentity();

  context.window.PAIdentity = {
    droidName: "artoo",
    board_capabilities: {}, // Empty object, PA_CAP_NATIVE_WIFI is missing
  };

  context.window.PAFeatureAvailability.setIdentity(context.window.PAIdentity);

  const resolve = context.window.PAFeatureAvailability.resolve;

  // Missing key in board_capabilities should return "checking" (availability unknown)
  const result = resolve({ boardCapability: "PA_CAP_NATIVE_WIFI" });
  assert.strictEqual(result.state, "identity-unavailable");
  assert.strictEqual(result.phase, "failed");
});

test("Layer 1: 204 No Content (empty/null response) is rejected", () => {
  const { context, windowMock, dispatchedEvents } = loadContextWithIdentity();

  // 204 No Content would be parsed as null, dispatch via pa:identity-updated to go through validation
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-updated", { detail: null }));

  // Layer 1 validation should reject null/empty responses
  const unavailableEvents = dispatchedEvents.filter(e => e.type === "pa:identity-unavailable");
  assert(unavailableEvents.length > 0, "Should dispatch pa:identity-unavailable for 204");
});

test("Resolver split: included state with phase=ready for compile-time only features", () => {
  const { context } = loadContextWithIdentity();

  context.window.PAIdentity = {
    droidName: "artoo",
    build_flags: { PA_HEAP_PROFILE: true },
  };

  context.window.PAFeatureAvailability.setIdentity(context.window.PAIdentity);

  const resolve = context.window.PAFeatureAvailability.resolve;

  // Compile-time only feature (hasToggle=false) should return "included" when present
  const result = resolve({ buildFlag: "PA_HEAP_PROFILE", hasToggle: false });
  assert.strictEqual(result.phase, "ready");
  assert.strictEqual(result.state, "included");
});

test("Layer 1: array as build_flags is rejected", () => {
  const { context, windowMock, dispatchedEvents } = loadContextWithIdentity();
  const identity = {
    droidName: "artoo",
    build_flags: [], // Array instead of object
  };

  // Dispatch via pa:identity-updated to go through publishIdentity validation
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-updated", { detail: identity }));

  const unavailableEvents = dispatchedEvents.filter(e => e.type === "pa:identity-unavailable");
  assert(unavailableEvents.length > 0, "Should reject array as build_flags");
});

