// =============================================================================
// test/test_web/helpers/lights_surface.js
//
// Lights as the browser runs it: data/lights.html, its own script chain, and a
// controller that answers the two reads the surface makes - GET /api/config and
// GET /api/servo/outputs. What the strips are showing reaches it the way it
// reaches every surface: a status frame through the shipped status stream into
// the shipped Live Reading, started the way the Operator Shell starts it.
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
import { servoRow, describe, applyRowSave, statusFrame } from "./fake_droid.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, "../../..");
const dataDir = join(root, "data");

const SCRIPTS = [
  "status_stream.js",
  "live_reading.js",
  "droid_parts.js",
  "droid_part_kind.js",
  "droid_build.js",
  "seq_protocol_check.js",
  "outputs.js",
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
// carries a light and a Part sits on it. Its Outputs come from the one fake
// droid (helpers/fake_droid.js), whose ids follow no pattern tied to a label
// or an address.
//
// `idOf(address)` is the stored id an Output's row carries, which is how the
// firmware keys its own status frame.
export const droid = () => {
  const outputs = describe([
    servoRow("ledc:7", "GPIO 49", { parts: ["utilUp"] }),
    servoRow("ledc:9", "ARM4", { parts: ["dataPanel"] }),
    servoRow("ledc:4", "GPIO 5"),
  ], {
    "ledc:9": { lightCapable: true, ledCount: 16, type: "rgb" },
    "ledc:4": { lightCapable: true, wired: false, type: "none" },
  });
  const idOf = (address) => outputs.find((row) => row.address === address).id;
  return {
    config: {
      components: { domeEsc: { enabled: true, label: "DOME" } },
      droidBuild: { domeDesign: "mk4", domeVariant: "complex", bodyDesign: "mk4", bodyVariant: "complex", fitted: ["dataPanel", "psiFront"] },
    },
    outputs,
    idOf,
    status: statusFrame({ lights: { [idOf("ledc:9")]: { r: 0, g: 90, b: 255, effect: "solid", available: true } } }),
  };
};

// The same droid with a SECOND lit wire, and a Part on it: what #413 exists
// for, and the shape a page matching a reading to a wire by position gets
// wrong. The two wires are given different colors so each plate can be checked
// against its own.
export const droidWithTwoLitWires = () => {
  const answer = droid();
  const second = answer.outputs[2];
  second.wired = true;
  second.component = "rgb";
  second.ledCount = 4;
  second.parts = ["cbi"];
  answer.config.droidBuild.fitted.push("cbi");
  answer.status.lights[answer.idOf("ledc:4")] = { r: 255, g: 0, b: 0, effect: "blink", available: true };
  return answer;
};

export const boot = ({ answer = droid() } = {}) => {
  const parsed = new MiniDOMParser().parseFromString(readFileSync(join(dataDir, "lights.html"), "utf8"));
  const tokens = rootTokens();
  const posts = [];
  const sections = new Map();
  const timers = [];

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
        throw new Error(`unexpected GET ${path}`);
      },
      postForm: async (path, body) => {
        posts.push({ path, body });
        return { ok: true, data: answer.config };
      },
      // An Output's settings, as its row (ADR 0068).
      postJson: async (path, body) => {
        posts.push({ path, body });
        if (path === "/api/config") applyRowSave(answer.outputs, body);
        return { ok: true, data: answer.config };
      },
    },
    PABootstrap: {
      registerSection: (name, load) => sections.set(name, load),
      setResourceLabels() {},
    },
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
    // A browser has one, so the Live Reading takes the stream rather than a
    // poll. It never opens: the assets are never announced ready here.
    EventSource: class {
      addEventListener() {}
      close() {}
    },
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
    // The Operator Shell starts the Live Reading before any surface runs, and
    // its boot read hands the droid's first frame to the stream.
    if (file === "live_reading.js") {
      windowMock.PALiveReading.start();
      if (answer.status) windowMock.PAStatusStream.seed(answer.status);
    }
  }

  const settle = async () => {
    for (let turn = 0; turn < 8; turn += 1) await new Promise((resolve) => setImmediate(resolve));
  };

  return {
    parsed,
    posts,
    answer,
    window: windowMock,
    settle,
    // Everything the surface registered with the bootstrap, as it runs them.
    runSections: () => Promise.all([...sections.values()].map((load) => load())),
    // A status frame arriving on the stream: a whole frame with `changes` on
    // top, through the shipped stream and Live Reading.
    pushStatus: (changes) => windowMock.PAStatusStream.seed(statusFrame(changes)),
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
