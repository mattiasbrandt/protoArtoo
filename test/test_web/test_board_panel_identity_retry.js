// =============================================================================
// test/test_web/test_board_panel_identity_retry.js
//
// Regression test for #202 Slice 6: board panel recovery after identity retry.
// When identity resolves after assets-ready has already fired (because identity
// was initially unavailable but is now retrying successfully), the board image
// src must be set directly, not via data-deferred-src (which won't be swept).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));

const makeElement = () => {
  const listeners = new Map();
  const attributes = new Map();
  const element = {
    id: "",
    dataset: {},
    style: {},
    className: "",
    classList: { add() {}, remove() {}, contains: () => false },
    textContent: "",
    innerHTML: "",
    value: "",
    checked: false,
    disabled: false,
    hidden: false,
    type: "checkbox",
    src: undefined,
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
    closest: () => null,
    appendChild() {},
    click() {},
  };
  return element;
};

test("Board panel: sets .src when PAAssetsReady true, data-deferred-src when false", async (t) => {
  const setupSrc = readFileSync(join(__dirname, "../../data/setup.js"), "utf8");

  // Test case 1: PAAssetsReady is false when identity resolves
  await t.test("Before assets-ready, uses data-deferred-src", async () => {
    const windowListeners = new Map();
    const boardImage = makeElement();
    boardImage.id = "board-image";

    const placeholder = makeElement();
    placeholder.id = "board-image-placeholder";

    const placeholderText = makeElement();
    placeholderText.id = "board-placeholder-text";

    const documentMock = {
      getElementById: (id) => {
        if (id === "board-image") return boardImage;
        if (id === "board-image-placeholder") return placeholder;
        if (id === "board-placeholder-text") return placeholderText;
        return makeElement();
      },
      querySelector: () => makeElement(),
      querySelectorAll: () => [],
      createElement: () => makeElement(),
      createTextNode: () => makeElement(),
      addEventListener() {},
      removeEventListener() {},
      body: makeElement(),
    };

    const windowMock = {
      document: documentMock,
      PAAssetsReady: false,  // Assets NOT ready yet
      PAIdentity: null,
      PABootstrap: { registerSection: () => {}, setResourceLabels() {} },
      PageBootstrap: { createBackgroundPoll: () => ({ start() {}, stop() {} }) },
      addEventListener(type, handler) {
        if (!windowListeners.has(type)) windowListeners.set(type, []);
        windowListeners.get(type).push(handler);
      },
      removeEventListener() {},
      BOARD_LABELS: {
        artoo_esp32: "Artoo Controller",
        firebeetle2: "FireBeetle 2",
      },
      setTimeout: () => 1,
      clearTimeout() {},
      setInterval: () => 1,
      clearInterval() {},
      location: { origin: "http://device", href: "http://device/setup.html" },
      localStorage: { getItem: () => null, setItem() {} },
      requestAnimationFrame: () => 1,
      confirm: () => true,
      CustomEvent: class { constructor(type, init = {}) { this.type = type; this.detail = init.detail; } },
      Event: class {},
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
      CustomEvent: windowMock.CustomEvent,
      Event: windowMock.Event,
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
    // Add window properties to context
    for (const key of ["PAApi", "PAUtils", "PABootstrap", "PageBootstrap"]) {
      if (windowMock[key]) context[key] = windowMock[key];
    }

    // Run setup.js in the context
    vm.runInNewContext(setupSrc, context, { filename: "setup.js" });

    // Fire identity-available event before assets-ready
    const handlers = windowListeners.get("pa:identity-available") || [];
    assert.ok(handlers.length > 0, "setup.js should register pa:identity-available handler");

    for (const handler of handlers) {
      handler({
        detail: {
          droidName: "artoo",
          board: "artoo_esp32",
          board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
        },
      });
    }

    // Before assets-ready: should use data-deferred-src, not .src
    assert.strictEqual(
      boardImage.dataset.deferredSrc,
      "/board_artoo_esp32.jpg",
      "Before PAAssetsReady, should set data-deferred-src"
    );
    assert.strictEqual(
      boardImage.src,
      undefined,
      "Before PAAssetsReady, should NOT set .src"
    );
  });

  // Test case 2: PAAssetsReady is true when identity resolves (late retry)
  await t.test("After assets-ready, sets .src directly", async () => {
    const windowListeners = new Map();
    const boardImage = makeElement();
    boardImage.id = "board-image";

    const placeholder = makeElement();
    placeholder.id = "board-image-placeholder";

    const placeholderText = makeElement();
    placeholderText.id = "board-placeholder-text";

    const documentMock = {
      getElementById: (id) => {
        if (id === "board-image") return boardImage;
        if (id === "board-image-placeholder") return placeholder;
        if (id === "board-placeholder-text") return placeholderText;
        return makeElement();
      },
      querySelector: () => makeElement(),
      querySelectorAll: () => [],
      createElement: () => makeElement(),
      createTextNode: () => makeElement(),
      addEventListener() {},
      removeEventListener() {},
      body: makeElement(),
    };

    const windowMock = {
      document: documentMock,
      PAAssetsReady: true,  // Assets ARE ready (one-shot sweep already ran)
      PAIdentity: null,
      PABootstrap: { registerSection: () => {}, setResourceLabels() {} },
      PageBootstrap: { createBackgroundPoll: () => ({ start() {}, stop() {} }) },
      addEventListener(type, handler) {
        if (!windowListeners.has(type)) windowListeners.set(type, []);
        windowListeners.get(type).push(handler);
      },
      removeEventListener() {},
      BOARD_LABELS: {
        artoo_esp32: "Artoo Controller",
        firebeetle2: "FireBeetle 2",
      },
      setTimeout: () => 1,
      clearTimeout() {},
      setInterval: () => 1,
      clearInterval() {},
      location: { origin: "http://device", href: "http://device/setup.html" },
      localStorage: { getItem: () => null, setItem() {} },
      requestAnimationFrame: () => 1,
      confirm: () => true,
      CustomEvent: class { constructor(type, init = {}) { this.type = type; this.detail = init.detail; } },
      Event: class {},
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
      CustomEvent: windowMock.CustomEvent,
      Event: windowMock.Event,
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
    // Add window properties to context
    for (const key of ["PAApi", "PAUtils", "PABootstrap", "PageBootstrap"]) {
      if (windowMock[key]) context[key] = windowMock[key];
    }

    // Run setup.js in the context
    vm.runInNewContext(setupSrc, context, { filename: "setup.js" });

    // Fire identity-available event after assets-ready (simulating late retry)
    const handlers = windowListeners.get("pa:identity-available") || [];
    assert.ok(handlers.length > 0, "setup.js should register pa:identity-available handler");

    for (const handler of handlers) {
      handler({
        detail: {
          droidName: "artoo",
          board: "artoo_esp32",
          board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
        },
      });
    }

    // After assets-ready: should set .src directly, not data-deferred-src
    assert.strictEqual(
      boardImage.src,
      "/board_artoo_esp32.jpg",
      "After PAAssetsReady, should set .src directly"
    );
    assert.strictEqual(
      boardImage.dataset.deferredSrc,
      undefined,
      "After PAAssetsReady, should NOT set data-deferred-src"
    );
  });
});
