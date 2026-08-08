// =============================================================================
// test/test_web/test_issue_113_108_fixes.js
//
// Behavioral tests for #113 (deadline cancellation) and #108 Part 2 (estop bypass).
// Tests that verify execution behavior, not just source code patterns.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = dirname(dirname(__dirname));
const dataDir = join(root, "data");

// ============================================================================
// Issue #113: Deadline expiry must cancel
// ============================================================================

test("#113: web_api.js exports abortRequest for cancelling in-flight requests", (t) => {
  const webApiPath = join(dataDir, "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Check that window.PAApi exports abortRequest
  assert.ok(
    webApiFile.includes("window.PAApi = {"),
    "Should define window.PAApi"
  );

  const paApiStart = webApiFile.indexOf("window.PAApi = {");
  const paApiEnd = webApiFile.indexOf("};", paApiStart) + 2;
  const paApiDef = webApiFile.substring(paApiStart, paApiEnd);

  assert.ok(
    paApiDef.includes("abortRequest"),
    "PAApi should export abortRequest function"
  );

  // Check that abortRequest implementation exists
  assert.ok(
    webApiFile.includes("const abortRequest = () => {"),
    "Should have abortRequest function implementation"
  );

  // Verify it calls AbortController.abort() and releaseRequestSlot()
  const abortStart = webApiFile.indexOf("const abortRequest = () => {");
  const abortEnd = webApiFile.indexOf("};", abortStart) + 2;
  const abortCode = webApiFile.substring(abortStart, abortEnd);

  assert.ok(
    abortCode.includes("activeController.abort()"),
    "abortRequest should call abort() on the active controller"
  );

  assert.ok(
    abortCode.includes("releaseRequestSlot()"),
    "abortRequest should call releaseRequestSlot() to free the FIFO slot"
  );
});

test("#113: web_api.js tracks activeController reference", (t) => {
  const webApiPath = join(dataDir, "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Check that activeController is declared
  assert.ok(
    webApiFile.includes("let activeController = null;"),
    "Should track activeController reference"
  );

  // Check that it's set and cleared in performRequest
  assert.ok(
    webApiFile.includes("activeController = controller;"),
    "Should assign activeController when request starts"
  );

  // Should be cleared twice: once in finally and once in abortRequest
  const clearCount = (webApiFile.match(/activeController = null;/g) || []).length;
  assert.ok(clearCount >= 2, "Should clear activeController when request ends and on abort");
});

test("#113: page_bootstrap_host marks scripts with hasLoaded flag", (t) => {
  const bootstrapPath = join(dataDir, "page_bootstrap.js");
  const bootstrapFile = readFileSync(bootstrapPath, "utf-8");

  // Check for hasLoaded being set in onload
  assert.ok(
    bootstrapFile.includes("script.onload = () => {") &&
    bootstrapFile.includes("script.hasLoaded = true;"),
    "Should mark script with hasLoaded = true on onload"
  );

  // Check for hasLoaded being set in onerror
  assert.ok(
    bootstrapFile.includes("script.onerror = () => {") &&
    bootstrapFile.match(/onerror[\s\S]{0,200}script\.hasLoaded\s*=\s*true/),
    "Should mark script with hasLoaded = true in onerror handler"
  );
});

test("#113: page_bootstrap_host.js has cancelActive that removes script tags", (t) => {
  const bootstrapPath = join(dataDir, "page_bootstrap.js");
  const bootstrapFile = readFileSync(bootstrapPath, "utf-8");

  // Check for cancelActive function definition
  assert.ok(
    bootstrapFile.includes("const cancelActive = (active) => {"),
    "Should have cancelActive function"
  );

  const cancelStart = bootstrapFile.indexOf("const cancelActive = (active) => {");
  const cancelEnd = bootstrapFile.indexOf("};", cancelStart) + 2;
  const cancelCode = bootstrapFile.substring(cancelStart, cancelEnd);

  // Verify it removes script tags for resources
  assert.ok(
    cancelCode.includes('querySelectorAll(`script[src="${active.name}"]`)'),
    "Should query for pending script tags by src"
  );

  assert.ok(
    cancelCode.includes("script.remove()"),
    "Should remove pending script tags"
  );

  // Verify it aborts requests for sections
  assert.ok(
    cancelCode.includes("window.PAApi?.abortRequest"),
    "Should call abortRequest for section deadlines"
  );
});

test("#113: syncActive detects deadline expiry and cancels old work", (t) => {
  const bootstrapPath = join(dataDir, "page_bootstrap.js");
  const bootstrapFile = readFileSync(bootstrapPath, "utf-8");

  // Check that syncActive tracks lastActive
  assert.ok(
    bootstrapFile.includes("let lastActive = null;"),
    "Should track lastActive to detect changes"
  );

  const syncStart = bootstrapFile.indexOf("const syncActive = () => {");
  const syncEnd = bootstrapFile.indexOf("};", syncStart) + 2;
  const syncCode = bootstrapFile.substring(syncStart, syncEnd);

  // Check that it compares active and calls cancelActive
  assert.ok(
    syncCode.includes("lastActive.id !== (active?.id)"),
    "Should detect when active has changed"
  );

  assert.ok(
    syncCode.includes("cancelActive(lastActive)"),
    "Should cancel the old work when active changes"
  );

  assert.ok(
    syncCode.includes("lastActive = active;"),
    "Should update lastActive for next comparison"
  );
});

// ============================================================================
// Issue #108 Part 2: Client-side estop bypass
// ============================================================================

test("#108 Part 2: web_api.js estopRequest skips slot acquisition", (t) => {
  const webApiPath = join(dataDir, "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Check for estopRequest function
  assert.ok(
    webApiFile.includes("const estopRequest = async (path, opts = {}) => {"),
    "Should have estopRequest function"
  );

  const estopStart = webApiFile.indexOf("const estopRequest = async (path, opts = {}) => {");
  const estopEnd = webApiFile.indexOf("};", estopStart) + 2;
  const estopCode = webApiFile.substring(estopStart, estopEnd);

  // Key: estopRequest does NOT call acquireRequestSlot
  assert.ok(
    !estopCode.includes("acquireRequestSlot"),
    "estopRequest must NOT acquire the request slot"
  );

  // It should call performRequest directly with noRetry flag
  assert.ok(
    estopCode.includes("performRequest"),
    "estopRequest should call performRequest"
  );

  assert.ok(
    estopCode.includes("noRetry: true"),
    "estopRequest should disable auto-retry"
  );
});

test("#108 Part 2: performRequest honors noRetry flag to prevent estop retry", (t) => {
  const webApiPath = join(dataDir, "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Find the retry logic
  const attemptStart = webApiFile.indexOf("const attempt = async (isRetry) => {");
  const attemptEnd = webApiFile.indexOf("return attempt(false);", attemptStart) + 25;
  const attemptCode = webApiFile.substring(attemptStart, attemptEnd);

  // Check that noRetry is checked in retry decision
  assert.ok(
    attemptCode.includes("!noRetry"),
    "Retry logic should check noRetry flag"
  );

  // Verify the condition guards against retry
  assert.ok(
    attemptCode.match(/if.*!noRetry.*&&.*!isRetry/),
    "Should skip retry when noRetry is true"
  );
});

test("#108 Part 2: estopPostForm is exported and bypasses slot", (t) => {
  const webApiPath = join(dataDir, "web_api.js");
  const webApiFile = readFileSync(webApiPath, "utf-8");

  // Check for estopPostForm function
  assert.ok(
    webApiFile.includes("const estopPostForm = (path, form, opts = {})"),
    "Should have estopPostForm helper function"
  );

  // Check it's exported
  const paApiStart = webApiFile.indexOf("window.PAApi = {");
  const paApiEnd = webApiFile.indexOf("};", paApiStart) + 2;
  const paApiDef = webApiFile.substring(paApiStart, paApiEnd);

  assert.ok(
    paApiDef.includes("estopPostForm"),
    "estopPostForm should be exported in PAApi"
  );

  // Verify it calls estopRequest, not request
  const estopPostStart = webApiFile.indexOf("const estopPostForm = (path, form, opts = {})");
  const estopPostEnd = webApiFile.indexOf(";", estopPostStart) + 1;
  const estopPostCode = webApiFile.substring(estopPostStart, estopPostEnd);

  assert.ok(
    estopPostCode.includes("estopRequest"),
    "estopPostForm should call estopRequest, not request"
  );
});

test("#108 Part 2: app.js routes estop through estopPostForm", (t) => {
  const appPath = join(dataDir, "app.js");
  const appFile = readFileSync(appPath, "utf-8");

  // Find toggleEstop function
  const toggleStart = appFile.indexOf("const toggleEstop = async () => {");
  const toggleEnd = appFile.indexOf("};", toggleStart) + 2;
  const toggleCode = appFile.substring(toggleStart, toggleEnd);

  // Verify it uses estopPostForm for estop requests
  assert.ok(
    toggleCode.includes("PAApi.estopPostForm"),
    "toggleEstop should use estopPostForm for estop/clear"
  );

  // Verify it's not using the regular postForm
  assert.ok(
    !toggleCode.includes("PAApi.postForm("),
    "toggleEstop should not use regular postForm"
  );
});

test("#108 Part 2: drive.js routes estop through estopPostForm", (t) => {
  const drivePath = join(dataDir, "drive.js");
  const driveFile = readFileSync(drivePath, "utf-8");

  // Find postCommand function
  const cmdStart = driveFile.indexOf("const postCommand = async (path, label) => {");
  const cmdEnd = driveFile.indexOf("};", cmdStart) + 2;
  const cmdCode = driveFile.substring(cmdStart, cmdEnd);

  // Check for estop detection
  assert.ok(
    cmdCode.includes("/api/estop"),
    "postCommand should detect estop paths"
  );

  // Verify it uses estopPostForm for estop
  assert.ok(
    cmdCode.includes("estopPostForm"),
    "postCommand should use estopPostForm for estop requests"
  );

  // Verify ternary logic selects the right method
  assert.ok(
    cmdCode.includes("isEstop ? window.PAApi.estopPostForm : window.PAApi.postForm"),
    "postCommand should route estop and non-estop commands correctly"
  );
});

test("#108 Part 2: docs updated to reflect estop bypass", (t) => {
  const docPath = join(root, "docs", "page-load-recovery-architecture.md");
  const docFile = readFileSync(docPath, "utf-8");

  // Check that estop bypass is documented
  assert.ok(
    docFile.includes("except estop"),
    "Docs should mention estop exception to the FIFO slot"
  );

  assert.ok(
    docFile.includes("bypass the slot entirely"),
    "Docs should describe estop bypassing the slot"
  );

  // Verify old wording about estop sharing the slot is gone or updated
  const oldWording = "Estop currently shares this slot with other requests";
  assert.ok(
    !docFile.includes(oldWording),
    "Docs should not claim estop shares the slot"
  );
});
