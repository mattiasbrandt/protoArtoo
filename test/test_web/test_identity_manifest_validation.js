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
// This test harness loads shell.js, then the Feature Availability module and the
// Configuration surface that reads it, into a vm context with mocked
// window/document, validating that Layer 1 validation prevents invalid manifests
// from reaching the resolver, and Layer 2 uses Object.hasOwn to distinguish
// missing keys from false values.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "node:fs";
import { statusFrame } from "./helpers/fake_droid.js";

// Recording MockElement that tracks appendChild calls for DOM state verification
class MockElement {
  constructor(tag = "div", id = "", ownerDocument = null) {
    this.tagName = tag;
    this.id = id;
    this.ownerDocument = ownerDocument;
    this.className = "";
    this.textContent = "";
    this.innerHTML = "";
    this.value = "";
    this.type = "div";
    this.dataset = {};
    this.style = {};
    this.checked = false;
    this.disabled = false;
    this.hidden = false;
    this.inert = false;
    this.children = []; // Track appended children
    this.attributes = new Map();
    this.eventListeners = [];
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
    if (name === "id") {
      this.id = value;
      if (this.ownerDocument && this.ownerDocument.elements) {
        this.ownerDocument.elements.set(value, this);
      }
    }
  }

  getAttribute(name) {
    return this.attributes.get(name) || null;
  }

  appendChild(child) {
    if (!child) return;
    if (!this.children.includes(child)) {
      this.children.push(child);
      if (child.id && this.ownerDocument && this.ownerDocument.elements) {
        this.ownerDocument.elements.set(child.id, child);
      }
    }
  }

  querySelector(selector) {
    if (selector === "button") {
      return this.children.find((c) => c.tagName === "button" || c.type === "button");
    }
    if (selector.startsWith(".")) {
      const className = selector.substring(1);
      return this.children.find((c) => c.className && c.className.includes(className));
    }
    return null;
  }

  querySelectorAll() {
    return [];
  }

  addEventListener(event, handler) {
    this.eventListeners.push({ event, handler });
  }

  removeEventListener() {}

  click() {}

  classList = {
    add: () => {},
    remove: () => {},
    contains: () => false,
    toggle: () => {},
  };
}

class MockDocument {
  constructor() {
    this.elements = new Map();
    this.body = new MockElement("body", "", this);
    this.visibilityState = "visible";
  }

  getElementById(id) {
    return this.elements.get(id) || null;
  }

  createElement(tag) {
    return new MockElement(tag, "", this);
  }

  createTextNode(text) {
    const node = new MockElement("#text", "", this);
    node.textContent = text;
    node.nodeValue = text;
    return node;
  }

  querySelector() {
    return null;
  }

  querySelectorAll() {
    return [];
  }

  addEventListener() {}

  removeEventListener() {}

  get activeElement() {
    return this.body;
  }
}

