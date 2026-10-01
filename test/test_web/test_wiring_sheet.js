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
const { existsSync, readFileSync } = require("node:fs");
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

// The droid's Outputs, each its row (helpers/fake_droid.js, #415, ADR 0068).
// Node loads the ES module helper from this CommonJS file directly.
const { servoRow: output, freshOutputs, describe } = require("./helpers/fake_droid.js");
const { shippedWords } = require("./helpers/shipped_words.cjs");

// The Component Toggles the Board Lanes join on, all off, which is what an
// unprovisioned controller carries (CONTEXT.md "Setup").
const LANE_TOGGLES = {
  drive: { enabled: false, label: "S1" },
  audio: { enabled: false, label: "S2" },
  protoR2link: { enabled: false, label: "S3" },
  domeEsc: { enabled: false, label: "DOME" },
};

// A set of rows as the droid reports them: every Output with a wired tick off
// unless `say` marks it wired (fake_droid's describe() words for the rest).
const rowsSaying = (rows, say = {}) =>
  describe(rows, Object.fromEntries(rows.map((row) => [row.address, { wired: !row.switchable, ...say[row.address] }])));

// GET /api/identity/components, cut to the rows the board's picture and name
// are found from (docs/api.md): the Body Controller family and the board GPIO
// product that borrows its picture. `running` is the one this image includes.
const lineup = (running = "artoo_pcb", extra = []) => ({
  categories: [],
  parts: [
    ...extra,
    { id: "artoo_pcb", name: "Artoo PCB (artoo.uk)", category: "body_controller", status: "supported", included: running === "artoo_pcb" },
    { id: "firebeetle2", name: "FireBeetle 2 (ESP32-P4)", category: "body_controller", status: "supported", included: running === "firebeetle2" },
    { id: "esp32_gpio_ledc", name: "Body controller board GPIO", category: "body_servo_controller", status: "supported", included: true },
  ],
});

// The page as a build stages it: its product wiring cards inlined from the
// build's asset set, else from the common data root, the order
// tools/gzip_fsdata.py resolves a partial in (ADR 0065). The legacy set
// carries none, so its page gets the common root's empty partial.
const stagedPage = (name, assetSet) =>
  readData(name).replace(/<!--\s*PA:INCLUDE\s+(_wiring_cards\.html)\s*-->/, (_directive, partial) =>
    readData(existsSync(join(dataDir, "asset-sets", assetSet, partial)) ? join("asset-sets", assetSet, partial) : partial));

