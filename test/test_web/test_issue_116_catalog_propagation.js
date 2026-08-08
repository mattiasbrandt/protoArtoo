// =============================================================================
// test/test_web/test_issue_116_catalog_propagation.js
//
// Verification that audio-catalog propagates errors when catalog is supported,
// but legitimately resolves when catalog is not supported (issue #116).
//
// This test proves both paths of loadAudioCatalogIfSupported:
// 1. When catalogSupported=false: returns successfully (not retried)
// 2. When catalogSupported=true: propagates fetch errors
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const soundPath = join(__dirname, "../../data/sound.js");
const soundFile = readFileSync(soundPath, "utf-8");

test("audio-catalog section legitimately resolves when not supported", (t) => {
  // When catalogSupported is false, loadAudioCatalogIfSupported returns early
  // without throwing. The harness will show this as RESOLVED, which is correct
  // behavior - we're not retrying a capability that doesn't exist.
  assert(
    soundFile.includes("if (!catalogSupported)"),
    "capability guard must check catalogSupported"
  );
  assert(
    soundFile.includes("Catalog not supported; skip with success"),
    "must explain that unsupported catalog skips intentionally"
  );
});

test("audio-catalog calls loadCatalog when catalog is supported", (t) => {
  // When catalogSupported is true, the section awaits loadCatalog(),
  // which will propagate any errors from the fetch.
  const loadCatalogCall = soundFile.includes("await loadCatalog()");
  assert(
    loadCatalogCall,
    "supported path must call loadCatalog()"
  );
});

test("loadCatalog rethrows errors to propagate to bootstrap", (t) => {
  // The loadCatalog function must rethrow after showing inline feedback
  // so errors propagate to the bootstrap for recovery.
  assert(
    soundFile.includes("showFeedback(catalogFeedback, `Catalog load failed:"),
    "must show inline feedback on failure"
  );

  // Find the throw statement in loadCatalog's catch block
  const catchBlockIdx = soundFile.indexOf("showFeedback(catalogFeedback, `Catalog load failed:");
  const afterFeedback = soundFile.substring(catchBlockIdx, catchBlockIdx + 300);
  assert(
    afterFeedback.includes("throw error;"),
    "loadCatalog must rethrow error after showing feedback"
  );
});

test("12000ms deadline is preserved on audio-catalog section", (t) => {
  // The catalog operation can take longer (ADR 0019 - two deadline categories).
  // The 12000ms deadline must be preserved in the section registration.
  assert(
    soundFile.includes('["audio-catalog", loadAudioCatalogIfSupported, "audio catalog", { deadlineMs: 12000 }]'),
    "audio-catalog must keep its 12000ms deadline"
  );
});
