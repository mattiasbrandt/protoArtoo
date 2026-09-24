// =============================================================================
// test/test_web/test_status_plate.js
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

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";

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
const liveReadingSrc = readData("live_reading.js");

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

const boot = async ({ status = null, stream = true } = {}) => {
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
    // Set true to make every later /api/status read refuse. The fallback poll
    // is the only reader on a browser with no EventSource, so a refusal there
    // is the whole of "nothing is arriving" on that path.
    statusFails: false,
    // Every interval the code under test registers, so a 3-second poll can be
    // driven in a millisecond instead of waited for.
    intervals: [],
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
      env.intervals.push({ fn, ms });
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
          if (env.statusFails) throw new Error("Network unreachable");
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
  // A browser without EventSource takes the shell's fallback poll instead of
  // the stream. Leaving the global off entirely is what `typeof EventSource
  // === "undefined"` actually reads (data/status_stream.js).
  if (stream) context.EventSource = FakeEventSource;
  context.globalThis = context;

  const REAL_SCRIPTS = { "/shell.js": shellSrc, "/status_stream.js": statusStreamSrc, "/live_reading.js": liveReadingSrc };
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
  // A frame exactly as the controller sends it, text and all -- the only way
  // to deliver something that is not a status object at all.
  env.pushRaw = (text) => env.source()?.deliver("status", { data: text });
  // Run every interval registered at this cadence, once. The shell's fallback
  // poll is the Live Reading's 5000 ms one (POLL_MS, data/live_reading.js).
  env.fireInterval = (ms) => env.intervals.filter((entry) => entry.ms === ms).forEach((entry) => entry.fn());
  env.statusReads = () => env.requests.filter((path) => path === "/api/status").length;

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
    assert.match(env.chipClass("drive"), /status-chip-stopped/, "and it is the one color for stopped");
  }

  // A Foot Drive nobody fitted is not a droid that stopped: no color.
  env.pushStatus({ drive: undefined });
  await sleep(5);
  assert.equal(env.chipValue("drive"), "OFF");
  assert.equal(env.chipClass("drive"), "status-chip", "a component nobody fitted takes no color");
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
  assert.equal(env.chipValue("rclink"), "UNMEASURED");
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
  assert.equal(env.chipValue("domelink"), "HELD BY SOUND");
  assert.equal(env.chipClass("domelink"), "status-chip", "a busy bus is not a stopped link");

  env.pushStatus({ dome_link: { state: "lost", uart_owner: "dome" } });
  await sleep(5);
  assert.equal(env.chipValue("domelink"), "LOST");
  assert.match(env.chipClass("domelink"), /status-chip-stopped/);

  // The same shape from the other end: link_ok false means either no answer or
  // a bus the dome is holding, and only rx_status tells them apart.
  env.pushStatus({ audio: { link_ok: false, rx_status: "blocked_by_dome_uart" } });
  await sleep(5);
  assert.equal(env.chipValue("soundlink"), "HELD BY DOME");
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

// ---------------------------------------------------------------------------
// One freshness state for the whole plate
// ---------------------------------------------------------------------------

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
  assert.equal(env.chipValue("drive"), "STOPPED", "the values are kept: a blank plate is the worse lie");
  assert.equal(env.chipValue("control"), "ON");
  // Except the estop's: once contact is lost nobody knows it is still latched
  // or still clear, and a move act is live only on a heard, clear one (#419).
  assert.equal(env.chipValue("estop"), "FINDING OUT", "the estop goes back to finding out");

  env.pushStatus({ estop: true });
  await sleep(5);
  assert.equal(env.freshnessState(), "live", "a frame arriving is the stream working again");
  assert.equal(env.chipValue("estop"), "LATCHED", "and the droid has said its estop again");
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
  assert.equal(env.document.getElementById("fw-meta")?.textContent, "Finding out",
    "and the firmware line the footer owns is still in the container the plate moved into");
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

// ---------------------------------------------------------------------------
// A frame the shell has not verified (#346 reopened)
//
// Three ways the plate came to report a state nobody confirmed, all failing in
// the direction that reads SAFE on a droid that is not:
//
//   1. the controller never said a failsafe latched (fixed in the firmware,
//      test/test_native/test_failsafe_gate),
//   2. the browser painted an error envelope as a droid with nothing wrong,
//   3. the browser never noticed that nothing had arrived at all, because the
//      one thing that could say so was an SSE event branch and the fallback
//      path has no SSE.
//
// The browser half of the answer is one rule: a frame is read only if the
// droid built it, and the freshness state is written by every path that can
// fail rather than by one of them.
// ---------------------------------------------------------------------------

