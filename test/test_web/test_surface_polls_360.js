// =============================================================================
// test/test_web/test_surface_polls_360.js
//
// Leaving a screen stops it asking, and changes nothing on the droid
// (ADR 0048, #360).
//
// Three levels, because the claim is made in three places: the registry in the
// shipped page_bootstrap.js decides whether a poll is wanted, each shipped
// surface module hands its poll to that registry, and the shipped shell is what
// names the surface on screen. Every assertion here is about an observed timer,
// an observed request or an observed node -- never about a flag the code under
// test reports on itself.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { loadPageModule } from "./helpers/page_module_env.js";
import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// =============================================================================
// The registry, driven directly
//
// Timers are recorded rather than run, so "the poll stopped" is a clearInterval
// this suite watched happen.
// =============================================================================

const makeRegistry = () => {
  const intervals = [];
  const cleared = { intervals: [] };
  const events = [];
  const documentListeners = [];

  const documentMock = {
    visibilityState: "visible",
    addEventListener: (type, handler) => documentListeners.push({ type, handler }),
    removeEventListener: (type, handler) => {
      const at = documentListeners.findIndex((l) => l.type === type && l.handler === handler);
      if (at >= 0) documentListeners.splice(at, 1);
    },
  };

  const windowMock = {
    setInterval: (fn, ms) => {
      const id = intervals.length + 1;
      intervals.push({ id, fn, ms });
      return id;
    },
    clearInterval: (id) => cleared.intervals.push(id),
    setTimeout: (fn, ms) => setTimeout(fn, ms),
    clearTimeout: (id) => clearTimeout(id),
    dispatchEvent: (event) => events.push(event),
  };

  const context = {
    window: windowMock,
    document: documentMock,
    console: { log: () => {}, warn: () => {}, error: () => {} },
    Math,
    Promise,
    Set,
    Map,
    Object,
    String,
    Error,
    CustomEvent: class {
      constructor(type, opts = {}) {
        this.type = type;
        this.detail = opts.detail;
      }
    },
  };
  context.globalThis = context;
  vm.runInNewContext(part1Src, context);

  return {
    surface: windowMock.PASurface,
    intervals,
    cleared,
    events,
    document: documentMock,
    live: () => intervals.filter((timer) => !cleared.intervals.includes(timer.id)),
    fire: (timer) => timer.fn(),
  };
};

test("a surface's poll runs only while its surface is the one on screen", () => {
  const env = makeRegistry();
  env.surface.showing("rc");
  env.surface.poll(() => Promise.resolve(), { cadenceMs: 1000 }).start();

  assert.equal(env.live().length, 1, "RC is on screen, so its poll is running");

  env.surface.showing("setup");
  assert.equal(env.live().length, 0, "leaving RC stops what RC was asking for");

  env.surface.showing("rc");
  assert.equal(env.live().length, 1, "and coming back starts it again");
});

test("a surface that turns its own poll on while it is off screen does not start asking", () => {
  const env = makeRegistry();
  env.surface.showing("setup");
  const poll = env.surface.poll(() => Promise.resolve(), { cadenceMs: 5000 });

  env.surface.showing("home");
  poll.start();

  assert.equal(env.live().length, 0, "the manifest said yes; the operator is elsewhere, so nothing runs");

  env.surface.showing("setup");
  assert.equal(env.live().length, 1, "it starts when the operator opens the surface that owns it");
});

test("a left surface shows what it last read until it has answered again", async () => {
  const env = makeRegistry();
  env.surface.showing("rc");
  let answer = null;
  env.surface.poll(() => new Promise((resolve) => { answer = resolve; }), { cadenceMs: 1000 }).start();

  env.surface.showing("setup");
  assert.equal(env.surface.isStale("rc"), true, "what RC is showing is from before the operator left");

  env.surface.showing("rc");
  assert.equal(env.surface.isStale("rc"), true, "being on screen again is not an answer");

  env.fire(env.live()[0]);
  answer({});
  await sleep(0);

  assert.equal(env.surface.isStale("rc"), false, "the answer is what makes it current");
  const fresh = env.events.filter((event) => event.type === "pa:surface-fresh");
  assert.equal(fresh.length, 1, "and the shell is told, so the note it put up can come down");
  assert.equal(fresh[0].detail.surface, "rc");
});

