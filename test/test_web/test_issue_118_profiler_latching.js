import { test } from "node:test";
import assert from "node:assert";

// Mock fetch responses for testing profiler error handling
function createMockSetup() {
  const state = {
    profilerNotSupported: false,
    profilerCapabilityKnown: false,
    fetchAttempts: 0,
    lastFetchStatus: null,
  };

  // Simulate the refr esherProfiler logic with injectable fetch
  const refreshProfiler = async (mockFetch) => {
    if (state.profilerNotSupported) return;

    if (!state.profilerCapabilityKnown) {
      // Simulate checkProfilerCapability — assume status always has profilerSupported: true
      // so the test focuses on the fetch latch logic
      state.profilerCapabilityKnown = true;
    }

    try {
      state.fetchAttempts++;
      const resp = await mockFetch("/api/profiler");
      state.lastFetchStatus = resp.status;

      if (!resp.ok) {
        if (resp.status === 404 || resp.status === 501) {
          state.profilerNotSupported = true;
        }
        return;
      }
      // Success path (200)
      return { success: true };
    } catch (_) {
      // Network error — do not latch
    }
  };

  return { state, refreshProfiler };
}

test("#118: 404 latches profilerNotSupported (stops retries)", async (t) => {
  const { state, refreshProfiler } = createMockSetup();

  const mockFetch = async (url) => ({
    ok: false,
    status: 404,
  });

  // First call should detect 404 and latch
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.profilerNotSupported, true, "404 should latch profilerNotSupported");
  assert.strictEqual(state.fetchAttempts, 1, "Should make one fetch");

  // Second call should return early without fetching
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.fetchAttempts, 1, "Should not make second fetch after 404 latch");
});

test("#118: 501 latches profilerNotSupported (stops retries)", async (t) => {
  const { state, refreshProfiler } = createMockSetup();

  const mockFetch = async (url) => ({
    ok: false,
    status: 501,
  });

  // First call should detect 501 and latch
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.profilerNotSupported, true, "501 should latch profilerNotSupported");
  assert.strictEqual(state.fetchAttempts, 1, "Should make one fetch");

  // Second call should return early without fetching
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.fetchAttempts, 1, "Should not make second fetch after 501 latch");
});

test("#118: 503 does NOT latch (retries allowed)", async (t) => {
  const { state, refreshProfiler } = createMockSetup();

  const mockFetch = async (url) => ({
    ok: false,
    status: 503,
  });

  // First call should see 503 but NOT latch
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.profilerNotSupported, false, "503 should NOT latch profilerNotSupported");
  assert.strictEqual(state.fetchAttempts, 1, "Should make one fetch");

  // Second call should retry (not return early)
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.fetchAttempts, 2, "Should make second fetch after 503 (transient)");
});

test("#118: Network error does NOT latch (retries allowed)", async (t) => {
  const { state, refreshProfiler } = createMockSetup();

  const mockFetch = async (url) => {
    throw new Error("Network error");
  };

  // First call should catch network error but NOT latch
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.profilerNotSupported, false, "Network error should NOT latch profilerNotSupported");
  assert.strictEqual(state.fetchAttempts, 1, "Should attempt one fetch");

  // Second call should retry (not return early)
  await refreshProfiler(mockFetch);
  assert.strictEqual(state.fetchAttempts, 2, "Should retry after network error (transient)");
});

test("#118: 200 OK succeeds (profiler available)", async (t) => {
  const { state, refreshProfiler } = createMockSetup();

  const mockFetch = async (url) => ({
    ok: true,
    status: 200,
    json: async () => ({
      heapFree: 10000,
      heapMin: 5000,
      fragRatio: 0.25,
    }),
  });

  // Call should succeed
  const result = await refreshProfiler(mockFetch);
  assert.deepStrictEqual(result, { success: true }, "200 response should succeed");
  assert.strictEqual(state.profilerNotSupported, false, "Success should not latch unsupported");
});