const boot = async ({
  outputs = freshOutputs(),
  say = {},
  lanes = {},
  manifest = identity(),
  running = "artoo_pcb",
  products = [],
  assetSet = "legacy",
} = {}) => {
  outputs = rowsSaying(outputs, say);
  // GET /api/config carries the lanes' toggles and no Output (ADR 0068).
  const components = { ...LANE_TOGGLES, ...lanes };
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

  const env = { document, outputs, components, manifest, posts: [], gets: [], files: new Map() };
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
      // The shipped words table's lookups (helpers/shipped_words.cjs).
      ...shippedWords(),
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
        if (path === "/api/identity/components") return { data: lineup(running, products) };
        if (path.endsWith(".html")) return { data: stagedPage(path.slice(1), assetSet) };
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
    // A saved file is a Blob behind an object URL, which is how the browser
    // hands a download over. The URL is the only thing the page keeps, so the
    // fake keeps the Blob behind it for the test to open.
    Blob,
    URL: {
      createObjectURL: (blob) => {
        const url = `blob:http://device/${env.files.size + 1}`;
        env.files.set(url, blob);
        return url;
      },
      revokeObjectURL: (url) => env.files.delete(url),
    },
  };
  context.globalThis = context;

  const REAL_SCRIPTS = {
    "/shell.js": readData("shell.js"),
    "/status_stream.js": readData("status_stream.js"),
    "/live_reading.js": readData("live_reading.js"),
    "/droid_parts.js": readData("droid_parts.js"),
    "/droid_part_kind.js": readData("droid_part_kind.js"),
    "/outputs.js": readData("outputs.js"),
    "/output_settings.js": readData("output_settings.js"),
    // The part-first picker the screen mounts under the sheet (#411).
    "/dome_command_map.js": readData("dome_command_map.js"),
    "/parts_mapping.js": readData("parts_mapping.js"),
    "/wiring.js": readData("wiring.js"),
    // The picker's lookup and frame, which name and picture the board.
    "/apply_timing.js": readData("apply_timing.js"),
    "/product_art.js": readData("product_art.js"),
    "/component_picker.js": readData("component_picker.js"),
    // The list of what does not line up reads the Health Signal readers and
    // the Droid Build (#454).
    "/health_signals.js": readData("health_signals.js"),
    "/droid_build.js": readData("droid_build.js"),
    "/dome_layout.js": readData("dome_layout.js"),
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

  // The one diagram's wires, each carrying the key it is drawn for: an Output
  // Address, or a Board Lane's key.
  env.wires = () => document.querySelectorAll(".wd-link");
  env.wire = (key) => env.wires().find((node) => node.dataset.wire === key);
  env.drawn = (key) => env.wire(key) !== undefined;
  env.diagrams = () => document.querySelectorAll(".wd");
  env.summary = () => document.getElementById("wiring-wires-summary").textContent;
  env.promise = () => document.getElementById("wiring-promise").textContent;
  env.rail = () => document.getElementById("wiring-rail").textContent;
  // Everything a builder can read on the mounted surface, chrome included.
  env.surfaceText = () => document.querySelector("[data-surface]").textContent;
  // The shell leaves this surface and comes back to it, which is what the
  // sheet has to survive: a Part moved on Servos and then read here.
  env.leaveAndReturn = async () => {
    windowMock.location.hash = "#home";
    await sleep(60);
    windowMock.location.hash = "#wiring";
    await sleep(80);
  };

  windowMock.location.hash = "#wiring";
  // Painted from the droid's answer: the summary counts the lines, drawn or
  // none (a fresh droid draws nothing at all).
  const deadline = Date.now() + 3000;
  while (!document.getElementById("wiring-wires-summary")?.textContent) {
    if (Date.now() > deadline) assert.fail("the Wiring surface never mounted and painted its wires");
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

// The drawing is the wires a builder has run, and nothing else (operator,
// 2026-09-29 on #411: "the drawing should only draw the actaul lines (wires)
// currently assigned/wired in"). An Output is wired when a Part is on it and
// free when none is (CONTEXT.md "Wiring"), whatever its tick says - the tick
// follows the Part on the droid, and an expander's channel has none. A free
// Output draws no line and reads free by what its board prints, in the one
// place the page says so: the free row of the parts wiring table (operator,
// 2026-10-01 on #463). The printed sheet, the same generator's, draws no
// line for it either.
test("an output with no Part on it draws no line and reads free, on the screen and in the saved sheet", async () => {
  const rows = [
    output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" }),
    output("ledc:1", "ARM2", { component: "mg996r" }),
    output("pca:0", ""),
  ];
  const env = await boot({ outputs: rows, say: { "ledc:1": { wired: true } } });
  assert.equal(env.drawn("ledc:0"), true, "the Output with a Part on it is drawn");
  assert.equal(env.drawn("ledc:1"), false, "the free one is not, though an old tick says wired");
  assert.equal(env.drawn("pca:0"), false, "nor an expander's free channel, which has no tick at all");
  const free = env.document.querySelectorAll("[data-free]").map((node) => node.dataset.free);
  assert.deepEqual(free, ["ledc:1", "pca:0"], "the free row lists the Outputs with no Part, and only those");
  assert.equal(env.document.querySelectorAll(".wd-pin").length, 0, "and no second list of the Outputs stands under the drawing");

  const link = env.document.getElementById("wiring-save");
  link.fire("click", {});
  const file = await env.files.get(link.getAttribute("href")).text();
  assert.match(file, /data-wire="ledc:0"/);
  assert.doesNotMatch(file, /data-wire="ledc:1"/, "the printed sheet leaves it out too");
});

// The bench session (operator, 2026-09-29 on #411: "foot drive is now set to
// "not fitted" so why is then wiring drawing still listing it as wired?"). A
// serial link whose component is Not fitted or switched off rides no wire, so
// it draws no line, as a free Output draws none, and the count over the
// drawing is the lines it draws.
test("a serial link switched off draws no line, and the count is only the lines drawn", async () => {
  const env = await boot({
    outputs: [output("ledc:0", "ARM1", { parts: ["utilUp"] }), output("ledc:1", "ARM2")],
    lanes: { drive: { enabled: false, label: "S1" }, protoR2link: { enabled: true, label: "S3" } },
  });
  assert.equal(env.drawn("drive"), false, "the switched-off link has no line");
  assert.equal(env.drawn("protor2link"), true, "the one switched on has");
  assert.equal(env.summary(), "2 wires · 1 output free");
});

// What is on a wire is ONE answer in two vocabularies (ADR 0067): a servo's
// model where it drives a servo, a Light Type where it lights something. The
// sheet used to need two answers to agree - the Output's own ledStripPin and a
// droid-wide aux_led_pin - and drew "a servo" on a wire that was really driving
// a strip whenever they disagreed. There is one field now (#413), and a wire
// that carries a light has to say so: a builder tracing this sheet to decide
// what to unplug is the person the wrong word costs.
test("a wire carrying a light says so, and one carrying a servo says that", async () => {
  const rows = [
    output("ledc:3", "ARM3", { parts: ["dataPanel"], component: "rgb" }),
    output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" }),
  ];
  const env = await boot({
    outputs: rows,
    say: {
      "ledc:3": { wired: true, lightCapable: true, type: "rgb" },
      "ledc:0": { wired: true },
    },
  });
  assert.match(env.wire("ledc:3").textContent, /LED strip/, "the lit wire names what lights it");
  assert.doesNotMatch(env.wire("ledc:0").textContent, /LED strip/, "and the servo's wire does not");
});

// A latched estop takes the pulse off every output, and that is not a fact
// about anybody's wiring. The output-first table on Servos reads switched-off
// off the pulse (data/servo.js), which would draw the whole droid not wired
// the moment the estop latches; this sheet reads the Component Toggle instead,
// so a latched droid reads exactly as it read a moment before.
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
    say: { "ledc:0": { wired: true } },
  });
  assert.equal(env.drawn("ledc:0"), true, "the wire is still the wire, whatever the estop is doing");
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

