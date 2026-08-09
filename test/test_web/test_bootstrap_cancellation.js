// =============================================================================
// test/test_web/test_bootstrap_cancellation.js
//
// Integration tests for section-run cancellation ownership: the SHIPPED
// page_bootstrap.js (reducer + browser host) and the SHIPPED web_api.js run
// together in one vm context, with real timers, node's real AbortController,
// and a fetch mock that honors AbortSignal. The bootstrap's deadline expiry
// drives a genuine cancelActive() -> controller.abort() sequence, so these
// tests observe which request actually gets aborted - not a reimplementation
// of the wiring.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapFile = readFileSync(join(__dirname, "../../data/page_bootstrap.js"), "utf-8");
const webApiSrc = readFileSync(join(__dirname, "../../data/web_api.js"), "utf-8");

// PART 1 (reducer) and PART 3 (browser host) of the shipped file. PART 2 is
// the recovery view; the host reaches it through optional chaining, so it can
// be absent here.
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Builds a context in which the host believes it is a page with no resource
// chain, executes web_api.js + reducer + host, and returns handles to drive it.
const makeEnv = () => {
  const env = {
    inFlight: 0,
    maxInFlight: 0,
    dispatches: [],
    aborted: [],
    hangMsFor: {},
  };

  const fetchMock = (path, opts = {}) =>
    new Promise((resolve, reject) => {
      env.dispatches.push(path);
      env.inFlight += 1;
      if (env.inFlight > env.maxInFlight) env.maxInFlight = env.inFlight;
      let settled = false;
      const finish = (fn, arg) => {
        if (settled) return;
        settled = true;
        env.inFlight -= 1;
        fn(arg);
      };
      const timer = setTimeout(() => {
        finish(resolve, {
          ok: true,
          status: 200,
          headers: { get: () => "application/json" },
          json: async () => ({ ok: true, path }),
          text: async () => JSON.stringify({ ok: true, path }),
        });
      }, env.hangMsFor[path] ?? 60);
      // The bootstrap retries a failing section indefinitely; unref so the
      // never-finishing mock responses cannot hold the test process open.
      timer.unref?.();
      opts.signal?.addEventListener("abort", () => {
        clearTimeout(timer);
        env.aborted.push(path);
        const abortError = new Error("The operation was aborted.");
        abortError.name = "AbortError";
        finish(reject, abortError);
      });
    });

  const windowListeners = new Map();
  // Timers scheduled by the code under test are unref'd: the bootstrap clock
  // keeps rescheduling itself while a section is failed-retrying, which would
  // otherwise keep the node:test process alive forever after the tests end.
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
    dispatchEvent: () => true,
    location: { origin: "http://device" },
  };

  const documentMock = {
    currentScript: { dataset: { scripts: "" } },
    readyState: "loading",
    hidden: false,
    visibilityState: "visible",
    addEventListener: () => {},
    querySelectorAll: () => [],
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
    document: documentMock,
    console: { warn: () => {}, log: () => {}, error: () => {} },
    AbortController,
    Date,
    URLSearchParams,
    JSON,
    Event: FakeEvent,
    CustomEvent: FakeCustomEvent,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    fetch: fetchMock,
  };
  context.globalThis = context;

  vm.runInNewContext(webApiSrc, context, { filename: "web_api.js" });
  vm.runInNewContext(part1Src, context, { filename: "page_bootstrap.part1.js" });
  vm.runInNewContext(part3Src, context, { filename: "page_bootstrap.part3.js" });

  env.window = windowMock;
  env.PAApi = windowMock.PAApi;
  env.PABootstrap = windowMock.PABootstrap;
  // The host deferred start() to the load event because readyState was
  // "loading"; tests register sections first, then fire it.
  env.fireLoad = () => {
    (windowListeners.get("load") || []).forEach((fn) => fn(new FakeEvent("load")));
  };
  return env;
};

const settle = async (promise) => {
  try {
    await promise;
    return "completed";
  } catch (error) {
    return error?.kind ? `error:${error.kind}` : `error:${error?.message || error}`;
  }
};

test("section loader receives this run's AbortSignal from the bootstrap", async () => {
  const env = makeEnv();
  let receivedSignal = null;
  env.PABootstrap.registerSection(
    "sig-check",
    ({ signal } = {}) => {
      receivedSignal = signal;
      return env.PAApi.get("/api/section", { timeoutMs: 3000, signal });
    },
    { label: "signal check" }
  );
  env.fireLoad();
  await sleep(150);
  assert.ok(receivedSignal, "the bootstrap must pass a per-run signal to the loader");
  assert.strictEqual(typeof receivedSignal.aborted, "boolean", "the loader must receive a real AbortSignal");
});

test("deadline expiry aborts ONLY the section's own request; an unrelated request issued during the load completes", async () => {
  const env = makeEnv();
  env.hangMsFor["/api/section"] = 10000; // never finishes on its own
  env.hangMsFor["/api/status"] = 80;

  env.PABootstrap.registerSection(
    "doomed-section",
    ({ signal } = {}) => env.PAApi.get("/api/section", { timeoutMs: 8000, signal }),
    { label: "doomed", deadlineMs: 400 }
  );
  env.fireLoad();
  await sleep(100); // section request is now in flight

  // The footer's request arrives mid-section-load - the production timeline.
  const footer = settle(env.PAApi.get("/api/status", { timeoutMs: 5000 }));

  // Wait past the 400 ms deadline plus a clock tick for the reducer to cancel.
  await sleep(700);

  assert.ok(env.aborted.includes("/api/section"), "the section's own request must be aborted at the deadline");
  assert.strictEqual(await footer, "completed", "the unrelated request must survive the section cancellation");
  assert.ok(!env.aborted.includes("/api/status"), "the unrelated request must never see an abort");
  assert.strictEqual(env.maxInFlight, 1, "single-slot invariant must hold through deadline cancellation");
});

test("an in-flight E-Stop POST survives a section deadline cancellation", async () => {
  const env = makeEnv();
  env.hangMsFor["/api/section"] = 10000;
  env.hangMsFor["/api/estop"] = 600;

  env.PABootstrap.registerSection(
    "doomed-section",
    ({ signal } = {}) => env.PAApi.get("/api/section", { timeoutMs: 8000, signal }),
    { label: "doomed", deadlineMs: 400 }
  );
  env.fireLoad();
  await sleep(100);

  // Operator hits E-Stop while the section load is in progress.
  const estop = settle(env.PAApi.estopPostForm("/api/estop", { engage: "1" }, { timeoutMs: 5000 }));

  await sleep(700); // deadline fires while the E-Stop POST is still in flight

  assert.ok(env.aborted.includes("/api/section"), "the section's request must be the one cancelled");
  assert.strictEqual(await estop, "completed", "the E-Stop POST must complete despite the section cancellation");
  assert.ok(!env.aborted.includes("/api/estop"), "the E-Stop POST must never see an abort");
});
