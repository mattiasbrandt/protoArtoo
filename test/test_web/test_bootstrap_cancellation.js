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

// =============================================================================
// Section Request Handle tests
// =============================================================================

test("section loader receives a handle alongside the raw signal", async () => {
  const env = makeEnv();
  let receivedHandle = null;
  env.PABootstrap.registerSection(
    "handle-check",
    ({ signal, handle } = {}) => {
      receivedHandle = handle;
      return Promise.resolve();
    },
    { label: "handle check" }
  );
  env.fireLoad();
  await sleep(150);
  assert.ok(receivedHandle, "the bootstrap must pass a handle to the loader");
  assert.ok(typeof receivedHandle.get === "function", "handle must have a get method");
  assert.ok(typeof receivedHandle.postForm === "function", "handle must have a postForm method");
  assert.ok(typeof receivedHandle.postJson === "function", "handle must have a postJson method");
  assert.ok(typeof receivedHandle.estopPostForm === "function", "handle must have an estopPostForm method");
});

test("handle.get() injects the section's signal", async () => {
  const env = makeEnv();
  let capturedSignal = null;
  const originalGet = env.PAApi.get;
  env.PAApi.get = function(path, opts) {
    capturedSignal = opts?.signal;
    return originalGet.call(this, path, opts);
  };

  env.PABootstrap.registerSection(
    "signal-inject-test",
    async ({ signal, handle } = {}) => {
      await handle.get("/api/test");
    },
    { label: "signal inject test" }
  );
  env.fireLoad();
  await sleep(200);
  assert.ok(capturedSignal, "handle.get must pass a signal to the fetch mock");
  assert.ok(capturedSignal instanceof AbortSignal, "the signal must be a real AbortSignal");

  env.PAApi.get = originalGet;
});

test("handle request carries the section's Operation Deadline: Ordinary by default (6000ms)", async () => {
  const env = makeEnv();
  let capturedTimeoutMs = null;
  // Monkey-patch PAApi.get to capture the timeoutMs
  const originalGet = env.PAApi.get;
  env.PAApi.get = function(path, opts) {
    capturedTimeoutMs = opts?.timeoutMs;
    return originalGet.call(this, path, opts);
  };

  env.PABootstrap.registerSection(
    "deadline-ordinary",
    async ({ signal, handle } = {}) => {
      await handle.get("/api/test");
    },
    { label: "deadline ordinary" }
  );
  env.fireLoad();
  await sleep(200);
  assert.strictEqual(capturedTimeoutMs, 6000, "handle.get must inject 6000ms (Ordinary) by default");

  env.PAApi.get = originalGet;
});

test("handle request carries the section's declared Operation Deadline", async () => {
  const env = makeEnv();
  let capturedTimeoutMs = null;
  const originalGet = env.PAApi.get;
  env.PAApi.get = function(path, opts) {
    capturedTimeoutMs = opts?.timeoutMs;
    return originalGet.call(this, path, opts);
  };

  env.PABootstrap.registerSection(
    "deadline-catalog",
    async ({ signal, handle } = {}) => {
      await handle.get("/api/test");
    },
    { label: "deadline catalog", deadlineMs: 12000 }
  );
  env.fireLoad();
  await sleep(200);
  assert.strictEqual(capturedTimeoutMs, 12000, "handle.get must inject the section's declared deadlineMs");

  env.PAApi.get = originalGet;
});

test("handle.postForm() injects signal and deadline", async () => {
  const env = makeEnv();
  let capturedOpts = null;
  const originalPostForm = env.PAApi.postForm;
  env.PAApi.postForm = function(path, form, opts) {
    capturedOpts = opts;
    return originalPostForm.call(this, path, form, opts);
  };

  env.PABootstrap.registerSection(
    "postform-test",
    async ({ signal, handle } = {}) => {
      await handle.postForm("/api/test", {});
    },
    { label: "postform test" }
  );
  env.fireLoad();
  await sleep(200);
  assert.ok(capturedOpts?.signal, "handle.postForm must pass a signal");
  assert.strictEqual(capturedOpts?.timeoutMs, 6000, "handle.postForm must inject 6000ms deadline");

  env.PAApi.postForm = originalPostForm;
});

test("handle.postJson() injects signal and deadline", async () => {
  const env = makeEnv();
  let capturedOpts = null;
  const originalPostJson = env.PAApi.postJson;
  env.PAApi.postJson = function(path, json, opts) {
    capturedOpts = opts;
    return originalPostJson.call(this, path, json, opts);
  };

  env.PABootstrap.registerSection(
    "postjson-test",
    async ({ signal, handle } = {}) => {
      await handle.postJson("/api/test", { data: "test" });
    },
    { label: "postjson test" }
  );
  env.fireLoad();
  await sleep(200);
  assert.ok(capturedOpts?.signal, "handle.postJson must pass a signal");
  assert.strictEqual(capturedOpts?.timeoutMs, 6000, "handle.postJson must inject 6000ms deadline");

  env.PAApi.postJson = originalPostJson;
});

