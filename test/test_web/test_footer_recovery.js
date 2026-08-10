// =============================================================================
// test/test_web/test_footer_recovery.js
//
// Version footer recovery (issue #149): the footer must not sit on
// "Firmware info unavailable" forever when the first status fetch loses a race
// with the SSE stream. It retries with backoff, polls only while it has no real
// data, and stands down the moment a status event arrives.
//
// Every test drives the shipped data/footer.js in a vm and asserts on what it
// did - what it rendered, which timers it armed and with what delay, which it
// cleared. Eight of the ten tests here used to assert on substrings of
// footer.js instead ("shipped code must have 500ms base delay"), which could
// not tell a working backoff from a broken one. Issue #146.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule, ApiError } from "./helpers/page_module_env.js";

const STATUS_PATH = "/api/status";
const FS_VERSION_PATH = "/fs-version.json";
const UNAVAILABLE = "Firmware info unavailable";

const STATUS_PAYLOAD = { firmwareVersion: "v1.0.0", fsVersion: "v1.0.0" };

// Brings footer.js up with a controllable transport and status stream.
//
// statusOutcome() decides what /api/status does on each call, so a test can
// fail the first N attempts and then succeed. sse.supported switches between
// the SSE lane and the fallback-polling lane.
const loadFooter = async ({
  statusOutcome = () => STATUS_PAYLOAD,
  sseSupported = true,
  lastStatus = null,
} = {}) => {
  const stream = { subscriber: null, lastStatus };

  const env = loadPageModule("footer.js", {
    respond: (path) => {
      if (path === FS_VERSION_PATH) return { data: { fsVersion: "v1.0.0" } };
      if (path === STATUS_PATH) {
        const outcome = statusOutcome();
        if (outcome instanceof Error) throw outcome;
        return { data: outcome };
      }
      return { data: {} };
    },
    overrides: {
      PAStatusStream: {
        isSupported: () => sseSupported,
        getLastStatus: () => stream.lastStatus,
        subscribe: (handler) => {
          stream.subscriber = handler;
          return () => {
            stream.subscriber = null;
          };
        },
      },
    },
  });

  await env.settle();
  return { ...env, stream, footer: env.element("fw-meta") };
};

const statusRequests = (env) => env.requests.filter((r) => r.path === STATUS_PATH);
const retryDelays = (env) => env.timeouts.map((t) => t.ms);

// Fires every retry timer armed so far, letting each one arm the next.
const drainRetries = async (env, rounds = 5) => {
  let fired = 0;
  for (let round = 0; round < rounds; round += 1) {
    const next = env.timeouts[fired];
    if (!next) break;
    fired += 1;
    next.fn();
    await env.settle();
  }
  return fired;
};

// -----------------------------------------------------------------------------
// Retry with backoff
// -----------------------------------------------------------------------------

test("A failed first status fetch leaves the footer saying so", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => new ApiError("Simulated fetch failure", { kind: "network" }),
  });

  assert.equal(env.footer.textContent, UNAVAILABLE, "a failed fetch must not leave stale text");
  assert.equal(statusRequests(env).length, 1, "the first attempt must have gone out");
});

test("Retries back off 500 ms, 1 s, 2 s and then stop at three attempts", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => new ApiError("Simulated fetch failure", { kind: "network" }),
  });

  await drainRetries(env);

  assert.deepEqual(
    retryDelays(env),
    [500, 1000],
    "backoff must double from a 500 ms base, and the third attempt is the last so it arms no timer"
  );
  assert.equal(
    statusRequests(env).length,
    3,
    "three attempts total - a fourth would mean the attempt cap is not being honoured"
  );
});

test("A retry that succeeds stops the retry chain", async (t) => {
  let attempt = 0;
  const env = await loadFooter({
    statusOutcome: () => {
      attempt += 1;
      return attempt === 1 ? new ApiError("Simulated fetch failure", { kind: "network" }) : STATUS_PAYLOAD;
    },
  });

  assert.equal(retryDelays(env).length, 1, "the failed first attempt must arm one retry");

  await drainRetries(env);

  assert.equal(retryDelays(env).length, 1, "a successful retry must not arm another");
  assert.equal(statusRequests(env).length, 2, "no further attempts after success");
  assert.match(env.footer.innerHTML, /v1\.0\.0/, "the footer must show the version it fetched");
});

