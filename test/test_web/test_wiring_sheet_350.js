// =============================================================================
// test/test_web/test_wiring_sheet_350.js
//
// Behaviour of the Wiring surface (#350): the sheet says what this image will
// actually drive, and it says it from what the droid answered.
//
// The shipped Operator Shell boots the shipped surface against a fake droid,
// the way test_outputs_table_362.js does inside its own file. Nothing here
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

// The bound the sheet's promise draws, taken off the shipped catalog rather
// than asserted as a literal: what the sheet must show is every Part this image
// could drive, which is every Part whose declared control path is not the dome
// link. The literal 42 is asserted separately, once, because that number IS the
// ticket's claim.
const catalogParts = () => {
  const context = { window: {} };
  vm.runInNewContext(readData("droid_parts.js"), context, { filename: "droid_parts.js" });
  return context.window.DroidParts.parts;
};

const inScope = (part) =>
  part.control !== null && part.control !== undefined && part.control !== "dome-link";

// ---------------------------------------------------------------------------
// The headline: a fresh droid reads honestly empty
// ---------------------------------------------------------------------------

test("a fresh droid reads 0 / 0 / 42 / 5, and the two empty tiers have no section at all", async () => {
  const env = await boot();

  assert.deepEqual(
    env.tierIds(),
    ["part-not-assigned", "output-no-part"],
    "an empty tier's section vanishes rather than standing as a heading over nothing",
  );
  assert.equal(env.countOf("part-not-assigned"), "42 parts");
  assert.equal(env.countOf("output-no-part"), "5 outputs");
  assert.equal(env.rowsOf("part-not-assigned").length, 42);
  assert.equal(env.rowsOf("output-no-part").length, 5);

  // And the 42 is the catalog's own answer, not a number this page carries.
  assert.equal(catalogParts().filter(inScope).length, 42, "42 is read off docs/droid-parts.yaml");
});

test("the four tier headings are the four words they are required to be", async () => {
  const env = await boot({
    outputs: [
      output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" }),
      output("ledc:1", "ARM2", { parts: ["utilLo"], component: "mg996r" }),
      output("ledc:3", "AUX1"),
    ],
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });

  assert.deepEqual(env.tierIds(), [
    "driven",
    "component-disabled",
    "part-not-assigned",
    "output-no-part",
  ]);
  assert.equal(env.headingOf("driven"), "Driven");
  assert.equal(env.headingOf("component-disabled"), "Wired, switched off");
  assert.equal(env.headingOf("part-not-assigned"), "Nothing drives it");
  assert.equal(env.headingOf("output-no-part"), "Output with no part");

  assert.equal(env.countOf("driven"), "1 part", "a count of one is not pluralised");
  assert.equal(env.countOf("component-disabled"), "1 part");
  assert.equal(env.countOf("output-no-part"), "1 output");
});

test("a part an output claims moves out of the unassigned tier and into the driven one", async () => {
  const env = await boot({
    outputs: [
      output("ledc:0", "ARM1", { parts: ["utilUp", "doorFL"], component: "mg996r" }),
      output("ledc:3", "AUX1"),
    ],
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });

  const driven = env.rowsOf("driven").map((row) => row.dataset.part);
  assert.deepEqual(driven.sort(), ["doorFL", "utilUp"], "a ganged lead names both parts it moves");
  assert.equal(env.rowsOf("part-not-assigned").length, 40, "42 less the two now on a lead");
  assert.equal(
    env.rowsOf("output-no-part").length,
    1,
    "ARM1 has parts on it now, so only AUX1 is a spare",
  );
});

// ---------------------------------------------------------------------------
// The tier token, and the why
// ---------------------------------------------------------------------------

test("every row carries its tier as a token, and no row is in two tiers", async () => {
  const env = await boot();
  const seen = new Map();
  env.sections().forEach((section) => {
    const tier = section.dataset.tier;
    section.querySelectorAll(".wiring-row").forEach((row) => {
      assert.equal(row.dataset.tier, tier, "a row's token is the section it is under");
      const subject = row.dataset.part || row.dataset.output;
      assert.ok(subject, "every row names its subject");
      assert.equal(seen.has(subject), false, `${subject} appears in two tiers`);
      seen.set(subject, tier);
    });
  });
  assert.equal(seen.size, 47, "47 rows on a fresh droid, each in exactly one tier");
});

