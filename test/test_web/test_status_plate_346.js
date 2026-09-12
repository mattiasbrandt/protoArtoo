// =============================================================================
// test/test_web/test_status_plate_346.js
//
// The Status Plate on the Operator Shell (#346, #324, CONTEXT.md).
//
// The shipped page_bootstrap.js + shell.js + status_stream.js are executed
// against the shipped data/index.html in a real node tree, with the event
// stream driven through a stub that delivers frames the way the device does --
// one "status" event carrying JSON, and a reconnect that replays the frame it
// already had. So "the plate says what the droid is doing, and says how old
// that is" is observed on the live document rather than reasoned about.
//
// The two traps the reference shipped are what most of these tests are about:
// a chip naming a value nothing measured, and a chip watching half of its own
// condition.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser, clickOn } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);
const shellSrc = readData("shell.js");
const statusStreamSrc = readData("status_stream.js");

const IDENTITY = {
  droidName: "artoo",
  board: "artoo_esp32",
  board_capabilities: { sbus: true },
  build_flags: { audio: true },
};

// A droid with everything fitted and nothing wrong: the shape every test below
// starts from, so a test says only what it changes.
const HEALTHY = Object.freeze({
  estop: false,
  sbusHwFailsafe: false,
  sbusSignalLost: false,
  webDriveExpired: false,
  webControlEnabled: true,
  sleepMode: false,
  stationary: false,
  speedLimitMax: 600,
  speedPreset: "normal",
  drive: { state: "idle", detail: "No drive command requested" },
  rcCh1: { state: "active", detail: "Drive SBUS active" },
  dome_link: { state: "connected", uart_owner: "dome" },
  audio: { state: "idle", link_ok: true, rx_status: "available" },
  // Telemetry the plate must refuse.
  uptimeMs: 27790,
  heapFree: 173152,
  wifiRssi: -70,
  wifiConnected: true,
});

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const boot = async ({ status = null } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = {
    document,
    requests: [],
    posts: [],
    // null means the boot read never answers, which is how a session with no
    // frame at all is reached.
    status,
  };

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
        if (path === "/api/status") {
          if (env.status === null) await new Promise(() => {});
          return { data: { ...env.status } };
        }
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      estopPostForm: async (path) => {
        env.posts.push({ path, via: "estopPostForm" });
        return { data: { ok: true } };
      },
      postForm: async (path) => {
        env.posts.push({ path, via: "postForm" });
        return { data: { ok: true } };
      },
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

  // The device's own delivery shape: a "status" event whose data is JSON text,
  // an onopen that fires on every (re)connect, and an onerror the transport
  // reports a drop through.
  const sources = [];
  class FakeEventSource {
    constructor(url) {
      this.url = url;
      this.listeners = new Map();
      sources.push(this);
    }
    addEventListener(type, handler) {
      if (!this.listeners.has(type)) this.listeners.set(type, []);
      this.listeners.get(type).push(handler);
    }
    close() {}
    deliver(type, event) {
      (this.listeners.get(type) || []).forEach((handler) => handler(event));
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
    EventSource: FakeEventSource,
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

  const REAL_SCRIPTS = { "/shell.js": shellSrc, "/status_stream.js": statusStreamSrc };
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
  env.navigate = (to) => {
    windowMock.location.hash = to;
  };
  env.source = () => sources[sources.length - 1];
  // A frame the droid pushed: fresh JSON text, so the shell sees an object it
  // has never seen before.
  env.pushStatus = (patch) =>
    env.source()?.deliver("status", { data: JSON.stringify({ ...HEALTHY, ...patch }) });
  // A reconnect: the stream replays the frame it already holds, which is the
  // same object every subscriber was handed before.
  env.reopenStream = () => env.source()?.onopen?.();
  env.breakStream = () => env.source()?.onerror?.();

  env.chips = () => env.document.querySelectorAll("[data-chip]");
  env.chip = (id) => env.document.getElementById(`chip-${id}`);
  env.chipValue = (id) => env.chip(id)?.querySelector(".status-chip-value")?.textContent;
  env.chipClass = (id) => env.chip(id)?.className;
  env.freshness = () => env.document.getElementById("status-plate-freshness")?.textContent;
  env.freshnessState = () => env.document.getElementById("status-plate-region")?.dataset.freshness;

  await sleep(160);
  return env;
};

// The eight, in the order #324 fixed them: by what bites fastest, never by
// subsystem. Written out here rather than read from shell.js, so a reordering
// of the shipped table fails this test instead of agreeing with it.
const EXPECTED_CHIPS = ["estop", "drive", "rclink", "control", "sleep", "spd", "domelink", "soundlink"];

// ---------------------------------------------------------------------------
// The plate
// ---------------------------------------------------------------------------

test("eight chips render in the fixed order, on one plate, with internal separators", async () => {
  const env = await boot({ status: { ...HEALTHY } });

  assert.deepEqual(
    env.chips().map((chip) => chip.dataset.chip),
    EXPECTED_CHIPS,
    "the order is read by muscle memory, so it is the contract",
  );

  const plates = env.document.querySelectorAll(".status-plate");
  assert.equal(plates.length, 1, "one recessed plate, not eight floating boxes");
  assert.deepEqual(
    plates[0].children.map((child) => child.dataset.chip),
    EXPECTED_CHIPS,
    "every chip is a cell of that one plate",
  );

  // The separator is the plate's own rule on the left edge of every cell after
  // the first, so a cell that is ever hidden takes its rule with it. Asserted
  // on the stylesheet because there is nothing in the markup to assert: no
  // JavaScript may touch a separator, and this is what says so.
  const css = readData("style.css");
  assert.match(css, /\.status-plate > \* \+ \* \{\s*border-left:/, "the plate draws its own separators");
  assert.deepEqual(
    env.chips().filter((chip) => /separator|divider/i.test(chip.className)),
    [],
    "no chip carries a separator of its own",
  );
});

test("the plate is chrome: the same nodes survive every navigation", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  const before = EXPECTED_CHIPS.map((id) => env.chip(id));
  const region = env.document.getElementById("status-plate-region");
  assert.ok(before.every(Boolean), "all eight are there to begin with");

  for (const route of ["drive", "dome", "sound", "servo", "seq", "rc", "setup", "wifi", "firmware", "home"]) {
    env.navigate(`#${route}`);
    await sleep(180);
    assert.equal(env.document.body.dataset.page, route, `${route} is the mounted surface`);
    assert.strictEqual(env.document.getElementById("status-plate-region"), region, `${route}: the same plate`);
    EXPECTED_CHIPS.forEach((id, index) => {
      assert.strictEqual(env.chip(id), before[index], `${route}: ${id} is the same node`);
    });
  }
});

test("every chip shows a value in both states, and none of them goes blank", async () => {
  // Each row drives one chip to both ends of its own condition. A plate that
  // is empty when all is well cannot be told from one that stopped updating,
  // so "" is a failure on either side.
  const bothWays = {
    estop: [{}, { estop: true }],
    drive: [{}, { drive: undefined }],
    rclink: [{}, { rcCh1: undefined }],
    control: [{}, { webControlEnabled: false }],
    sleep: [{}, { sleepMode: true }],
    spd: [{}, { speedLimitMax: 300 }],
    domelink: [{}, { dome_link: { state: "disabled" } }],
    soundlink: [{}, { audio: undefined }],
  };

  const env = await boot({ status: { ...HEALTHY } });
  for (const [id, patches] of Object.entries(bothWays)) {
    const seen = [];
    for (const patch of patches) {
      // undefined in a patch means "the key the firmware omits", which
      // JSON.stringify drops for us -- the same absence the device sends.
      env.pushStatus(patch);
      await sleep(5);
      const value = env.chipValue(id);
      assert.ok(value && value.trim().length > 0, `${id} went blank for ${JSON.stringify(patch)}`);
      seen.push(value);
    }
    assert.notEqual(seen[0], seen[1], `${id} says the same thing in both states`);
  }
});

test("no telemetry reaches the plate, and there is no WiFi chip", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  const plateText = env.document.querySelector(".status-plate").textContent;

  assert.ok(!EXPECTED_CHIPS.includes("wifi"), "WiFi earns no chip: if WiFi is down nobody is reading this");
  for (const telemetry of ["27790", "173152", "-70", "UPTIME", "HEAP", "RSSI", "WIFI"]) {
    assert.ok(
      !plateText.toUpperCase().includes(telemetry),
      `${telemetry} is Dashboard's and never the plate's, found in ${JSON.stringify(plateText)}`,
    );
  }
});

// ---------------------------------------------------------------------------
// A chip watching half its condition -- the reference's shipped Bug 2
// ---------------------------------------------------------------------------

test("DRIVE reads every input that can hold the feet, not one of them", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  assert.equal(env.chipValue("drive"), "ARMED");

  // Each of these alone makes DriveTask emit zero frames. A chip reading only
  // the estop sits dark for the other three.
  for (const patch of [
    { estop: true },
    { sbusHwFailsafe: true },
    { sbusSignalLost: true },
    { webDriveExpired: true },
  ]) {
    env.pushStatus(patch);
    await sleep(5);
    assert.equal(
      env.chipValue("drive"),
      "STOPPED",
      `the feet are held by ${Object.keys(patch)[0]} and the chip must say so`,
    );
    assert.match(env.chipClass("drive"), /status-chip-stopped/, "and it is the one colour for stopped");
  }

  // A Foot Drive nobody fitted is not a droid that stopped: no colour.
  env.pushStatus({ drive: undefined });
  await sleep(5);
  assert.equal(env.chipValue("drive"), "OFF");
  assert.equal(env.chipClass("drive"), "status-chip", "a component nobody fitted takes no colour");
});

