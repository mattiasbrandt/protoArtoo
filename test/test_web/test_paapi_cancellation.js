// =============================================================================
// test/test_web/test_paapi_cancellation.js
//
// Behavioral tests for the PAApi cancellation lane: caller-owned AbortSignal,
// single-release slot accounting, and the non-abortable E-Stop bypass.
//
// The shipped data/web_api.js is executed (vm.runInNewContext), never
// reimplemented or string-matched. Node's real AbortController and real
// timers are used, and the fetch mock honors AbortSignal through
// addEventListener so an abort landing while a request is in flight is
// actually observable. Supersedes test_issue_113_108_fixes.js,
// test_issue_148_fixes.js, and test_issue_148_ownership.js, whose harnesses
// could not observe an in-flight abort.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const webApiSrc = readFileSync(join(__dirname, "../../data/web_api.js"), "utf-8");

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Executes the shipped module in a fresh context. The fetch mock hangs each
// request for hangMs (default 120 ms), resolves with JSON, and rejects with a
// real AbortError when the request's signal fires mid-flight. Concurrency is
// measured at the fetch layer: dispatch-to-settle overlap, not call time.
const makeEnv = ({ hangMs = 120 } = {}) => {
  const env = {
    inFlight: 0,
    maxInFlight: 0,
    dispatches: [],
    failNext: 0, // when > 0, that many dispatches reject with a network error
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
      const shouldFail = env.failNext > 0;
      if (shouldFail) env.failNext -= 1;
      const timer = setTimeout(() => {
        if (shouldFail) {
          finish(reject, new TypeError("simulated network failure"));
          return;
        }
        finish(resolve, {
          ok: true,
          status: 200,
          headers: { get: () => "application/json" },
          json: async () => ({ ok: true, path }),
          text: async () => JSON.stringify({ ok: true, path }),
        });
      }, hangMs);
      opts.signal?.addEventListener("abort", () => {
        clearTimeout(timer);
        const abortError = new Error("The operation was aborted.");
        abortError.name = "AbortError";
        finish(reject, abortError);
      });
    });

  const windowMock = {
    setTimeout: (fn, ms) => setTimeout(fn, ms),
    clearTimeout: (id) => clearTimeout(id),
    setInterval: (fn, ms) => setInterval(fn, ms),
    clearInterval: (id) => clearInterval(id),
    addEventListener: () => {},
    location: { origin: "http://device" },
  };

  const context = {
    window: windowMock,
    document: { addEventListener: () => {} },
    console: { warn: () => {}, log: () => {}, error: () => {} },
    AbortController,
    Date,
    URLSearchParams,
    JSON,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    fetch: fetchMock,
  };
  context.globalThis = context;

  vm.runInNewContext(webApiSrc, context, { filename: "web_api.js" });

  env.PAApi = windowMock.PAApi;
  env.window = windowMock;
  return env;
};

// Settle helper: distinguishes completion, cancellation, timeout, and other
// failures by the ApiError kind the shipped module attaches.
const settle = async (promise) => {
  try {
    await promise;
    return "completed";
  } catch (error) {
    return error?.kind ? `error:${error.kind}` : `error:${error?.message || error}`;
  }
};

// -----------------------------------------------------------------------------
// Control: the harness itself must let an ordinary request complete.
// -----------------------------------------------------------------------------
test("CONTROL: ordinary GET with no signal completes", async () => {
  const env = makeEnv();
  const outcome = await settle(env.PAApi.get("/api/status", { timeoutMs: 3000 }));
  assert.strictEqual(outcome, "completed");
});

// -----------------------------------------------------------------------------
// Defect 1: ownership. Cancelling the section's request must not touch any
// other module's request, in either interleaving of slot and queue.
// -----------------------------------------------------------------------------
test("D1: unrelated in-flight GET survives a section cancellation (section queued)", async () => {
  const env = makeEnv();
  // Footer's request dispatches first and holds the single slot.
  const footer = settle(env.PAApi.get("/api/status", { timeoutMs: 3000 }));
  await sleep(20);
  // Section request queues behind it, carrying its owner's signal.
  const sectionOwner = new AbortController();
  const section = settle(env.PAApi.get("/api/config", { timeoutMs: 3000, signal: sectionOwner.signal }));
  await sleep(20);
  // Bootstrap cancels its own run only.
  sectionOwner.abort();
  assert.strictEqual(await section, "error:cancelled", "cancelled section request must reject as cancelled");
  assert.strictEqual(await footer, "completed", "unrelated request must be untouched by the cancellation");
  // The cancelled request never went out on the wire: only the footer dispatched.
  assert.deepStrictEqual(env.dispatches, ["/api/status"]);
});

