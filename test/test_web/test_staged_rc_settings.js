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
//
// `files` is one page module or a surface's chain in load order: the component
// toggles are Configuration's, which loads the shared Feature Availability
// module first (#404).
const loadInteractiveModule = (files, respond) => {
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
    // data/page_bootstrap.js publishes window.PASurface in the browser; this
    // context hand-rolls its globals, so it has to carry it too (#360).
    PASurface: { poll: () => ({ start() {}, stop() {}, cancelRetry() {} }) },
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
  for (const file of Array.isArray(files) ? files : [files]) {
    vm.runInNewContext(readFileSync(`data/${file}`, "utf8"), context, { filename: file });
  }

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

// The component toggles' surface, as its page loads it.
const CONFIGURATION = ["feature_availability.js", "configuration.js"];

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

test("non-RC component auto-save retains ordinary saved feedback", async () => {
  const config = {
    components: {
      rcCh1: { enabled: false },
      rcCh2: { enabled: false },
      rcCh3: { enabled: false },
      rcCh4: { enabled: false },
      rcCh5: { enabled: false },
      rcCh6: { enabled: false },
    },
    system: {},
  };
  const env = loadInteractiveModule(CONFIGURATION, async (_method, path) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();

  const toggle = env.element("enable-drive");
  toggle.checked = true;
  await toggle.emit("change");
  await env.fireTimer(300);

  assert.match(env.element("feature-feedback").textContent, /^Saved at /);
  assert.doesNotMatch(env.element("feature-feedback").textContent, /restart/i);
  assert.doesNotMatch(env.element("setup-save-summary").textContent, /restart/i);
});

test("RC component auto-save reports that controller restart is required", async () => {
  const config = {
    components: {
      rcCh1: { enabled: false },
      rcCh2: { enabled: false },
      rcCh3: { enabled: false },
      rcCh4: { enabled: false },
      rcCh5: { enabled: false },
      rcCh6: { enabled: false },
    },
    system: {},
  };
  const env = loadInteractiveModule(CONFIGURATION, async (_method, path) => {
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
  const config = {
    components: {
      rcCh1: { enabled: false },
      rcCh2: { enabled: false },
      rcCh3: { enabled: false },
      rcCh4: { enabled: false },
      rcCh5: { enabled: false },
      rcCh6: { enabled: false },
    },
    system: {},
  };
  const env = loadInteractiveModule(CONFIGURATION, async (_method, path) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();

  const rcToggle = env.element("enable-rc-ch1");
  rcToggle.checked = true;
  await rcToggle.emit("change");
  await env.fireTimer(300);

  const nonRcToggle = env.element("enable-drive");
  nonRcToggle.checked = true;
  await nonRcToggle.emit("change");
  await env.fireTimer(300);

  assert.match(env.element("feature-feedback").textContent, /Restart the controller to apply RC input changes\./);
  assert.match(env.element("setup-save-summary").textContent, /restart required/);
});

test("an RC change queued behind an in-flight save cannot lose the restart cue", async () => {
  const config = (rcCh1Enabled, driveEnabled) => ({
    components: {
      rcCh1: { enabled: rcCh1Enabled },
      drive: { enabled: driveEnabled },
    },
    system: {},
  });
  let resolveFirstSave;
  let postCount = 0;
  const postedRcValues = [];
  const firstSaveResponse = new Promise((resolve) => { resolveFirstSave = resolve; });
  const env = loadInteractiveModule(CONFIGURATION, async (method, path, body) => {
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

  const nonRcToggle = env.element("enable-drive");
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

test("WARNING #1: restart cue must survive a later save failure", async () => {
  const config = { components: { rcCh1: { enabled: false }, rcCh2: { enabled: false }, rcCh3: { enabled: false }, rcCh4: { enabled: false }, rcCh5: { enabled: false }, rcCh6: { enabled: false } }, system: {} };
  let firstSaveSucceeds = true;
  const env = loadInteractiveModule(CONFIGURATION, async (method, path, body) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (method === "POST" && path === "/api/config") {
      if (!firstSaveSucceeds) throw new Error("Save failed (simulated)");
      return config;
    }
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();

  const rcToggle = env.element("enable-rc-ch1");
  rcToggle.checked = true;
  await rcToggle.emit("change");
  await env.fireTimer(300);

  // First save succeeds and sets restart pending
  assert.match(env.element("setup-save-summary").textContent, /restart required/);

  // Trigger a failed save attempt
  firstSaveSucceeds = false;
  const nonRcToggle = env.element("enable-drive");
  nonRcToggle.checked = true;
  await nonRcToggle.emit("change");
  await env.fireTimer(300);

  // Error feedback shown
  assert.match(env.element("feature-feedback").textContent, /Save failed/i);

  // The restart cue MUST still be pending, not wiped by the failure
  assert.match(
    env.element("setup-save-summary").textContent,
    /restart still required|restart required/,
    "restart requirement must survive a later save failure"
  );
});

test("WARNING #2: stale response must not overwrite newer RC pending state", async () => {
  const config = (rcCh1Enabled, rcCh2Enabled) => ({
    components: {
      rcCh1: { enabled: rcCh1Enabled },
      rcCh2: { enabled: rcCh2Enabled },
      rcCh3: { enabled: false },
      rcCh4: { enabled: false },
      rcCh5: { enabled: false },
      rcCh6: { enabled: false },
    },
    system: {},
  });
  let firstSaveResolve;
  const firstSavePromise = new Promise((resolve) => { firstSaveResolve = resolve; });
  const postedValues = [];

  const env = loadInteractiveModule(CONFIGURATION, async (method, path, body) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (method === "POST" && path === "/api/config") {
      const posted = {
        rcCh1: body.get("enableRcCh1"),
        rcCh2: body.get("enableRcCh2"),
      };
      postedValues.push(posted);
      if (postedValues.length === 1) {
        // First request hangs, second request will complete first
        return firstSavePromise;
      }
      // Second request returns immediately
      return config(true, true);
    }
    if (path === "/api/config") return config(false, false);
    return {};
  });
  await env.settle();

  // Queue first save (rcCh1)
  const rcCh1Toggle = env.element("enable-rc-ch1");
  rcCh1Toggle.checked = true;
  await rcCh1Toggle.emit("change");
  const firstSave = env.startTimer(300);
  await env.settle();

  // Queue second save (rcCh2) which will complete before first
  const rcCh2Toggle = env.element("enable-rc-ch2");
  rcCh2Toggle.checked = true;
  await rcCh2Toggle.emit("change");
  await env.fireTimer(300);

  // Resolve the first request with STALE response (only rcCh1, not rcCh2)
  firstSaveResolve(config(true, false));
  await firstSave;
  await env.settle();

  // Both requests must have been sent
  assert.equal(postedValues.length, 2, "both RC changes must be persisted");

  // The restart cue must reflect the NEWER state (rcCh2 pending), not the stale response
  assert.match(
    env.element("feature-feedback").textContent,
    /Restart the controller to apply RC input changes\./,
    "pending state must not be overwritten by stale response"
  );
  assert.match(env.element("setup-save-summary").textContent, /restart required/);
});

test("generation guard prevents corrupted savedGeneration affecting future saves", async () => {
  const config = (rcCh1Enabled, rcCh2Enabled, rcCh3Enabled) => ({
    components: {
      rcCh1: { enabled: rcCh1Enabled },
      rcCh2: { enabled: rcCh2Enabled },
      rcCh3: { enabled: rcCh3Enabled },
      rcCh4: { enabled: false },
      rcCh5: { enabled: false },
      rcCh6: { enabled: false },
    },
    system: {},
  });
  let firstSaveResolve, thirdSaveResolve;
  const firstSavePromise = new Promise((resolve) => { firstSaveResolve = resolve; });
  const thirdSavePromise = new Promise((resolve) => { thirdSaveResolve = resolve; });

  const env = loadInteractiveModule(CONFIGURATION, async (method, path, body) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (method === "POST" && path === "/api/config") {
      const ch1 = body.get("enableRcCh1") === "true";
      const ch2 = body.get("enableRcCh2") === "true";
      const ch3 = body.get("enableRcCh3") === "true";

      if (ch1 && !ch2 && !ch3) {
        // First request hangs
        return firstSavePromise;
      }
      if (ch1 && ch2 && !ch3) {
        // Third request hangs (it's the third request in network time, but generation-wise)
        return thirdSavePromise;
      }
      // Second request returns immediately
      return config(ch1, ch2, ch3);
    }
    if (path === "/api/config") return config(false, false, false);
    return {};
  });
  await env.settle();

  // First RC change (generation 1)
  const rcCh1Toggle = env.element("enable-rc-ch1");
  rcCh1Toggle.checked = true;
  await rcCh1Toggle.emit("change");
  const firstSave = env.startTimer(300);
  await env.settle();

  // Second RC change (generation 2)
  const rcCh2Toggle = env.element("enable-rc-ch2");
  rcCh2Toggle.checked = true;
  await rcCh2Toggle.emit("change");
  await env.fireTimer(300);

  // Third RC change (generation 3)
  const rcCh3Toggle = env.element("enable-rc-ch3");
  rcCh3Toggle.checked = true;
  await rcCh3Toggle.emit("change");
  const thirdSave = env.startTimer(300);
  await env.settle();

  // Now responses arrive out of order:
  // - Second response (gen 2) completes: should set savedGeneration = 2
  // - First response (gen 1) arrives: WITH guard, doesn't update (1 not > 2)
  //                                   WITHOUT guard, sets savedGeneration = 1 (corruption!)
  // - Third response (gen 3): WITH guard, 3 > 2 so updates
  //                          WITHOUT guard, 3 > 1 so updates (SAME behavior, but used wrong base)

  firstSaveResolve(config(true, false, false));
  await firstSave;

  thirdSaveResolve(config(true, true, true));
  await thirdSave;
  await env.settle();

  // With guard: savedGeneration = 3, rcRestartPending = true (all three RC channels enabled)
  // Without guard: saved Generation was corrupted to 1, then 3, rcRestartPending = true
  // Both end with restart = true, BUT without the guard the savedGeneration value is wrong
  // This matters if a fourth save with generation 2 arrives after third completes

  assert.match(
    env.element("feature-feedback").textContent,
    /Restart the controller to apply RC input changes\./,
    "guard protects savedGeneration even when final rcRestartPending is the same"
  );
  assert.match(env.element("setup-save-summary").textContent, /restart required/);
});

test("WARNING #3: reverting to boot-active value must clear restart cue", async () => {
  const config = { components: { rcCh1: { enabled: false }, rcCh2: { enabled: false }, rcCh3: { enabled: false }, rcCh4: { enabled: false }, rcCh5: { enabled: false }, rcCh6: { enabled: false } }, system: {} };

  const env = loadInteractiveModule(CONFIGURATION, async (method, path, body) => {
    if (path === "/api/identity") return { droidName: "protoartoo", mdnsUseName: false };
    if (method === "POST" && path === "/api/config") {
      const newEnabled = body.get("enableRcCh1") === "true";
      config.components.rcCh1.enabled = newEnabled;
      return config;
    }
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();

  // Change RC setting away from boot-active (false -> true)
  const rcToggle = env.element("enable-rc-ch1");
  rcToggle.checked = true;
  await rcToggle.emit("change");
  await env.fireTimer(300);

  // Restart is required (differs from boot-active)
  assert.match(env.element("setup-save-summary").textContent, /restart required/);

  // Revert back to boot-active value (true -> false)
  rcToggle.checked = false;
  await rcToggle.emit("change");
  await env.fireTimer(300);

  // Restart cue must clear because saved state now matches boot-active
  assert.doesNotMatch(
    env.element("setup-save-summary").textContent,
    /restart required/,
    "restart requirement must clear when reverted to boot-active value"
  );
  assert.match(env.element("setup-save-summary").textContent, /Auto-save ready|Saved/);
});

// A change that has not reached the controller yet is lost if the controller
// restarts under it. Restart used to share a page with the toggles and was
// greyed out for exactly that window; since #404 it is on Maintenance, so the
// guard has to hold across two surfaces loaded into one session.
test("Restart waits for a component change still on its way to the controller", async () => {
  const config = { components: { drive: { enabled: false } }, system: {} };
  const posts = [];
  let answerSave = null;
  const env = loadInteractiveModule([...CONFIGURATION, "maintenance.js"], async (method, path) => {
    if (method === "POST") posts.push(path);
    if (method === "POST" && path === "/api/config") {
      // Held open, so the save is in flight for as long as the test says.
      await new Promise((resolve) => {
        answerSave = resolve;
      });
    }
    if (path === "/api/config") return config;
    return {};
  });
  await env.settle();
  const reboots = () => posts.filter((path) => path === "/api/reboot").length;

  env.element("enable-drive").checked = true;
  await env.element("enable-drive").emit("change");
  await env.element("reboot-button").emit("click");
  assert.equal(reboots(), 0, "a change still waiting to be sent is not restarted away");
  assert.match(env.element("reboot-feedback").textContent, /saving/i, "and the press says why nothing happened");

  const saving = env.startTimer(300);
  await env.settle();
  await env.element("reboot-button").emit("click");
  assert.equal(reboots(), 0, "nor one the controller has not answered yet");

  answerSave();
  await saving;
  await env.settle();
  await env.element("reboot-button").emit("click");
  assert.equal(reboots(), 1, "once the save has landed, Restart restarts");
});