// The bench copy and the screen copy are "the same document from one
// generator" (CONTEXT.md "Wiring"): the file a builder saves and prints must
// carry exactly the wires the surface is showing, and none of what writes -
// the controls of the parts wiring table are the screen's, never the
// generator's (#411, #463). And it is opened at a bench, often with no droid in
// reach, so it must ask for nothing when it opens - no script, no stylesheet,
// no image (#366).
test("the saved sheet is the sheet on the screen, and loads nothing when it opens", async () => {
  const { MiniDOMParser } = await import("./helpers/mini_dom.js");
  const rows = [
    output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" }),
    output("ledc:1", "ARM2", { parts: ["utilLo"], component: "mg996r" }),
    output("ledc:3", "AUX1"),
  ];
  const env = await boot({ outputs: rows, say: { "ledc:0": { wired: true } } });

  const link = env.document.getElementById("wiring-save");
  let followed = true;
  link.fire("click", { preventDefault: () => (followed = false) });
  assert.ok(followed, "the press hands the file to the browser rather than stopping it");
  const blob = env.files.get(link.getAttribute("href"));
  assert.ok(blob, "the link carries the saved file when the press is followed");
  const file = await blob.text();
  const saved = new MiniDOMParser().parseFromString(file);

  const sheetOf = (root) => ({
    wires: root.querySelectorAll(".wd-link").map((wire) => wire.dataset.wire),
    pickers: root.querySelectorAll("button").length,
  });
  const onScreen = sheetOf(env.document);
  assert.deepEqual(onScreen.wires, ["ledc:0", "ledc:1"], "the fixture draws the two Outputs with a Part");
  assert.ok(onScreen.pickers > 0, "and the screen carries the table's controls");
  // The bench copy has nothing to press, and it writes nothing (operator,
  // 2026-09-19 on #411).
  assert.deepEqual(sheetOf(saved), { ...onScreen, pickers: 0 });
  assert.doesNotMatch(file, /<(select|button|dialog|form)\b/i, "nothing in the saved file can be pressed");
  assert.equal(saved.querySelectorAll(".wd").length, env.diagrams().length);

  assert.doesNotMatch(file, /<(script|style|img|iframe|object)\b/i);
  assert.doesNotMatch(file, /url\(|@import/i);
  for (const [, href] of file.matchAll(/<link\b[^>]*\bhref="([^"]*)"/gi)) {
    assert.match(href, /^data:/, `a <link> that opens ${href} fetches it`);
  }
});

