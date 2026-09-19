// =============================================================================
// test/test_web/helpers/configuration_surface.js
//
// Configuration as the browser runs it, for the suites about what is drawn on
// it (#369): data/configuration.html with one asset set's drawings inlined,
// Configuration's own chain with guided Setup over it, and a controller that
// answers GET /api/identity/components row for row from
// include/component_registry.inc - so a registry row added tomorrow is a card
// in every suite that uses this, and none of them restates the lineup.
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
  "feature_availability.js",
  "product_art.js",
  "apply_timing.js",
  "component_picker.js",
  "configuration.js",
  "setup.js",
];

const INCLUDE_RE = /<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+)\s*-->/g;

// Configuration as one asset set's build serves it: that set's drawings
// inlined, the recovery kernel left as the comment it is.
const surfaceDocument = (set) => {
  const page = readFileSync(join(dataDir, "configuration.html"), "utf8").replace(INCLUDE_RE, (directive, target) =>
    target === "_product_art.html" ? readFileSync(join(dataDir, "asset-sets", set, target), "utf8") : directive,
  );
  return new MiniDOMParser().parseFromString(page);
};

// GET /api/identity/components, read out of the registry itself.
const lineupFor = (board) => {
  const inc = readFileSync(join(root, "include", "component_registry.inc"), "utf8");
  const categories = [...inc.matchAll(/^PA_COMPONENT_CATEGORY\((\w+),\s*"(\w+)",\s*"([^"]+)",\s*(nullptr|"\w+")\)/gm)].map(
    ([, enumerator, id, name, key]) => ({ enumerator, id, name, member_key: key === "nullptr" ? null : key.slice(1, -1) }),
  );
  const byEnum = new Map(categories.map((category) => [category.enumerator, category.id]));
  const parts = [...inc.matchAll(/^PA_COMPONENT_PART\(\s*\d+,\s*"(\w+)",\s*"([^"]+)",\s*(\w+),\s*"(\w+)",\s*COMPONENT_STATUS_(\w+),/gm)].map(
    ([, id, name, category, protocol, status]) => {
      let included = status === "SUPPORTED";
      if (id === "artoo_pcb") included = board === "artoo_esp32";
      if (id === "firebeetle2") included = board === "firebeetle2";
      return { id, name, category: byEnum.get(category), protocol, status: status.toLowerCase(), included };
    },
  );
  return {
    categories: categories.map(({ id, name, member_key: memberKey }) => ({
      id,
      name,
      member_key: memberKey,
      active_member: id === "sound" ? "dy_sv5w" : id === "radio_controller" ? "hotrc_ds650" : null,
    })),
    parts,
  };
};

const configured = () => ({
  components: {
    domeEsc: { enabled: false },
    rcCh1: { enabled: true },
    rcCh2: { enabled: true },
    rcCh3: { enabled: false },
    rcCh4: { enabled: false },
    rcCh5: { enabled: false },
    rcCh6: { enabled: false },
    drive: { enabled: true },
    audio: { enabled: true, member: "dy_sv5w", activeMember: "dy_sv5w" },
    protoR2link: { enabled: true },
  },
  rc: { member: "hotrc_ds650", inputMode: "dual_sbus", sbus: { recvCh2: false } },
  system: { logLevel: 3 },
  aux_led_pin: 2,
  aux_led_count: 16,
  wifi: { provisioned: true, mode: "client", staSsid: "bench" },
  guidedSetup: { run: "not-run", recorded: true, visited: [] },
});

const stub = () => ({
  id: "",
  dataset: {},
  style: {},
  className: "",
  classList: { add() {}, remove() {}, contains: () => false },
  textContent: "",
  value: "",
  checked: false,
  disabled: false,
  hidden: false,
  addEventListener() {},
  setAttribute() {},
  removeAttribute() {},
  querySelectorAll: () => [],
  querySelector: () => null,
  closest: () => null,
  appendChild() {},
});