test("A successful fetch renders firmware and filesystem versions", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => ({ firmwareVersion: "v1.2.3", fsVersion: "v4.5.6" }),
  });

  assert.match(env.footer.innerHTML, /FW:.*v1\.2\.3/s, "firmware version must be shown");
  assert.match(env.footer.innerHTML, /FS:.*v4\.5\.6/s, "filesystem version must be shown");
  assert.notEqual(env.footer.textContent, UNAVAILABLE);
});

test("A status version containing markup is escaped before it reaches the footer", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => ({ firmwareVersion: '<img src=x onerror="alert(1)">', fsVersion: "v1.0.0" }),
  });

  assert.ok(
    !env.footer.innerHTML.includes("<img"),
    "a version string from the controller must not be able to inject markup"
  );
  assert.match(env.footer.innerHTML, /&lt;img/);
});

// -----------------------------------------------------------------------------
// The SSE lane
// -----------------------------------------------------------------------------

test("A status event from the stream renders the footer", async (t) => {
  const env = await loadFooter({ lastStatus: STATUS_PAYLOAD });

  assert.ok(env.stream.subscriber, "SSE mode must subscribe to the status stream");
  env.stream.subscriber("status", { firmwareVersion: "v9.9.9", fsVersion: "v9.9.9" });

  assert.match(env.footer.innerHTML, /v9\.9\.9/, "the footer must render the streamed status");
});

test("Events other than status leave the footer alone", async (t) => {
  const env = await loadFooter({ lastStatus: STATUS_PAYLOAD });
  env.stream.subscriber("status", STATUS_PAYLOAD);
  const rendered = env.footer.innerHTML;

  env.stream.subscriber("log", { line: "something else entirely" });

  assert.equal(env.footer.innerHTML, rendered, "an unrelated event must not repaint the footer");
});

test("A status event arriving mid-backoff cancels the pending retry", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => new ApiError("Simulated fetch failure", { kind: "network" }),
  });

  const armed = env.timeouts.at(-1);
  assert.ok(armed, "a failed fetch must have armed a retry");
  assert.equal(env.cleared.timeouts.length, 0, "nothing cancelled yet");

  env.stream.subscriber("status", STATUS_PAYLOAD);

  assert.ok(
    env.cleared.timeouts.includes(armed.id),
    "the pending retry must be cancelled once the stream delivers a status"
  );
  assert.match(env.footer.innerHTML, /v1\.0\.0/, "the streamed status must be what the footer shows");
});

test("SSE mode skips its first fetch when the stream already has a status", async (t) => {
  const env = await loadFooter({ lastStatus: STATUS_PAYLOAD });

  assert.equal(
    statusRequests(env).length,
    0,
    "a cached status makes the startup fetch redundant work"
  );
});

test("SSE mode fetches at startup when the stream has nothing cached", async (t) => {
  const env = await loadFooter({ lastStatus: null });

  assert.equal(
    statusRequests(env).length,
    1,
    "with no cached status the footer must fetch, or it shows unavailable until the next event"
  );
});

// -----------------------------------------------------------------------------
// Conditional polling
// -----------------------------------------------------------------------------

test("SSE mode installs a 5 s poll", async (t) => {
  const env = await loadFooter({ lastStatus: STATUS_PAYLOAD });

  assert.deepEqual(
    env.intervals.map((i) => i.ms),
    [5000],
    "exactly one poll, at the 5 s cadence"
  );
});

test("The SSE poll stands down once the footer has real data", async (t) => {
  const env = await loadFooter({ lastStatus: STATUS_PAYLOAD });
  env.stream.subscriber("status", STATUS_PAYLOAD);
  const before = statusRequests(env).length;

  env.fireInterval(env.intervals[0].id);
  await env.settle();

  assert.equal(
    statusRequests(env).length,
    before,
    "polling on top of a live stream is exactly the duplicate traffic this poll exists to avoid"
  );
});

