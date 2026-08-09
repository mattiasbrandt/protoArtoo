// =============================================================================
// test/test_web/test_issue_113_108_fixes.js
//
// Tests for #113 (deadline cancellation) and #108 Part 2 (estop bypass).
//
// Note: The comprehensive behavioral tests for #148 (the fixes to these issues)
// are in test_issue_148_fixes.js. This file verifies documentation and behavior
// that may not be exercised by other test suites.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = dirname(dirname(__dirname));

test("page-load-recovery-architecture.md documents estop bypass", (t) => {
  const docPath = join(root, "docs", "page-load-recovery-architecture.md");
  const docFile = readFileSync(docPath, "utf-8");

  // Verify estop exception is documented
  assert.ok(
    docFile.includes("estop") || docFile.includes("E-Stop") || docFile.includes("ESTOP"),
    "Docs should mention estop"
  );
});

test("ADR 0019 is referenced for slot limiting rationale", (t) => {
  const webApiPath = join(root, "data", "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Verify the code comment references ADR 0019
  assert.ok(
    webApiFile.includes("ADR 0019"),
    "web_api.js should reference ADR 0019 for single-slot requirement"
  );
});

test("web_api.js exports PAApi.abortRequest", (t) => {
  const webApiPath = join(root, "data", "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Verify abortRequest is exported
  assert.ok(
    webApiFile.includes("abortRequest"),
    "web_api.js should export abortRequest"
  );

  // Verify it's in the PAApi export
  assert.ok(
    webApiFile.includes("window.PAApi = {") &&
    webApiFile.includes("abortRequest,"),
    "abortRequest should be exported in PAApi object"
  );
});

test("web_api.js exports PAApi.estopPostForm", (t) => {
  const webApiPath = join(root, "data", "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Verify estopPostForm is exported
  assert.ok(
    webApiFile.includes("estopPostForm"),
    "web_api.js should export estopPostForm"
  );

  // Verify it's in the PAApi export
  assert.ok(
    webApiFile.includes("window.PAApi = {") &&
    webApiFile.includes("estopPostForm,"),
    "estopPostForm should be exported in PAApi object"
  );
});

test("estopRequest uses noRetry flag", (t) => {
  const webApiPath = join(root, "data", "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Find estopRequest function
  const estopRequestMatch = webApiFile.match(
    /const estopRequest = async \(path, opts = \{\}\) => \{[^}]*noRetry[^}]*\}/
  );

  assert.ok(
    estopRequestMatch,
    "estopRequest should use noRetry flag to prevent automatic retries"
  );
});

test("request vs estopRequest use different paths", (t) => {
  const webApiPath = join(root, "data", "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Verify request() goes through slot acquisition
  assert.ok(
    webApiFile.includes("const request = async (path, opts = {}) => {") &&
    webApiFile.includes("acquireRequestSlot()"),
    "request() should acquire slot"
  );

  // Verify estopRequest() does NOT go through slot acquisition
  const estopRequestSection = webApiFile.substring(
    webApiFile.indexOf("const estopRequest"),
    webApiFile.indexOf("const abortRequest")
  );

  assert.ok(
    !estopRequestSection.includes("acquireRequestSlot"),
    "estopRequest() should NOT acquire slot"
  );
});

test("section requests use activeSectionController, estop does not", (t) => {
  const webApiPath = join(root, "data", "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Verify _isSection parameter is used to distinguish
  assert.ok(
    webApiFile.includes("_isSection"),
    "performRequest should use _isSection parameter"
  );

  // Verify activeSectionController is set conditionally
  assert.ok(
    webApiFile.includes("if (_isSection)") &&
    webApiFile.includes("activeSectionController = controller"),
    "Only section requests should set activeSectionController"
  );
});

test("abortRequest only affects section controller, not estop", (t) => {
  const webApiPath = join(root, "data", "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Find abortRequest function
  const abortRequestStart = webApiFile.indexOf("const abortRequest = () => {");
  const abortRequestEnd = webApiFile.indexOf("};", abortRequestStart) + 2;
  const abortRequestCode = webApiFile.substring(abortRequestStart, abortRequestEnd);

  // Verify it only aborts activeSectionController
  assert.ok(
    abortRequestCode.includes("activeSectionController"),
    "abortRequest should reference activeSectionController"
  );

  // Verify it doesn't call releaseRequestSlot (which would cause double-release)
  assert.ok(
    !abortRequestCode.includes("releaseRequestSlot"),
    "abortRequest should NOT call releaseRequestSlot"
  );
});