const loadContextWithIdentity = ({ identity = null } = {}) => {
  const windowListeners = new Map();
  const dispatchedEvents = [];
  const timers = [];
  const intervals = [];
  const mockDocument = new MockDocument();

  const windowMock = {
    PAIdentity: null,
    document: mockDocument,
    addEventListener(type, handler) {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(handler);
    },
    removeEventListener() {},
    dispatchEvent(event) {
      dispatchedEvents.push(event);
      for (const handler of windowListeners.get(event.type) || []) handler(event);
    },
    location: { origin: "http://device", href: "http://device/configuration.html" },
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
      retryNow: (name) => {
        if (!windowMock.PABootstrap._retryCalls) windowMock.PABootstrap._retryCalls = [];
        windowMock.PABootstrap._retryCalls.push(name);
      },
    },
    PageBootstrap: { createBackgroundPoll: () => ({ start() {}, stop() {} }) },
    // data/page_bootstrap.js publishes window.PASurface in the browser; this
    // context hand-rolls its globals, so it has to carry it too (#360).
    PASurface: { poll: () => ({ start() {}, stop() {}, cancelRetry() {} }) },
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
  windowMock.document.body.dataset.page = "configuration";

  // Pre-create the identity-actions element (configuration.js will get it via getElementById)
  const identityActions = new MockElement("div", "identity-actions", mockDocument);
  mockDocument.elements.set("identity-actions", identityActions);
  mockDocument.body.appendChild(identityActions);
  // And the line the diagnosis of a failed identity is written to.
  const identityDiagnosis = new MockElement("div", "identity-diagnosis", mockDocument);
  mockDocument.elements.set("identity-diagnosis", identityDiagnosis);

  // Load the shell's own chain first: the status stream and the Live Reading
  // it starts, then shell.js.
  for (const file of ["status_stream.js", "live_reading.js", "shell.js"]) {
    vm.runInNewContext(readFileSync(`data/${file}`, "utf8"), context, { filename: file });
  }

  // Then the surface's own chain: the resolver both surfaces share, and the
  // surface whose identity card carries the retry.
  for (const file of ["feature_availability.js", "configuration.js"]) {
    vm.runInNewContext(readFileSync(`data/${file}`, "utf8"), context, { filename: file });
  }

  return { context, windowMock, dispatchedEvents, mockDocument };
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

// =============================================================================
// Retry Affordance Tests (Slice 2b Wiring)
// =============================================================================

test("Retry button is appended to identity-actions on pa:identity-unavailable", () => {
  const { context, windowMock, mockDocument } = loadContextWithIdentity();
  const identityActions = mockDocument.getElementById("identity-actions");

  // Initial state: no children
  assert.strictEqual(identityActions.children.length, 0, "identity-actions should start empty");

  // Dispatch unavailable event (e.g., from failed validation)
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-unavailable"));

  // Button should have been appended
  const buttons = identityActions.children.filter((child) => child.tagName === "button");
  assert.strictEqual(buttons.length, 1, "Exactly one button should be appended to identity-actions");
  assert.strictEqual(buttons[0].className, "btn accent", "Button should have correct className");
  assert.strictEqual(buttons[0].textContent, "Retry now", "Button should have correct text");
});

test("Retry button calls PABootstrap.retryNow with shell-identity when clicked", () => {
  const { context, windowMock, mockDocument } = loadContextWithIdentity();
  const identityActions = mockDocument.getElementById("identity-actions");

  // Clear retry calls
  windowMock.PABootstrap._retryCalls = [];

  // Dispatch unavailable to create button
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-unavailable"));

  // Get the appended button and invoke its stored click handler
  const button = identityActions.children[0];
  assert.ok(button, "Button should be appended");

  const clickHandler = button.eventListeners.find((el) => el.event === "click");
  assert.ok(clickHandler, "Button should have a click handler");

  // Invoke the click handler
  clickHandler.handler({ preventDefault: () => {} });

  // retryNow should have been called with "shell-identity"
  assert.strictEqual(
    windowMock.PABootstrap._retryCalls.length,
    1,
    "retryNow should be called once"
  );
  assert.strictEqual(
    windowMock.PABootstrap._retryCalls[0],
    "shell-identity",
    "retryNow should be called with section name 'shell-identity'"
  );
});

test("Retry button clears from identity-actions on pa:identity-available", () => {
  const { context, windowMock, mockDocument } = loadContextWithIdentity();
  const identityActions = mockDocument.getElementById("identity-actions");

  // First, show the button
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-unavailable"));
  assert(identityActions.children.length > 0, "Button should be appended first");

  // Then, dispatch available to clear it
  const validIdentity = {
    droidName: "artoo",
    board: "artoo_esp32",
    board_capabilities: { PA_CAP_NATIVE_WIFI: true },
    build_flags: { PA_HEAP_PROFILE: false },
  };
  windowMock.dispatchEvent(new context.CustomEvent("pa:identity-available", { detail: validIdentity }));

  // innerHTML should be cleared (empty string)
  assert.strictEqual(identityActions.innerHTML, "", "identity-actions innerHTML should be cleared on pa:identity-available");
});

// Why the page cannot read the droid's feature list decides what the builder
// does next: upload both halves of one release, or stop re-uploading the same
// one. The two are told apart by the firmware the droid reports running, read
// from the Live Reading (#419), against the version this web bundle was built
// for.
test("a failed identity is diagnosed from the firmware the droid reports running", async () => {
  const diagnosisFor = async (running) => {
    const { context, windowMock, mockDocument } = loadContextWithIdentity();
    windowMock.PAApi = {
      get: async (path) => {
        if (path === "/fw-version.json") return { data: { fwVersion: "2.0.0" } };
        throw new Error(`unexpected GET ${path}`);
      },
    };
    windowMock.PAStatusStream.seed(statusFrame({ firmwareVersion: running }));
    windowMock.dispatchEvent(new context.CustomEvent("pa:identity-unavailable", { detail: { reason: "incompatible" } }));
    windowMock.dispatchEvent(new context.CustomEvent("pa:assets-ready"));
    for (let turn = 0; turn < 4; turn += 1) await new Promise((resolve) => setImmediate(resolve));
    return mockDocument.getElementById("identity-diagnosis").textContent;
  };

  assert.match(await diagnosisFor("1.9.0"), /do not match/, "a firmware from another release is the cause");
  assert.match(await diagnosisFor("2.0.0"), /same release again will not fix it/, "the same release is not");
});
