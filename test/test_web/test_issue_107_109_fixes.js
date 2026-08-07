// =============================================================================
// test/test_web/test_issue_107_109_fixes.js
//
// Behavioral and structural tests for #107 (loader error propagation) and
// #109 (component grid valid markup and incremental updates).
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
// Issue #107: Section loader error propagation
// ============================================================================

test("#107: All three loaders propagate errors to bootstrap (not catching)", (t) => {
  const appPath = join(dataDir, "app.js");
  const appFile = readFileSync(appPath, "utf-8");

  // Pattern: async function with try/await but no catch block that swallows errors
  const loadRecentLogsMatch = appFile.match(
    /const loadRecentLogs = async \(\) => \{[\s\S]*?if \(!window\.PAApi.*?\) throw[\s\S]*?const result = await[\s\S]*?setLogLines/m
  );
  assert.ok(
    loadRecentLogsMatch,
    "loadRecentLogs should throw on API unavailable and propagate fetch errors"
  );

  const loadLogLevelMatch = appFile.match(
    /const loadLogLevel = async \(\) => \{[\s\S]*?if \(!window\.PAApi.*?\) throw[\s\S]*?const result = await[\s\S]*?renderLogLevelPill/m
  );
  assert.ok(
    loadLogLevelMatch,
    "loadLogLevel should throw on API unavailable and propagate fetch errors"
  );

  const loadCommandTokensMatch = appFile.match(
    /const loadCommandTokens = async \(\) => \{[\s\S]*?if \(!window\.PAApi.*?\) throw[\s\S]*?const result = await[\s\S]*?commandTokens =/m
  );
  assert.ok(
    loadCommandTokensMatch,
    "loadCommandTokens should throw on API unavailable and propagate fetch errors"
  );
});

test("#107: Loaders have no try-catch blocks swallowing errors", (t) => {
  const appPath = join(dataDir, "app.js");
  const appFile = readFileSync(appPath, "utf-8");

  // Extract each loader function
  const loadRecentLogsStart = appFile.indexOf("const loadRecentLogs = async () => {");
  const loadRecentLogsEnd = appFile.indexOf("\n  const LOG_LEVELS", loadRecentLogsStart);
  const loadRecentLogsCode = appFile.substring(loadRecentLogsStart, loadRecentLogsEnd);

  // Should not have try-catch wrapping the entire logic
  const hasCatchSwallow = loadRecentLogsCode.includes("} catch (error)");
  assert.strictEqual(
    hasCatchSwallow,
    false,
    "loadRecentLogs should not catch and swallow errors"
  );

  const loadLogLevelStart = appFile.indexOf("const loadLogLevel = async () => {");
  const loadLogLevelEnd = appFile.indexOf("\n  const cycleLogLevel", loadLogLevelStart);
  const loadLogLevelCode = appFile.substring(loadLogLevelStart, loadLogLevelEnd);

  assert.strictEqual(
    loadLogLevelCode.includes("} catch (error)"),
    false,
    "loadLogLevel should not catch and swallow errors"
  );

  const loadCommandTokensStart = appFile.indexOf("const loadCommandTokens = async () => {");
  const loadCommandTokensEnd = appFile.indexOf("\n  const appendCommandLine", loadCommandTokensStart);
  const loadCommandTokensCode = appFile.substring(loadCommandTokensStart, loadCommandTokensEnd);

  assert.strictEqual(
    loadCommandTokensCode.includes("} catch ("),
    false,
    "loadCommandTokens should not have try-catch wrapping errors"
  );
});

// ============================================================================
// Issue #109: Component grid markup validity and incremental updates
// ============================================================================

test("#109: All served pages have valid dt/dd markup (wrapped in dl)", (t) => {
  const htmlFiles = ["index.html", "drive.html", "setup.html", "wifi.html"];

  for (const filename of htmlFiles) {
    const filePath = join(dataDir, filename);
    const html = readFileSync(filePath, "utf-8");

    // Check for dt/dd elements
    const hasDt = html.includes("<dt>");
    const hasDd = html.includes("<dd>");

    if (!hasDt && !hasDd) {
      continue; // File has no dt/dd, which is fine
    }

    // If it has dt/dd, all must be inside dl tags
    // Count dt/dd pairs
    const dtMatches = html.match(/<dt>/g) || [];
    const dlMatches = html.match(/<dl[^>]*>/g) || [];

    assert.ok(
      dlMatches.length > 0,
      `${filename}: dt/dd elements found but no <dl> wrapper — markup is invalid`
    );

    // Simple check: each dt should appear after the most recent <dl> and before any closing </dl>
    // More sophisticated: we'd parse HTML properly, but this catches the obvious case
    const dlRegex = /<dl[^>]*>[\s\S]*?<\/dl>/g;
    const dlSections = html.match(dlRegex) || [];

    // Every dt must be in some dl section
    for (const dtTag of dtMatches) {
      const hasValidContext = dlSections.some((dl) => dl.includes("<dt>"));
      // This is a simplified check; a full parser would be more thorough
    }

    // More direct check: look for dt outside dl
    const withoutDl = html.replace(/<dl[^>]*>[\s\S]*?<\/dl>/g, "");
    assert.strictEqual(
      withoutDl.includes("<dt>"),
      false,
      `${filename}: found <dt> outside <dl> — invalid markup`
    );
    assert.strictEqual(
      withoutDl.includes("<dd>"),
      false,
      `${filename}: found <dd> outside <dl> — invalid markup`
    );
  }
});

test("#109: App renderComponentStatus wraps grid in <dl>", (t) => {
  const appPath = join(dataDir, "app.js");
  const appFile = readFileSync(appPath, "utf-8");

  const renderStart = appFile.indexOf("const renderComponentStatus = (payload) => {");
  const renderEnd = appFile.indexOf("\n  const renderOpMode", renderStart);
  const renderCode = appFile.substring(renderStart, renderEnd);

  // Check that it creates <dl> wrapper
  assert.ok(
    renderCode.includes('<dl class="status-grid">'),
    "renderComponentStatus should wrap grid in <dl class=\"status-grid\">"
  );

  // Check that dt/dd are still used (not replaced with divs)
  assert.ok(
    renderCode.includes("<dt>"),
    "renderComponentStatus should use <dt> elements"
  );
  assert.ok(
    renderCode.includes("<dd"),
    "renderComponentStatus should use <dd> elements"
  );
});

test("#109: Incremental update signature is implemented", (t) => {
  const appPath = join(dataDir, "app.js");
  const appFile = readFileSync(appPath, "utf-8");

  // Check for renderedComponentIds signature tracking
  assert.ok(
    appFile.includes("let renderedComponentIds = null;"),
    "Should have renderedComponentIds signature tracking variable"
  );

  const renderStart = appFile.indexOf("const renderComponentStatus = (payload) => {");
  const renderEnd = appFile.indexOf("\n  const renderOpMode", renderStart);
  const renderCode = appFile.substring(renderStart, renderEnd);

  // Check for signature building that includes component IDs and transport flags
  assert.ok(
    renderCode.includes("const signature = "),
    "Should build a signature for rebuild decision"
  );

  assert.ok(
    renderCode.includes("transportFlags"),
    "Signature should include transport line flags"
  );

  assert.ok(
    renderCode.includes("dome_link"),
    "Signature should check dome_link state"
  );

  assert.ok(
    renderCode.includes("rx_status"),
    "Signature should check sound rx_status"
  );

  // Check for rebuild vs patch logic
  assert.ok(
    renderCode.includes("signature !== renderedComponentIds"),
    "Should compare signatures to decide rebuild vs patch"
  );

  assert.ok(
    renderCode.includes("innerHTML = `<dl"),
    "Should only rebuild innerHTML when signature changes"
  );

  assert.ok(
    renderCode.includes(".textContent = safeState"),
    "Should patch textContent for incremental updates"
  );

  assert.ok(
    renderCode.includes(".textContent = safeDetail"),
    "Should patch detail text for incremental updates"
  );
});

test("#109: Dynamic grid gets stable IDs for patching", (t) => {
  const appPath = join(dataDir, "app.js");
  const appFile = readFileSync(appPath, "utf-8");

  const renderStart = appFile.indexOf("const renderComponentStatus = (payload) => {");
  const renderEnd = appFile.indexOf("\n  const renderOpMode", renderStart);
  const renderCode = appFile.substring(renderStart, renderEnd);

  // Check that elements get stable IDs
  assert.ok(
    renderCode.includes('id="comp-'),
    "Status items should have stable IDs for patching (comp-KEY)"
  );

  assert.ok(
    renderCode.includes('id="state-'),
    "State elements should have stable IDs (state-KEY)"
  );

  assert.ok(
    renderCode.includes('id="detail-'),
    "Detail elements should have stable IDs (detail-KEY)"
  );

  // Check that document.getElementById is used to patch
  assert.ok(
    renderCode.includes("document.getElementById(`state-"),
    "Should use getElementById to patch state element"
  );

  assert.ok(
    renderCode.includes("document.getElementById(`detail-"),
    "Should use getElementById to patch detail element"
  );
});
