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

// The droid's Outputs, each its row (helpers/fake_droid.js, #415, ADR 0068).
// Node loads the ES module helper from this CommonJS file directly.
const { servoRow: output, freshOutputs, describe } = require("./helpers/fake_droid.js");

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
const lineup = (running = "artoo_pcb") => ({
  categories: [],
  parts: [
    { id: "artoo_pcb", name: "Artoo PCB (artoo.uk)", category: "body_controller", status: "supported", included: running === "artoo_pcb" },
    { id: "firebeetle2", name: "FireBeetle 2 (ESP32-P4)", category: "body_controller", status: "supported", included: running === "firebeetle2" },
    { id: "esp32_gpio_ledc", name: "Body controller board GPIO", category: "body_servo_controller", status: "supported", included: true },
  ],
});

const boot = async ({
  outputs = freshOutputs(),
  say = {},
  lanes = {},
  manifest = identity(),
  running = "artoo_pcb",
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
        if (path === "/api/identity/components") return { data: lineup(running) };
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
    "/outputs.js": readData("outputs.js"),
    "/output_settings.js": readData("output_settings.js"),
    "/wiring.js": readData("wiring.js"),
    // The picker's lookup and frame, which name and picture the board.
    "/apply_timing.js": readData("apply_timing.js"),
    "/product_art.js": readData("product_art.js"),
    "/component_picker.js": readData("component_picker.js"),
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
  env.isLive = (key) => env.wire(key).classList.contains("is-live");
  env.unusedRows = () => document.getElementById("wiring-unused").querySelectorAll(".wiring-row");
  env.diagrams = () => document.querySelectorAll(".wd");
  env.footnote = () => document.getElementById("wiring-footnote").textContent;
  env.summary = () => document.getElementById("wiring-wires-summary").textContent;
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
  while (env.wires().length === 0) {
    if (Date.now() > deadline) assert.fail("the Wiring surface never mounted and drew its wires");
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

test("switching an output off dashes its wire and keeps it on the sheet", async () => {
  const wired = [output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" })];
  const on = await boot({
    outputs: structuredClone(wired),
    say: { "ledc:0": { wired: true } },
  });
  assert.equal(on.isLive("ledc:0"), true);

  const off = await boot({
    outputs: structuredClone(wired),
  });
  assert.equal(off.isLive("ledc:0"), false, "a wire nobody marked wired is drawn not wired");
  assert.match(off.wire("ledc:0").textContent, /Upper utility arm/, "and still says what is on its end");
  assert.equal(
    off.unusedRows().find((row) => row.dataset.part === "utilUp"),
    undefined,
    "a part on an output is not Unused, whether or not the output is wired",
  );
});

// Every "no" names the builder's next move, and a wrong destination is the
// defect CONTEXT.md "Availability Family" records (16 strings once named a
// place a builder could not reach). An Output is marked wired on this surface
// now (#369), not on Configuration, so that is where its wire sends them - in
// words, because a picture carries no link and the saved copy has no page.
test("an output not wired names where it is marked wired, and not Configuration", async () => {
  const rows = [output("ledc:0", "ARM1", { parts: ["utilUp"], component: "mg996r" })];
  const off = await boot({ outputs: rows });
  const text = off.wire("ledc:0").textContent;
  assert.match(text, /Mark it under Outputs/, "the wire names the control on this surface");
  assert.doesNotMatch(text, /Configuration/, "and no longer the page the control left");
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
// about anybody's wiring. The output-first table on Parts reads switched-off
// off the pulse (data/parts.js), which would draw the whole droid not wired
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
  assert.equal(env.isLive("ledc:0"), true, "the wire is still the wire, whatever the estop is doing");
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
// carry exactly the tiers, rows and counts the surface is showing. And it is
// opened at a bench, often with no droid in reach, so it must ask for nothing
// when it opens - no script, no stylesheet, no image (#366).
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
    wires: root.querySelectorAll(".wd-link").map((wire) => `${wire.dataset.wire}:${wire.classList.contains("is-live")}`),
    unused: root.querySelectorAll(".wiring-row").map((row) => row.dataset.part),
  });
  const onScreen = sheetOf(env.document);
  assert.ok(onScreen.wires.some((wire) => wire.endsWith(":true")), "the fixture draws a wired wire");
  assert.ok(onScreen.wires.some((wire) => wire.endsWith(":false")), "and one not wired");
  assert.ok(onScreen.unused.length > 0, "and Unused parts");
  // The bench copy is the wires and their power; Unused stays on the screen
  // only (operator, 2026-09-19 on #411).
  assert.deepEqual(sheetOf(saved), { ...onScreen, unused: [] });
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
// Outputs are one list in data/outputs.js's order (#415), which is the order
// the Outputs plates below take their colors from too. Each wire takes the
// palette color at its place (--wire-n), from the stylesheet and never a
// literal, and a wire to something not wired takes --wire-off.
test("the wires are the Outputs the firmware reports, named as the board prints them, colored by place", async () => {
  const rows = [
    output("ledc:0", "GPIO 49", { parts: ["utilUp"], component: "mg996r" }),
    output("ledc:3", "GPIO 4"),
    output("pca:0", ""),
  ];
  const env = await boot({
    outputs: rows,
    say: { "ledc:0": { wired: true } },
    lanes: {
      drive: { enabled: true, label: "GPIO 20/21" },
      audio: { enabled: false, label: "GPIO 34/36" },
      protoR2link: { enabled: true, label: "GPIO 22/23" },
    },
  });
  assert.deepEqual(
    env.wires().map((wire) => wire.dataset.wire),
    ["ledc:0", "ledc:3", "pca:0", "drive", "audio", "protor2link"],
  );
  const silk = (key) => env.wire(key).querySelector(".wd-silk")?.textContent ?? "";
  assert.equal(silk("ledc:0"), "GPIO 49");
  assert.equal(silk("ledc:3"), "GPIO 4");
  const ink = (key) => env.wire(key).querySelector(".wd-line").getAttribute("style");
  assert.equal(ink("ledc:0"), "stroke:var(--wire-1)");
  assert.equal(ink("ledc:3"), "stroke:var(--wire-off)", "not marked wired");
  assert.equal(ink("pca:0"), "stroke:var(--wire-3)", "no tick anybody could turn off, so it is wired");
  assert.equal(ink("drive"), "stroke:var(--wire-4)");
  assert.equal(ink("protor2link"), "stroke:var(--wire-6)");
});

// The diagram pictures and names the Body Controller this image runs on, from
// the lineup row the picker pictures it by (#411). The lineup lists every
// peer board, so taking the family's first row rather than the included one
// would name - and draw - a board that is not in the droid.
test("the diagram is titled and pictured with the board this image runs on, not a peer", async () => {
  const env = await boot({ running: "firebeetle2" });
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

