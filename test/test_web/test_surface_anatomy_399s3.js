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

test("the Speed preset head names the preset the droid answered with, and its limit", async () => {
  const env = loadDrive({ ...PRESETS, speedLimitMax: 350, speedPreset: "normal" });
  await env.runSection("drive-configuration");
  await env.settle();

  assert.equal(env.element("preset-summary").textContent, "Normal · limit 350");
});

test("a second droid on a different preset reads differently, so the head is not a constant", async () => {
  const env = loadDrive({ ...PRESETS, speedLimitMax: 600, speedPreset: "turbo" });
  await env.runSection("drive-configuration");
  await env.settle();

  assert.equal(env.element("preset-summary").textContent, "Turbo · limit 600");
});

test("a limit that matches no preset says so rather than rounding itself to one", async () => {
  // 425 is between Normal and Turbo. The buttons below show nothing pressed, so
  // a head that claimed a preset would disagree with them.
  const env = loadDrive({ ...PRESETS, speedLimitMax: 425, speedPreset: null });
  await env.runSection("drive-configuration");
  await env.settle();

  assert.equal(env.element("preset-summary").textContent, "limit 425 · no preset matches");
});

test("a preset is named in words alone - no snail, no lightning bolt", () => {
  const source = readData("drive.js");
  const labels = /const PRESET_LABELS = \{([\s\S]*?)\};/.exec(source);
  assert.ok(labels, "data/drive.js no longer declares PRESET_LABELS as this test reads it");
  assert.match(labels[1], /slow: "Slow"/);
  assert.match(labels[1], /normal: "Normal"/);
  assert.match(labels[1], /turbo: "Turbo"/);
});

// -----------------------------------------------------------------------------
// Foot Drive: the pad is a cross of arrows from the sprite, each with its word
// -----------------------------------------------------------------------------

