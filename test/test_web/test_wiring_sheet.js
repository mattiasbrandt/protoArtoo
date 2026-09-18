// =============================================================================
// test/test_web/test_wiring_sheet.js
//
// Behaviour of the Wiring surface (#350): the sheet says what this image will
// actually drive, and it says it from what the droid answered.
//
// The shipped Operator Shell boots the shipped surface against a fake droid,
// the way test_outputs_table.js does inside its own file. Nothing here
// bends the DOM to the code under test; the mocks stand in for a controller
// and for nothing else (test/test_web/README.md). The catalog is the REAL
// data/droid_parts.js, because the headline claim of this ticket is a count
// taken off that catalog and a hand-written stand-in would prove a fixture.
// =============================================================================
const test = require("node:test");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");

const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);

// The artoo-esp32 arm of include/config.h, as GET /api/identity reports it
// (docs/api.md:116). Kept as the shipped example rather than as round numbers,
// so a lane the sheet prints can be read back against the board.
const LANES = {
  drive: { uart: 1, tx: 16, rx: 17 },
  audio: { uart: 2, tx: 26, rx: 35 },
  protor2link: { uart: 2, tx: 33, rx: 34 },
};

const identity = (lanes = LANES, capabilities = {}) => ({
  droidName: "artoo",
  board: "artoo_esp32",
  board_capabilities: { PA_CAP_DEDICATED_AUDIO_UART: false, ...capabilities },
  board_lanes: lanes,
  build_flags: { PA_HEAP_PROFILE: false },
});

// One Servo Output row as GET /api/servo/outputs answers it. Only the four
// fields the sheet reads are here; the rest of the row is the calibration
// dial's and is not this surface's business.
const output = (address, name, extra = {}) => ({
  address,
  name,
  parts: [],
  component: "none",
  bandLoUs: 1000,
  bandHiUs: 2000,
  openUs: 2000,
  centreUs: 1500,
  closeUs: 1000,
  calibrated: false,
  held: false,
  limp: "off",
  commandedUs: null,
  targetUs: null,
  nudgesDone: 0,
  ...extra,
});

// A controller nobody has configured: the five LEDC rows
// servoOutputTableDefaults() seeds, every one with an empty Part list
// (include/servo_output_row.h:585-604, docs/api.md:663).
const freshOutputs = () => [
  output("ledc:0", "ARM1", { component: "mg996r" }),
  output("ledc:1", "ARM2", { component: "mg996r" }),
  output("ledc:3", "AUX1"),
  output("ledc:4", "AUX2"),
  output("ledc:5", "AUX3"),
];

// A fresh controller's Component Toggles: everything off, which is what an
// unprovisioned controller carries (CONTEXT.md "Setup").
const freshComponents = () => ({
  arm1: { enabled: false, type: "mg996r", label: "ARM1" },
  arm2: { enabled: false, type: "mg996r", label: "ARM2" },
  aux1: { enabled: false, type: "none", label: "AUX1" },
  aux2: { enabled: false, type: "none", label: "AUX2" },
  aux3: { enabled: false, type: "none", label: "AUX3" },
  drive: { enabled: false, label: "S1" },
  audio: { enabled: false, label: "S2" },
  protoR2link: { enabled: false, label: "S3" },
  domeEsc: { enabled: false, label: "DOME" },
});

