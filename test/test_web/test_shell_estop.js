// =============================================================================
// test/test_web/test_shell_estop.js
//
// The Latching Estop on the Operator Shell (#359, ADR 0048).
//
// Part 1 runs the shipped page_bootstrap.js + shell.js + status_stream.js
// against the shipped data/index.html in a real node tree, and mounts the
// shipped surface documents through a recording transport -- so "the estop is
// still there, still says the same thing, and nobody asked the droid to clear
// it" is observed on the live document rather than reasoned about.
//
// Part 2 runs the shipped app.js and drive.js on their own, to check the half
// of the decision that is deliberately NOT on the chrome: releasing a latched
// estop stays on Dashboard and Drive.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";
import { loadPageModule } from "./helpers/page_module_env.js";
import { statusFrame } from "./helpers/fake_droid.js";

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

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Boots the shell the way test_operator_shell.js does, plus the two things
// an estop needs and a router does not: a /api/status answer, and a transport
// that records POSTs and can be made to refuse one.
const boot = async ({ estop = false, statusHangs = false, statusFails = false, estopPostFails = false } = {}) => {
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
    requests: [],   // every GET path
    posts: [],      // { path, via } -- via names which PAApi method carried it
    status: statusFrame({ estop, webControlEnabled: true }),
    statusHangs,
    statusFails,
    estopPostFails,
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
    localStorage: {
      getItem: () => null,
      setItem: () => {},
      removeItem: () => {},
    },
    PAApi: {
      get: async (path) => {
        env.requests.push(path);
        if (path === "/api/identity") return { data: IDENTITY };
        if (path === "/api/status") {
          // Never settles: the control has to say something while the first
          // answer is still outstanding, and a timer would hold the process.
          if (env.statusHangs) await new Promise(() => {});
          if (env.statusFails) throw new Error("status unavailable");
          return { data: { ...env.status } };
        }
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      estopPostForm: async (path) => {
        env.posts.push({ path, via: "estopPostForm" });
        if (env.estopPostFails) throw new Error("no route to device");
        if (path === "/api/estop") env.status = { ...env.status, estop: true };
        if (path === "/api/estop/clear") env.status = { ...env.status, estop: false };
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

  env.streamsOpened = [];
  context.EventSource = class {
    constructor(url) {
      env.streamsOpened.push(url);
      this.url = url;
    }
    addEventListener() {}
    close() {}
  };

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
  env.estopButton = () => document.getElementById("shell-estop-button");
  env.estopStateText = () => document.getElementById("shell-estop-state")?.textContent;
  env.estopFeedbackText = () => document.getElementById("shell-estop-feedback")?.textContent;
  env.press = () => env.estopButton().fire("click", { type: "click" });

  await sleep(160);
  return env;
};

// ---------------------------------------------------------------------------
// Part 1: the control on the chrome
// ---------------------------------------------------------------------------

test("the estop is on every surface, and it is one control rather than one per surface", async () => {
  const env = await boot();
  const button = env.estopButton();
  assert.ok(button, "the shell renders it");

  for (const route of ["drive", "dome", "sound", "servo", "seq", "rc", "configuration", "maintenance", "wifi", "firmware", "home"]) {
    env.navigate(`#${route}`);
    await sleep(180);
    assert.equal(env.document.body.dataset.page, route, `${route} is the mounted surface`);
    assert.strictEqual(
      env.estopButton(),
      button,
      `${route}: the same node, so the handler bound to it at boot is still the one on screen`,
    );
  }

  // The two page-level copies this slice replaced. Their ids are what the page
  // scripts bound to, so their absence is what proves the copies are gone
  // rather than merely hidden.
  env.navigate("#drive");
  await sleep(180);
  assert.equal(env.document.getElementById("estop-button"), null, "Drive no longer carries its own latch button");
  env.navigate("#home");
  await sleep(180);
  assert.equal(env.document.getElementById("estop-toggle"), null, "the Dashboard no longer carries its own estop toggle");
});

test("the estop shows a value in both states, and says so before the droid has answered", async () => {
  const waiting = await boot({ statusHangs: true });
  assert.equal(
    waiting.estopStateText(),
    "Estop: finding out",
    "nothing has arrived yet, and a blank control cannot be told from one that stopped updating",
  );

  const clear = await boot({ estop: false });
  assert.equal(clear.estopStateText(), "Estop: clear");

  const latched = await boot({ estop: true });
  assert.equal(latched.estopStateText(), "Estop: latched");
});

test("navigating never clears a latched estop", async () => {
  const env = await boot({ estop: true });
  assert.equal(env.estopStateText(), "Estop: latched");

  for (const route of ["#seq", "#dome", "#rc", "#home", "#drive", "#sound"]) {
    env.navigate(route);
    await sleep(180);
    assert.equal(env.estopStateText(), "Estop: latched", `${route}: the latch is still reported`);
  }

  assert.deepEqual(
    env.posts.filter((post) => post.path === "/api/estop/clear"),
    [],
    "and nothing asked the droid to release it -- moving where a control is rendered must not change what it does",
  );
});

test("the estop's liveness follows the droid, not the surface that happens to be mounted", async () => {
  const env = await boot({ estop: false });
  env.navigate("#sound");
  await sleep(180);
  assert.equal(env.estopStateText(), "Estop: clear");

  // A status arriving on the shared stream while a surface that knows nothing
  // about the estop is the one on screen.
  env.window.PAStatusStream.seed(statusFrame({ estop: true }));
  assert.equal(env.estopStateText(), "Estop: latched", "the control repaints wherever the operator is");
});

test("pressing it asks the droid to stop, off the request queue, and says the stop went", async () => {
  const env = await boot({ estop: false });
  env.press();
  await sleep(60);

  assert.deepEqual(
    env.posts.map((post) => post.path),
    ["/api/estop"],
    "one latch request and nothing else",
  );
  assert.equal(
    env.posts[0].via,
    "estopPostForm",
    "carried by the estop path, which skips the request slot and is never retried",
  );
  assert.equal(env.estopFeedbackText(), "Stop sent");
  assert.equal(env.estopStateText(), "Estop: latched", "and the control reports what the droid now says");
});

test("a stop that did not reach the droid says so, and does not claim the droid stopped", async () => {
  const env = await boot({ estop: false, estopPostFails: true });
  env.press();
  await sleep(60);

  assert.match(env.estopFeedbackText(), /^Stop failed: /, "the failure is reported");
  assert.match(env.estopFeedbackText(), /press again/, "and it names the next move");
  assert.equal(
    env.estopStateText(),
    "Estop: clear",
    "the state line still reports the last thing the droid said, which is not a claim about the press",
  );
});

test("a second press goes out rather than being swallowed while the first is in flight", async () => {
  const env = await boot({ estop: false });
  env.press();
  env.press();
  await sleep(60);

  assert.equal(
    env.posts.filter((post) => post.path === "/api/estop").length,
    2,
    "an operator pressing again because nothing visibly happened must reach the droid",
  );
});

test("the session reads status once at boot, and hands it to the stream so nobody reads it twice", async () => {
  const env = await boot({ estop: true });

  assert.equal(
    env.requests.filter((path) => path === "/api/status").length,
    1,
    "one read for the session -- the device pushes on a change and tells a fresh client nothing",
  );
  assert.deepEqual(
    env.window.PAStatusStream.getLastStatus(),
    statusFrame({ estop: true, webControlEnabled: true }),
    "and it is the stream that holds it afterwards, so a later consumer is answered rather than fetching again",
  );
});

test("a status read that fails leaves the control saying it does not know, rather than saying clear", async () => {
  const env = await boot({ statusFails: true });
  assert.equal(env.estopStateText(), "Estop: finding out");
  assert.equal(env.window.PAStatusStream.getLastStatus(), null);
});

// ---------------------------------------------------------------------------
// Part 2: the half that is deliberately NOT on the chrome
// ---------------------------------------------------------------------------

// PAApi is replaced wholesale rather than the shared helper growing a method
// for this file, so the transport here is local and every call is recorded
// with the method that carried it.
const apiFor = (calls, status) => ({
  get: async (path) => {
    calls.push({ path, via: "get" });
    return { data: { ...status } };
  },
  postForm: async (path) => {
    calls.push({ path, via: "postForm" });
    return { data: {} };
  },
  estopPostForm: async (path) => {
    calls.push({ path, via: "estopPostForm" });
    return { data: { ok: true } };
  },
  messageFor: (error) => error?.message || "Request failed",
  gateControls: () => {},
});

// The shipped stream module, run on its own, so a test that is about who reads
// a seeded frame first is not written against a hand-made model of the reader.
// EventSource is stubbed inert: isSupported() asks whether the browser has the
// constructor, and the Dashboard takes the polling path when it does not -- the
// path where this defect cannot happen.
const loadStatusStream = () => {
  const context = {
    window: {
      addEventListener: () => {},
      setTimeout: () => 0,
      clearTimeout: () => {},
    },
    document: { visibilityState: "visible", addEventListener: () => {} },
    EventSource: class {
      constructor(url) {
        this.url = url;
      }
      close() {}
    },
    Math,
    JSON,
    console: { warn: () => {} },
  };
  context.globalThis = context;
  vm.createContext(context);
  vm.runInContext(statusStreamSrc, context);
  return context.window.PAStatusStream;
};

test("the Dashboard's Clear is live when the session's status already says latched", async () => {
  // The Operator Shell does one /api/status read at boot and hands it to the
  // stream (ADR 0048). A droid that is already stopped when the operator opens
  // the Dashboard is that read's answer.
  const stream = loadStatusStream();
  stream.seed(statusFrame({ estop: true }));

  const calls = [];
  const env = loadPageModule("app.js", {
    overrides: { PAApi: apiFor(calls, statusFrame({ estop: true })), PAStatusStream: stream },
  });
  await env.settle();

  assert.equal(
    env.element("estop-clear").disabled,
    false,
    "the droid is latched and this is one of the two screens that can release it",
  );

  // And nothing was going to repair it later: the Dashboard reads no status of
  // its own, and the droid pushes one only when something changes -- which a
  // stopped droid nobody is touching never does.
  assert.deepEqual(
    calls.filter((call) => call.path === "/api/status"),
    [],
    "the seeded frame is the only status this session gets until something moves",
  );
});

test("the Dashboard keeps the release, and its button is dead while there is no latch to release", async () => {
  const calls = [];
  const status = statusFrame();
  const env = loadPageModule("app.js", { overrides: { PAApi: apiFor(calls, status) } });
  const button = env.element("estop-clear");

  assert.throws(
    () => env.emitOn("estop-toggle", "click"),
    /registered no "click" listener/,
    "the Dashboard's own latch-or-release toggle is gone; latching is the shell's",
  );

  assert.equal(button.disabled, true, "there is nothing to release yet");
  env.emitOn("estop-clear", "click");
  await env.settle();
  assert.deepEqual(calls, [], "and pressing it asks the droid for nothing");

  // The droid says it is latched, in the answer to the session's status read.
  status.estop = true;
  await env.window.PALiveReading.read();
  await env.settle();
  assert.equal(button.disabled, false, "now there is something to release");

  calls.length = 0;
  env.emitOn("estop-clear", "click");
  await env.settle();
  assert.deepEqual(
    calls.filter((call) => call.path.startsWith("/api/estop")),
    [{ path: "/api/estop/clear", via: "estopPostForm" }],
    "one release, carried off the request queue, and never a latch",
  );
});

// ---------------------------------------------------------------------------
// Part 2b: one place decides a latch
//
// Wave 1 answered "is it latched" four different ways -- `=== true` on the
// plate, `!!payload.estop` in the chrome and on Foot Drive, a truthy read on
// the Dashboard -- and #346 could only publish the shell's answer beside them.
// The Live Reading decides it now, for the chrome and every surface (#419).
// ---------------------------------------------------------------------------

// Frames that tell `=== true` apart from truthiness: a field that arrived as
// something other than a boolean must not read as a latch, and a frame that
// never mentioned the estop is not a reading at all (#346).
const LATCH_FRAMES = [
  { frame: (() => { const frame = statusFrame(); delete frame.estop; return frame; })(), says: "Estop: finding out" },
  { frame: statusFrame({ estop: true }), says: "Estop: latched" },
  { frame: statusFrame({ estop: false }), says: "Estop: clear" },
  { frame: statusFrame({ estop: 1 }), says: "Estop: clear" },
  { frame: statusFrame({ estop: "true" }), says: "Estop: clear" },
  { frame: statusFrame({ estop: null }), says: "Estop: clear" },
];

test("the chrome reads a latch only from a whole frame, and only from `true`", async () => {
  const env = await boot({ statusHangs: true });

  for (const { frame, says } of LATCH_FRAMES) {
    env.window.PAStatusStream.seed(frame);
    assert.equal(env.estopStateText(), says, `${JSON.stringify(frame)} must read "${says}"`);
  }
});

test("Foot Drive and the Dashboard follow the Live Reading, not the field", async () => {
  // The frame the droid last sent says LATCHED, and then the droid stops
  // answering. A reader deciding from the field would keep offering Clear; the
  // Live Reading says nobody knows the estop any more, so neither may.
  const latched = statusFrame({ estop: true });
  for (const [file, button] of [["app.js", "estop-clear"], ["drive.js", "clear-estop-button"]]) {
    let answering = true;
    const env = loadPageModule(file, {
      respond: (path) => {
        if (path === "/api/status" && !answering) throw new Error("no response from the droid");
        return { data: path === "/api/status" ? latched : {} };
      },
    });
    await env.window.PALiveReading.read();
    await env.settle();
    assert.equal(env.element(button).disabled, false, `${file}: a heard latch offers its release`);

    answering = false;
    env.intervals.forEach((timer) => timer.fn());
    await env.settle();
    assert.equal(
      env.element(button).disabled,
      true,
      `${file}: once contact is lost the latch is not known, whatever the last frame said`,
    );
  }
});

// ---------------------------------------------------------------------------
// Part 3: what may paint over it
// ---------------------------------------------------------------------------

// The stylesheet read the way a browser stacks it: rules flattened, the last
// declaration for a property winning, so this asserts the painted order rather
// than the presence of a string.
const zIndexOf = (selector) => {
  const css = readData("style.css").replace(/\/\*[\s\S]*?\*\//g, "");
  let value = null;
  const rule = /([^{}]+)\{([^{}]*)\}/g;
  let match;
  while ((match = rule.exec(css)) !== null) {
    const selectors = match[1].split(",").map((part) => part.trim());
    if (!selectors.includes(selector)) continue;
    const declared = /(?:^|;)\s*z-index\s*:\s*([^;]+)/.exec(match[2]);
    if (declared) value = Number(declared[1].trim());
  }
  return value;
};

test("the sleep overlay does not paint over the one control it says stays active", () => {
  const estop = zIndexOf(".shell-estop");
  const overlay = zIndexOf(".sleep-overlay");

  assert.ok(Number.isFinite(overlay), "the sleep overlay stacks explicitly");
  assert.ok(Number.isFinite(estop), "and so does the estop, or it is painted over by whatever comes last");
  assert.ok(
    estop > overlay,
    `the estop (${estop}) must stack above the sleep overlay (${overlay}) -- the overlay is a full-viewport`
      + " scrim that takes pointer events, and its own panel says drive and safety controls remain active",
  );
});

test("Drive keeps the release, sends it off the request queue, and no longer latches", async () => {
  const calls = [];
  const env = loadPageModule("drive.js", { overrides: { PAApi: apiFor(calls, statusFrame({ estop: true })) } });

  assert.throws(
    () => env.emitOn("estop-button", "click"),
    /registered no "click" listener/,
    "the Drive surface no longer carries a latch of its own",
  );

  calls.length = 0;
  env.emitOn("clear-estop-button", "click");
  await env.settle();
  assert.deepEqual(
    calls,
    [{ path: "/api/estop/clear", via: "estopPostForm" }],
    "the release is still here, and still skips the request slot",
  );
});