test("every non-driven row states why inline, and a driven row states nothing", async () => {
  const env = await boot({
    outputs: [
      output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" }),
      output("ledc:1", "ARM2", { parts: ["utilLo"], component: "mg996r" }),
      output("ledc:3", "AUX1"),
    ],
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });

  ["component-disabled", "part-not-assigned", "output-no-part"].forEach((tier) => {
    env.rowsOf(tier).forEach((row) => {
      const why = row.querySelector(".wiring-reason").textContent.trim();
      assert.notEqual(why, "", `a ${tier} row must say why on the row`);
    });
  });
  env.rowsOf("driven").forEach((row) => {
    assert.equal(
      row.querySelector(".wiring-reason").textContent.trim(),
      "",
      "a driven row has nothing to explain",
    );
  });

  // The two actionable reasons name the move AND route to a destination that
  // exists (CONTEXT.md "Availability Family").
  const offRow = env.rowsOf("component-disabled")[0];
  assert.match(offRow.querySelector(".wiring-reason").textContent, /ARM2 is switched off/);
  assert.equal(
    offRow.querySelector(".wiring-reason").querySelector("a").getAttribute("href"),
    "/setup.html",
    "switched off routes to where a Component Toggle is changed",
  );
  const unwired = env.rowsOf("part-not-assigned")[0];
  assert.equal(
    unwired.querySelector(".wiring-reason").querySelector("a").getAttribute("href"),
    "/parts.html",
    "a part nothing drives routes to where a part is put on an output",
  );
});

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

