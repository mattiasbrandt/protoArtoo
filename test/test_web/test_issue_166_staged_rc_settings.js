// =============================================================================
// Issue #166: saved RC settings are staged until controller restart.
//
// These tests execute the shipped page modules. In particular, the RC page
// must not mistake newly saved component toggles for the boot-active receiver
// state exposed by /api/rc.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "node:fs";
import vm from "node:vm";

import { loadPageModule } from "./helpers/page_module_env.js";

const makeInteractiveElement = () => {
  const listeners = new Map();
  const element = {
    dataset: {},
    style: {},
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    textContent: "",
    innerHTML: "",
    value: "",
    checked: false,
    disabled: false,
    hidden: false,
    children: [],
    addEventListener(type, handler) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type).push(handler);
    },
    setAttribute() {},
    removeAttribute() {},
    querySelector: () => makeInteractiveElement(),
    querySelectorAll: () => [],
    appendChild() {},
    focus() {},
    click() {},
    closest: () => null,
    emit: async (type, event = {}) => {
      for (const handler of listeners.get(type) || []) {
        await handler({ target: element, preventDefault() {}, ...event });
      }
    },
  };
  return element;
};

// A small interaction-capable browser host for the save handlers. The shared
// page-module harness intentionally does not retain per-element listeners, so
// this local host keeps the issue-specific test independent of that contract.
const loadInteractiveModule = (file, respond) => {
  const elements = new Map();
  const timers = [];
  const modeCards = ["standard_pwm", "single_sbus", "dual_sbus"].map((mode) => {
    const card = makeInteractiveElement();
    card.dataset.mode = mode;
    return card;
  });
  const element = (id) => {
    if (!elements.has(id)) elements.set(id, makeInteractiveElement());
    return elements.get(id);
  };
  const call = async (method, path, body) => ({ data: await respond(method, path, body) });
  class ApiError extends Error {}

  const windowMock = {
    PAApi: {
      ApiError,
      get: (path) => call("GET", path),
      postForm: (path, body) => call("POST", path, body),
      postJson: (path, body) => call("POST", path, body),
      messageFor: (error) => error?.message || "Request failed",
    },
    PAUtils: {
      debounce: (fn) => fn,
      escapeHtml: (value) => String(value ?? ""),
      escapeAttr: (value) => String(value ?? ""),
    },
    PABootstrap: {
      registerSection() {},
      setResourceLabels() {},
    },
    PageBootstrap: {
      createBackgroundPoll: () => ({ start() {}, stop() {} }),
    },
    PAStatusStream: {
      isSupported: () => false,
      subscribe() {},
      getLastStatus: () => null,
    },
    PA_HEAP: {},
    setTimeout: (fn, ms) => {
      const id = timers.length + 1;
      timers.push({ id, fn, ms, cleared: false });
      return id;
    },
    clearTimeout: (id) => {
      const timer = timers.find((entry) => entry.id === id);
      if (timer) timer.cleared = true;
    },
    setInterval: () => 1,
    clearInterval() {},
    addEventListener() {},
    removeEventListener() {},
    dispatchEvent() {},
    requestAnimationFrame: () => 1,
    localStorage: { getItem: () => null, setItem() {} },
    location: { origin: "http://device", href: "http://device/" },
  };
  const documentMock = {
    visibilityState: "visible",
    body: makeInteractiveElement(),
    getElementById: element,
    querySelector: () => makeInteractiveElement(),
    querySelectorAll: (selector) => selector === ".rc-mode-card" ? modeCards : [],
    createElement: () => makeInteractiveElement(),
    createTextNode: () => makeInteractiveElement(),
    addEventListener() {},
  };
  const context = {
    window: windowMock,
    document: documentMock,
    navigator: { sendBeacon() {} },
    console: { log() {}, warn() {}, error() {}, info() {} },
    setTimeout: windowMock.setTimeout,
    clearTimeout: windowMock.clearTimeout,
    setInterval: windowMock.setInterval,
    clearInterval: windowMock.clearInterval,
    fetch: async () => ({ json: async () => ({}) }),
    confirm: () => true,
    Blob: class {},
    Event: class {},
    CustomEvent: class {},
    AbortController,
    URLSearchParams,
    JSON,
    Math,
    Date,
    Number,
    String,
    Boolean,
    Array,
    Object,
    Set,
    Map,
    Promise,
    Error,
    TypeError,
    RegExp,
    parseInt,
    parseFloat,
    isNaN,
  };
  context.globalThis = context;
  for (const key of ["PAApi", "PAUtils", "PABootstrap", "PAStatusStream"]) {
    context[key] = windowMock[key];
  }
  vm.runInNewContext(readFileSync(`data/${file}`, "utf8"), context, { filename: file });

  const settle = async () => {
    for (let turn = 0; turn < 4; turn += 1) {
      await new Promise((resolve) => setImmediate(resolve));
    }
  };
  return {
    element,
    modeCard: (mode) => modeCards.find((card) => card.dataset.mode === mode),
    settle,
    fireTimer: async (ms) => {
      const timer = timers.findLast((entry) => entry.ms === ms && !entry.cleared);
      assert.ok(timer, `expected an active ${ms}ms timer`);
      timer.cleared = true;
      await timer.fn();
      await settle();
    },
    startTimer: (ms) => {
      const timer = timers.findLast((entry) => entry.ms === ms && !entry.cleared);
      assert.ok(timer, `expected an active ${ms}ms timer`);
      timer.cleared = true;
      return timer.fn();
    },
  };
};