const boot = async ({
  outputs = freshOutputs(),
  components = freshComponents(),
  manifest = identity(),
} = {}) => {
  const { MiniDocument, MiniDOMParser } = await import("./helpers/mini_dom.js");

  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) =>
    document.body.appendChild(document.importNode(child, true))
  );
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = { document, outputs, components, manifest, posts: [], gets: [] };
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
        env.gets.push(path);
        if (path === "/api/identity") return { data: env.manifest };
        if (path === "/api/status") return { data: { estop: false } };
        if (path === "/api/servo/outputs") {
          return { data: { outputs: structuredClone(env.outputs) } };
        }
        if (path === "/api/config") {
          return { data: { components: structuredClone(env.components) } };
        }
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      // Wiring writes nothing, so every write door records the attempt and
      // nothing else: a test can then assert the surface never knocked on one.
      postForm: async (path, form) => {
        env.posts.push({ path, form: { ...form } });
        return { ok: true, status: 200, data: { ok: true } };
      },
      postJson: async (path, body) => {
        env.posts.push({ path, body });
        return { ok: true, status: 200, data: { ok: true } };
      },
      estopPostForm: async (path) => {
        env.posts.push({ path, form: {} });
        return { data: { ok: true } };
      },
      messageFor: (error) => error.message,
      gateControls: (elements, enabled) => {
        elements.forEach((el) => {
          if (!el) return;
          el.disabled = !enabled;
          el.setAttribute("aria-disabled", enabled ? "false" : "true");
        });
      },
    },
    PAUtils: {
      escapeHtml: (value) =>
        String(value ?? "")
          .replace(/&/g, "&amp;")
          .replace(/</g, "&lt;")
          .replace(/>/g, "&gt;")
          .replace(/"/g, "&quot;"),
      escapeAttr: (value) => String(value ?? "").replace(/"/g, "&quot;"),
      showFeedback: (el, text, level = "") => {
        if (!el) return;
        el.textContent = text;
        el.className = level ? `feedback ${level}` : "feedback";
      },
      debounce: (fn) => fn,
    },
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
    structuredClone,
    Event: FakeEvent,
    CustomEvent: FakeCustomEvent,
    DOMParser: class {
      parseFromString(html, type) {
        return new MiniDOMParser().parseFromString(html, type);
      }
    },
    EventSource: class {
      addEventListener() {}
      close() {}
    },
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
  context.globalThis = context;

  const REAL_SCRIPTS = {
    "/shell.js": readData("shell.js"),
    "/status_stream.js": readData("status_stream.js"),
    "/droid_parts.js": readData("droid_parts.js"),
    "/wiring.js": readData("wiring.js"),
  };
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
  env.window = windowMock;

  env.sections = () => document.querySelectorAll(".wiring-tier");
  env.section = (tier) => env.sections().find((node) => node.dataset.tier === tier);
  env.tierIds = () => env.sections().map((node) => node.dataset.tier);
  // mini_dom's selector grammar has no tag-with-a-digit, and a shared harness
  // is not this slice's to widen: the heading is the first thing in the section
  // head, which the anatomy already requires it to be.
  env.headingOf = (tier) =>
    env.section(tier).querySelector(".sect").children[0].textContent.trim();
  env.countOf = (tier) => env.section(tier).querySelector(".sub").textContent.trim();
  env.rowsOf = (tier) =>
    env.section(tier).querySelectorAll(".wiring-row");
  env.loomRows = () => document.getElementById("wiring-loom").querySelectorAll(".wiring-row");
  env.loomRow = (key) => env.loomRows().find((node) => node.dataset.lane === key);
  env.diagrams = () => document.querySelectorAll(".wd");
  env.footnote = () => document.getElementById("wiring-footnote").textContent;
  env.summary = () => document.getElementById("wiring-summary").textContent;
  env.loomSummary = () => document.getElementById("wiring-loom-summary").textContent;
  env.promise = () => document.getElementById("wiring-promise").textContent;
  env.rail = () => document.getElementById("wiring-rail").textContent;
  env.bound = () => document.querySelector(".wiring-bound")?.textContent ?? "";
  // Everything a builder can read on the mounted surface, chrome included.
  env.surfaceText = () => document.querySelector("[data-surface]").textContent;
  // The shell leaves this surface and comes back to it, which is what the
  // sheet has to survive: a Part moved on Parts and then read here.
  env.leaveAndReturn = async () => {
    windowMock.location.hash = "#home";
    await sleep(60);
    windowMock.location.hash = "#wiring";
    await sleep(80);
  };

  windowMock.location.hash = "#wiring";
  const deadline = Date.now() + 3000;
  while (env.sections().length === 0) {
    if (Date.now() > deadline) assert.fail("the Wiring surface never mounted and painted its rows");
    await sleep(5);
  }
  await sleep(20);
  return env;
};

// ---------------------------------------------------------------------------
// The headline: a fresh droid reads honestly empty
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The tier token, and the why
// ---------------------------------------------------------------------------

test("switching an output off moves its part without touching the wiring", async () => {
  const wired = [output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" })];
  const on = await boot({
    outputs: structuredClone(wired),
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });
  assert.deepEqual(on.rowsOf("driven").map((row) => row.dataset.part), ["utilUp"]);

  const off = await boot({
    outputs: structuredClone(wired),
    components: { ...freshComponents(), arm1: { enabled: false, label: "ARM1" } },
  });
  assert.equal(off.section("driven"), undefined, "nothing is driven, so there is no Driven tier");
  assert.deepEqual(off.rowsOf("component-disabled").map((row) => row.dataset.part), ["utilUp"]);
});

// A latched estop takes the pulse off every output, and that is not a fact
// about anybody's wiring. The output-first table on Parts reads switched-off
// off the pulse (data/parts.js), which would put the whole droid under "Wired,
// switched off" the moment the estop latches; this sheet reads the Component
// Toggle instead, so a latched droid reads exactly as it read a moment before.
test("a latched estop does not rewrite the sheet", async () => {
  const latched = [
    output("ledc:0", "ARM1", {
      parts: ["utilUp"],
      component: "mg996r",
      commandedUs: null,
      targetUs: null,
      limp: "estop",
    }),
  ];
  const env = await boot({
    outputs: latched,
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });
  assert.deepEqual(
    env.rowsOf("driven").map((row) => row.dataset.part),
    ["utilUp"],
    "the lead is still the lead, whatever the estop is doing",
  );
  assert.equal(env.section("component-disabled"), undefined);
});

// ---------------------------------------------------------------------------
// The promise, the scope and the pictures
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The loom, from the lanes the firmware reports
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The footnote, the bound, and the vocabulary
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// It is a reference, and it stays current
// ---------------------------------------------------------------------------

test("the sheet writes nothing to the droid", async () => {
  const env = await boot();
  await env.leaveAndReturn();
  assert.deepEqual(env.posts, [], "Wiring is a reference, not a control surface");
});

test("mounting asks the droid for each answer once, not twice", async () => {
  const env = await boot();
  assert.equal(
    env.gets.filter((path) => path === "/api/servo/outputs").length,
    1,
    "the section run is the first read; the poll's first start is the one it covers",
  );
  assert.equal(env.gets.filter((path) => path === "/api/config").length, 1);
});

