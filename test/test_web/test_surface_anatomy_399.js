// =============================================================================
// test/test_web/test_surface_anatomy_399.js
//
// The Surface Anatomy on the chrome and the Dashboard (#399, ADR 0066).
//
// Two halves, because the two files under test are reached differently. The
// chrome is observed by running the shipped page_bootstrap.js + shell.js
// against the shipped data/index.html in a real node tree, the way
// test_status_plate_346.js does - the rail, the topbar and the plate are
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

test("every rail entry draws its own surface's icon, and every icon resolves in the sprite", async () => {
  const env = await bootShell();
  const declared = declaredIcons();
  assert.ok(declared.size >= 11, `expected the shipped surfaces, read ${declared.size}`);

  // The sprite the chrome injected, read from the DOCUMENT rather than from the
  // source: a symbol that never reached the page is not one a <use> can find,
  // which is the whole failure this guards.
  const symbols = new Set(
    env.document.querySelectorAll("symbol").map((node) => node.getAttribute("id")),
  );
  assert.ok(symbols.size > 0, "the chrome must inject the sprite, or every icon is an empty box");

  const drawn = new Map();
  env.document.querySelectorAll("[data-surface-link]").forEach((link) => {
    const use = link.querySelector("use");
    assert.ok(use, `${link.dataset.surfaceLink} has no icon at all`);
    drawn.set(link.dataset.surfaceLink, use.getAttribute("href"));
  });

  assert.ok(drawn.size >= 11, `expected every surface in the rail, drew ${drawn.size}`);
  for (const [page, href] of drawn) {
    assert.equal(
      href,
      `#i-${declared.get(page)}`,
      `${page} must draw the icon its SURFACES row declares, not another surface's`,
    );
    assert.ok(symbols.has(href.slice(1)), `${page} points at ${href}, which the sprite does not define`);
  }

  // Eleven surfaces with one icon between them is well-formed markup that says
  // nothing, so the rail is asserted to tell them apart.
  assert.equal(
    new Set(drawn.values()).size,
    new Set(declared.values()).size,
    "each surface is told from the next by its icon as well as its word",
  );
});

test("the topbar names the surface the operator is on, and follows them to the next one", async () => {
  const env = await bootShell();
  const where = () => env.document.getElementById("shell-where")?.textContent;

  assert.equal(where(), "Dashboard", "the landing names itself");

  env.navigate("#drive");
  await sleep(140);
  assert.equal(
    where(),
    "Foot Drive",
    "and it reads the one place a surface is named, so it cannot say Drive while the nav says Foot Drive (#288)",
  );
  assert.equal(env.document.title, "Foot Drive - artoo", "the browser title says the same");
});

test("a plate cell is its label over its value, with the signal light inside the value", async () => {
  const env = await bootShell();
  const chip = env.document.getElementById("chip-estop");
  assert.ok(chip, "the plate renders its cells");

  const parts = chip.children.map((child) => child.className);
  assert.deepEqual(
    parts,
    ["status-chip-label", "status-chip-value"],
    "the engraving first, then what the needle says - an instrument reads label over value",
  );
  assert.ok(
    chip.querySelector(".status-chip-value").querySelector(".status-chip-dot"),
    "and the light is inside the value line, where the state it reports is",
  );
});

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

test("the Controls subtitle and each control's own readout say the same three things", async () => {
  const env = dashboard({ stationary: true, activeMood: 10, sleepMode: true });
  await env.runSection("app-initial-status");
  await env.settle();

  assert.equal(env.element("snapshot-mode").textContent, "Stationary");
  assert.equal(env.element("snapshot-mood").textContent, "Quiet");
  assert.equal(env.element("snapshot-sleep").textContent, "asleep");

  // The same three words beside the control each belongs to, from the same
  // frame: a subtitle that could disagree with the control under it is worse
  // than no subtitle.
  assert.equal(env.element("opmode-now").textContent, "Stationary");
  assert.equal(env.element("mood-now").textContent, "Quiet");
  assert.equal(env.element("sleep-now").textContent, "asleep");
});

test("the three postures are read independently of one another", async () => {
  // The two booleans are set AGAINST each other on purpose. Mode and sleep are
  // both booleans on the same frame, and a renderer that read one of them for
  // both is invisible while they agree - which they do on an ordinary droid
  // most of the time. A droid parked on a stand but wide awake is the state
  // that tells them apart.
  const standing = dashboard({ stationary: true, activeMood: 14, sleepMode: false });
  await standing.runSection("app-initial-status");
  await standing.settle();

  assert.equal(standing.element("snapshot-mode").textContent, "Stationary");
  assert.equal(standing.element("snapshot-mood").textContent, "Awake+");
  assert.equal(standing.element("snapshot-sleep").textContent, "awake");

  // And the other way round: driving, and asleep. Sleep parks the lights and
  // the chatter; drive and the estop stay awake, so this is a real state.
  const driving = dashboard({ stationary: false, activeMood: 10, sleepMode: true });
  await driving.runSection("app-initial-status");
  await driving.settle();

  assert.equal(driving.element("snapshot-mode").textContent, "Driving");
  assert.equal(driving.element("snapshot-sleep").textContent, "asleep");
});