// Written out here rather than read from shell.js: this is the contract, and a
// list the test read from the code under test would agree with any change to
// it. Every one of these is a field a chip reads with no unknown branch of its
// own, so its absence would resolve to a VALUE -- and for four of the six that
// value is "nothing is holding the feet".
const REQUIRED_SAFETY_FIELDS = [
  "estop",
  "sbusHwFailsafe",
  "sbusSignalLost",
  "webDriveExpired",
  "webControlEnabled",
  "sleepMode",
];

// The bytes buildStatusJson() actually sends when it cannot build a payload
// (src/web/web_server.cpp), over the same "status" event as a real frame.
const OVERFLOW_ENVELOPE = '{"ok":false,"error":"status payload overflow"}';

test("an error envelope is not a status frame, and never clears a latch", async () => {
  const env = await boot({ status: { ...HEALTHY } });
  env.pushStatus({ estop: true, webControlEnabled: false });
  await sleep(5);
  assert.equal(env.chipValue("estop"), "LATCHED");

  env.pushRaw(OVERFLOW_ENVELOPE);
  await sleep(5);

  assert.equal(env.chipValue("estop"), "LATCHED", "the droid is still latched; only the report failed");
  assert.equal(env.chipValue("control"), "OFF");
  assert.notEqual(env.freshnessState(), "live", "a refresh that produced nothing is not a working readout");
  assert.match(env.freshness(), /could not report/, "and it says which of the two failed");
});

test("a frame missing the fields a safety reading is made from is not read as safe", async () => {
  // Not the envelope shape: a truncated or older frame that parses perfectly
  // and simply does not carry the key. Reading one gives `undefined === true`
  // -> false -> "nothing is stopping the droid", which is the whole defect.
  for (const field of REQUIRED_SAFETY_FIELDS) {
    const env = await boot({ status: { ...HEALTHY } });
    env.pushStatus({ estop: true });
    await sleep(5);
    assert.equal(env.chipValue("estop"), "LATCHED", `${field}: the latch was on screen first`);

    const thin = { ...HEALTHY, estop: true };
    delete thin[field];
    env.pushRaw(JSON.stringify(thin));
    await sleep(5);

    assert.equal(env.chipValue("estop"), "LATCHED", `dropping ${field} must not repaint the plate`);
    assert.notEqual(env.freshnessState(), "live", `dropping ${field} must not read as a good frame`);
  }
});

test("a missing safety field reads as unknown, and never as clear", async () => {
  // With no earlier frame to keep, an unverifiable one leaves the plate saying
  // it has not heard -- grey, per CONTEXT.md "Status Color", where grey is
  // "not reporting, never asked". Green here would be a droid reporting itself
  // healthy on a frame that never mentioned its estop.
  const withoutEstop = { ...HEALTHY };
  delete withoutEstop.estop;
  const env = await boot({ status: withoutEstop });

  assert.equal(env.chipValue("estop"), "FINDING OUT");
  assert.notEqual(env.chipValue("estop"), "CLEAR");
  assert.equal(env.chipClass("estop"), "status-chip", "and it takes no color at all");
  assert.notEqual(env.freshnessState(), "live");
});

test("an error envelope never becomes the session's status", async () => {
  // Refused by the transport rather than by each reader, because the session's
  // cache is what every page that asks later is answered from -- pages this
  // shell cannot reach into. A reader-side guard alone would protect the plate
  // and leave the envelope sitting in the cache, to be handed to the Dashboard
  // and replayed on every reconnect for the rest of the session.
  const env = await boot({ status: { ...HEALTHY } });
  env.pushStatus({ estop: true });
  await sleep(5);

  env.pushRaw(OVERFLOW_ENVELOPE);
  await sleep(5);

  const cached = env.window.PAStatusStream.getLastStatus();
  assert.equal(cached.estop, true, "the session still holds the last frame the droid built");
  assert.equal(cached.ok, undefined, "and not the envelope saying it could not build one");

  const handed = [];
  env.window.PAStatusStream.subscribe((eventType, payload) => {
    if (eventType === "status") handed.push(payload);
  });
  assert.deepEqual(
    handed.map((frame) => frame.estop),
    [true],
    "and a reader that subscribes afterwards is answered from it, not from the envelope",
  );
});