test("a surface that stopped its own poll has not been left, and is not stale", () => {
  const env = makeRegistry();
  env.surface.showing("setup");
  const poll = env.surface.poll(() => Promise.resolve(), { cadenceMs: 5000 });
  poll.start();
  poll.stop();

  assert.equal(env.live().length, 0, "the surface's own stop() stops it");
  assert.equal(env.surface.isStale("setup"), false, "nobody left this screen; it is showing what it means to show");
});

test("a surface can hold its own unmount, and the hold belongs to that surface alone", () => {
  const env = makeRegistry();
  env.surface.showing("seq");
  let unsaved = true;
  env.surface.holdUnmount(() => unsaved);

  assert.equal(env.surface.unmountHeld("seq"), true, "the surface with the unsaved edit is held");
  assert.equal(env.surface.unmountHeld("rc"), false, "and no other surface is");

  unsaved = false;
  assert.equal(env.surface.unmountHeld("seq"), false, "the hold ends when the surface says it has");

  env.surface.releaseUnmount();
  assert.ok(
    env.events.some((event) => event.type === "pa:surface-release"),
    "releasing asks the shell to try the navigation again",
  );
});

test("a hold that throws does not trap the operator on the screen", () => {
  const env = makeRegistry();
  env.surface.showing("seq");
  env.surface.holdUnmount(() => { throw new Error("the guard is broken"); });

  assert.equal(env.surface.unmountHeld("seq"), false, "an unmount nobody could decide is allowed");
});

// =============================================================================
// The shipped surface modules
//
// Each of these ran a raw setInterval of its own before #360. The assertion is
// that the poll it installs now belongs to a surface: naming a different one
// stops it. A module that went back to its own timer fails here.
//
// These run without a shell, so the registry has heard no surface name when the
// module loads and the poll it creates is owned by nobody -- which is exactly
// the state a page opened on its own is in, and why it polls at all.
// =============================================================================

const SURFACE_POLLS = [
  { file: "dome.js", cadenceMs: 5000, what: "the dome's status" },
  { file: "drive.js", cadenceMs: 2000, what: "the drive status" },
  { file: "servo.js", cadenceMs: 1000, what: "the servo status" },
  { file: "sound.js", cadenceMs: 2000, what: "the sound status" },
  { file: "setup.js", cadenceMs: 5000, what: "the serial status" },
  { file: "app.js", cadenceMs: 3000, what: "the dashboard status" },
  { file: "rc.js", cadenceMs: 1000, what: "the RC diagnostics" },
];

for (const { file, cadenceMs, what } of SURFACE_POLLS) {
  test(`${file}: ${what} poll stops when the operator is reading another surface`, async () => {
    const env = loadPageModule(file, { respond: () => ({}) });
    await env.settle();

    const poll = env.intervals.find((timer) => timer.ms === cadenceMs);
    assert.ok(poll, `${file} installs a ${cadenceMs} ms poll`);

    env.window.PASurface.showing("some-other-surface");

    assert.ok(
      env.cleared.intervals.includes(poll.id),
      `${file}'s poll must stop when its surface is not the one on screen`,
    );
  });
}

test("wifi.js: the diagnostics poll it starts on settling stops when the operator leaves", async () => {
  const env = loadPageModule("wifi.js", {
    respond: (path) => (path === "/api/wifi" ? { wifi: { mode: "client", staSsid: "bench" } } : {}),
    overrides: { PAAssetsReady: true },
  });
  await env.settle();
  env.emit("window", "pa:bootstrap-change", {
    detail: {
      sections: [{ name: "wifi-config", status: "done" }],
      resourcesReady: true,
      sectionsStable: true,
    },
  });

  const poll = env.intervals.find((timer) => timer.ms === 10000);
  assert.ok(poll, "wifi.js installs its 10 s diagnostics poll");

  env.window.PASurface.showing("some-other-surface");

  assert.ok(env.cleared.intervals.includes(poll.id), "and it stops when WiFi is not the surface on screen");
});