test("the memory readout prints the block the Health signal beside it judges memory on", async () => {
  // heapLargestBlock is NOT that number: health_signals.js records that it
  // reads a capability mask dominated by leftover IRAM malloc can never hand
  // out, and sits frozen regardless of pressure. A readout that printed it
  // would explain a red Memory light with a number that has nothing to do with
  // why it is red. The two are deliberately far apart here.
  const env = dashboard({ heapFree: 177152, heapLargest8bit: 14540, heapLargestBlock: 61440 });
  await env.runSection("app-initial-status");
  await env.settle();

  assert.equal(env.element("readout-heap").innerHTML, "173<small>kB</small>");
  assert.match(env.element("readout-heap-detail").textContent, /largest single piece 14 kB/);
  assert.doesNotMatch(
    env.element("readout-heap-detail").textContent,
    /60 kB/,
    "the capability mask is not the number admission control sheds against",
  );
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

test("uptime reads as a clock, and a run over a day says how many days", async () => {
  const short = dashboard({ uptimeMs: 2538000, resetReason: "Power-on" });
  await short.runSection("app-initial-status");
  await short.settle();
  assert.equal(short.element("build-uptime").textContent, "00:42:18");
  assert.equal(short.element("build-uptime-detail").textContent, "since a power-on reset");

  const long = dashboard({ uptimeMs: 3 * 86400000 + 3661000, resetReason: "Software" });
  await long.runSection("app-initial-status");
  await long.settle();
  assert.equal(
    long.element("build-uptime").textContent,
    "3d 01:01:01",
    "73:01:01 is not a number anyone converts in their head",
  );
});

test("the firmware row says whether the web assets came from the same build", async () => {
  const matched = dashboard({ firmwareVersion: "v1.3.0", fsVersion: "v1.3.0" });
  await matched.runSection("app-initial-status");
  await matched.settle();
  assert.equal(matched.element("build-firmware").textContent, "v1.3.0");
  assert.match(matched.element("build-firmware-detail").textContent, /match/);

  const drifted = dashboard({ firmwareVersion: "v1.3.0", fsVersion: "v1.2.0" });
  await drifted.runSection("app-initial-status");
  await drifted.settle();
  assert.match(
    drifted.element("build-firmware-detail").textContent,
    /does not match/,
    "a surface talking to an API that moved is what this row exists to show",
  );
});

test("the Health section head counts the states the evaluators actually returned", async () => {
  // Driven through the shipped health_signals.js, so this counts what the
  // Dashboard really shows rather than what a fixture says it shows.
  // One frame carrying all four states. Amber comes from memory because that
  // is where amber is left after #402: a reading the controller did send, that
  // the builder can act on. Not-joined WiFi is grey now, not degraded.
  const env = dashboard({
    heapLargest8bit: 13000,        // -> warn (between the warn and fail floors)
    littleFsReady: false,          // -> fail
    wifiConnected: false,          // -> off (not joined)
    domeEnabled: false,            // -> off (not fitted)
  });
  await env.runSection("app-initial-status");
  await env.settle();

  const summary = env.element("health-summary").textContent;
  assert.match(summary, /^7 signals/, "the count of rows comes first");
  assert.match(summary, /1 degraded/, "and each state the evaluators returned is counted");
  assert.match(summary, /1 faulted/);
  assert.match(summary, /2 not reporting/);
  assert.doesNotMatch(summary, /0 /, "a state with nothing in it is left out rather than written as zero");
});

test("a signal's word is its evaluator's reason, not its state said twice", async () => {
  const env = dashboard({ littleFsReady: true });
  await env.runSection("app-initial-status");
  await env.settle();

  assert.equal(
    env.element("ht-fs").textContent,
    "Mounted",
    "the light beside it already says ok; OK: Mounted says that twice",
  );
  assert.equal(env.element("h-fs").className, "indicator ok", "and the light is what carries the state");
});

test("the Droid Build row rides the config the log level already fetched", async () => {
  const env = dashboard();
  await env.runSection("app-log-level");
  await env.settle();

  assert.equal(
    env.element("build-design").textContent,
    "MrBaddeley MK4 complex",
    "read through the shipped apply seam and named from the shipped catalog",
  );
  assert.match(env.element("build-design-detail").innerHTML, /3 parts fitted/);
  assert.match(env.element("build-design-detail").innerHTML, /Setup/, "and it says where the answer is changed");

  // The controller sheds connections under load and droid_build.js says so
  // itself: a page holding a config payload adopts it and spends no request.
  assert.equal(
    env.requests.filter((request) => request.path === "/api/config").length,
    1,
    "one read of /api/config for the surface, not one per consumer of it",
  );
});