test("RC LINK reads the hardware failsafe bit as well as the frames", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  assert.equal(env.chipValue("rclink"), "OK");

  // A transmitter switched off: the receiver keeps sending frames and asserts
  // its failsafe bit, so the channel still reads "active" and only the bit
  // says the link is dead. This is the half a one-source chip misses.
  env.pushStatus({ sbusHwFailsafe: true, rcCh1: { state: "active" } });
  await sleep(5);
  assert.equal(env.chipValue("rclink"), "FAILSAFE");

  // single_sbus + useCh2: the firmware routes the drive receiver to rcCh2 and
  // omits rcCh1 entirely. A chip reading rcCh1 alone reports no RC on a
  // working droid.
  env.pushStatus({ rcCh1: undefined, rcCh2: { state: "active" } });
  await sleep(5);
  assert.equal(env.chipValue("rclink"), "OK", "the routed receiver is still the RC link");

  env.pushStatus({ rcCh1: { state: "signal_lost" } });
  await sleep(5);
  assert.equal(env.chipValue("rclink"), "LOST");

  env.pushStatus({ rcCh1: { state: "not_seen" } });
  await sleep(5);
  assert.equal(env.chipValue("rclink"), "NO FRAMES");
  assert.equal(env.chipClass("rclink"), "status-chip", "a link that never started has not stopped");

  // Standard PWM: the firmware publishes no liveness for PWM inputs at all,
  // so the chip names the input and claims no link. A chip may only print what
  // something measured.
  env.pushStatus({ rcCh1: { state: "ready" } });
  await sleep(5);
  assert.equal(env.chipValue("rclink"), "PWM");
});

