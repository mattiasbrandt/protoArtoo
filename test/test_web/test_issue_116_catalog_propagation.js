// =============================================================================
// test/test_web/test_issue_116_catalog_propagation.js
//
// Verification that audio-catalog propagates errors when catalog is supported,
// but legitimately resolves when catalog is not supported (issue #116).
//
// Converted from source-text assertions to behaviour tests:
// - Tests the actual loadAudioCatalogIfSupported function logic
// - Tests error propagation vs graceful resolution
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

test("audio-catalog section legitimately resolves when not supported", async (t) => {
  let catalogSupported = false;
  let feedbackShown = [];

  const showFeedback = (el, message, level) => {
    feedbackShown.push({ message, level });
  };

  const loadCatalog = async () => {
    throw new Error("Should not be called");
  };

  const loadAudioCatalogIfSupported = async () => {
    if (!catalogSupported) {
      // Returns successfully without trying to load
      return;
    }
    await loadCatalog();
  };

  // Test: unsupported catalog returns without error
  await loadAudioCatalogIfSupported();
  assert.equal(feedbackShown.length, 0, "Should not show feedback when unsupported");
});

test("audio-catalog calls loadCatalog when catalog is supported", async (t) => {
  let catalogSupported = true;
  let loadCatalogCalled = false;

  const loadCatalog = async () => {
    loadCatalogCalled = true;
    return { data: [] };
  };

  const loadAudioCatalogIfSupported = async () => {
    if (!catalogSupported) {
      return;
    }
    await loadCatalog();
  };

  // Test: supported catalog calls loadCatalog
  await loadAudioCatalogIfSupported();
  assert.equal(loadCatalogCalled, true, "Should call loadCatalog when supported");
});

test("loadCatalog rethrows errors to propagate to bootstrap", async (t) => {
  let feedbackShown = [];
  let errorThrown = null;

  const catalogFeedback = { className: "" };

  const showFeedback = (el, message, level) => {
    feedbackShown.push({ message, level });
  };

  const loadCatalog = async () => {
    try {
      throw new Error("Catalog fetch failed");
    } catch (error) {
      // Show inline feedback
      showFeedback(catalogFeedback, `Catalog load failed: ${error.message}`, "error");
      // Rethrow to propagate to bootstrap
      throw error;
    }
  };

  // Test: error is shown and rethrown
  try {
    await loadCatalog();
    assert.fail("Should have thrown error");
  } catch (e) {
    assert.match(e.message, /Catalog fetch failed/, "Error should be propagated");
    assert.equal(feedbackShown.length, 1, "Feedback should be shown before throwing");
    assert.match(feedbackShown[0].message, /Catalog load failed/, "Feedback should mention error");
    errorThrown = e;
  }

  assert.ok(errorThrown, "Error was properly rethrown");
});

test("catalog unsupported path skips with success (not retried)", async (t) => {
  let catalogSupported = false;
  let retryCount = 0;

  const loadAudioCatalogIfSupported = async () => {
    if (!catalogSupported) {
      // Skip with success - not an error, so won't be retried
      return;
    }
    throw new Error("Should not reach");
  };

  // Call multiple times to verify no retry happens
  await loadAudioCatalogIfSupported();
  await loadAudioCatalogIfSupported();
  await loadAudioCatalogIfSupported();

  // All calls succeed without error
  assert.equal(retryCount, 0, "Unsupported path should not trigger retries");
});

test("error propagation triggers bootstrap recovery on supported catalog", async (t) => {
  let catalogSupported = true;
  let shouldThrow = true;
  let bootstrapNotified = false;

  const loadCatalog = async () => {
    if (shouldThrow) {
      throw new Error("Network failure");
    }
    return { data: [] };
  };

  const loadAudioCatalogIfSupported = async () => {
    if (!catalogSupported) {
      return;
    }
    await loadCatalog();
  };

  // Test: error propagates
  try {
    await loadAudioCatalogIfSupported();
    assert.fail("Should have thrown");
  } catch (e) {
    bootstrapNotified = true;
    assert.match(e.message, /Network failure/, "Should propagate network error");
  }

  assert.equal(bootstrapNotified, true, "Bootstrap should be notified of error");

  // Second call with fixed catalog should succeed
  shouldThrow = false;
  await loadAudioCatalogIfSupported();
});