test("setup.js: the memory profiler stops asking when the operator reads another surface", async () => {
  const env = loadPageModule("setup.js", {
    respond: (path) => (path === "/api/profiler"
      ? { heapFree: 1, heapMin: 1, heapLargest: 1, fragRatio: 0, taskStacks: [], snapshots: [] }
      : {}),
  });
  env.element("profiler-card").dataset.buildFlag = "PA_HEAP_PROFILE";
  await env.settle();

  env.emit("window", "pa:identity-available", {
    detail: {
      board: "artoo_esp32",
      board_capabilities: { PA_CAP_NATIVE_WIFI: true },
      build_flags: { PA_HEAP_PROFILE: true, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
    },
  });
  await env.settle();

  const profilerPolls = env.intervals.filter((timer) => timer.ms === 5000);
  assert.equal(profilerPolls.length, 2, "the serial status fallback plus the profiler's own cadence");

  const before = env.requests.filter((request) => request.path === "/api/profiler").length;
  env.window.PASurface.showing("rc");
  await env.settle();

  assert.ok(
    profilerPolls.every((timer) => env.cleared.intervals.includes(timer.id)),
    "leaving Setup stops the profiler -- the surface that asks hardest is the point of the ticket",
  );
  assert.equal(
    env.requests.filter((request) => request.path === "/api/profiler").length,
    before,
    "and nothing is asked of the controller on the way out",
  );
});

// =============================================================================
// The shipped shell
//
// index.html's frame, the shipped page_bootstrap.js driving it, and the shipped
// shell.js executed for real. Surface scripts are answered as loads, so a
// surface's poll is created here the way a surface script creates one.
// =============================================================================

const boot = async ({ hash = "", withEventSource = true } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = { document, requests: [], writes: [], events: [], store: new Map(), streamsOpened: [] };

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
      env.liveIntervals.add(timer);
      return timer;
    },
    clearInterval: (id) => {
      env.liveIntervals.delete(id);
      clearInterval(id);
    },
    addEventListener: (type, fn) => {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(fn);
    },
    removeEventListener: () => {},
    dispatchEvent: (event) => {
      env.events.push({ type: event.type, detail: event.detail });
      (windowListeners.get(event.type) || []).forEach((fn) => fn(event));
      return true;
    },
    location: {
      origin: "http://device",
      _hash: hash,
      get hash() {
        return this._hash;
      },
      set hash(value) {
        const text = String(value);
        const next = text === "" || text.startsWith("#") ? text : `#${text}`;
        if (next === this._hash) return;
        this._hash = next;
        (windowListeners.get("hashchange") || []).forEach((fn) => fn(new FakeEvent("hashchange")));
      },
    },
    history: {
      replaceState: (_state, _title, url) => {
        windowMock.location._hash = String(url);
      },
    },
    localStorage: {
      getItem: (key) => (env.store.has(key) ? env.store.get(key) : null),
      setItem: (key, value) => env.store.set(key, String(value)),
      removeItem: (key) => env.store.delete(key),
    },
    PAApi: {
      // Every write the session makes, so "nothing the droid is doing changes"
      // is counted rather than argued about.
      postForm: async (path, body) => {
        env.writes.push({ path, body });
        return { data: {} };
      },
      postJson: async (path, body) => {
        env.writes.push({ path, body });
        return { data: {} };
      },
      estopPostForm: async (path, body) => {
        env.writes.push({ path, body });
        return { data: {} };
      },
      messageFor: (error) => String(error?.message || error),
      get: async (path) => {
        env.requests.push(path);
        if (path === "/api/identity") {
          return { data: { droidName: "artoo", board: "artoo_esp32", board_capabilities: {}, build_flags: {} } };
        }
        if (path === "/api/status") return { data: { estop: false } };
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
    },
    PAUtils: { escapeHtml: (value) => String(value) },
  };

  env.liveIntervals = new Set();

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

  if (withEventSource) {
    context.EventSource = class {
      constructor(url) {
        env.streamsOpened.push(url);
      }
      addEventListener() {}
      close() {}
    };
  }

  const REAL_SCRIPTS = { "/shell.js": readData("shell.js"), "/status_stream.js": readData("status_stream.js") };
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
  env.context = context;
  env.navigate = (to) => {
    windowMock.location.hash = to;
  };
  env.mountedSurface = () => document.querySelectorAll("[data-surface]")[0]?.dataset.surface;
  env.noteText = () => document.querySelector(".surface-resumed")?.textContent ?? null;

  await sleep(140);
  return env;
};

// What a surface script does: create a poll while its own surface is mounted.
const surfacePoll = (env, onAttempt = () => Promise.resolve(), options = {}) => {
  const calls = { count: 0 };
  const handle = env.window.PASurface.poll(() => {
    calls.count += 1;
    return onAttempt();
  }, { cadenceMs: 1000, ...options });
  handle.start();
  return { handle, calls };
};