test("DOME LINK and SOUND LINK both read who owns the shared bus", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  assert.equal(env.chipValue("domelink"), "OK");
  assert.equal(env.chipValue("soundlink"), "OK");

  // The dome and the sound module share UART2. While sound holds it the dome
  // heartbeat cannot arrive and the firmware reports a plain "lost" -- so a
  // chip reading only the state says the link died when nobody could ask.
  env.pushStatus({ dome_link: { state: "lost", uart_owner: "audio" } });
  await sleep(5);
  assert.equal(env.chipValue("domelink"), "SOUND HAS BUS");
  assert.equal(env.chipClass("domelink"), "status-chip", "a busy bus is not a stopped link");

  env.pushStatus({ dome_link: { state: "lost", uart_owner: "dome" } });
  await sleep(5);
  assert.equal(env.chipValue("domelink"), "LOST");
  assert.match(env.chipClass("domelink"), /status-chip-stopped/);

  // The same shape from the other end: link_ok false means either no answer or
  // a bus the dome is holding, and only rx_status tells them apart.
  env.pushStatus({ audio: { link_ok: false, rx_status: "blocked_by_dome_uart" } });
  await sleep(5);
  assert.equal(env.chipValue("soundlink"), "DOME HAS BUS");
  assert.equal(env.chipClass("soundlink"), "status-chip");

  env.pushStatus({ audio: { link_ok: false, rx_status: "no_response" } });
  await sleep(5);
  assert.equal(env.chipValue("soundlink"), "NO ANSWER");
  assert.match(env.chipClass("soundlink"), /status-chip-stopped/);
});

