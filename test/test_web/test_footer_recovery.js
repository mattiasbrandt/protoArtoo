// =============================================================================
// test/test_web/test_footer_recovery.js
//
// Regression test for footer SSE mode retry/recovery and conditional polling.
// Executes shipped footer.js in a vm with mocked window/document/PAApi.
// Issue: #149 Version footer permanently shows "Firmware info unavailable"
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { runInNewContext } from "vm";

const __dirname = dirname(fileURLToPath(import.meta.url));
const footerPath = join(__dirname, "../../data/footer.js");

function createFooterTestContext() {
  const events = [];
  let elementTextContent = "Firmware info unavailable";
  let elementInnerHTML = "";
  const asyncSettlements = [];

  const mockContext = {
    window: {
      PAUtils: {
        escapeHtml: (val) => {
          if (!val) return "";
          return String(val)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;");
        },
      },
      PAApi: null,
      PAStatusStream: null,
      setInterval: (fn, ms) => {
        const id = Symbol(`interval-${events.length}`);
        events.push({ type: "setInterval", ms, id, time: events.length });
        return id;
      },
      clearInterval: (id) => {
        events.push({ type: "clearInterval", id, time: events.length });
      },
      setTimeout: (fn, ms) => {
        const id = Symbol(`timeout-${events.length}`);
        events.push({ type: "setTimeout", ms, id, time: events.length });
        // Simulate async execution
        asyncSettlements.push(
          new Promise((resolve) => {
            setTimeout(() => {
              fn();
              resolve();
            }, ms);
          })
        );
        return id;
      },
      clearTimeout: (id) => {
        events.push({ type: "clearTimeout", id, time: events.length });
      },
      addEventListener: () => {},
    },
    document: {
      getElementById: (id) => {
        if (id === "fw-meta") {
          return {
            get textContent() {
              return elementTextContent;
            },
            set textContent(val) {
              elementTextContent = val;
            },
            get innerHTML() {
              return elementInnerHTML;
            },
            set innerHTML(val) {
              elementInnerHTML = val;
            },
          };
        }
        return null;
      },
      addEventListener: () => {},
      visibilityState: "visible",
    },
    console,
  };

  return {
    mockContext,
    events,
    asyncSettlements,
    getFooterText: () => elementTextContent,
    getFooterHTML: () => elementInnerHTML,
    waitForAsync: () => Promise.all(asyncSettlements),
  };
}

test("footer SSE mode calls fetchStatus when no cached status, attempts retry on failure", async (t) => {
  const { mockContext, events, getFooterText, waitForAsync } = createFooterTestContext();
  const footerCode = readFileSync(footerPath, "utf-8");

  let fetchCalls = [];
  mockContext.window.PAApi = {
    get: async (path) => {
      fetchCalls.push({ path, time: Date.now() });
      if (path === "/fs-version.json") {
        return { data: { fsVersion: "v1.0.0" } };
      }
      if (path === "/api/status") {
        // All status fetches fail to trigger retry
        throw new Error("Simulated fetch failure");
      }
      return { data: {} };
    },
  };

  mockContext.window.PAStatusStream = {
    isSupported: () => true,
    getLastStatus: () => null,
    subscribe: () => () => {},
  };

  runInNewContext(footerCode, mockContext);
  await waitForAsync();
  await new Promise((r) => setTimeout(r, 100));

  // Should have scheduled at least one retry timer
  const timeoutEvents = events.filter((e) => e.type === "setTimeout");
  assert.ok(
    timeoutEvents.length >= 1,
    `should schedule retry timers, got ${timeoutEvents.length}`
  );

  // Footer should show unavailable after failed fetches
  assert.strictEqual(
    getFooterText(),
    "Firmware info unavailable",
    "footer should show unavailable after fetch failures"
  );
});

