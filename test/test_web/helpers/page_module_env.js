// =============================================================================
// test/test_web/helpers/page_module_env.js
//
// Runs a shipped data/*.js page module for real, in a vm context with a
// permissive DOM stub, and hands back the seams a test needs to drive it: the
// section loaders it registered with the bootstrap, and a log of the API calls
// it made.
//
// The DOM stub is deliberately permissive - a page module touches dozens of
// elements at load time and none of that is what these tests are about. What
// it is NOT permissive about is the transport: every PAApi call is recorded and
// answered by a per-test responder, so a test can assert on what the module
// actually asked the controller for. Issue #146.
// =============================================================================

import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../../data");

// A stub element that answers any property access with something plausible, so
// module-level wiring never crashes on an element this test does not care
// about. Writes are accepted and discarded.
const makeElement = () => {
  const own = {
    dataset: {},
    style: {},
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    textContent: "",
    innerHTML: "",
    value: "",
    checked: false,
    disabled: false,
    hidden: false,
    children: [],
  };
  return new Proxy(own, {
    get(target, key) {
      if (key in target) return target[key];
      if (key === Symbol.toPrimitive || typeof key === "symbol") return undefined;
      // Any unknown property is treated as a method returning a fresh element,
      // which covers querySelector/closest/appendChild/etc. in one rule.
      return (...args) => (key === "querySelectorAll" ? [] : makeElement(args));
    },
    set(target, key, value) {
      target[key] = value;
      return true;
    },
  });
};

class ApiError extends Error {
  constructor(message, { kind = "network", status = 0 } = {}) {
    super(message);
    this.name = "ApiError";
    this.kind = kind;
    this.status = status;
  }
}

// Loads data/<file> and returns the handles a test drives it through.
//
// respond(path, opts) is the whole transport: return a value to resolve the
// call with (wrapped as { data } unless it already looks like a response), or
// throw to fail it. Every call is appended to `requests` first, so a rejected
// call is still visible to the test.
export const loadPageModule = (file, { respond = () => ({}) } = {}) => {
  const source = readFileSync(join(dataDir, file), "utf-8");
  const requests = [];
  const sections = new Map();
  const intervals = [];
  const timeouts = [];

  const call = async (method, path, opts = {}) => {
    requests.push({ method, path, opts });
    const result = await respond(path, { ...opts, method });
    return result && typeof result === "object" && "data" in result ? result : { data: result ?? {} };
  };

  const windowMock = {
    PAApi: {
      ApiError,
      get: (path, opts) => call("GET", path, opts),
      postForm: (path, body, opts) => call("POST", path, { ...opts, body }),
      postJson: (path, body, opts) => call("POST", path, { ...opts, body }),
      messageFor: (error) => error?.message || "Request failed",
      gateControls: () => {},
    },
    PAUtils: {
      showFeedback: () => {},
      escapeHtml: (value) => String(value ?? ""),
      escapeAttr: (value) => String(value ?? ""),
      debounce: (fn) => fn,
    },
    PABootstrap: {
      registerSection: (name, load, opts = {}) => sections.set(name, { load, opts }),
      setResourceLabels: () => {},
      declareSections: () => {},
      retryNow: () => {},
      refreshSections: () => {},
      getState: () => ({}),
    },
    PAStatusStream: {
      isSupported: () => false,
      subscribe: () => () => {},
      getLastStatus: () => null,
    },
    // Timers are recorded rather than run: a page module installs polling at
    // load time and a live interval would keep the test process alive.
    setInterval: (fn, ms) => {
      intervals.push({ fn, ms });
      return intervals.length;
    },
    clearInterval: () => {},
    setTimeout: (fn, ms) => {
      timeouts.push({ fn, ms });
      return timeouts.length;
    },
    clearTimeout: () => {},
    addEventListener: () => {},
    removeEventListener: () => {},
    location: { origin: "http://device", href: "http://device/" },
    localStorage: {
      getItem: () => null,
      setItem: () => {},
      removeItem: () => {},
      clear: () => {},
    },
    matchMedia: () => ({ matches: false, addEventListener: () => {}, removeEventListener: () => {} }),
    requestAnimationFrame: (fn) => {
      timeouts.push({ fn, ms: 0 });
      return timeouts.length;
    },
    getSelection: () => null,
  };

  const documentMock = {
    readyState: "complete",
    visibilityState: "visible",
    body: makeElement(),
    documentElement: makeElement(),
    currentScript: { dataset: {} },
    getElementById: () => makeElement(),
    querySelector: () => makeElement(),
    querySelectorAll: () => [],
    createElement: () => makeElement(),
    createTextNode: () => makeElement(),
    createDocumentFragment: () => makeElement(),
    addEventListener: () => {},
    removeEventListener: () => {},
  };

  const context = {
    window: windowMock,
    document: documentMock,
    console: { log: () => {}, warn: () => {}, error: () => {}, info: () => {} },
    navigator: { userAgent: "node" },
    setTimeout: windowMock.setTimeout,
    clearTimeout: windowMock.clearTimeout,
    setInterval: windowMock.setInterval,
    clearInterval: windowMock.clearInterval,
    Event: class {},
    CustomEvent: class {},
    AbortController,
    URLSearchParams,
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
    TypeError,
    RegExp,
    isNaN,
    parseInt,
    parseFloat,
    encodeURIComponent,
    decodeURIComponent,
  };
  context.globalThis = context;
  // Page modules read globals both as `window.X` and bare `X`, the way a
  // browser resolves them. Mirror the published objects onto the context.
  for (const key of ["PAApi", "PAUtils", "PABootstrap", "PAStatusStream"]) {
    context[key] = windowMock[key];
  }

  vm.runInNewContext(source, context, { filename: file });

  return {
    window: windowMock,
    document: documentMock,
    requests,
    intervals,
    timeouts,
    sectionNames: () => [...sections.keys()],
    // Runs a registered section loader the way the bootstrap would.
    runSection: (name, opts = {}) => {
      const entry = sections.get(name);
      if (!entry) throw new Error(`page_module_env: ${file} registered no "${name}" section`);
      return entry.load(opts);
    },
    sectionOptions: (name) => sections.get(name)?.opts ?? null,
    pathsRequested: () => requests.map((r) => r.path),
  };
};

export { ApiError };
