// =============================================================================
// test/test_web/helpers/lights_surface.js
//
// Lights as the browser runs it: data/lights.html, its own script chain, and a
// controller that answers the three reads the surface makes - GET /api/config,
// GET /api/servo/outputs and GET /api/status - plus the status stream it
// subscribes to.
//
// The droid it answers with is deliberately NOT the bench's: the Outputs carry
// labels and addresses that follow no pattern, so a suite asking "does this
// page ever name an Output" cannot pass by the page happening to say something
// that looks like a label.
//
// getComputedStyle answers from data/style.css's own :root, because the page
// reads the dome's color values out of the token layer. Restating them here
// would mean a swatch could drift from the color it sends and no suite would
// see it.
// =============================================================================

import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { MiniDOMParser } from "./mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, "../../..");
const dataDir = join(root, "data");

const SCRIPTS = [
  "droid_parts.js",
  "droid_part_kind.js",
  "droid_build.js",
  "seq_protocol_check.js",
  "apply_timing.js",
  "output_settings.js",
  "lights.js",
];

// The custom properties data/style.css declares on :root, which is where the
// page reads a dome color from.
const rootTokens = () => {
  const css = readFileSync(join(dataDir, "style.css"), "utf8");
  const block = css.slice(css.indexOf(":root {"), css.indexOf("}", css.indexOf(":root {")));
  const tokens = new Map();
  for (const [, name, value] of block.matchAll(/(--[a-z0-9-]+):\s*([^;]+);/gi)) {
    tokens.set(name, value.trim());
  }
  return tokens;
};

// A droid whose Outputs are named nothing like the bench's, wired so one wire
// carries a light and a Part sits on it.
export const droid = () => ({
  config: {
    components: {
      w7: { enabled: true, label: "GPIO 49", address: "ledc:7", enabledField: "wiredO7", typeField: "servoO7", type: "mg996r" },
      w9: { enabled: true, label: "ARM4", address: "ledc:9", ledStripPin: 2, enabledField: "wiredO9", typeField: "servoO9", type: "rgb" },
      w4: { enabled: false, label: "GPIO 5", address: "ledc:4", ledStripPin: 3, enabledField: "wiredO4", typeField: "servoO4", type: "none" },
      domeEsc: { enabled: true, label: "DOME" },
    },
    droidBuild: { domeDesign: "mk4", domeVariant: "complex", bodyDesign: "mk4", bodyVariant: "complex", fitted: ["dataPanel", "psiFront"] },
    aux_led_pin: 2,
    aux_led_count: 16,
  },
  outputs: [
    { address: "ledc:7", name: "GPIO 49", parts: ["utilUp"] },
    { address: "ledc:9", name: "ARM4", parts: ["dataPanel"] },
    { address: "ledc:4", name: "GPIO 5", parts: [] },
  ],
  status: { auxLed: { pin: 2, r: 0, g: 90, b: 255, effect: "solid", available: true } },
});

export const boot = ({ answer = droid() } = {}) => {
  const parsed = new MiniDOMParser().parseFromString(readFileSync(join(dataDir, "lights.html"), "utf8"));
  const tokens = rootTokens();
  const posts = [];
  const sections = new Map();
  const timers = [];
  let streamListener = null;
  let lastStatus = answer.status;

  const documentMock = {
    documentElement: parsed.documentElement,
    getElementById: (id) => parsed.getElementById(id),
    querySelectorAll: (selector) => {
      try {
        return parsed.querySelectorAll(selector);
      } catch {
        return [];
      }
    },
    querySelector: (selector) => {
      try {
        return parsed.querySelector(selector);
      } catch {
        return null;
      }
    },
    createElement: (tag) => parsed.createElement(tag),
    addEventListener() {},
    body: parsed.body,
  };

  const windowMock = {
    document: documentMock,
    PAApi: {
      messageFor: (error) => String(error?.message || error),
      get: async (path) => {
        if (path === "/api/config") return { ok: true, data: answer.config };
        if (path === "/api/servo/outputs") return { ok: true, data: { outputs: answer.outputs } };
        if (path === "/api/status") return { ok: true, data: lastStatus };
        throw new Error(`unexpected GET ${path}`);
      },
      postForm: async (path, body) => {
        posts.push({ path, body });
        return { ok: true, data: answer.config };
      },
    },
    PABootstrap: {
      registerSection: (name, load) => sections.set(name, load),
      setResourceLabels() {},
    },
    PAStatusStream: {
      isSupported: () => true,
      getLastStatus: () => lastStatus,
      subscribe: (listener) => {
        streamListener = listener;
        return () => {
          streamListener = null;
        };
      },
    },
    PASurface: { poll: () => ({ start() {}, stop() {} }) },
    getComputedStyle: () => ({ getPropertyValue: (name) => tokens.get(name) || "" }),
    addEventListener() {},
    // Recorded, never run by a clock: a test that waited on real time could
    // pass by timing out.
    setTimeout: (fn) => {
      timers.push(fn);
      return timers.length;
    },
    clearTimeout() {},
    location: { origin: "http://device", href: "http://device/lights.html" },
    CustomEvent: class {
      constructor(type, init = {}) {
        this.type = type;
        this.detail = init.detail;
      }
    },
  };

  const context = {
    window: windowMock,
    document: documentMock,
    getComputedStyle: windowMock.getComputedStyle,
    console: { log() {}, warn() {}, error() {}, info() {} },
    setTimeout: windowMock.setTimeout,
    clearTimeout: windowMock.clearTimeout,
    URLSearchParams,
    CustomEvent: windowMock.CustomEvent,
  };
  context.globalThis = context;
  for (const file of SCRIPTS) {
    vm.runInNewContext(readFileSync(join(dataDir, file), "utf8"), context, { filename: file });
  }

  const settle = async () => {
    for (let turn = 0; turn < 8; turn += 1) await new Promise((resolve) => setImmediate(resolve));
  };

  return {
    parsed,
    posts,
    window: windowMock,
    settle,
    // Everything the surface registered with the bootstrap, as it runs them.
    runSections: () => Promise.all([...sections.values()].map((load) => load())),
    // A status frame arriving on the stream the surface subscribed to.
    pushStatus: (status) => {
      lastStatus = status;
      streamListener?.("status", status);
    },
    // The settles the surface asked for, run when a test wants them.
    flushTimers: () => {
      const due = timers.splice(0, timers.length);
      due.forEach((fn) => fn());
    },
    plateFor: (partId) => parsed.querySelectorAll(".light-plate").find((node) => node.dataset.part === partId) || null,
    // Everything a builder can read on the surface.
    text: () => parsed.body.textContent,
  };
};