test("D1b: unrelated GET issued DURING a section load survives when the section is cancelled", async () => {
  // The production case from the device timeline: the section's request is the
  // one in flight, the footer's request arrives while it runs, then the
  // bootstrap cancels the section.
  const env = makeEnv();
  const sectionOwner = new AbortController();
  const section = settle(env.PAApi.get("/api/config", { timeoutMs: 3000, signal: sectionOwner.signal }));
  await sleep(20);
  const footer = settle(env.PAApi.get("/api/status", { timeoutMs: 3000 }));
  await sleep(20);
  sectionOwner.abort();
  assert.strictEqual(await section, "error:cancelled");
  assert.strictEqual(await footer, "completed", "footer request issued mid-section-load must complete");
  assert.strictEqual(env.maxInFlight, 1, "single-slot invariant must hold through the abort");
});

test("D1c: cancelling one owner's request leaves a second owner's request untouched", async () => {
  const env = makeEnv();
  const ownerA = new AbortController();
  const ownerB = new AbortController();
  const a = settle(env.PAApi.get("/api/a", { timeoutMs: 3000, signal: ownerA.signal }));
  const b = settle(env.PAApi.get("/api/b", { timeoutMs: 3000, signal: ownerB.signal }));
  await sleep(20);
  ownerA.abort();
  assert.strictEqual(await a, "error:cancelled");
  assert.strictEqual(await b, "completed", "owner B's request must not be affected by owner A's abort");
});

// -----------------------------------------------------------------------------
// Defect 2: slot accounting. Aborts must not ratchet the concurrency ceiling;
// accounting after an abort must equal accounting after normal completion.
// -----------------------------------------------------------------------------
const measureBurstConcurrency = async (env, count = 4) => {
  env.maxInFlight = 0;
  await Promise.all(
    Array.from({ length: count }, (_, i) => settle(env.PAApi.get(`/api/burst${i}`, { timeoutMs: 3000 })))
  );
  return env.maxInFlight;
};

test("D2: max concurrency stays 1 on a fresh page and after repeated aborts", async () => {
  const env = makeEnv({ hangMs: 40 });
  assert.strictEqual(await measureBurstConcurrency(env), 1, "fresh-load ceiling must be 1");

  for (let round = 1; round <= 5; round += 1) {
    const owner = new AbortController();
    const victim = settle(env.PAApi.get("/api/doomed", { timeoutMs: 3000, signal: owner.signal }));
    const bystanders = Array.from({ length: 3 }, (_, i) =>
      settle(env.PAApi.get(`/api/bystander${i}`, { timeoutMs: 3000 }))
    );
    await sleep(15);
    owner.abort();
    await victim;
    await Promise.all(bystanders);
    assert.strictEqual(
      await measureBurstConcurrency(env),
      1,
      `ceiling must still be 1 after ${round} abort(s); a higher value means the slot accounting ratcheted`
    );
  }
});

test("D2b: an aborted request releases its slot exactly once (parity with normal completion)", async () => {
  const env = makeEnv({ hangMs: 40 });
  // Normal completion baseline.
  await env.PAApi.get("/api/baseline", { timeoutMs: 3000 });
  const afterNormal = await measureBurstConcurrency(env);
  // Aborted in-flight request.
  const owner = new AbortController();
  const doomed = settle(env.PAApi.get("/api/doomed", { timeoutMs: 3000, signal: owner.signal }));
  await sleep(15);
  owner.abort();
  await doomed;
  const afterAbort = await measureBurstConcurrency(env);
  assert.strictEqual(afterAbort, afterNormal, "slot accounting after abort must equal accounting after completion");
});

// -----------------------------------------------------------------------------
// Defect 3: the E-Stop lane must never be cancellable and must not corrupt
// slot accounting it never participated in.
// -----------------------------------------------------------------------------
test("D3: E-Stop POST survives an abort of every other owner's signal", async () => {
  const env = makeEnv({ hangMs: 60 });
  const sectionOwner = new AbortController();
  const section = settle(env.PAApi.get("/api/config", { timeoutMs: 3000, signal: sectionOwner.signal }));
  await sleep(15);
  const estop = settle(env.PAApi.estopPostForm("/api/estop", { engage: "1" }, { timeoutMs: 3000 }));
  await sleep(15);
  sectionOwner.abort();
  assert.strictEqual(await estop, "completed", "an in-flight E-Stop must survive a section cancellation");
  assert.strictEqual(await section, "error:cancelled");
});