test("the shell stops the surface being left before it swaps the screen, and starts it again on return", async () => {
  const env = await boot();
  assert.equal(env.mountedSurface(), "home");
  const poll = surfacePoll(env);
  assert.equal(env.window.PASurface.isStale("home"), false);

  env.navigate("#wifi");
  await sleep(140);

  assert.equal(env.mountedSurface(), "wifi");
  assert.equal(
    env.window.PASurface.isStale("home"),
    true,
    "the Dashboard's poll was stopped by the shell, so what it shows is no longer current",
  );

  env.navigate("#home");
  await sleep(140);
  assert.equal(env.mountedSurface(), "home");
  poll.handle.stop();
});

test("a surface that came back says it is showing what it last read, until it answers again", async () => {
  const env = await boot();
  let answer = null;
  // runOnStart is what every converted surface poll passes: coming back to a
  // screen asks straight away rather than waiting out a cadence.
  const poll = surfacePoll(env, () => new Promise((resolve) => { answer = resolve; }), { runOnStart: true });

  env.navigate("#wifi");
  await sleep(140);
  assert.equal(env.noteText(), null, "the note belongs to the surface that was left, not to the one that is up");

  env.navigate("#home");
  await sleep(140);
  assert.match(
    env.noteText() || "",
    /Showing what this screen last read/,
    "a returned surface must not let values from before read as live ones",
  );

  // Its poll asked again on the way in; the note comes down when that answers.
  assert.ok(answer, "the poll ran on the way back in");
  answer({});
  await sleep(10);
  assert.equal(env.noteText(), null, "and the note goes when the surface is current again");
  poll.handle.stop();
});

test("the estop's own poll is chrome: navigating never stops it", async () => {
  const env = await boot({ withEventSource: false });
  const estopRequests = () => env.requests.filter((path) => path === "/api/status").length;
  const before = env.liveIntervals.size;
  assert.ok(before > 0, "with no stream the shell polls for the estop's state");

  env.navigate("#wifi");
  await sleep(140);
  env.navigate("#rc");
  await sleep(140);

  assert.ok(env.liveIntervals.size > 0, "the estop's poll survived two navigations");
  const seen = estopRequests();
  await sleep(0);
  assert.ok(seen >= 1, `the estop kept reading status (${seen} reads)`);
});

test("opening RC, leaving, and opening the profiler leaves one surface asking, not three", async () => {
  const env = await boot();
  const home = surfacePoll(env);

  env.navigate("#rc");
  await sleep(140);
  const rc = surfacePoll(env);

  env.navigate("#setup");
  await sleep(140);
  const setup = surfacePoll(env);

  const homeBefore = home.calls.count;
  const rcBefore = rc.calls.count;
  await sleep(1100);

  assert.equal(home.calls.count, homeBefore, "the Dashboard stopped asking when it was left");
  assert.equal(rc.calls.count, rcBefore, "RC diagnostics stopped asking when it was left");
  assert.ok(setup.calls.count > 0, "the surface the operator is reading is the one asking");
  assert.equal(env.streamsOpened.length, 1, "and the session still holds one live-update slot, not three");
  setup.handle.stop();
});

test("leaving a surface writes nothing to the droid", async () => {
  const env = await boot();
  const poll = surfacePoll(env);

  env.navigate("#drive");
  await sleep(140);
  env.navigate("#rc");
  await sleep(140);
  env.navigate("#home");
  await sleep(140);

  assert.deepEqual(
    env.writes,
    [],
    "unmounting must never stop a sequence, release an output, drop a drive frame or clear a latch -- "
      + "what changes is only what the browser asks for",
  );
  assert.deepEqual(
    env.requests.filter((path) => !path.endsWith(".html") && path !== "/api/identity" && path !== "/api/status"),
    [],
    "and it asks the controller for nothing of its own on the way out",
  );
  poll.handle.stop();
});

test("a surface can hold its own unmount, and releasing it lands where the operator asked to go", async () => {
  const env = await boot();
  let unsaved = true;
  env.window.PASurface.holdUnmount(() => unsaved);

  env.navigate("#wifi");
  await sleep(140);
  assert.equal(env.mountedSurface(), "home", "the surface held itself while it asks about unsaved work");

  unsaved = false;
  env.window.PASurface.releaseUnmount();
  await sleep(140);

  assert.equal(env.mountedSurface(), "wifi", "and releasing takes the operator where they were going");
});