// The browser knows no Output (operator, 2026-09-19 on #411): which Outputs
// a board has, and what the board prints beside each, are the firmware's
// answer. A board reporting a different set - two Outputs, printed GPIO 49
// and GPIO 4, and an expander channel no board prints anything for - is drawn
// as exactly that, in the firmware's order, before the serial links: the
// Outputs are one list in data/outputs.js's order (#415). Each wire takes the
// palette color at its place (--wire-n), from the stylesheet and never a
// literal. A free Output, and a link switched off, are not drawn but keep
// their place, so the lines after them keep their colors as Parts come and go.
test("the wires are the Outputs the firmware reports, named as the board prints them, colored by place", async () => {
  const rows = [
    output("ledc:0", "GPIO 49", { parts: ["utilUp"], component: "mg996r" }),
    output("ledc:3", "GPIO 4"),
    output("pca:0", "", { parts: ["doorFL"] }),
  ];
  const env = await boot({
    outputs: rows,
    lanes: {
      drive: { enabled: true, label: "GPIO 20/21" },
      audio: { enabled: false, label: "GPIO 34/36" },
      protoR2link: { enabled: true, label: "GPIO 22/23" },
    },
  });
  assert.deepEqual(
    env.wires().map((wire) => wire.dataset.wire),
    ["ledc:0", "pca:0", "drive", "protor2link"],
  );
  const silk = (key) => env.wire(key).querySelector(".wd-silk")?.textContent ?? "";
  assert.equal(silk("ledc:0"), "GPIO 49");
  const ink = (key) => env.wire(key).querySelector(".wd-line").getAttribute("style");
  assert.equal(ink("ledc:0"), "stroke:var(--wire-1)");
  assert.equal(ink("pca:0"), "stroke:var(--wire-3)", "the expander's channel with a Part, at its own place past the free GPIO 4");
  assert.equal(ink("drive"), "stroke:var(--wire-4)");
  assert.equal(ink("protor2link"), "stroke:var(--wire-6)");
});

// The diagram pictures and names the Body Controller this image runs on, from
// the lineup row the picker pictures it by (#411). The lineup lists every
// peer board, so taking the family's first row rather than the included one
// would name - and draw - a board that is not in the droid.
test("the diagram is titled and pictured with the board this image runs on, not a peer", async () => {
  const env = await boot({ running: "firebeetle2", outputs: [output("ledc:0", "GPIO 49", { parts: ["utilUp"] })] });
  const deadline = Date.now() + 2000;
  const title = () => env.document.querySelector(".wd-title")?.textContent ?? "";
  while (title() === "" && Date.now() < deadline) await sleep(10);
  assert.equal(title(), "FireBeetle 2 (ESP32-P4)");
  const photo = env.document.querySelector(".wd-board-slot").querySelector("img");
  // Set now or on the deferred-asset sweep, whichever the page is past.
  const source = photo?.getAttribute("src") || photo?.dataset.deferredSrc;
  assert.equal(source, "/firebeetle2.webp", "the picture is the same board's");
});