test("every pad button carries a word, and the four directions carry a sprite arrow", () => {
  const html = readData("drive.html");
  const buttons = [...html.matchAll(/<button class="btn[^"]*dpad-btn[^"]*"[\s\S]*?<\/button>/g)].map((m) => m[0]);
  assert.equal(buttons.length, 5, "the pad is five buttons");

  const words = buttons.map((b) => b.replace(/<[^>]*>/g, "").trim());
  assert.deepEqual(words, ["Forward", "Left", "Stop", "Right", "Reverse"]);

  // Forward and reverse are the horizontal arrow turned a quarter, because the
  // sprite carries no vertical pair. The classes are what turns them.
  assert.match(buttons[0], /class="i dpad-icon dpad-up"[\s\S]*#i-arrow-right/);
  assert.match(buttons[1], /#i-arrow-left/);
  assert.match(buttons[3], /#i-arrow-right/);
  assert.match(buttons[4], /class="i dpad-icon dpad-down"[\s\S]*#i-arrow-right/);

  // The centre carries no icon at all: the one stop-circle in this product is
  // the Latching Estop in the topbar.
  assert.doesNotMatch(buttons[2], /<svg/);
  assert.doesNotMatch(buttons[2], /stop-circle/);
});

test("the sprite defines the two arrows the pad draws", () => {
  const shell = readData("shell.js");
  assert.match(shell, /"arrow-left": "M[\d,.\w ]+Z"/);
  assert.match(shell, /"arrow-right": "M[\d,.\w ]+Z"/);
});

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

test("the dome's rotation reading is the direction as a word, with the number beside it", () => {
  const env = loadDome();

  env.status({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: 0 });
  assert.equal(env.element("dome-rotation-state").textContent, "Idle");
  assert.equal(env.element("dome-speed-display").textContent, "0%");

  env.status({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: 0.42 });
  assert.equal(env.element("dome-rotation-state").textContent, "Forward");
  assert.equal(env.element("dome-speed-display").textContent, "42%");

  env.status({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: -0.42 });
  assert.equal(
    env.element("dome-rotation-state").textContent,
    "Reverse",
    "the word says the direction and the percentage says the size, so neither is printed twice",
  );
  assert.equal(env.element("dome-speed-display").textContent, "-42%");
});

test("neither direction takes a state class or paints the bar, because a direction is not a symptom", () => {
  const env = loadDome();

  env.status({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: 0.42 });
  const forwardFill = env.element("dome-live-fill").style.background;
  env.status({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: -0.42 });
  const reverseFill = env.element("dome-live-fill").style.background;

  assert.notEqual(
    typeof env.element("dome-rotation-state").className,
    "string",
    "the rotation reading took a state class, and forward is not healthier than reverse",
  );
  assert.equal(forwardFill, undefined, "the renderer painted the live bar instead of leaving it to the stylesheet");
  assert.equal(reverseFill, undefined, "the live bar changed colour with the direction");
});

test("the dome motor's own head follows the droid, and reads as a choice rather than a fault", () => {
  const env = loadDome();

  env.status({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: 0 });
  assert.equal(env.element("dome-hardware-state").textContent, "switched on");

  env.status({ webControlEnabled: true, domeEnabled: false, domeTargetSpeed: 0 });
  assert.equal(env.element("dome-hardware-state").textContent, "switched off in Setup");
  assert.notEqual(
    typeof env.element("dome-hardware-state").className,
    "string",
    "an Availability Family is told apart by treatment, never by hue (CONTEXT.md)",
  );
});

test("web control is reported as the feet's consent, in a line that follows the frame", () => {
  const env = loadDome();

  env.status({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: 0 });
  assert.match(env.element("dome-web-note").textContent, /^Web control is on\./);
  assert.match(env.element("dome-web-note").textContent, /the dome turns on the radio and inside a sequence either way/);

  env.status({ webControlEnabled: false, domeEnabled: true, domeTargetSpeed: 0 });
  assert.match(env.element("dome-web-note").textContent, /^Web control is off\./);
});

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

test("a cached dome layout is a plain note, because blue reports no state", async () => {
  const banner = await renderDomeCard("cached").expand();
  assert.match(banner, /class="note dome-source-banner"/);
  assert.doesNotMatch(banner, /note-act/, "nothing here is for the builder to act on");
  assert.match(banner, /Last known layout/);
});

test("a dome the builder can go and plug in takes the amber act note", async () => {
  const banner = await renderDomeCard("vendored").expand();
  assert.match(banner, /class="note note-act dome-source-banner"/);
  assert.match(banner, /Dome not reachable/);
});

test("a panel advisory is written at a level this stylesheet actually defines", () => {
  const source = readData("dome_control.js");
  const css = readData("style.css");
  const levels = [...source.matchAll(/showFeedback\((?:[^,]+),\s*'(\w+)'\)/g)].map((m) => m[1]);
  assert.ok(levels.includes("warning"), "no advisory is written at a warning level at all");
  assert.ok(
    !levels.includes("warn"),
    "'warn' is not a level .feedback has - an advisory written at it renders in the plain ink",
  );
  // And the level it is written at is one the stylesheet paints.
  new Set(levels).forEach((level) => {
    if (level === "info" || level === "") return;
    assert.match(css, new RegExp(`\\.feedback\\.${level}\\s*\\{`), `.feedback.${level} is not declared`);
  });
});

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

test("the module's play state is a bare word in every branch", async () => {
  for (const [playState, expected] of [["playing", "Playing"], ["paused", "Paused"], ["stopped", "Idle"]]) {
    const env = loadSound({ ...AUDIO_BASE, play_state: playState });
    await env.runSection("audio-status");
    await env.settle();
    assert.equal(env.element("sound-state-badge").textContent, expected);
  }
});

test("a module that is not answering says so, and that is the branch red is for", async () => {
  const env = loadSound({ ...AUDIO_BASE, link_ok: false, play_state: "stopped" });
  await env.runSection("audio-status");
  await env.settle();

  assert.equal(env.element("sound-state-badge").textContent, "No module response");
  assert.equal(env.element("sound-state-badge").dataset.state, "error");
});

test("playing is not a health state: only the link keeps green", () => {
  const css = readData("style.css");
  const rule = (state) =>
    new RegExp(`\\.sound-state-badge\\[data-state="${state}"\\]\\s*\\{\\s*color: var\\((--[\\w-]+)\\)`).exec(css);

  assert.equal(rule("ok")[1], "--success", "the module link is up, which is what green means");
  assert.equal(
    rule("playing")[1],
    "--text",
    "a module sitting idle is exactly as nominal as one playing a clip",
  );
  assert.equal(rule("idle")[1], "--text");
  assert.equal(rule("error")[1], "--danger");
  assert.equal(rule("disabled")[1], "--text-dim");
});

// Read rather than executed, deliberately, and the one test here that is. The
// permissive DOM stub's classList is a no-op, so which class name the renderer
// writes is not observable through it, and the claim IS the class name: .active
// is what .seg lights, .accent is the anatomy's primary act and this surface
// had two of them. The stylesheet rule is asserted beside it so the class that
// is written is a class that lights something.
test("the two workspaces are one segmented control, not two primary acts", () => {
  const source = readData("sound.js");
  const html = readData("sound.html");
  const css = readData("style.css");
  assert.match(css, /\.seg > button\.active,/);
  assert.match(source, /button\.classList\.toggle\("active", active\)/);
  assert.doesNotMatch(source, /button\.classList\.toggle\("accent", active\)/);
  assert.match(html, /<div class="seg" role="group" aria-label="Sound workspace">/);
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

test("a receiver that is linked says how old its last frame is", async () => {
  const env = loadRc(
    { rc: { inputMode: "single_sbus" }, components: RC_COMPONENTS },
    { sources: { sbus1: { enabled: true, linked: true, ageMs: 12 }, sbus2: {}, pwm: {} } },
  );
  await env.runSection("rc-diagnostics");
  await env.settle();

  assert.match(env.element("rc-preview-source-health").innerHTML, /linked · 12ms old/);
});

test("the Receiver type head names the receiver the droid answered with", async () => {
  const single = loadRc({ rc: { inputMode: "single_sbus" }, components: RC_COMPONENTS }, { sources: {} });
  await single.runSection("rc-mode-mapping");
  await single.settle();
  assert.equal(single.element("rc-mode-summary").textContent, "Single SBUS");

  const dual = loadRc({ rc: { inputMode: "dual_sbus" }, components: RC_COMPONENTS }, { sources: {} });
  await dual.runSection("rc-mode-mapping");
  await dual.settle();
  assert.equal(
    dual.element("rc-mode-summary").textContent,
    "Dual SBUS",
    "the head is computed from the droid's answer, so it cannot disagree with the lit card",
  );
});

test("the Bindings head counts the channels that are mapped", async () => {
  const none = loadRc({ rc: { inputMode: "single_sbus" }, components: RC_COMPONENTS, __map: [] }, { sources: {} });
  await none.runSection("rc-mode-mapping");
  await none.settle();
  assert.equal(none.element("rc-summary-count").textContent, "nothing mapped yet");

  const one = loadRc(
    { rc: { inputMode: "single_sbus" }, components: RC_COMPONENTS,
      __map: [{ source: "sbus1", channel: 1, action: "speed" }] },
    { sources: {} },
  );
  await one.runSection("rc-mode-mapping");
  await one.settle();
  assert.equal(one.element("rc-summary-count").textContent, "1 channel mapped");

  const two = loadRc(
    { rc: { inputMode: "single_sbus" }, components: RC_COMPONENTS,
      __map: [{ source: "sbus1", channel: 1, action: "speed" },
              { source: "sbus1", channel: 2, action: "steer" }] },
    { sources: {} },
  );
  await two.runSection("rc-mode-mapping");
  await two.settle();
  assert.equal(two.element("rc-summary-count").textContent, "2 channels mapped");
});

test("the action test button is named, and the picker's radio is drawn rather than typed", () => {
  const source = readData("rc.js");
  const css = readData("style.css");
  assert.match(source, /data-action-test="[^"]*"[^>]*\$\{inFlight \? ' disabled' : ''\}>Try it<\/button>/);
  assert.match(source, /<span class="rc-action-radio" aria-hidden="true"><\/span>/);
  assert.doesNotMatch(source, /rc-action-radio[^\n]*[●○]/);
  assert.match(css, /\.rc-action-row\.selected \.rc-action-radio \{/);
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

test("a step card is its type NAME, with no glyph standing beside the word", () => {
  const { seam, elementById } = newSeqPage();
  seam.renderEditorView({ name: "DM:WAKE", toggleGroup: "none", suppressMs: 8000, steps: SEQ_STEPS });
  const html = elementById("seq-editor-view").innerHTML;

  ["Sound", "Panel Action", "Logic / PSI Mode", "Spin Dome", "Sequence End"].forEach((name) => {
    assert.match(html, new RegExp(`<span class="step-card-type">${name.replace(/\//g, "\\/")}</span>`));
  });
  assert.doesNotMatch(html, /step-card-emoji/, "the emoji span survived the sweep");
  assert.doesNotMatch(html, /step-type-card-emoji/);
  assert.doesNotMatch(html, /step-type-reference-emoji/);
});

test("the step card's open marker and its remove button are drawn and named", () => {
  const { seam, elementById } = newSeqPage();
  seam.renderEditorView({ name: "DM:WAKE", toggleGroup: "none", suppressMs: 8000, steps: SEQ_STEPS });
  const html = elementById("seq-editor-view").innerHTML;

  assert.match(html, /<svg class="i chev"[^>]*><use href="#i-chevron-right"\/><\/svg>/);
  assert.match(html, /<button class="step-remove" type="button" tabindex="-1">Remove<\/button>/);
  assert.doesNotMatch(html, /[▼▶×]/, "a geometric character is still carrying a control");
});

test("the editor's head names the sequence and counts its steps", () => {
  const { seam, elementById } = newSeqPage();

  seam.renderEditorView({ name: "DM:WAKE", toggleGroup: "none", suppressMs: 8000, steps: SEQ_STEPS });
  assert.match(elementById("seq-editor-view").innerHTML, /<h2>Editing<\/h2><span class="sub">DM:WAKE &middot; 5 steps<\/span>/);

  seam.renderEditorView({ name: "DM:ONE", toggleGroup: "none", suppressMs: 8000, steps: [{ t: 0, type: "end" }] });
  assert.match(
    elementById("seq-editor-view").innerHTML,
    /<h2>Editing<\/h2><span class="sub">DM:ONE &middot; 1 step<\/span>/,
    "the count follows the sequence, and one step is not 1 steps",
  );
});

test("the list's empty section carries no colour of its own", () => {
  const { seam, elementById } = newSeqPage();
  // No learned sequences, one factory sequence: the populated state renders and
  // the "your sequences" section is the one that is empty.
  seam.renderListWithMocks([], [{ name: "DM:STAND", stepCount: 5, toggleGroup: "none", suppressMs: 500 }]);
  const html = elementById("seq-cards-container").innerHTML;

  assert.match(html, /<p class="prose seq-section-empty"><b>Nothing of your own yet\.<\/b>/);
  assert.doesNotMatch(html, /style="color:/, "a colour literal is back inside a renderer");
  assert.doesNotMatch(html, /#[0-9a-fA-F]{3,6}\b/, "a colour literal is back inside a renderer");
});

test("nothing on a sequence card reports a state in the interaction blue", () => {
  const css = readData("style.css");
  const nameRule = /\.seq-card-header h4 \{[^}]*color: var\((--[\w-]+)\)/.exec(css);
  assert.equal(nameRule[1], "--text", "a sequence's name is a name, not a link");

  const badgeRule = /\.seq-badge \{[^}]*\}/.exec(css)[0];
  assert.doesNotMatch(badgeRule, /--accent/, "a badge names a kind of thing and takes no colour");

  const factoryRule = /\.seq-card-factory \{[^}]*\}/.exec(css)[0];
  assert.doesNotMatch(factoryRule, /border-left/, "an Availability Family is told apart by treatment, never by hue");
});