export const boot = ({ set = "legacy", board = "artoo_esp32", assetsReady = true, config = configured() } = {}) => {
  const parsed = surfaceDocument(set);
  const lineup = lineupFor(board);
  const posts = [];
  const sections = new Map();
  const timers = [];
  let attached = true;

  // The surface's own document. `attached` is the shell taking the surface out
  // of the document while the builder reads another: its nodes live on, and
  // an id lookup finds none of them.
  const documentMock = {
    getElementById: (id) => (attached ? parsed.getElementById(id) : null),
    querySelectorAll: (selector) => {
      try {
        return parsed.querySelectorAll(selector);
      } catch {
        return [];
      }
    },
    querySelector: (selector) => {
      try {
        return parsed.querySelector(selector) || stub();
      } catch {
        return stub();
      }
    },
    createElement: (tag) => parsed.createElement(tag),
    createElementNS: (_ns, tag) => parsed.createElement(tag),
    addEventListener() {},
    removeEventListener() {},
    body: parsed.body,
  };

  const apply = (body) => {
    const read = (key) => (body instanceof URLSearchParams ? body.get(key) : body[key]) ?? null;
    if (read("soundMember") !== null) config.components.audio.member = read("soundMember");
    if (read("rcMember") !== null) config.rc.member = read("rcMember");
    if (read("rcInputMode") !== null) config.rc.inputMode = read("rcInputMode");
    for (const [field, key] of [["enableAudio", "audio"], ["enableDrive", "drive"], ["enableProtoR2link", "protoR2link"], ["enableDomeEsc", "domeEsc"]]) {
      if (read(field) !== null) config.components[key].enabled = read(field) === "true";
    }
    return read;
  };

  const windowMock = {
    document: documentMock,
    PAAssetsReady: assetsReady,
    PAIdentity: { droidName: "artoo", mdnsUseName: false, board, board_capabilities: {}, build_flags: {} },
    PAApi: {
      messageFor: (error) => String(error?.message || error),
      get: async (path) => {
        if (path === "/api/config") return { ok: true, data: config };
        if (path === "/api/identity/components") return { ok: true, data: lineup };
        throw new Error(`unexpected GET ${path}`);
      },
      postForm: async (path, body) => {
        const read = apply(body);
        posts.push({ path, get: read });
        return { ok: true, data: config };
      },
    },
    PABootstrap: {
      registerSection: (name, load) => sections.set(name, load),
      setResourceLabels() {},
      refreshSections() {},
    },
    PASurface: { poll: () => ({ start() {}, stop() {}, cancelRetry() {} }) },
    addEventListener() {},
    removeEventListener() {},
    // Recorded, never run by a clock: a test that waited on real time could
    // pass by timing out.
    setTimeout: (fn) => {
      timers.push(fn);
      return timers.length;
    },
    clearTimeout() {},
    location: { origin: "http://device", href: "http://device/configuration.html" },
    CustomEvent: class {
      constructor(type, init = {}) {
        this.type = type;
        this.detail = init.detail;
      }
    },
    Event: class {},
  };

  const context = {
    window: windowMock,
    document: documentMock,
    console: { log() {}, warn() {}, error() {}, info() {} },
    setTimeout: windowMock.setTimeout,
    clearTimeout: windowMock.clearTimeout,
    URLSearchParams,
    CustomEvent: windowMock.CustomEvent,
    Event: windowMock.Event,
  };
  context.globalThis = context;
  for (const file of SCRIPTS) vm.runInNewContext(readFileSync(join(dataDir, file), "utf8"), context, { filename: file });

  const host = (family) => parsed.querySelector(`[data-component-family="${family}"]`);
  const plate = (family, option) => host(family).querySelector(`[data-option="${option}"]`);

  return {
    parsed,
    posts,
    config,
    // What the surface's modules published on window, for a test that reads
    // one of them the way another page does (the RC page's shown cards).
    window: windowMock,
    host,
    plate,
    // The run's rail, read the way a builder reads it.
    railAnswer: (step) => parsed.getElementById("wizard-rail").querySelector(`[data-step="${step}"]`)
      ?.querySelector(".wizard-chip-answer")?.textContent,
    settle: async () => {
      for (let turn = 0; turn < 6; turn += 1) await new Promise((resolve) => setImmediate(resolve));
    },
    // Everything the surface registered with the bootstrap, as it runs them.
    runSections: () => Promise.all([...sections.values()].map((load) => load())),
    leave: () => {
      attached = false;
    },
    returnTo: () => {
      attached = true;
    },
    // A plate's own press, if it has one: the <button> directly inside it.
    press: (element) => element.children.find((child) => child.tagName === "BUTTON") || null,
  };
};

export const ready = async (options) => {
  const env = boot(options);
  await env.runSections();
  await env.settle();
  return env;
};