const SAVED_RC_DISABLED = {
  rc: { inputMode: "single_sbus", sbus: { recvCh2: false } },
  components: {
    rcCh1: { enabled: false },
    rcCh2: { enabled: false },
    rcCh3: { enabled: false },
    rcCh4: { enabled: false },
    rcCh5: { enabled: false },
    rcCh6: { enabled: false },
  },
};

const ACTIVE_RC_ENABLED = {
  mode: "single_sbus",
  sources: {
    sbus1: { enabled: true, linked: true, ageMs: 12 },
    sbus2: { enabled: false, linked: false, ageMs: 0 },
    pwm: { enabled: false, linked: false, ageMs: 0 },
  },
  raw: {},
  channels: [],
};

test("Setup scopes restart guidance to RC input channels", () => {
  const html = readFileSync("data/setup.html", "utf8");
  const rxSection = html.slice(html.indexOf("🎮 RX IN/OUT"), html.indexOf("🔌 SERIAL COMMS"));
  assert.match(rxSection, /RC input changes save immediately and apply after controller restart\./);
  assert.doesNotMatch(html, /Component changes save immediately\. Restart the controller to apply them\./);

  const env = loadPageModule("setup.js", { respond: () => ({}) });
  assert.equal(
    env.element("setup-save-summary").textContent,
    "💾 Auto-save ready",
    "the global summary must not claim every component save needs restart"
  );
});

test("inactive RC card explains boot-active source, mode, and routing semantics", () => {
  const html = readFileSync("data/rc.html", "utf8");
  const card = html.slice(html.indexOf('id="rc-disabled-card"'), html.indexOf("<div class=\"card\">", html.indexOf('id="rc-disabled-card"')));
  assert.match(card, /No RC source is active for the current receiver type and routing\./);
  assert.match(card, /Enable the applicable RC input[\s\S]*or select the intended receiver type and routing[\s\S]*then restart the controller/i);
  assert.match(card, /Mapping edits remain available\./);
  assert.doesNotMatch(card, /all RC input channels are disabled/i);
});

test("Receiver Type separates restart-staged settings from live mapping edits", () => {
  const html = readFileSync("data/rc.html", "utf8");
  assert.match(html, /Receiver type and single-SBUS routing save immediately and apply after controller restart\./);
  assert.match(html, /Channel mapping changes apply when saved\./);
});