test("D3b: a signal passed to the E-Stop lane is stripped, so even its own caller cannot cancel it", async () => {
  const env = makeEnv({ hangMs: 60 });
  const owner = new AbortController();
  const estop = settle(env.PAApi.estopPostForm("/api/estop", { engage: "1" }, { timeoutMs: 3000, signal: owner.signal }));
  await sleep(15);
  owner.abort();
  assert.strictEqual(await estop, "completed", "the E-Stop lane must ignore any caller signal");
});

test("D3c: E-Stop bypasses the queue and leaves slot accounting untouched", async () => {
  const env = makeEnv({ hangMs: 60 });
  // Occupy the single slot, then fire E-Stop: it must dispatch immediately
  // rather than queue behind the GET.
  const blocker = settle(env.PAApi.get("/api/slow", { timeoutMs: 3000 }));
  await sleep(15);
  const estop = settle(env.PAApi.estopPostForm("/api/estop", { engage: "1" }, { timeoutMs: 3000 }));
  await sleep(15);
  assert.deepStrictEqual(env.dispatches, ["/api/slow", "/api/estop"], "E-Stop must dispatch while the GET holds the slot");
  assert.strictEqual(await estop, "completed");
  assert.strictEqual(await blocker, "completed");
  // E-Stop never acquired a slot, so it must not have released one either.
  assert.strictEqual(await measureBurstConcurrency(env), 1, "ceiling must be 1 after an E-Stop bypass");
});

// -----------------------------------------------------------------------------
// Error classification and queue/retry edges.
// -----------------------------------------------------------------------------
test("caller cancellation is kind 'cancelled'; a genuine timeout stays kind 'timeout'", async () => {
  const env = makeEnv({ hangMs: 5000 });
  const owner = new AbortController();
  const cancelled = settle(env.PAApi.get("/api/a", { timeoutMs: 3000, signal: owner.signal }));
  await sleep(15);
  owner.abort();
  assert.strictEqual(await cancelled, "error:cancelled");

  const timedOut = await settle(env.PAApi.get("/api/b", { timeoutMs: 80 }));
  assert.strictEqual(timedOut, "error:timeout");
});

test("a request cancelled while queued never dispatches and keeps accounting intact", async () => {
  const env = makeEnv({ hangMs: 80 });
  const blocker = settle(env.PAApi.get("/api/slow", { timeoutMs: 3000 }));
  await sleep(10);
  const owner = new AbortController();
  const queued = settle(env.PAApi.get("/api/queued", { timeoutMs: 3000, signal: owner.signal }));
  owner.abort();
  assert.strictEqual(await queued, "error:cancelled");
  assert.strictEqual(await blocker, "completed");
  assert.ok(!env.dispatches.includes("/api/queued"), "a request cancelled while queued must never reach fetch");
  assert.strictEqual(await measureBurstConcurrency(env), 1);
});

test("cancellation during the transient-retry delay stops the retry", async () => {
  const env = makeEnv({ hangMs: 20 });
  env.failNext = 1; // first dispatch fails as a network transient -> retry path
  const owner = new AbortController();
  const outcome = settle(env.PAApi.get("/api/flaky", { timeoutMs: 3000, signal: owner.signal }));
  await sleep(60); // inside the 150 ms retry delay
  owner.abort();
  assert.strictEqual(await outcome, "error:cancelled");
  await sleep(250); // past the retry delay
  assert.strictEqual(
    env.dispatches.filter((p) => p === "/api/flaky").length,
    1,
    "the retry must not go out after the caller cancelled"
  );
});

test("the transient retry itself still works for callers that did not cancel", async () => {
  const env = makeEnv({ hangMs: 20 });
  env.failNext = 1;
  const outcome = await settle(env.PAApi.get("/api/flaky", { timeoutMs: 3000 }));
  assert.strictEqual(outcome, "completed");
  assert.strictEqual(env.dispatches.filter((p) => p === "/api/flaky").length, 2);
});

// -----------------------------------------------------------------------------
// API surface: the shared-global abort entry point is gone.
// -----------------------------------------------------------------------------
test("PAApi no longer exposes a global abortRequest", () => {
  const env = makeEnv();
  assert.strictEqual(env.PAApi.abortRequest, undefined, "abortRequest must not exist; cancellation is caller-owned");
});