// The print act has no word "Printable" left on it - the icon carries it - so
// an icon that resolves to nothing would leave a builder a blank square and
// "wiring sheet". The shell's sprite is the one place an icon is drawn from.
test("the print act's icon is one the shell's sprite draws", async () => {
  const env = await boot();
  const link = env.document.getElementById("wiring-save");
  const href = link.querySelector("use").getAttribute("href");
  assert.ok(env.document.getElementById(href.slice(1)), `${href} resolves to no symbol`);
  assert.equal(link.getAttribute("aria-label"), "Printable wiring sheet");
});

test("the sheet writes nothing to the droid", async () => {
  const env = await boot();
  await env.leaveAndReturn();
  // Its one act saves a file on this computer, and that is not a write either.
  env.document.getElementById("wiring-save").fire("click", {});
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


// What the builder said against what the droid reports (#454). Silence is not
// a second answer: a fitted sound module the droid asked and heard nothing
// from stays the builder's answer - declared, in the Health Signal's own word
// and light, the Status Plate's "No answer" - and is never contradicted. A
// foot drive that declares it reports nothing cannot be asked at all, so it is
// not probed rather than declared-and-silent. Every value here is live, so none
// of it reaches the saved sheet (research 5.3, 9.2; CONTEXT.md "Health Signal").
test("a fitted part the droid cannot hear from is never read as contradicting you, and the list stays off the saved sheet", async () => {
  const env = await boot({
    lanes: {
      drive: { enabled: true, label: "S1" },
      audio: { enabled: true, label: "S2", member: "dy_sv5w" },
    },
    products: [
      { id: "dy_sv5w", name: "DY-SV5W", category: "sound", status: "supported", included: true },
      { id: "hoverboard", name: "Hoverboard", category: "foot_drive", status: "supported", included: true, capabilities: 0 },
    ],
  });
  // A good frame: the six fields the Live Reading needs, the feet running
  // this boot with no wheel controller block, and a sound block whose module
  // did not answer (link_ok false, the line not held).
  env.window.PAStatusStream.seed({
    estop: false, sbusHwFailsafe: false, sbusSignalLost: false, webDriveExpired: false,
    webControlEnabled: false, sleepMode: false,
    drive: { state: "idle", detail: "Enabled" },
    audio: { output: "on", link_ok: false, rx_status: "ok" },
  });
  await sleep(40);
  const row = (key) => env.document.querySelector(`[data-row="${key}"]`);

  const sound = row("lane:audio");
  assert.ok(sound, "the drawn sound wire has its own row");
  assert.equal(sound.dataset.state, "declared");
  assert.match(sound.textContent, /No answer/);
  assert.ok(sound.querySelector(".indicator").classList.contains("fail"), "red, as on the Status Plate");

  const feet = row("lane:drive");
  assert.equal(feet.dataset.state, "not probed", "a foot drive that reports nothing cannot be asked");

  assert.equal(env.document.querySelectorAll('[data-state="contradicted"]').length, 0);

  env.document.getElementById("wiring-save").fire("click", {});
  const saved = await [...env.files.values()].at(-1).text();
  assert.ok(!/No answer|lineup/.test(saved), "the saved sheet carries no live reading");
});

// How to wire and power a product is on Wiring only for a product that is on
// the droid, and the pins beside it are the running board's (#458). A card is
// per product and pins are per board, so a pin that came from card text would
// be right on one board and wrong on the next: the lane here is one no card
// could know. On the screen the card opens on the row of the product it
// belongs to, under that row and nowhere else (operator, 2026-10-01 on #463);
// the plate that prints every card is paper's. The saved sheet ends with the
// same cards, from the same generator. A family answered Not fitted has no
// card, and an image built without the cards - the legacy asset set - offers
// none on any row and has no such section in the file, for the very same
// droid (ADR 0065).
test("a product's wiring card follows the droid's own answers, and an image without the cards has no such section", async () => {
  const sound = { id: "dy_sv5w", name: "DY-SV5W", category: "sound", status: "supported", included: true };
  const droid = (enabled) => ({
    manifest: identity({ ...LANES, audio: { uart: 3, tx: 41, rx: 42 } }),
    lanes: { audio: { enabled, label: "ROW 41/42", member: "dy_sv5w" } },
    products: [sound],
  });
  const read = async (env) => {
    env.document.getElementById("wiring-save").fire("click", {});
    return {
      cards: env.document.querySelectorAll(".wcard").map((card) => card.dataset.product),
      plate: env.document.getElementById("wiring-products-card"),
      acts: env.document.querySelectorAll("[data-act]").filter((node) => node.dataset.act === "card"),
      saved: await [...env.files.values()].at(-1).text(),
    };
  };

  const fitted = await boot({ ...droid(true), assetSet: "default" });
  const shown = await read(fitted);
  assert.ok(shown.cards.includes("dy_sv5w"), "the fitted sound module has its card");
  const card = fitted.document.querySelectorAll(".wcard").find((each) => each.dataset.product === "dy_sv5w");
  const pins = card.querySelector(".wcard-pins").textContent;
  assert.match(pins, /ROW 41\/42/, "the label the running board prints for the lane");
  assert.match(pins, /TX 41 \/ RX 42/, "and where the firmware routes it");
  assert.match(shown.saved, /data-product="dy_sv5w"/, "the saved sheet carries the same card");
  assert.match(shown.saved, /TX 41 \/ RX 42/);

  // On the screen: the act on the sound link's own row opens the card as the
  // next row of the table, and asks the droid for nothing.
  const table = fitted.document.getElementById("wiring-parts-table");
  const rows = () => table.querySelectorAll("tr");
  const soundRow = () => rows().find((row) => row.dataset.link === "audio");
  const act = soundRow().querySelectorAll("[data-act]").find((node) => node.dataset.product === "dy_sv5w");
  assert.ok(act, "the sound link's row offers its product's card");
  assert.equal(table.querySelectorAll(".wcard").length, 0, "no card is open until one is asked for");
  table.fire("click", { target: act });
  const under = rows()[rows().indexOf(soundRow()) + 1];
  assert.equal(under.dataset.card, "dy_sv5w", "the card opens on the row under the row it belongs to");
  assert.match(under.textContent, /TX 41 \/ RX 42/);
  assert.deepEqual(fitted.posts, [], "opening a card writes nothing");

  const declined = await read(await boot({ ...droid(false), assetSet: "default" }));
  assert.ok(!declined.cards.includes("dy_sv5w"), "sound answered Not fitted has no card");
  assert.ok(!declined.acts.some((node) => node.dataset.product === "dy_sv5w"), "and its row offers none");
  assert.doesNotMatch(declined.saved, /data-product="dy_sv5w"/, "on the saved sheet either");

  // A family with a Component Member is answered by that member alone. The
  // lineup here carries one sound module and the droid holds no member: the
  // builder chose nothing, so no module's card is drawn for them.
  const unchosen = await read(await boot({
    ...droid(true),
    lanes: { audio: { enabled: true, label: "ROW 41/42" } },
    assetSet: "default",
  }));
  assert.ok(!unchosen.cards.includes("dy_sv5w"), "a sound module nobody chose has no card");

  const without = await read(await boot({ ...droid(true), assetSet: "legacy" }));
  assert.deepEqual(without.cards, []);
  assert.deepEqual(without.acts, [], "no row offers a card");
  assert.equal(without.plate, null, "the page has no plate for them, hidden or otherwise");
  assert.doesNotMatch(without.saved, /Product wiring|data-product/);
});

// Paper carries the parts wiring table as text (operator, 2026-10-01 on
// #463): the page's own print copy and the saved sheet are one generator's
// table, so they are the same markup, and neither has anything to press. An
// Output is on it once: with its Part while it is wired, in the free row
// while it is not, and never both. A serial link that is not fitted is still
// listed, saying so.
test("paper carries the parts table as text: the same table on the page's print copy and in the saved sheet", async () => {
  const rows = [
    output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg90s" }),
    output("ledc:1", "ARM2"),
    output("ledc:3", "ARM3", { parts: ["dataPanel"], component: "rgb" }),
    output("pca:0", ""),
  ];
  const env = await boot({
    outputs: rows,
    say: { "ledc:3": { lightCapable: true, type: "rgb" } },
    lanes: { drive: { enabled: false, label: "S1" }, protoR2link: { enabled: true, label: "S3" } },
  });
  const { MiniDOMParser } = await import("./helpers/mini_dom.js");

  env.document.getElementById("wiring-save").fire("click", {});
  const file = await [...env.files.values()].at(-1).text();
  const onPage = env.document.getElementById("wiring-parts-sheet");
  const inFile = new MiniDOMParser().parseFromString(file).querySelector(".sheet-table");
  assert.ok(inFile, "the saved sheet carries the table");
  assert.equal(onPage.querySelectorAll("button").length + onPage.querySelectorAll("a").length, 0, "nothing on it can be pressed");

  const linesOf = (root) => root.querySelectorAll("tr").map((row) => row.children.map((cell) => cell.textContent));
  const lines = linesOf(onPage);
  assert.deepEqual(linesOf(inFile), lines, "the saved sheet's table is the page's own print copy");
  assert.deepEqual(lines, [
    ["", "Part", "Output", "On the wire"],
    ["Board outputs"],
    ["", "Upper utility arm", "ARM1", "MG90S"],
    ["", "Data Panel", "ARM3", "LED strip"],
    ["", "free", "ARM2, pca:0", ""],
    ["Links"],
    ["", "Foot Drive", "S1", "not fitted"],
    ["", "Sound", "S2", "not fitted"],
    ["", "Dome link", "S3", "serial · UART 2 - TX 33 / RX 34"],
  ]);
});

// A card opens on the row of the product it belongs to, so a fitted product
// with no row would have its card on paper only. No Board Lane reports the
// dome's ESC, the radio or its receiver, and each still has a row (operator,
// 2026-10-01 on #463: "every fitted product's card opens on screen as well as
// on paper"). Whatever paper prints a card for, some row of the table opens.
test("every wiring card paper prints can be opened on the screen, from a row of the table", async () => {
  const env = await boot({
    lanes: {
      audio: { enabled: true, label: "S2", member: "dy_sv5w" },
      domeEsc: { enabled: true, label: "DOME" },
    },
    products: [
      { id: "dy_sv5w", name: "DY-SV5W", category: "sound", status: "supported", included: true },
      { id: "isdt_esc70", name: "ISDT ESC70", category: "dome_rotation", status: "supported", included: true },
    ],
    assetSet: "default",
  });
  const table = env.document.getElementById("wiring-parts-table");
  const onPaper = env.document.getElementById("wiring-products").querySelectorAll(".wcard").map((card) => card.dataset.product);
  const acts = () => table.querySelectorAll("[data-act]").filter((node) => node.dataset.act === "card");
  assert.ok(onPaper.includes("isdt_esc70") && onPaper.includes("dy_sv5w"), `the fixture fits a product with a lane and one without: ${onPaper}`);
  assert.deepEqual(acts().map((node) => node.dataset.product).sort(), [...onPaper].sort(), "one act in the table for each card on paper");

  // The ESC has no lane: its row names the family as Configuration does, the
  // product, and what the board prints for it. Where it is routed nobody
  // reports, and the row does not make it up.
  const rows = () => table.querySelectorAll("tr");
  const escRow = () => rows().find((row) => row.dataset.link === "product:isdt_esc70");
  assert.deepEqual(escRow().children.slice(0, 3).map((cell) => cell.textContent), ["Dome RotationISDT ESC70", "DOME", ""]);
  table.fire("click", { target: acts().find((node) => node.dataset.product === "isdt_esc70") });
  assert.equal(rows()[rows().indexOf(escRow()) + 1].dataset.card, "isdt_esc70", "its card opens under its own row");
  assert.deepEqual(env.posts, [], "and nothing is written");
});

