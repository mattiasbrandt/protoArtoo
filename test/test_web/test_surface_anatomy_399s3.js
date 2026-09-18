// =============================================================================
// test/test_web/test_surface_anatomy_399s3.js
//
// The Surface Anatomy on the Drive and Perform group: Foot Drive, Dome, Sound,
// RC Control and Sequences (#399 slice 3, ADR 0066).
//
// What is mechanical about the anatomy on these five surfaces, and nothing
// else. Three kinds of claim, because the sweep makes three:
//
//   A SECTION HEAD CARRIES A COUNT OR A STATE. docs/ui-copy-voice.md rule 8
//   says a heading never appears bare, and every subtitle this slice writes is
//   computed from what the droid answered rather than typed into the markup. A
//   constant would look right on every screenshot and be wrong on every droid,
//   so each one is driven with at least two different answers.
//
//   A STATE IS ON THE LIGHT, NEVER ON A DIRECTION OR A POSTURE. CONTEXT.md
//   "Status Colour" reserves the four signal colours for how a thing is doing
//   right now. This slice took colour off six things that were not that: the
//   dome motor's on/off (an Availability Family), the dome's rotation direction
//   (a value), the live bar's fill (the same value), consent to drive from a
//   browser (a chosen posture), an RC source nobody switched on, and Sound's
//   "playing". The assertions below are on the words AND on the absence of the
//   class or the inline style beside them, because a renderer that stopped
//   writing the word and went on painting the element would pass half of it.
//
//   A GLYPH THAT CARRIED A CONTROL IS DRAWN OR NAMED. ADR 0066 lets an icon
//   stand beside a label, never instead of one. The step card's open marker and
//   Foot Drive's pad are drawn from the sprite; the remove button, the action
//   test button and the picker's radio are named or drawn.
//
// Foot Drive, Dome, Sound and RC Control run through helpers/page_module_env.js
// - the shipped module, a permissive DOM stub, and a responder standing in for
// the droid. Sequences runs through its own vm context here, the way
// test_seq_capacity_382.js does, because the list and the editor are reached
// through window.__seqEditorForTesting rather than through a section loader.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import vm from "node:vm";

import { loadPageModule } from "./helpers/page_module_env.js";