// ---------------------------------------------------------------------------
// A chip naming a value nothing measured -- the reference's shipped Bug 1
// ---------------------------------------------------------------------------

test("SPD prints the cap the droid sent and never a preset name it cannot check", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  assert.equal(env.chipValue("spd"), "600", "the cap, straight from the payload");

  env.pushStatus({ speedLimitMax: 450, speedPreset: "normal" });
  await sleep(5);
  assert.equal(
    env.chipValue("spd"),
    "450",
    "a limit written directly leaves the payload's preset name saying normal regardless,"
      + " so the chip prints the number that is measured",
  );
  assert.ok(
    !env.chipValue("spd").toLowerCase().includes("normal"),
    "and never the name, which would be a preset the number does not belong to",
  );
});

// ---------------------------------------------------------------------------
// A route, a control, and an affordance that says which
// ---------------------------------------------------------------------------

test("a chip press opens the screen where that thing is changed, through the one navigation path", async () => {
  const env = await boot({ status: { ...HEALTHY } });

  const destinations = { drive: "drive", rclink: "rc", control: "drive", sleep: "home", spd: "drive", domelink: "dome", soundlink: "sound" };
  for (const [id, page] of Object.entries(destinations)) {
    assert.equal(env.chip(id).getAttribute("href"), `#${page}`, `${id} carries the address of ${page}`);
    env.navigate("#home");
    await sleep(180);
    // A hash address is the whole navigation: the same path the nav, a legacy
    // link and a typed address all take. Nothing here is a second router.
    env.navigate(env.chip(id).getAttribute("href"));
    await sleep(200);
    assert.equal(env.document.body.dataset.page, page, `${id} landed on ${page}`);
  }

  assert.deepEqual(env.posts, [], "and not one of the seven asked the droid for anything");
});

test("only the estop acts, and it is the same stop the topbar button sends", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  const estopChip = env.chip("estop");

  assert.equal(estopChip.tagName, "BUTTON", "the one cell that acts is not an address");
  assert.equal(estopChip.getAttribute("href"), null, "and it routes nowhere");

  estopChip.fire("click", { type: "click" });
  await sleep(40);
  assert.deepEqual(
    env.posts.map((post) => post.path),
    ["/api/estop"],
    "one latch request and nothing else",
  );
  assert.equal(
    env.posts[0].via,
    "estopPostForm",
    "carried by the estop path, which skips the request slot and is never retried -- the same"
      + " function the topbar button calls, so the two cannot drift",
  );

  // And the release is still not one press from every screen: the chip
  // latches, it never clears.
  assert.deepEqual(
    env.posts.filter((post) => post.path === "/api/estop/clear"),
    [],
    "the direction that lets the droid move again stays on Drive and Dashboard",
  );
});

test("what a press does is visible on the page, and separate from what a chip reports", async () => {
  const env = await boot({ status: { ...HEALTHY } });

  // Visible text at the entrance, not a title: a title carries no affordance
  // on a button and a bench tablet has no hover at all (ADR 0059).
  const affordance = env.document.querySelector(".status-plate-affordance").textContent;
  assert.match(affordance, /Press a chip to open the screen/, "it says what pressing does");
  assert.match(affordance, /ESTOP cuts drive/, "including the one cell that acts instead");

  // The title is fixed and names the consequence; the chip's text is the
  // state. Two surfaces, and the paint function only ever touches one of them.
  const titleBefore = env.chip("domelink").getAttribute("title");
  assert.match(titleBefore, /^Opens Dome, where this is changed$/);
  env.pushStatus({ dome_link: { state: "lost", uart_owner: "dome" } });
  await sleep(5);
  assert.equal(env.chip("domelink").getAttribute("title"), titleBefore, "the affordance does not move with the state");
  assert.equal(env.chipValue("domelink"), "LOST", "while the state does");

  // The destination is named by reading SURFACES, so a rename stays one field.
  assert.equal(
    env.chip("drive").getAttribute("title"),
    "Opens Foot Drive, where this is changed",
    "B2d renamed Drive to Foot Drive with its page untouched; the plate reads the name rather than restating it",
  );

  // Nothing carries a second copy of the state: the visible text is the
  // accessible name (WCAG 2.5.3), so there is no aria-label to go stale.
  assert.deepEqual(
    env.chips().filter((chip) => chip.getAttribute("aria-label") !== null).map((chip) => chip.dataset.chip),
    [],
    "a chip with an aria-label has two copies of its state",
  );
});

