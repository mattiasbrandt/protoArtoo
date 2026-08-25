// =============================================================================
// Identity reason tracking and diagnosis (#200, slice 3)
//
// Tests that:
// 1. "no-response" reason shows retryable copy with reconnection promise
// 2. "incompatible" reason shows terminal copy without reconnection promise
// 3. Three diagnosis sentences exist and are correctly placed
// 4. Diagnosis function exists and does not block bootstrap
// 5. shell.js dispatches reason field in identity-unavailable event
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";

test("three diagnosis sentences are correctly defined in setup.js", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Sentence 1: firmware and filesystem mismatch
  assert.strictEqual(
    setupContent.includes("The firmware and filesystem do not match. Upload both from the same release."),
    true,
    "should include sentence for fw/fs mismatch"
  );

  // Sentence 2: versions match but manifest invalid
  assert.strictEqual(
    setupContent.includes("The controller reported an invalid manifest. Uploading the same release again will not fix it."),
    true,
    "should include sentence for invalid manifest"
  );

  // Sentence 3: no version evidence
  assert.strictEqual(
    setupContent.includes("The controller could not report which features are available."),
    true,
    "should include sentence for no version evidence"
  );
});

test("reasonFor function distinguishes identity-unavailable by reason", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify that reasonFor checks identityErrorReason
  assert.strictEqual(
    setupContent.includes("if (identityErrorReason === \"incompatible\")"),
    true,
    "should check identityErrorReason for incompatible"
  );

  // Verify terminal copy is returned for incompatible
  assert.strictEqual(
    setupContent.includes("return \"The controller's manifest is invalid.\";"),
    true,
    "should return terminal copy for incompatible reason"
  );

  // Verify retryable copy is returned for no-response (default)
  assert.strictEqual(
    setupContent.includes("return `Could not check ${featureName}. Reconnecting to the controller…`;"),
    true,
    "should return retryable copy for no-response/default"
  );
});

test("setIdentityError accepts reason parameter and stores it", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify function signature
  assert.strictEqual(
    setupContent.includes("const setIdentityError = (reason = \"no-response\") =>"),
    true,
    "setIdentityError should accept reason parameter with default"
  );

  // Verify reason is stored in identityErrorReason
  assert.strictEqual(
    setupContent.includes("identityErrorReason = reason;"),
    true,
    "should store reason in identityErrorReason variable"
  );
});

test("pa:identity-unavailable event listener extracts and passes reason", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify listener extracts reason from event detail
  assert.strictEqual(
    setupContent.includes("const reason = event.detail?.reason || \"no-response\";"),
    true,
    "should extract reason from event detail with default"
  );

  // Verify reason is passed to setIdentityError
  assert.strictEqual(
    setupContent.includes("window.PAFeatureAvailability.setIdentityError(reason);"),
    true,
    "should pass extracted reason to setIdentityError"
  );
});

test("incompatible reason triggers lazy diagnosis listener registration", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify diagnosis is triggered for incompatible
  assert.strictEqual(
    setupContent.includes("if (reason === \"incompatible\") {"),
    true,
    "should check for incompatible reason to trigger diagnosis"
  );

  // Verify pa:assets-ready listener is registered
  assert.strictEqual(
    setupContent.includes("window.addEventListener(\"pa:assets-ready\", () => {"),
    true,
    "should register pa:assets-ready listener"
  );

  // Verify performIdentityDiagnosis is called
  assert.strictEqual(
    setupContent.includes("performIdentityDiagnosis();"),
    true,
    "should call performIdentityDiagnosis on assets-ready"
  );
});

test("performIdentityDiagnosis function exists and is async", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify function exists
  assert.strictEqual(
    setupContent.includes("const performIdentityDiagnosis = async () => {"),
    true,
    "performIdentityDiagnosis should be defined as async function"
  );

  // Verify it fetches fw-version.json
  assert.strictEqual(
    setupContent.includes('await window.PAApi.get(\"/fw-version.json\"'),
    true,
    "should fetch /fw-version.json"
  );

  // Verify it uses PAStatusStream
  assert.strictEqual(
    setupContent.includes("window.PAStatusStream?.getLastStatus?.()"),
    true,
    "should use PAStatusStream.getLastStatus()"
  );

  // Verify it calls setDiagFeedback with messages
  assert.strictEqual(
    setupContent.includes("setDiagFeedback(diagMessage, \"error\");"),
    true,
    "should call setDiagFeedback with diagnosis message"
  );
});

test("diagnosis logic compares expected vs running firmware versions", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify version comparison logic
  assert.strictEqual(
    setupContent.includes("if (expectedFwVersion !== runningFwVersion)"),
    true,
    "should compare expected vs running firmware versions"
  );

  // Verify first condition returns fw mismatch sentence
  assert.strictEqual(
    setupContent.includes("diagMessage = \"The firmware and filesystem do not match. Upload both from the same release.\";"),
    true,
    "should show mismatch sentence when versions differ"
  );

  // Verify else condition returns invalid manifest sentence
  assert.strictEqual(
    setupContent.includes("diagMessage = \"The controller reported an invalid manifest. Uploading the same release again will not fix it.\";"),
    true,
    "should show invalid manifest sentence when versions match"
  );
});

test("shell.js dispatches reason field in pa:identity-unavailable event", (t) => {
  const shellContent = readFileSync("./data/shell.js", "utf8");

  // Verify incompatible reason is dispatched on validation failure
  assert.strictEqual(
    shellContent.includes("detail: { error: \"invalid manifest\", reason: \"incompatible\" }"),
    true,
    "should dispatch reason: incompatible on validation failure"
  );

  // Verify no-response reason is dispatched on transport failure
  assert.strictEqual(
    shellContent.includes("detail: { error, reason: \"no-response\" }"),
    true,
    "should dispatch reason: no-response on transport error"
  );
});

test("identityErrorReason variable is initialized in module scope", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify variable declaration
  assert.strictEqual(
    setupContent.includes("let identityErrorReason = null;  // \"incompatible\" or \"no-response\" when phase === \"error\""),
    true,
    "should declare identityErrorReason variable in module scope"
  );
});

test("diagnosis message defaults to no version evidence when neither version is known", (t) => {
  const setupContent = readFileSync("./data/setup.js", "utf8");

  // Verify default message
  assert.strictEqual(
    setupContent.includes("let diagMessage = \"The controller could not report which features are available.\";"),
    true,
    "should default to no-version-evidence message"
  );
});