test("handle.estopPostForm() injects signal and deadline", async () => {
  const env = makeEnv();
  let capturedOpts = null;
  const originalEstopPostForm = env.PAApi.estopPostForm;
  env.PAApi.estopPostForm = function(path, form, opts) {
    capturedOpts = opts;
    return originalEstopPostForm.call(this, path, form, opts);
  };

  env.PABootstrap.registerSection(
    "estop-test",
    async ({ signal, handle } = {}) => {
      await handle.estopPostForm("/api/estop", {});
    },
    { label: "estop test" }
  );
  env.fireLoad();
  await sleep(200);
  assert.ok(capturedOpts?.signal, "handle.estopPostForm must pass a signal");
  assert.strictEqual(capturedOpts?.timeoutMs, 6000, "handle.estopPostForm must inject 6000ms deadline");

  env.PAApi.estopPostForm = originalEstopPostForm;
});

test("aborting the section run cancels only handle requests, not unrelated traffic", async () => {
  const env = makeEnv();
  env.hangMsFor["/api/section"] = 10000; // loader's own request, never finishes
  env.hangMsFor["/api/unrelated"] = 80;

  env.PABootstrap.registerSection(
    "handle-cancellation",
    async ({ signal, handle } = {}) => {
      await handle.get("/api/section");
    },
    { label: "handle cancel", deadlineMs: 400 }
  );
  env.fireLoad();
  await sleep(100); // handle request is now in flight

  // An unrelated request arrives mid-load
  const unrelated = settle(env.PAApi.get("/api/unrelated", { timeoutMs: 5000 }));

  // Wait for the deadline to expire
  await sleep(700);

  assert.ok(env.aborted.includes("/api/section"), "handle request must be aborted at deadline");
  assert.strictEqual(await unrelated, "completed", "unrelated request must survive section cancellation");
  assert.ok(!env.aborted.includes("/api/unrelated"), "unrelated request must never be aborted");
  assert.strictEqual(env.maxInFlight, 1, "single-slot invariant must hold");
});

test("CRITICAL P1: handle injects the section run's signal — proven via active work replacement", async () => {
  // Mutation P1: replace controller.signal with new AbortController().signal.
  // This test must turn RED — only the section's true signal should abort requests.
  //
  // Trigger: active work replacement via retry
  //   - Section fails → goes to failed-retrying (retry scheduled at now + 2000ms backoff)
  //   - When retry time arrives, reducer gets new id via takeId
  //   - syncActive() sees id changed → calls cancelActive(oldId)
  //   - OLD controller is aborted, cancelling all its requests
  //   - P1 mutation: wrong signal is never aborted, request hangs until PAApi 6000ms timeout
  const env = makeEnv();
  env.hangMsFor["/api/handle-hang"] = 20000; // never finishes on its own (well past retry backoff)
  env.hangMsFor["/api/unrelated"] = 100;

  let handleRequestPromise = null;
  env.PABootstrap.registerSection(
    "handle-signal-replacement-test",
    ({ handle } = {}) => {
      // Start a handle request but DON'T wait for it. This leaves the request
      // hanging with the handle's signal (or wrong signal if P1 mutated).
      // Track the promise so we can observe when it settles.
      handleRequestPromise = settle(handle.get("/api/handle-hang"));
      // Fail immediately, leaving the request pending
      throw new Error("Loader fails immediately to trigger retry");
    },
    { label: "handle signal replacement test" }
  );
  env.fireLoad();
  await sleep(100); // handle request is now in flight

  // A second module's unrelated request arrives mid-load
  const unrelated = settle(env.PAApi.get("/api/unrelated", { timeoutMs: 5000 }));

  // Wait for the section's first attempt to fail and the retry backoff to expire (2000ms).
  // Add buffer for the retry to execute.
  await sleep(2500);

  // Now observe the outcomes:
  // - With correct code: handleRequestPromise should settle as "error:cancelled"
  //   because the section's old controller was aborted
  // - With P1 mutation: handleRequestPromise would settle as "error:timeout"
  //   because the wrong signal is never aborted and PAApi's 6000ms timeout fires
  const handleResult = await handleRequestPromise;

  assert.strictEqual(
    handleResult,
    "error:cancelled",
    "handle.get request MUST settle as 'cancelled' (proves section's signal was used and aborted)"
  );
  assert.strictEqual(
    await unrelated,
    "completed",
    "unrelated concurrent request MUST complete and never be aborted"
  );
});

test("raw signal remains available to the loader as escape hatch", async () => {
  const env = makeEnv();
  let loaderSignal = null;
  env.PABootstrap.registerSection(
    "raw-signal-test",
    ({ signal, handle } = {}) => {
      loaderSignal = signal;
      return env.PAApi.get("/api/test", { timeoutMs: 3000, signal });
    },
    { label: "raw signal test" }
  );
  env.fireLoad();
  await sleep(150);
  assert.ok(loaderSignal, "loader must receive the raw signal");
  assert.ok(loaderSignal instanceof AbortSignal, "raw signal must be a real AbortSignal");
});