// ---------------------------------------------------------------------------
// One freshness state for the whole plate
// ---------------------------------------------------------------------------

test("the plate says its own age, once, and never per chip", async () => {
  const env = await boot({ status: { ...HEALTHY } });

  assert.equal(env.document.querySelectorAll(".status-plate-freshness").length, 1, "one freshness state");
  assert.match(env.freshness(), /Last heard from the droid/, "and it says how old the readout is");
  assert.equal(env.freshnessState(), "live");

  assert.deepEqual(
    env.chips().filter((chip) => /fresh|stale|age/i.test(chip.className)).map((chip) => chip.dataset.chip),
    [],
    "eight markers would repeat one fact seven times",
  );
});

test("before the droid has said anything the plate says it is still finding out", async () => {
  // The boot read never answers, so no frame has arrived at all.
  const env = await boot({ status: null });

  assert.equal(env.freshnessState(), "finding-out");
  assert.match(env.freshness(), /Still finding out/);
  for (const id of EXPECTED_CHIPS) {
    assert.equal(env.chipValue(id), "FINDING OUT", `${id} says so rather than going blank`);
  }
});

test("a dropped stream keeps the values and says it is reconnecting", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  env.pushStatus({ estop: true });
  await sleep(5);
  assert.equal(env.chipValue("estop"), "LATCHED");

  env.breakStream();
  assert.equal(env.freshnessState(), "finding-out");
  assert.match(env.freshness(), /Reconnecting/);
  assert.match(env.freshness(), /the values it last sent/);
  assert.equal(env.chipValue("estop"), "LATCHED", "the values are kept: a blank plate is the worse lie");

  env.pushStatus({ estop: true });
  await sleep(5);
  assert.equal(env.freshnessState(), "live", "a frame arriving is the stream working again");
});

test("a replayed frame does not restamp the age", async () => {
  // The stream re-emits the frame it already holds when it reconnects and when
  // a hidden tab comes back. Counting that as a new measurement is the plate
  // claiming a reading nobody took, at exactly the moment an operator is most
  // likely to be looking at it.
  const env = await boot({ status: { ...HEALTHY } });
  env.pushStatus({});
  await sleep(2600);

  // Read the seconds rather than the whole line: the line ticks once a second
  // on its own, so comparing the text across a second boundary would fail for
  // the one reason this test is not about.
  const secondsShown = () => {
    const match = /droid (\d+)s ago/.exec(env.freshness());
    return match ? Number(match[1]) : null;
  };

  const aged = secondsShown();
  assert.ok(aged !== null && aged >= 2, `the age advanced on its own, got ${JSON.stringify(env.freshness())}`);

  env.reopenStream();
  const afterReplay = secondsShown();
  assert.ok(
    afterReplay !== null && afterReplay >= aged,
    `replaying the cached frame must not reset the age: ${JSON.stringify(env.freshness())}`,
  );

  env.pushStatus({});
  await sleep(5);
  assert.match(env.freshness(), /just now/, "while a frame the droid actually sent does");
});