const here = dirname(fileURLToPath(import.meta.url));
const dataDir = join(here, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf8");

// -----------------------------------------------------------------------------
// Foot Drive: the Speed preset head says which limit the droid is sitting on
// -----------------------------------------------------------------------------

const loadDrive = (drive) =>
  loadPageModule("drive.js", {
    respond: () => ({ data: { drive, components: { drive: { enabled: true } } } }),
    overrides: {
      PAStatusStream: { isSupported: () => true, subscribe: () => () => {}, getLastStatus: () => null },
    },
  });

const PRESETS = { speedPresetSlow: 200, speedPresetNormal: 350, speedPresetTurbo: 600, webDriveTimeoutMs: 500 };

test("a limit that matches no preset says so rather than rounding itself to one", async () => {
  // 425 is between Normal and Turbo. The buttons below show nothing pressed, so
  // a head that claimed a preset would disagree with them.
  const env = loadDrive({ ...PRESETS, speedLimitMax: 425, speedPreset: null });
  await env.runSection("drive-configuration");
  await env.settle();

  assert.equal(env.element("preset-summary").textContent, "limit 425 · no preset matches");
});

// -----------------------------------------------------------------------------
// Foot Drive: the pad is a cross of arrows from the sprite, each with its word
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Dome: a direction is a value, and the motor's on/off is an Availability Family
// -----------------------------------------------------------------------------

const loadDome = () => {
  let deliver = null;
  const env = loadPageModule("dome.js", {
    respond: () => ({ data: { domeEsc: {}, components: { domeEsc: { enabled: true } } } }),
    overrides: {
      PAStatusStream: {
        isSupported: () => true,
        subscribe: (handler) => { deliver = handler; return () => {}; },
        getLastStatus: () => null,
      },
    },
  });
  env.status = (payload) => deliver("status", payload);
  return env;
};

// -----------------------------------------------------------------------------
// Dome panels on the Dashboard: the provenance banners are the anatomy's notes
// -----------------------------------------------------------------------------

function renderDomeCard(source) {
  const made = [];
  const makeStub = (className) => {
    const element = {
      className: className || "",
      dataset: {},
      innerHTML: "",
      textContent: "",
      inserted: [],
      children: [],
      listeners: {},
      classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
      setAttribute() {},
      getAttribute: () => null,
      addEventListener(name, fn) { (element.listeners[name] = element.listeners[name] || []).push(fn); },
      removeEventListener() {},
      appendChild(child) { element.children.push(child); return child; },
      insertAdjacentHTML(position, html) { element.inserted.push({ position, html }); },
      remove() {},
      contains: () => false,
      querySelector: () => null,
      querySelectorAll: () => [],
      focus() {},
      get firstElementChild() { return makeStub("banner"); },
    };
    made.push(element);
    return element;
  };

  const card = makeStub("dome-control-card");
  const header = makeStub("dome-control-header");
  const body = makeStub("dome-control-body");
  const feedback = makeStub("dome-control-feedback");
  card.querySelector = (selector) => {
    if (selector === ".dome-control-header") return header;
    if (selector === ".dome-control-body") return body;
    if (selector === ".dome-control-feedback") return feedback;
    return makeStub();
  };

  const sandbox = {
    PAAssetsReady: true,
    addEventListener() {},
    DOME_PANEL_MAP_SVG: '<svg class="vendored-mk4"></svg>',
    DomeLayout: {
      load: () => Promise.resolve(),
      getModel: () => ({ elements: [], complementKnown: true, usesVendoredDrawing: true }),
      getSource: () => source,
      onChange() {},
    },
    DomeLayoutRender: { renderPicker: () => "" },
    DomeCommandMap: { decodeCommandToElement: () => null },
    PAApi: {
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      get: () => Promise.resolve({ ok: true, data: [] }),
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml: (value) => String(value), escapeAttr: (value) => String(value) },
    document: {
      body: { dataset: { page: "home" } },
      getElementById: (id) => (id === "dome-control-card" ? card : null),
      createElement: (tag) => makeStub(tag),
      addEventListener() {},
    },
    setTimeout,
    clearTimeout,
    Promise,
    console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(readData("dome_control.js"), sandbox, { filename: "dome_control.js" });

  return {
    expand: async () => {
      await header.listeners.click[0]({ target: header });
      return body.inserted.map((entry) => entry.html).join("");
    },
  };
}

// -----------------------------------------------------------------------------
// Sound: the play state is a word, and green is the link's alone
// -----------------------------------------------------------------------------

const loadSound = (audio) =>
  loadPageModule("sound.js", {
    respond: (path) => {
      if (path === "/api/audio") return { data: audio };
      // The surface polls /api/status too, and a frame with no audio key is how
      // the controller says the component is switched off. Keeping it present
      // is what leaves the module status path reachable.
      if (path === "/api/status") return { data: { audio: { link_ok: audio.link_ok } } };
      return { data: {} };
    },
    overrides: {
      PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    },
  });

const AUDIO_BASE = { driver: "CHIRP", capabilities: 0x3f, link_ok: true, device: "CHIRP v1.4", total_tracks: 412 };

test("a module that is not answering says so, and that is the branch red is for", async () => {
  const env = loadSound({ ...AUDIO_BASE, link_ok: false, play_state: "stopped" });
  await env.runSection("audio-status");
  await env.settle();

  assert.equal(env.element("sound-state-badge").textContent, "No module response");
  assert.equal(env.element("sound-state-badge").dataset.state, "error");
});

// -----------------------------------------------------------------------------
// RC Control: three inputs as health rows, and two heads that count
// -----------------------------------------------------------------------------

const loadRc = (config, diagnostics) =>
  loadPageModule("rc.js", {
    respond: (path) => {
      if (path === "/api/config") return { data: config };
      if (path === "/api/rc/map") return { data: { mode: config?.rc?.inputMode, map: config?.__map || [] } };
      if (path === "/api/rc") return { data: diagnostics };
      return { data: {} };
    },
    overrides: {
      PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    },
  });

const RC_COMPONENTS = { rcCh1: { enabled: true }, rcCh2: { enabled: false } };

test("a receiver nobody switched on reads unlit, never green", async () => {
  const env = loadRc(
    { rc: { inputMode: "single_sbus" }, components: RC_COMPONENTS },
    { sources: { sbus1: { enabled: true, linked: true, ageMs: 12 },
                 sbus2: { enabled: true, linked: false, ageMs: 900 },
                 pwm: { enabled: false } } },
  );
  await env.runSection("rc-diagnostics");
  await env.settle();

  const html = env.element("rc-preview-source-health").innerHTML;
  assert.match(html, /<div class="indicator ok"[^>]*><\/div>\s*<span>SBUS1<\/span>/, "a linked receiver is nominal");
  assert.match(html, /<div class="indicator warn"[^>]*><\/div>\s*<span>SBUS2<\/span>/, "a receiver still waiting is degraded");
  assert.match(html, /<div class="indicator off"[^>]*><\/div>\s*<span>PWM<\/span>/, "a source never asked reads grey");
  assert.doesNotMatch(html, /indicator ok"[^>]*><\/div>\s*<span>PWM/, "a source nobody switched on read as nominal");
  assert.match(html, /not switched on/);
});

// -----------------------------------------------------------------------------
// Sequences: a step is its name, and nothing on the list reports state in blue
// -----------------------------------------------------------------------------

const SEQ_CHAIN = [
  "droid_parts.js",
  "droid_build.js",
  "dome_command_map.js",
  "dome_panel_model.js",
  "dome_layout.js",
  "seq_protocol_check.js",
  "seq.js",
];

const escapeForSeq = (value) =>
  String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

function newSeqPage() {
  const elements = new Map();
  const makeStub = () => {
    const element = {
      dataset: {},
      style: {},
      value: "",
      innerHTML: "",
      textContent: "",
      children: [],
      classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
      setAttribute() {},
      getAttribute: () => null,
      addEventListener() {},
      removeEventListener() {},
      appendChild(child) { element.children.push(child); return child; },
      insertAdjacentHTML() {},
      remove() {},
      focus() {},
      closest: () => null,
      querySelector: () => null,
      querySelectorAll: () => [],
    };
    return element;
  };
  const elementById = (id) => {
    if (!elements.has(id)) elements.set(id, makeStub());
    return elements.get(id);
  };

  const sandbox = {
    PAAssetsReady: true,
    PAApi: {
      get: () => Promise.resolve({ ok: true, status: 200, data: [] }),
      postForm: () => Promise.resolve({ ok: true, data: {} }),
      postJson: () => Promise.resolve({ ok: true, data: {} }),
      messageFor: (error) => String(error && error.message),
    },
    PAUtils: { escapeHtml: escapeForSeq, escapeAttr: escapeForSeq, showFeedback() {}, debounce: (fn) => fn },
    PABootstrap: {
      registerSection() {}, setResourceLabels() {}, declareSections() {}, retryNow() {}, refreshSections() {},
    },
    PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    localStorage: { length: 0, key: () => null, getItem: () => null, setItem() {}, removeItem() {} },
    document: {
      readyState: "complete",
      body: makeStub(),
      documentElement: makeStub(),
      getElementById: elementById,
      querySelector: () => null,
      querySelectorAll: () => [],
      createElement: () => makeStub(),
      addEventListener() {},
      removeEventListener() {},
    },
    addEventListener() {},
    removeEventListener() {},
    alert() {},
    confirm: () => false,
    setTimeout, clearTimeout, setInterval, clearInterval, Promise, console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  SEQ_CHAIN.forEach((name) => vm.runInContext(readData(name), sandbox, { filename: name }));
  return { seam: sandbox.window.__seqEditorForTesting, elementById };
}

const SEQ_STEPS = [
  { t: 0, type: "audio", cmd: "$H" },
  { t: 400, type: "dome", cmd: ":OP01" },
  { t: 900, type: "dome", cmd: "DL:FLD:NORMAL:BLUE:5" },
  { t: 1400, type: "domeRotate", speedPct: 40, durationMs: 900 },
  { t: 2400, type: "end" },
];