test("non-RC component auto-save retains ordinary saved feedback", async () => {
  const config = { components: {}, system: {} };
  const env = loadInteractiveModule("setup.js", async (_method, path) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();

  const toggle = env.element("enable-arm1");
  toggle.checked = true;
  await toggle.emit("change");
  await env.fireTimer(300);

  assert.match(env.element("feature-feedback").textContent, /^Saved at /);
  assert.doesNotMatch(env.element("feature-feedback").textContent, /restart/i);
  assert.doesNotMatch(env.element("setup-save-summary").textContent, /restart/i);
});

test("RC component auto-save reports that controller restart is required", async () => {
  const config = { components: {}, system: {} };
  const env = loadInteractiveModule("setup.js", async (_method, path) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();

  const toggle = env.element("enable-rc-ch1");
  toggle.checked = true;
  await toggle.emit("change");
  await env.fireTimer(300);

  assert.match(env.element("feature-feedback").textContent, /Restart the controller to apply RC input changes\./);
  assert.match(env.element("setup-save-summary").textContent, /restart required/);
});

test("restart remains pending after a later non-RC component save", async () => {
  const config = { components: {}, system: {} };
  const env = loadInteractiveModule("setup.js", async (_method, path) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();

  const rcToggle = env.element("enable-rc-ch1");
  rcToggle.checked = true;
  await rcToggle.emit("change");
  await env.fireTimer(300);

  const nonRcToggle = env.element("enable-arm1");
  nonRcToggle.checked = true;
  await nonRcToggle.emit("change");
  await env.fireTimer(300);

  assert.match(env.element("feature-feedback").textContent, /Restart the controller to apply RC input changes\./);
  assert.match(env.element("setup-save-summary").textContent, /restart required/);
});

test("an RC change queued behind an in-flight save cannot lose the restart cue", async () => {
  const config = (rcCh1Enabled, arm1Enabled) => ({
    components: {
      rcCh1: { enabled: rcCh1Enabled },
      arm1: { enabled: arm1Enabled },
    },
    system: {},
  });
  let resolveFirstSave;
  let postCount = 0;
  const postedRcValues = [];
  const firstSaveResponse = new Promise((resolve) => { resolveFirstSave = resolve; });
  const env = loadInteractiveModule("setup.js", async (method, path, body) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (method === "POST" && path === "/api/config") {
      postCount += 1;
      postedRcValues.push(body.get("enableRcCh1"));
      if (postCount === 1) return firstSaveResponse;
      return config(true, true);
    }
    if (path === "/api/config") return config(false, false);
    return {};
  });
  await env.settle();

  const nonRcToggle = env.element("enable-arm1");
  nonRcToggle.checked = true;
  await nonRcToggle.emit("change");
  const firstSave = env.startTimer(300);
  await env.settle();

  const rcToggle = env.element("enable-rc-ch1");
  rcToggle.checked = true;
  await rcToggle.emit("change");
  await env.fireTimer(300);

  resolveFirstSave(config(false, true));
  await firstSave;
  await env.settle();

  assert.equal(postCount, 2, "the RC change must be persisted by the queued save");
  assert.deepEqual(
    postedRcValues,
    ["false", "true"],
    "the first response must not overwrite the newer RC toggle before the queued request is built"
  );
  assert.match(env.element("feature-feedback").textContent, /Restart the controller to apply RC input changes\./);
  assert.match(env.element("setup-save-summary").textContent, /restart required/);
});

test("receiver mode and single-SBUS routing saves report restart required", async () => {
  const env = loadInteractiveModule("rc.js", async (method, path, body) => {
    if (method === "POST" && path === "/api/config") {
      if (body?.rc?.sbus) return { rc: { inputMode: "dual_sbus", sbus: body.rc.sbus } };
      return { rc: { inputMode: body.rcInputMode, sbus: { recvCh2: false } } };
    }
    if (path === "/api/rc/map") return { mode: "dual_sbus", map: [] };
    return {};
  });

  await env.modeCard("dual_sbus").emit("click");
  await env.settle();
  assert.match(env.element("rc-mode-feedback").textContent, /Restart the controller to apply\./);

  const receiver = env.element("sbus-recv-sel");
  receiver.value = "true";
  await receiver.emit("change");
  assert.match(env.element("sbus-recv-feedback").textContent, /Restart the controller to apply\./);
});

test("boot-active RC diagnostics override staged disabled component settings", async () => {
  const env = loadPageModule("rc.js", {
    respond: (path) => {
      if (path === "/api/config") return SAVED_RC_DISABLED;
      if (path === "/api/rc/map") return { mode: "single_sbus", map: [] };
      if (path === "/api/rc") return ACTIVE_RC_ENABLED;
      return {};
    },
  });

  await env.runSection("rc-mode-mapping");
  assert.equal(
    env.element("rc-learn-btn").disabled,
    true,
    "the saved settings alone describe the staged, disabled state"
  );

  await env.runSection("rc-diagnostics");
  assert.equal(
    env.element("rc-learn-btn").disabled,
    false,
    "live tools must follow boot-active /api/rc sources until restart"
  );
});