test("The SSE poll keeps fetching while the footer has no real data", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => new ApiError("Simulated fetch failure", { kind: "network" }),
  });
  const before = statusRequests(env).length;

  env.fireInterval(env.intervals[0].id);
  await env.settle();

  assert.equal(
    statusRequests(env).length,
    before + 1,
    "the poll is the recovery path when the stream never delivers"
  );
});

test("The poll skips a hidden tab", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => new ApiError("Simulated fetch failure", { kind: "network" }),
  });
  const before = statusRequests(env).length;

  env.document.visibilityState = "hidden";
  env.fireInterval(env.intervals[0].id);
  await env.settle();

  assert.equal(statusRequests(env).length, before, "a background tab must not be polled");
});

test("Without SSE support the footer falls back to polling and fetches immediately", async (t) => {
  const env = await loadFooter({ sseSupported: false });

  assert.equal(statusRequests(env).length, 1, "fallback mode must fetch at startup");
  assert.deepEqual(
    env.intervals.map((i) => i.ms),
    [5000],
    "fallback mode must install the same 5 s poll"
  );
  assert.equal(env.stream.subscriber, null, "no subscription without stream support");
});

test("The fallback poll keeps fetching even after it has real data", async (t) => {
  const env = await loadFooter({ sseSupported: false });
  const before = statusRequests(env).length;

  env.fireInterval(env.intervals[0].id);
  await env.settle();

  assert.equal(
    statusRequests(env).length,
    before + 1,
    "with no stream to feed it, the poll is the only source of fresh data"
  );
});

// -----------------------------------------------------------------------------
// Visibility and teardown
// -----------------------------------------------------------------------------

test("Returning to a visible tab refetches the status", async (t) => {
  const env = await loadFooter({ lastStatus: STATUS_PAYLOAD });
  const before = statusRequests(env).length;

  env.document.visibilityState = "visible";
  env.emit("document", "visibilitychange");
  await env.settle();

  assert.equal(statusRequests(env).length, before + 1, "a returning operator must see current data");
});

test("Unloading the page clears the poll, the retry and the subscription", async (t) => {
  const env = await loadFooter({
    statusOutcome: () => new ApiError("Simulated fetch failure", { kind: "network" }),
  });

  const poll = env.intervals[0];
  const retry = env.timeouts.at(-1);
  assert.ok(poll && retry, "the fixture must have both a poll and a pending retry to clean up");

  env.emit("window", "beforeunload");

  assert.ok(env.cleared.intervals.includes(poll.id), "the poll interval must be cleared");
  assert.ok(env.cleared.timeouts.includes(retry.id), "the pending retry must be cleared");
  assert.equal(env.stream.subscriber, null, "the stream subscription must be released");
});

// -----------------------------------------------------------------------------
// Source-text check, deliberately kept
// -----------------------------------------------------------------------------

test("footer.js is ASCII only", async (t) => {
  // Justified source-text assertion: this is a lint rule about the bytes of the
  // file, not about behaviour. It exists because non-ASCII punctuation in
  // served JS has broken the bundle before, and there is no runtime observation
  // that could show it - the code behaves identically either way.
  const { readFileSync } = await import("fs");
  const { fileURLToPath } = await import("url");
  const { dirname, join } = await import("path");
  const here = dirname(fileURLToPath(import.meta.url));
  const footerCode = readFileSync(join(here, "../../data/footer.js"), "utf-8");

  // String and comment contents are excluded: operator-facing copy may legitimately
  // carry non-ASCII, the ban is on code and punctuation.
  const codeOnly = footerCode.replace(/"[^"]*"/g, "").replace(/'[^']*'/g, "").replace(/`[^`]*`/g, "");
  const offenders = codeOnly.match(/[^\x00-\x7F]/g);

  assert.equal(
    offenders,
    null,
    `footer.js must use ASCII only; found ${JSON.stringify(offenders)}`
  );
});