test("footer SSE mode installs 5s polling interval (code structure)", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  // Verify setInterval(fn, 5000) is called in startSseMode
  const sseStart = footerCode.indexOf("const startSseMode");
  const sseEnd = footerCode.indexOf("};", sseStart);
  const sseBody = footerCode.substring(sseStart, sseEnd);

  assert.ok(
    sseBody.includes("setInterval"),
    "startSseMode must call setInterval"
  );

  assert.ok(
    sseBody.includes("5000"),
    "startSseMode must use 5000ms interval"
  );

  // Verify it's assigned to pollTimer
  assert.ok(
    sseBody.includes("pollTimer = window.setInterval"),
    "polling interval must be assigned to pollTimer"
  );

  // Verify visibility check
  assert.ok(
    sseBody.includes('visibilityState === "hidden"'),
    "poll must skip when page is hidden"
  );
});

test("footer retry uses exponential backoff with 500ms base and 2x multiplier", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  // Extract and verify backoff logic is in the code
  assert.ok(
    footerCode.includes("baseDelayMs = 500"),
    "shipped code must have 500ms base delay"
  );

  assert.ok(
    footerCode.includes("Math.pow(2, attempt)"),
    "shipped code must use exponential backoff (2^attempt)"
  );

  // Verify attempt limiting
  assert.ok(
    footerCode.includes("maxAttempts = 3"),
    "shipped code must limit to 3 attempts"
  );

  // Verify retry scheduling
  assert.ok(
    footerCode.includes("attempt < maxAttempts - 1"),
    "shipped code must check remaining attempts"
  );
});

test("fetchStatus returns boolean to indicate success vs failure", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  // Verify success return
  const fetchStart = footerCode.indexOf("const fetchStatus = async");
  const fetchEnd = footerCode.indexOf("};", fetchStart);
  const fetchBody = footerCode.substring(fetchStart, fetchEnd);

  assert.ok(fetchBody.includes("return true"), "fetchStatus must return true on success");

  assert.ok(fetchBody.includes("return false"), "fetchStatus must return false on failure");

  // Verify both returns are in the function
  const successCount = (fetchBody.match(/return true/g) || []).length;
  const failCount = (fetchBody.match(/return false/g) || []).length;
  assert.ok(successCount >= 1, "must have at least one success return");
  assert.ok(failCount >= 1, "must have at least one failure return");
});

test("retryFetchWithBackoff awaits and checks fetchStatus result before scheduling next attempt", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  const retryStart = footerCode.indexOf("const retryFetchWithBackoff");
  const retryEnd = footerCode.indexOf("};", retryStart);
  const retryBody = footerCode.substring(retryStart, retryEnd);

  // Verify it awaits fetchStatus result
  assert.ok(
    retryBody.includes("const success = await fetchStatus()") ||
      retryBody.includes("const success=await fetchStatus()"),
    "retryFetchWithBackoff must await fetchStatus() and capture result"
  );

  // Verify it checks success
  assert.ok(
    retryBody.includes("if (success)"),
    "retryFetchWithBackoff must check if fetch succeeded"
  );

  // Verify early return on success
  assert.ok(
    retryBody.includes("if (success)") && retryBody.includes("return;"),
    "retryFetchWithBackoff must return early on success"
  );

  // Verify retry only scheduled if more attempts available
  assert.ok(
    retryBody.includes("if (attempt < maxAttempts - 1)"),
    "retry must only schedule if attempts remain"
  );
});

test("subscription handler cancels retry timer when status event arrives (code structure)", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  // Verify subscription callback checks for status event
  const sseStart = footerCode.indexOf("const startSseMode");
  const sseEnd = footerCode.indexOf("};", sseStart);
  const sseBody = footerCode.substring(sseStart, sseEnd);

  // Verify it subscribes and handles status events
  assert.ok(
    sseBody.includes("subscribe"),
    "startSseMode must subscribe to PAStatusStream"
  );

  assert.ok(
    sseBody.includes('if (eventType === "status")'),
    "subscription callback must check for status event type"
  );

  // Verify it calls renderFooter
  assert.ok(
    sseBody.includes("renderFooter(payload)"),
    "subscription callback must render footer with payload"
  );

  // Verify it clears retry timer on status event
  const subscribeStart = sseBody.indexOf(".subscribe");
  const subscribeEnd = sseBody.indexOf("});", subscribeStart);
  const subscribeBody = sseBody.substring(subscribeStart, subscribeEnd);

  assert.ok(
    subscribeBody.includes("if (retryTimer !== null)") &&
      subscribeBody.includes("clearTimeout(retryTimer)"),
    "subscription callback must clear retryTimer when status event arrives"
  );
});