test("the freshness state is never amber", async () => {
  // CONTEXT.md "Status Plate": the operator cannot act on a reconnect that is
  // already running, and #327 reserves amber for what they can act on. Read
  // off the stylesheet, resolved through :root, because "the rule does not
  // contain the string --warning" is not the same claim.
  const css = readData("style.css").replace(/\/\*[\s\S]*?\*\//g, "");
  const root = /:root\s*\{([^{}]*)\}/.exec(css)[1];
  const amber = /--warning:\s*([^;]+)/.exec(root)[1].trim();
  assert.match(amber, /^#[0-9a-fA-F]{6}$/, `expected a colour for --warning, read ${amber}`);

  const offenders = [];
  const rule = /([^{}]+)\{([^{}]*)\}/g;
  let match;
  while ((match = rule.exec(css)) !== null) {
    const selector = match[1].trim();
    if (!/status-plate|status-chip|ignored-input/.test(selector)) continue;
    for (const declaration of match[2].split(";")) {
      const resolved = declaration.replace(/var\(\s*(--[\w-]+)[^)]*\)/g, (whole, name) => {
        const value = new RegExp(`${name}:\\s*([^;]+)`).exec(root);
        return value ? value[1].trim() : whole;
      });
      if (resolved.includes(amber) || /--warning/.test(declaration)) {
        offenders.push(`${selector} {${declaration} }`);
      }
    }
  }
  assert.deepEqual(offenders, [], "the plate must not spend amber anywhere");
});

// ---------------------------------------------------------------------------
// What the plate must not have cost
// ---------------------------------------------------------------------------

test("the plate reads the status the session already holds and opens no second stream", async () => {
  const env = await boot({ status: { ...HEALTHY } });

  assert.equal(
    env.requests.filter((path) => path === "/api/status").length,
    1,
    "one read for the session -- the plate rides the estop's, it does not add its own",
  );
  assert.equal(env.document.getElementById("fw-meta")?.textContent, "Loading firmware info...",
    "and the firmware line the footer owns is still in the container the plate moved into");
});

test("a plate click is not a page load, so the shell keeps its session", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  // The shell's capture handler turns a link to a surface DOCUMENT into a
  // route. A chip carries a hash address instead, which the browser resolves
  // without a request at all -- so this asserts the chip never reaches that
  // handler and never asks the device for a document.
  const before = env.requests.length;
  const event = clickOn(env.document, env.chip("dome"), { target: env.chip("domelink") });
  assert.equal(event.defaultPrevented, false, "a hash address needs no interception");
  assert.equal(env.requests.length, before, "and it fetched nothing");
});

// ---------------------------------------------------------------------------
// The Ignored Input Notice: the other end of the same mechanism
//
// The chip assumes you are looking at the screen; the notice assumes you know
// where to go. Together they answer what and where (#324).
// ---------------------------------------------------------------------------

// A control a surface has switched off, the way gateControls() switches one
// off, and the press the browser still delivers on it: pointerdown, because a
// disabled control suppresses mousedown and click outright.
const refusedControl = (env) => {
  const button = env.document.createElement("button");
  button.setAttribute("aria-disabled", "true");
  button.disabled = true;
  env.document.getElementById("shell-content").appendChild(button);
  return button;
};

const pointer = (env, target) => ({
  down: () => env.document.dispatch("pointerdown", { type: "pointerdown", target }),
  up: () => env.document.dispatch("pointerup", { type: "pointerup", target }),
});

const noticeShown = (env) => !env.document.getElementById("ignored-input-notice").classList.contains("hidden");
const noticeText = (env) => env.document.getElementById("ignored-input-text").textContent;
const noticeRoute = (env) => env.document.getElementById("ignored-input-route");

test("a press on a control the droid cannot act on says what is off, instead of nothing", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  await sleep(5);
  assert.equal(noticeShown(env), false, "nothing has been pressed yet");

  pointer(env, refusedControl(env)).down();

  assert.equal(noticeShown(env), true, "the silence is the failure this closes");
  assert.match(noticeText(env), /That control is switched off right now\./, "it reports the attempt");
  assert.match(noticeText(env), /The estop is latched\./, "and names what is off");
  assert.equal(
    env.document.getElementById("ignored-input-notice").getAttribute("role"),
    "status",
    "a notice, not an alert: nothing failed and nothing is refusing the operator personally",
  );
});

test("the notice routes where that thing is changed, and to the chip's own destination", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: false, webControlEnabled: false } });
  await sleep(5);
  pointer(env, refusedControl(env)).down();

  assert.match(noticeText(env), /has not consented to browser control/);
  assert.equal(
    noticeRoute(env).getAttribute("href"),
    env.chip("control").getAttribute("href"),
    "the notice says what and the chip says where -- one destination, not two",
  );
  assert.match(noticeRoute(env).textContent, /^Open Foot Drive, where that is changed$/,
    "and the destination is named by reading SURFACES, so a rename stays one field");
});

test("only the rising edge of a press is an attempt", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  await sleep(5);
  const press = pointer(env, refusedControl(env));

  press.down();
  assert.match(noticeText(env), /The estop is latched\./);

  // The droid's state changes under a pointer that is still down. A second
  // pointerdown -- a second finger, or a browser repeating one -- is not a
  // second attempt, so the notice must not follow it.
  env.pushStatus({ estop: false, webControlEnabled: false });
  await sleep(5);
  press.down();
  assert.match(
    noticeText(env),
    /The estop is latched\./,
    "a pointerdown while already pressing is the same attempt",
  );

  // Releasing and pressing again is a new attempt.
  press.up();
  press.down();
  assert.match(noticeText(env), /has not consented to browser control/, "and this one is heard");
});

