// =============================================================================
// test/test_web/test_unreported_state_dashboard.js
//
// The Surface Anatomy on the chrome and the Dashboard (#399, ADR 0066).
//
// Two halves, because the two files under test are reached differently. The
// chrome is observed by running the shipped page_bootstrap.js + shell.js
// against the shipped data/index.html in a real node tree, the way
// test_status_plate.js does - the rail, the topbar and the plate are
// written by shell.js and there is nowhere else to read them from. The
// Dashboard's renderers are driven through helpers/page_module_env.js, with the
// SHIPPED health_signals.js, droid_parts.js and droid_build.js published into
// the same context rather than modelled, so "the summary counts what the
// evaluators returned" is a claim about the evaluators and not about a fixture.
//
// What these are about is the part of the anatomy that is mechanical: an icon
// that resolves, a cell that is a label over a value, a readout that prints the
// number the signal beside it judges on, and a plate that asks the controller
// for nothing it was not already being asked.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";
import { loadPageModule } from "./helpers/page_module_env.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);
const shellSrc = readData("shell.js");

const IDENTITY = {
  droidName: "artoo",
  board: "artoo_esp32",
  board_capabilities: { sbus: true },
  build_flags: { audio: true },
};

const HEALTHY = Object.freeze({
  estop: false,
  sbusHwFailsafe: false,
  sbusSignalLost: false,
  webDriveExpired: false,
  webControlEnabled: true,
  sleepMode: false,
  stationary: false,
  speedLimitMax: 600,
  drive: { state: "idle" },
  rcCh1: { state: "active" },
  dome_link: { state: "connected", uart_owner: "dome" },
  audio: { state: "idle", link_ok: true, rx_status: "available" },
});