test("the bounded promise and the scope statement are on the face of the view", async () => {
  const env = await boot();
  assert.match(env.promise(), /what this image will actually drive/);
  assert.match(env.promise(), /signal \+ ground only, power distribution is your build's business/);
});

test("the scope statement is on every picture, because a picture gets cropped", async () => {
  const env = await boot({
    outputs: [output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" })],
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });
  const diagrams = env.diagrams();
  assert.equal(diagrams.length, 2, "the loom, and the signals it is driving");
  diagrams.forEach((svg) => {
    const scope = svg.querySelectorAll(".wd-scope").map((node) => node.textContent);
    assert.deepEqual(scope, [
      "signal + ground only, power distribution is your build's business",
    ]);
  });
});

// SVG text does not wrap, so a label longer than its box runs out over whatever
// is beside it. Four parts ganged to one lead is what reaches that, and it is
// exactly the build a bench sheet is drawn for.
test("a label too long for its box is cut, and the whole of it travels in a title", async () => {
  const env = await boot({
    outputs: [
      output("ledc:0", "ARM1", {
        parts: ["doorFL", "doorFR", "doorRL", "doorRR"],
        component: "mg996r",
      }),
    ],
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });
  const box = env
    .diagrams()[1]
    .querySelectorAll("[class]")
    .find((node) => (node.getAttribute("class") || "") === "wd-name");
  assert.match(box.textContent, /\.\.\./, "the drawn label is cut rather than overrunning");
  const whole = box.querySelector("title").textContent;
  assert.equal(
    whole,
    "Left body door + Right body door + Rear-left body door + Rear-right body door",
    "and nothing is lost: the whole label is what a hover and a reader get",
  );
});

test("a droid with nothing driven draws the loom and no signal picture", async () => {
  const env = await boot();
  assert.equal(env.diagrams().length, 1, "there are no driven signals to draw");
});

test("the shared rail is explained once, and the cadence carries whose figure it is", async () => {
  const env = await boot();
  const rail = env.rail();
  assert.match(rail, /~450 ms \(one servo at a time\)/);
  assert.match(rail, /measured on the Dome Controller/, "CONTEXT.md: that figure is the dome's");
  assert.equal(
    (env.surfaceText().match(/450 ms/g) || []).length,
    1,
    "stated once, where the shared rail is explained",
  );
  assert.match(rail, /never power distribution|no picture on this page is a claim about it/);
});

// ---------------------------------------------------------------------------
// The loom, from the lanes the firmware reports
// ---------------------------------------------------------------------------

test("the loom is the lanes the firmware reported, and a lane nobody named still draws", async () => {
  const env = await boot({
    manifest: identity({
      drive: { uart: 1, tx: 16, rx: 17 },
      spectrometer: { uart: 3, tx: 8, rx: 9 },
    }),
  });
  assert.deepEqual(
    env.loomRows().map((row) => row.dataset.lane),
    ["drive", "spectrometer"],
    "adding a lane is adding a row, and the sheet follows the manifest",
  );
  assert.match(env.loomRow("drive").textContent, /UART 1 - TX 16 \/ RX 17/);
  assert.match(env.loomRow("spectrometer").textContent, /UART 3 - TX 8 \/ RX 9/);
  // Foot Drive is switched off in this fixture; the invented lane has no
  // Component Toggle at all, and a signal nothing has switched off is not one
  // this sheet may report as off.
  assert.equal(env.loomRow("drive").dataset.live, "no");
  assert.equal(env.loomRow("spectrometer").dataset.live, "yes");
  assert.equal(env.loomSummary(), "2 lanes · 1 switched on");
});

// The two payloads spell one signal differently: include/board_lanes.inc
// declares `protor2link` and src/web/api_config.cpp reports `protoR2link`. An
// absent toggle reads as on, so joining on the name as written drew the dome
// link as live on a droid whose dome link is switched off.
test("the dome link's lane finds its own toggle, whatever case each payload spells it in", async () => {
  const env = await boot();
  assert.equal(env.loomRow("protor2link").dataset.live, "no");
  assert.match(
    env.loomRow("protor2link").querySelector(".wiring-reason").textContent,
    /Dome link is switched off/,
  );

  const on = await boot({
    components: { ...freshComponents(), protoR2link: { enabled: true, label: "S3" } },
  });
  assert.equal(on.loomRow("protor2link").dataset.live, "yes");
});

test("a switched-off lane is drawn broken and says why on its own row", async () => {
  const env = await boot({
    components: { ...freshComponents(), drive: { enabled: true, label: "S1" } },
  });
  assert.equal(env.loomRow("drive").dataset.live, "yes");
  assert.equal(env.loomRow("audio").dataset.live, "no");
  assert.equal(
    env.loomRow("drive").querySelector(".wiring-reason").textContent.trim(),
    "",
    "a live lane has nothing to explain",
  );
  assert.match(
    env.loomRow("audio").querySelector(".wiring-reason").textContent,
    /Sound is switched off, so nothing rides this lane/,
  );

  const loom = env.diagrams()[0];
  const idle = loom.querySelectorAll("[class]").filter((node) =>
    (node.getAttribute("class") || "").includes("is-idle")
  );
  assert.equal(idle.length, 2, "two of the three lanes are off, and both lines are drawn broken");
});

test("the board's own legend is printed where the board has one, and never invented", async () => {
  const withLegend = await boot();
  assert.match(withLegend.loomRow("drive").textContent, /S1/);

  // firebeetle2 has no Board Component Label rows yet
  // (include/component_labels.inc), so the config reports none.
  const noLegend = await boot({
    components: { ...freshComponents(), drive: { enabled: false } },
  });
  assert.match(
    noLegend.loomRow("drive").textContent,
    /no legend printed on this board/,
    "an absent legend is said, not guessed",
  );
});

test("the audio lane says it is sharing the dome link's controller", async () => {
  const shared = await boot();
  assert.match(shared.loomRow("audio").textContent, /shared with the dome link, RX only/);

  const own = await boot({
    manifest: identity(LANES, { PA_CAP_DEDICATED_AUDIO_UART: true }),
  });
  assert.doesNotMatch(own.loomRow("audio").textContent, /shared with the dome link/);
});

// ---------------------------------------------------------------------------
// The footnote, the bound, and the vocabulary
// ---------------------------------------------------------------------------

test("the footnote defines the tiers on the sheet and no others", async () => {
  const fresh = await boot();
  assert.match(fresh.footnote(), /Nothing drives it/);
  assert.match(fresh.footnote(), /Output with no part/);
  assert.doesNotMatch(fresh.footnote(), /Wired, switched off/);
  assert.doesNotMatch(
    fresh.footnote(),
    /A Driven row/,
    "a sheet with nothing driven does not define Driven",
  );

  const wired = await boot({
    outputs: [output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" })],
    components: { ...freshComponents(), arm1: { enabled: true, label: "ARM1" } },
  });
  assert.match(wired.footnote(), /A <b>Driven<\/b> row|A Driven row/);
  assert.match(wired.footnote(), /Design name/, "the naming bridge is defined once a row shows one");
});

test("no row is hidden: the bound is named and counted rather than filtered away", async () => {
  const env = await boot();
  const outside = catalogParts().filter((part) => !inScope(part));
  assert.equal(outside.length, 16, "14 on the dome link and 2 with no control path at all");
  assert.match(env.bound(), /14 parts the Dome Controller drives over the dome link/);
  assert.match(env.bound(), /2 parts nothing on this droid drives at all/);
  assert.match(env.bound(), /what this image will actually drive/);
});

test("a settled no carries no link and no destination", async () => {
  const env = await boot();
  const bound = env.document.querySelector(".wiring-bound");
  assert.equal(
    bound.querySelectorAll("a").length,
    0,
    "there is nothing a builder can do about the dome's parts, so nothing is offered",
  );
});

test("the word controller is always qualified on this surface", async () => {
  const env = await boot();
  const text = env.surfaceText();
  assert.match(text, /Body Controller/);
  // Every occurrence of the word, with whatever qualifies it kept, so a
  // failure names the phrase that broke rather than a count.
  const mentions = text.match(/(\S+\s+)?[Cc]ontrollers?\b/g) || [];
  assert.deepEqual(
    mentions.filter((hit) => !/(Body|Dome) Controllers?$/.test(hit.trim())),
    [],
    '"controller" is qualified unconditionally in this view (#298)',
  );
});

// ---------------------------------------------------------------------------
// It is a reference, and it stays current
// ---------------------------------------------------------------------------

test("the sheet writes nothing to the droid", async () => {
  const env = await boot();
  await env.leaveAndReturn();
  assert.deepEqual(env.posts, [], "Wiring is a reference, not a control surface");
});

test("coming back to the surface reads the droid again", async () => {
  const env = await boot();
  assert.equal(env.rowsOf("part-not-assigned").length, 42);

  // A Part put on an output while the builder was on Parts.
  env.outputs[0].parts = ["utilUp"];
  env.components.arm1 = { enabled: true, label: "ARM1" };
  const before = env.gets.filter((path) => path === "/api/servo/outputs").length;

  await env.leaveAndReturn();

  assert.ok(
    env.gets.filter((path) => path === "/api/servo/outputs").length > before,
    "the surface asked again on the way back in",
  );
  assert.deepEqual(env.rowsOf("driven").map((row) => row.dataset.part), ["utilUp"]);
  assert.equal(env.rowsOf("part-not-assigned").length, 41);
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

test("the generator hands the whole sheet to one caller, so a second one cannot disagree", async () => {
  const env = await boot();
  const wiring = env.window.PAWiring;
  const sheet = wiring.wiringDocument({
    parts: catalogParts(),
    outputs: freshOutputs().map((row) => ({
      address: row.address,
      name: row.name,
      parts: row.parts,
      component: row.component,
    })),
    components: freshComponents(),
    lanes: LANES,
    capabilities: { PA_CAP_DEDICATED_AUDIO_UART: false },
  });

  // Everything the screen mounted, from one pure call: a file caller (#353)
  // needs no second derivation of any of it.
  assert.equal(sheet.promise, "what this image will actually drive");
  assert.equal(sheet.scope, "signal + ground only, power distribution is your build's business");
  assert.equal(sheet.cadence, "~450 ms (one servo at a time)");
  assert.match(sheet.promiseHtml, /what this image will actually drive/);
  assert.match(sheet.railHtml, /~450 ms \(one servo at a time\)/);
  assert.equal(
    JSON.stringify(sheet.tiers.map((tier) => [tier.id, tier.count])),
    JSON.stringify([
      ["part-not-assigned", 42],
      ["output-no-part", 5],
    ]),
    "the arrays come out of the vm's own realm, so they are compared as text",
  );
  assert.equal(sheet.summary, "47 rows · 0 driven · 47 not");
  assert.ok(sheet.loomHtml.includes("wd-scope"));
  assert.ok(sheet.tiersHtml.includes('data-tier="part-not-assigned"'));
  assert.ok(sheet.footnoteHtml.includes("Nothing drives it"));
});