test("bursts merge over a 1500 ms wall-clock window", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  await sleep(5);
  const press = pointer(env, refusedControl(env));
  const node = env.document.getElementById("ignored-input-notice");

  press.down();
  press.up();
  assert.equal(noticeShown(env), true);

  // Put the notice away the way its own visible window does, so a second
  // showing is observable rather than indistinguishable from the first.
  node.classList.add("hidden");
  press.down();
  press.up();
  assert.equal(noticeShown(env), false, "a second attempt inside the window merges into the first");

  await sleep(1600);
  press.down();
  assert.equal(noticeShown(env), true, "and past the window it is a new attempt");
});

test("the rate limit resets the moment the thing stops being off", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  await sleep(5);
  const press = pointer(env, refusedControl(env));
  const node = env.document.getElementById("ignored-input-notice");

  press.down();
  press.up();
  assert.equal(noticeShown(env), true);
  node.classList.add("hidden");

  // Cleared, then latched again, well inside the burst window. Having been
  // told once is not a reason to be silent about the next time it happens:
  // changing your mind and back must not buy a second and a half of silence.
  env.pushStatus({ estop: false });
  await sleep(5);
  env.pushStatus({ estop: true });
  await sleep(5);

  press.down();
  assert.equal(noticeShown(env), true, "the window was released when the estop cleared");
});

test("the notice comes down when its cause clears, because the door it opened has closed", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  await sleep(5);
  pointer(env, refusedControl(env)).down();
  assert.equal(noticeShown(env), true);

  env.pushStatus({ estop: false });
  await sleep(5);
  assert.equal(noticeShown(env), false, "what it was pointing at is fixed");
});

test("the notice stays quiet when the droid is holding nothing the plate carries", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  await sleep(5);
  pointer(env, refusedControl(env)).down();
  assert.equal(
    noticeShown(env),
    false,
    "a control off for a reason this plate does not carry would otherwise be told"
      + " whatever happened to be off, which is a guess dressed as a fact",
  );
});

test("pressing a control that is not switched off says nothing at all", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  await sleep(5);
  const live = env.document.createElement("button");
  env.document.getElementById("shell-content").appendChild(live);

  pointer(env, live).down();
  assert.equal(noticeShown(env), false, "nothing was ignored: the press went through");
});

test("the notice is uncoloured, and never wears a reserved colour", async () => {
  // Nothing failed, so it is a Note (#327, docs/ui-copy-voice.md rule 11).
  const css = readData("style.css").replace(/\/\*[\s\S]*?\*\//g, "");
  const root = /:root\s*\{([^{}]*)\}/.exec(css)[1];
  const reserved = ["--warning", "--danger"].map(
    (name) => new RegExp(`${name}:\\s*([^;]+)`).exec(root)[1].trim(),
  );

  const offenders = [];
  const rule = /([^{}]+)\{([^{}]*)\}/g;
  let match;
  while ((match = rule.exec(css)) !== null) {
    if (!/ignored-input/.test(match[1])) continue;
    for (const declaration of match[2].split(";")) {
      const resolved = declaration.replace(/var\(\s*(--[\w-]+)[^)]*\)/g, (whole, name) => {
        const value = new RegExp(`${name}:\\s*([^;]+)`).exec(root);
        return value ? value[1].trim() : whole;
      });
      if (reserved.some((colour) => resolved.includes(colour))) offenders.push(`${match[1].trim()} {${declaration} }`);
    }
  }
  assert.deepEqual(offenders, [], "a Note carries neither amber nor red");
});

test("a control that is merely waiting for the droid is not a control that is off", async () => {
  // The Dashboard marks a control aria-disabled while its request is in
  // flight, and those carry .is-pending. "That control is switched off" is
  // false about one that is waiting for an answer -- and it would fire on
  // exactly the second press an impatient operator makes.
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  await sleep(5);
  const pending = refusedControl(env);
  pending.classList.add("is-pending");

  pointer(env, pending).down();
  assert.equal(noticeShown(env), false, "busy is not off");
});