// -----------------------------------------------------------------------------
// The chrome: the shipped shell, booted
// -----------------------------------------------------------------------------
const bootShell = async () => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = { document, requests: [] };
  const windowListeners = new Map();
  const windowMock = {
    setTimeout: (fn, ms) => {
      const timer = setTimeout(fn, ms);
      timer.unref?.();
      return timer;
    },
    clearTimeout: (id) => clearTimeout(id),
    setInterval: (fn, ms) => {
      const timer = setInterval(fn, ms);
      timer.unref?.();
      return timer;
    },
    clearInterval: (id) => clearInterval(id),
    addEventListener: (type, fn) => {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(fn);
    },
    removeEventListener: () => {},
    dispatchEvent: (event) => {
      (windowListeners.get(event.type) || []).forEach((fn) => fn(event));
      return true;
    },
    location: {
      origin: "http://device",
      _hash: "",
      get hash() {
        return this._hash;
      },
      set hash(value) {
        const text = String(value);
        const next = text === "" || text.startsWith("#") ? text : `#${text}`;
        if (next === this._hash) return;
        this._hash = next;
        (windowListeners.get("hashchange") || []).forEach((fn) => fn({ type: "hashchange" }));
      },
    },
    history: {
      replaceState: (_state, _title, url) => {
        windowMock.location._hash = String(url);
      },
    },
    localStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
    PAApi: {
      get: async (path) => {
        env.requests.push(path);
        if (path === "/api/identity") return { data: IDENTITY };
        if (path === "/api/status") return { data: { ...HEALTHY } };
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      estopPostForm: async () => ({ data: { ok: true } }),
      postForm: async () => ({ data: { ok: true } }),
      messageFor: (error) => error?.message || "Request failed",
      gateControls: () => {},
    },
    PAUtils: { escapeHtml: (value) => String(value), showFeedback: () => {} },
  };

  class FakeEvent {
    constructor(type) {
      this.type = type;
    }
  }
  class FakeCustomEvent extends FakeEvent {
    constructor(type, opts = {}) {
      super(type);
      this.detail = opts.detail;
    }
  }

  const context = {
    window: windowMock,
    document,
    console: { warn: () => {}, log: () => {}, error: () => {} },
    AbortController,
    Date,
    JSON,
    Object,
    Array,
    Set,
    Map,
    String,
    Number,
    Boolean,
    Promise,
    Error,
    Math,
    Event: FakeEvent,
    CustomEvent: FakeCustomEvent,
    DOMParser: class {
      parseFromString(html, type) {
        return new MiniDOMParser().parseFromString(html, type);
      }
    },
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
  context.globalThis = context;

  const REAL_SCRIPTS = { "/shell.js": shellSrc };
  document.onAttach = (node) => {
    if (node.nodeType !== 1 || node.tagName !== "SCRIPT" || !node.src) return;
    const src = node.src;
    setTimeout(() => {
      if (REAL_SCRIPTS[src]) vm.runInNewContext(REAL_SCRIPTS[src], context, { filename: src });
      node.onload?.();
    }, 2).unref?.();
  };

  vm.runInNewContext(part1Src, context, { filename: "page_bootstrap.part1.js" });
  vm.runInNewContext(part3Src, context, { filename: "page_bootstrap.part3.js" });

  env.navigate = (to) => {
    windowMock.location.hash = to;
  };
  await sleep(180);
  return env;
};

// The surfaces and the icon each one declares, read out of the shipped table so
// this asserts the rendered markup against the declaration rather than against
// a second copy of it that could drift the same way.
const declaredIcons = () =>
  new Map(
    [...shellSrc.matchAll(/\{ page: "([a-z]+)", doc: "[^"]+", icon: "([a-z0-9-]+)"/g)].map(
      (match) => [match[1], match[2]],
    ),
  );

// -----------------------------------------------------------------------------
// The Dashboard: the shipped renderers, with the shipped models beside them
// -----------------------------------------------------------------------------

// Publishes a shipped browser module's globals without modelling them.
const publish = (files) => {
  const bag = { window: {} };
  bag.globalThis = bag;
  bag.console = { log: () => {}, warn: () => {}, error: () => {} };
  Object.assign(bag, { JSON, Math, Date, Number, String, Boolean, Array, Object, Set, Map, Promise, Error, RegExp, isNaN, parseInt, parseFloat });
  files.forEach((file) => vm.runInNewContext(readData(file), bag, { filename: file }));
  return bag.window;
};

const MODELS = publish(["health_signals.js", "droid_parts.js", "droid_build.js"]);

const CONFIG = {
  system: { logLevel: 3 },
  droidBuild: {
    domeDesign: "mk4",
    domeVariant: "complex",
    bodyDesign: "mk4",
    bodyVariant: "complex",
    fitted: ["pie1", "pie2", "doorFL"],
  },
};

const dashboard = (status = {}, { config = CONFIG } = {}) =>
  loadPageModule("app.js", {
    respond: (path) => {
      if (path === "/api/status") return { data: { ...HEALTHY, ...status } };
      if (path === "/api/config") return { data: config };
      if (path === "/api/logs") return { data: "" };
      return { data: {} };
    },
    overrides: {
      PAHealthSignals: MODELS.PAHealthSignals,
      DroidParts: MODELS.DroidParts,
      DroidBuild: MODELS.DroidBuild,
      PAUi: { setupActionHtml: (action) => `${action} in <a href="/setup.html">Setup</a>` },
    },
  });

test("a WiFi signal nothing measured is not printed as a very strong one", async () => {
  // wifiRssi is zero whenever the droid is not joined to a network as a
  // station (deriveWiFiConnectivityFields). Zero dBm would be the strongest
  // reading the scale has, so printing it is the readout lying loudest.
  const joined = dashboard({ wifiRssi: -54 });
  await joined.runSection("app-initial-status");
  await joined.settle();
  assert.equal(joined.element("readout-wifi").innerHTML, "-54<small>dBm</small>");
  assert.match(joined.element("readout-wifi-detail").textContent, /joined/);

  const alone = dashboard({ wifiRssi: 0 });
  await alone.runSection("app-initial-status");
  await alone.settle();
  assert.equal(alone.element("readout-wifi").innerHTML, "--", "nothing measured prints as nothing");
  assert.match(alone.element("readout-wifi-detail").textContent, /nothing measured/);
});