test("the estop's own state line refuses a frame that never mentioned the estop", async () => {
  // The Latching Estop reads the same stream and had the same hole:
  // `!!payload.estop` on a frame with no estop key is "Estop: clear" beside a
  // release control the same frame disables.
  const env = await boot({ status: { ...HEALTHY } });
  const stateLine = () => env.document.getElementById("shell-estop-state")?.textContent;

  env.pushStatus({ estop: true });
  await sleep(5);
  assert.equal(stateLine(), "Estop: latched");

  // Not the envelope: the transport refuses that one before any reader sees
  // it, so an envelope alone would leave this guard unexercised. This frame
  // parses, carries no `ok:false`, and simply does not mention the estop.
  const silent = { ...HEALTHY };
  delete silent.estop;
  env.pushRaw(JSON.stringify(silent));
  await sleep(5);
  assert.equal(stateLine(), "Estop: latched", "a frame that says nothing does not release a latch");

  env.pushRaw(OVERFLOW_ENVELOPE);
  await sleep(5);
  assert.equal(stateLine(), "Estop: latched", "and neither does an envelope");
});

test("a replayed cache is not confirmed connectivity", async () => {
  // The stream re-emits the frame it already holds the moment it reconnects.
  // Treating that as the link working again put the plate back on "live" with
  // values nobody had re-measured.
  const env = await boot({ status: { ...HEALTHY } });
  env.pushStatus({ estop: true });
  await sleep(5);

  env.breakStream();
  assert.notEqual(env.freshnessState(), "live");

  env.reopenStream();
  assert.notEqual(
    env.freshnessState(),
    "live",
    "the cached frame came back, not the droid: nothing has been confirmed yet",
  );
  assert.equal(env.chipValue("drive"), "STOPPED", "and the values are still kept");
  assert.equal(env.chipValue("estop"), "FINDING OUT", "but a replay is not the droid saying its estop");
});

test("a reconnect asks the droid rather than trusting the cache", async () => {
  const env = await boot({ status: { ...HEALTHY, estop: true } });
  const before = env.statusReads();

  env.breakStream();
  env.reopenStream();
  await sleep(20);

  assert.equal(env.statusReads(), before + 1, "a reconnect resynchronises from buildStatusJson()");
  assert.equal(env.freshnessState(), "live", "and the answer to that read is confirmed connectivity");
});

test("a failed fallback poll reaches the plate, not only the console", async () => {
  // No EventSource: the shell's background poll is the only reader, and its
  // failure branch used to console.warn and nothing else. The one flag that
  // could say "not hearing" was written inside an SSE event branch that cannot
  // fire on this path at all.
  const env = await boot({ status: { ...HEALTHY, estop: true }, stream: false });
  assert.equal(env.freshnessState(), "live", "the boot read answered, so the plate starts confirmed");
  assert.equal(env.chipValue("estop"), "LATCHED");

  env.statusFails = true;
  env.fireInterval(5000);
  await sleep(20);

  assert.notEqual(env.freshnessState(), "live", "a poll that is refused is the link not answering");
  assert.equal(env.chipValue("drive"), "STOPPED", "the values are kept here too");
  assert.equal(env.chipValue("estop"), "FINDING OUT", "and the estop is not known");
});

test("the plate stops reading live from every path that can fail, not one", async () => {
  // The three failures are three calls into one writer. Asserted together
  // because "whatever replaces the flag is set by every path that can fail" is
  // the requirement, and a per-path test passes while two of three are wired.
  const dropped = await boot({ status: { ...HEALTHY } });
  dropped.breakStream();
  assert.notEqual(dropped.freshnessState(), "live", "the stream dropped");

  const refused = await boot({ status: { ...HEALTHY } });
  refused.pushRaw(OVERFLOW_ENVELOPE);
  await sleep(5);
  assert.notEqual(refused.freshnessState(), "live", "the droid could not report");

  const polled = await boot({ status: { ...HEALTHY }, stream: false });
  polled.statusFails = true;
  polled.fireInterval(5000);
  await sleep(20);
  assert.notEqual(polled.freshnessState(), "live", "the fallback poll was refused");
});