// The stylesheet read the way a browser stacks it: rules flattened, the last
// declaration for a property winning, so this asserts the painted order rather
// than the presence of a string. Same shape as the estop's own stacking test.
const zIndexOf = (selector) => {
  const css = readData("style.css").replace(/\/\*[\s\S]*?\*\//g, "");
  let value = null;
  const rule = /([^{}]+)\{([^{}]*)\}/g;
  let match;
  while ((match = rule.exec(css)) !== null) {
    if (!match[1].split(",").map((part) => part.trim()).includes(selector)) continue;
    const declared = /(?:^|;)\s*z-index\s*:\s*([^;]+)/.exec(match[2]);
    if (declared) value = Number(declared[1].trim());
  }
  return value;
};

test("the sleep overlay does not paint over the plate that says the droid is asleep", () => {
  // Measured in a browser before the rule existed: elementFromPoint over the
  // first chip returned the overlay, so an operator looking at a sleeping
  // droid could neither read the SLEEP chip that says why nor press ESTOP.
  const plate = zIndexOf(".status-plate-region");
  const overlay = zIndexOf(".sleep-overlay");

  assert.ok(Number.isFinite(overlay), "the sleep overlay stacks explicitly");
  assert.ok(Number.isFinite(plate), "and so must the plate, or the overlay covers it");
  assert.ok(
    plate >= overlay,
    `the plate (${plate}) must stack at or above the sleep overlay (${overlay}) -- a posture the`
      + " droid is in must not hide the chrome that reports it (#330)",
  );
});

test("the plate's separators survive the chip's own border reset", () => {
  // A cascade loss is invisible to a test with no CSS engine, and this one
  // happened: written the reference's way the separator rule sits at the same
  // specificity as .status-chip's `border: none` and further up the file, so
  // the plate drew as one undivided strip. Measured at 1px per cell in a
  // browser after the fix; asserted here on the two things that made it lose.
  const css = readData("style.css").replace(/\/\*[\s\S]*?\*\//g, "");
  const specificity = (selector) => (selector.match(/[.#[]/g) || []).length;

  const rule = /([^{}]+)\{([^{}]*)\}/g;
  let separator = null;
  let reset = null;
  let order = 0;
  let match;
  while ((match = rule.exec(css)) !== null) {
    const selector = match[1].trim().replace(/\s+/g, " ");
    order += 1;
    if (/\+ \*$/.test(selector) && /border-left/.test(match[2])) separator = { selector, order };
    if (selector === ".status-chip" && /(?:^|;)\s*border\s*:/.test(match[2])) reset = { selector, order };
  }

  assert.ok(separator, "the plate declares a separator on every cell after the first");
  assert.ok(reset, "and the chip resets its border, which is what the separator has to survive");
  assert.ok(
    specificity(separator.selector) > specificity(reset.selector) || separator.order > reset.order,
    `the separator (${separator.selector}, rule ${separator.order}) must outrank or follow`
      + ` ${reset.selector} (rule ${reset.order}), or no separator is drawn`,
  );
});

test("a press on a refused control the browser hides from hit testing is still heard", async () => {
  // data/style.css puts `pointer-events: none` on a disabled .btn, so the
  // browser delivers the press to the CONTAINER and event.target never names
  // the control. Measured on the shipped stylesheet: pressing the Dashboard's
  // disabled estop-clear button landed on its .top-action wrapper. This is
  // that delivery shape, with the geometry the browser supplies.
  const env = await boot({ status: { ...HEALTHY, webControlEnabled: false } });
  await sleep(5);

  const container = env.document.createElement("div");
  const button = env.document.createElement("button");
  button.setAttribute("aria-disabled", "true");
  button.disabled = true;
  button.getBoundingClientRect = () => ({ left: 100, right: 200, top: 20, bottom: 60 });
  container.appendChild(button);
  env.document.getElementById("shell-content").appendChild(container);

  env.document.dispatch("pointerdown", { type: "pointerdown", target: container, clientX: 150, clientY: 40 });
  assert.equal(noticeShown(env), true, "the control under the pointer is the one that was refused");
  assert.match(noticeText(env), /has not consented to browser control/);

  // And a press on the same container away from that control is not a press on
  // it: the box is the whole test, so it has to actually be consulted.
  env.document.getElementById("ignored-input-notice").classList.add("hidden");
  env.document.dispatch("pointerup", { type: "pointerup", target: container });
  env.document.dispatch("pointerdown", { type: "pointerdown", target: container, clientX: 400, clientY: 400 });
  assert.equal(noticeShown(env), false, "nothing refused was pressed");
});
