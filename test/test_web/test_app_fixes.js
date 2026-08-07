// =============================================================================
// test/test_web/test_app_fixes.js
//
// Tests for issue #107 (section loader error propagation) and #109 (component
// status grid markup and incremental updates).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const appPath = join(__dirname, "../../data/app.js");
const appFile = readFileSync(appPath, "utf-8");

// Test 1: Verify app.js has dt/dd in renderComponentStatus (the issue to fix)
test("#109: Current app.js uses invalid dt/dd markup in renderComponentStatus", () => {
  const renderStart = appFile.indexOf("const renderComponentStatus = (payload) => {");
  const renderEnd = appFile.indexOf("\n  const renderOpMode", renderStart);
  const renderCode = appFile.substring(renderStart, renderEnd);

  // Check for the problematic pattern: <dt> and <dd> inside divs
  const hasDtTag = renderCode.includes("<dt>");
  const hasDdTag = renderCode.includes("<dd>");
  const hasDivWrapper = renderCode.includes("<div class=\"status-item\">");

  // The bug is: dt/dd inside div (invalid)
  assert.ok(
    hasDtTag && hasDdTag && hasDivWrapper,
    "Current code should have dt/dd inside status-item divs (the bug we're fixing)"
  );
});

// Test 2: Verify loadCommandTokens currently catches errors (the issue to fix)
test("#107: Current loadCommandTokens catches errors instead of propagating", () => {
  const extractStart = appFile.indexOf("const loadCommandTokens = async () => {");
  const extractEnd = appFile.indexOf("\n  const appendCommandLine", extractStart);
  const functionCode = appFile.substring(extractStart, extractEnd);

  // Check for catch block that swallows errors
  const hasCatchBlock = functionCode.includes(".catch");
  const assignsEmptyArray = functionCode.includes("commandTokens = []");

  assert.ok(
    hasCatchBlock && assignsEmptyArray,
    "Current loadCommandTokens should have a catch block that assigns empty array (the bug)"
  );
});

// Test 3: Verify loadRecentLogs currently catches errors
test("#107: Current loadRecentLogs catches errors instead of propagating", () => {
  const extractStart = appFile.indexOf("const loadRecentLogs = async () => {");
  const extractEnd = appFile.indexOf("\n  const LOG_LEVELS", extractStart);
  const functionCode = appFile.substring(extractStart, extractEnd);

  // Check for catch block
  const hasCatchBlock = functionCode.includes(".catch");

  assert.ok(
    hasCatchBlock,
    "Current loadRecentLogs should have a catch block (the bug)"
  );
});

// Test 4: Verify loadLogLevel currently catches errors
test("#107: Current loadLogLevel catches errors instead of propagating", () => {
  const extractStart = appFile.indexOf("const loadLogLevel = async () => {");
  const extractEnd = appFile.indexOf("\n  const cycleLogLevel", extractStart);
  const functionCode = appFile.substring(extractStart, extractEnd);

  // Check for catch block
  const hasCatchBlock = functionCode.includes(".catch");

  assert.ok(
    hasCatchBlock,
    "Current loadLogLevel should have a catch block (the bug)"
  );
});
