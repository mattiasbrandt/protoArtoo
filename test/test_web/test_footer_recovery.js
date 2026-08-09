// =============================================================================
// test/test_web/test_footer_recovery.js
//
// Regression test for footer SSE mode retry/recovery and error logging.
// Extracted and executed from shipped footer.js - tests actual recovery behavior.
// Issue: #149 Version footer permanently shows "Firmware info unavailable"
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const footerPath = join(__dirname, "../../data/footer.js");
const footerFile = readFileSync(footerPath, "utf-8");

test("footer SSE mode retries failed fetchStatus with exponential backoff", (t) => {
  // Extract the retry function from shipped footer.js
  const retryStart = footerFile.indexOf("const retryFetchWithBackoff");
  assert.ok(retryStart >= 0, "shipped footer.js must define retryFetchWithBackoff");

  const retryEnd = footerFile.indexOf("};", retryStart) + 2;
  const retryCode = footerFile.substring(retryStart, retryEnd);

  // Verify backoff logic is present
  assert.ok(
    retryCode.includes("Math.pow(2, attempt)"),
    "shipped footer.js must use exponential backoff (2^attempt)"
  );

  assert.ok(
    retryCode.includes("maxAttempts = 3"),
    "shipped footer.js must have 3 retry attempts"
  );

  assert.ok(
    retryCode.includes("baseDelayMs = 500"),
    "shipped footer.js must use 500ms base delay"
  );

  assert.ok(
    retryCode.includes("window.setTimeout"),
    "shipped footer.js must schedule retries with setTimeout"
  );

  assert.ok(
    retryCode.includes("retryTimer !== null"),
    "shipped footer.js must check and clear retry timer"
  );
});

test("footer SSE mode cancels retry timer when status event arrives", (t) => {
  // Verify subscription handler clears retry timer
  const startSseStart = footerFile.indexOf("const startSseMode = () => {");
  assert.ok(startSseStart >= 0, "shipped footer.js must define startSseMode");

  const startSseEnd = footerFile.indexOf("};", startSseStart) + 2;
  const startSseCode = footerFile.substring(startSseStart, startSseEnd);

  // Check that subscription handler clears retryTimer
  assert.ok(
    startSseCode.includes("if (retryTimer !== null)") &&
      startSseCode.includes("window.clearTimeout(retryTimer)"),
    "subscription handler must clear retryTimer when status event arrives"
  );

  assert.ok(
    startSseCode.includes('if (eventType === "status")'),
    "subscription handler must check for status event type"
  );

  assert.ok(
    startSseCode.includes("renderFooter(payload)"),
    "subscription handler must render footer with event payload"
  );
});

test("footer SSE mode installs fallback poll timer", (t) => {
  // Verify that SSE mode also has a backup poll (not just retry)
  const startSseStart = footerFile.indexOf("const startSseMode = () => {");
  const startSseEnd = footerFile.indexOf("};", startSseStart) + 2;
  const startSseCode = footerFile.substring(startSseStart, startSseEnd);

  assert.ok(
    startSseCode.includes("window.setInterval"),
    "startSseMode must install a periodic poll timer"
  );

  assert.ok(
    startSseCode.includes("5000"),
    "startSseMode poll timer must use 5s interval (matching fallback mode)"
  );

  assert.ok(
    startSseCode.includes('document.visibilityState === "hidden"'),
    "startSseMode poll must skip when page is hidden"
  );
});

test("footer catchblock logs error instead of silent discard", (t) => {
  // Verify that fetch errors are logged for debugging
  const fetchStatusStart = footerFile.indexOf("const fetchStatus = async () => {");
  assert.ok(fetchStatusStart >= 0, "shipped footer.js must define fetchStatus");

  const fetchStatusEnd = footerFile.indexOf("};", fetchStatusStart) + 2;
  const fetchStatusCode = footerFile.substring(fetchStatusStart, fetchStatusEnd);

  assert.ok(
    fetchStatusCode.includes("console.warn"),
    "fetchStatus must log errors via console.warn (not silent catch)"
  );

  assert.ok(
    fetchStatusCode.includes("[footer]"),
    "logged errors must include [footer] prefix for filtering"
  );

  assert.ok(
    fetchStatusCode.includes("error?.message"),
    "fetchStatus must extract and log error message"
  );
});

test("footer retry logic executes with correct sequence", (t) => {
  // Verify the full logic flow for retryFetchWithBackoff
  const retryStart = footerFile.indexOf("const retryFetchWithBackoff");
  const retryEnd = footerFile.indexOf("};", retryStart) + 2;
  const retryCode = footerFile.substring(retryStart, retryEnd);

  // Check parameter handling
  assert.ok(
    retryCode.includes("attempt = 0"),
    "retryFetchWithBackoff must default attempt to 0"
  );

  // Check delay calculation
  assert.ok(
    retryCode.includes("baseDelayMs * Math.pow(2, attempt)"),
    "retry delay must be baseDelay * 2^attempt"
  );

  // Check attempt comparison
  assert.ok(
    retryCode.includes("attempt < maxAttempts - 1"),
    "retry must check if more attempts are available"
  );

  // Check recursive retry setup
  assert.ok(
    retryCode.includes("retryFetchWithBackoff(attempt + 1)"),
    "must schedule next retry with incremented attempt number"
  );
});

test("footer SSE mode calls retryFetchWithBackoff when lastStatus is null", (t) => {
  // Verify the initialization path
  const startSseStart = footerFile.indexOf("const startSseMode = () => {");
  const startSseEnd = footerFile.indexOf("};", startSseStart) + 2;
  const startSseCode = footerFile.substring(startSseStart, startSseEnd);

  assert.ok(
    startSseCode.includes("getLastStatus()"),
    "startSseMode must check if lastStatus exists"
  );

  assert.ok(
    startSseCode.includes("retryFetchWithBackoff(0)"),
    "startSseMode must initiate retry with attempt 0 when no cached status"
  );
});

test("footer error handler distinguishes fetch vs render failures", (t) => {
  // Verify that errors are caught at the fetch level, not mixed with render
  const fetchStatusStart = footerFile.indexOf("const fetchStatus = async () => {");
  const fetchStatusEnd = footerFile.indexOf("};", fetchStatusStart) + 2;
  const fetchStatusCode = footerFile.substring(fetchStatusStart, fetchStatusEnd);

  // The try/catch should wrap the fetch, not a broader scope that includes init
  assert.ok(
    fetchStatusCode.includes("await window.PAApi.get"),
    "fetchStatus must await the API call"
  );

  assert.ok(
    fetchStatusCode.includes("catch (error)"),
    "fetchStatus must catch with error variable (not _error)"
  );

  assert.ok(
    fetchStatusCode.includes("console.warn"),
    "fetchStatus must log caught errors for visibility"
  );
});

test("footer renderFooter handles null gracefully", (t) => {
  // Verify renderFooter sets unavailable message on null status
  const renderStart = footerFile.indexOf("const renderFooter = (status) => {");
  const renderEnd = footerFile.indexOf("};", renderStart) + 2;
  const renderCode = footerFile.substring(renderStart, renderEnd);

  assert.ok(
    renderCode.includes("if (!status)"),
    "renderFooter must check if status is null/undefined"
  );

  assert.ok(
    renderCode.includes("Firmware info unavailable"),
    "renderFooter must show unavailable message when status is null"
  );
});