test("footer errors logged via console.warn with [footer] prefix (visible, not silent)", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  // Verify error logging is in fetchStatus
  const fetchStart = footerCode.indexOf("const fetchStatus = async");
  const fetchEnd = footerCode.indexOf("};", fetchStart);
  const fetchBody = footerCode.substring(fetchStart, fetchEnd);

  assert.ok(
    fetchBody.includes('console.warn("[footer]'),
    "fetchStatus must call console.warn with [footer] prefix"
  );

  // Verify error.message extraction
  assert.ok(
    fetchBody.includes("error?.message"),
    "fetchStatus must extract error.message for debugging"
  );

  // Verify this is a catch block (not just any console.warn)
  assert.ok(
    fetchBody.includes("catch") && fetchBody.includes('console.warn("[footer]'),
    "error logging must be in error handler"
  );
});

test("polling is conditional - only runs when footer has no real data (hasRealData flag)", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  // Verify hasRealData flag exists
  assert.ok(
    footerCode.includes("hasRealData"),
    "shipped code must track hasRealData flag"
  );

  // Verify flag is initialized
  assert.ok(
    footerCode.includes("let hasRealData"),
    "hasRealData must be declared as state variable"
  );

  // Verify flag is set to true on successful render
  const renderStart = footerCode.indexOf("const renderFooter");
  const renderEnd = footerCode.indexOf("};", renderStart);
  const renderBody = footerCode.substring(renderStart, renderEnd);

  assert.ok(
    renderBody.includes("hasRealData = true"),
    "renderFooter must set hasRealData = true on successful render"
  );

  // Verify flag is set to false on error
  assert.ok(
    renderBody.includes("hasRealData = false"),
    "renderFooter must set hasRealData = false when status is null"
  );

  // Verify poll checks the flag
  const sseStart = footerCode.indexOf("const startSseMode");
  const sseEnd = footerCode.indexOf("};", sseStart);
  const sseBody = footerCode.substring(sseStart, sseEnd);

  assert.ok(
    sseBody.includes("if (hasRealData) return;"),
    "poll in SSE mode must skip if hasRealData is true"
  );
});

test("beforeunload cleanup prevents timer leaks (clears retryTimer)", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  const beforeStart = footerCode.indexOf('addEventListener("beforeunload"');
  const beforeEnd = footerCode.indexOf("});", beforeStart) + 3;
  const beforeBody = footerCode.substring(beforeStart, beforeEnd);

  // Verify retryTimer cleanup
  assert.ok(
    beforeBody.includes("retryTimer"),
    "beforeunload must reference retryTimer"
  );

  assert.ok(
    beforeBody.includes("clearTimeout(retryTimer)"),
    "beforeunload must clear retryTimer with clearTimeout"
  );

  // Verify it's guarded
  assert.ok(
    beforeBody.includes("if (retryTimer !== null)"),
    "retryTimer cleanup must be guarded by null check"
  );
});

test("footer comments and code use ASCII only (no em-dashes or unicode)", (t) => {
  const footerCode = readFileSync(footerPath, "utf-8");

  // Check for em-dashes and similar unicode
  const forbidden = /[‐-―]|[^\x00-\x7F]/g;
  const matches = footerCode.match(forbidden);

  // Filter out content inside strings (basic check for common unicode in values)
  const codeWithoutStrings = footerCode.replace(/"[^"]*"/g, "").replace(/'[^']*'/g, "");
  const codeMatches = codeWithoutStrings.match(forbidden);

  assert.ok(
    !codeMatches || codeMatches.length === 0,
    "code must use ASCII only (no em-dashes or unicode operators)"
  );
});
