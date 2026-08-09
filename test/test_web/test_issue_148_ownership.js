// =============================================================================
// test/test_web/test_issue_148_ownership.js
//
// Real AbortController + real timers + proper fetch mock.
// Validates the ownership fix: only section loader requests are cancellable.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const webApiPath = join(__dirname, "../../data/web_api.js");
const webApiFile = readFileSync(webApiPath, "utf-8");

// Helper to create simple ok response
const okResponse = () => ({
  ok: true,
  status: 200,
  headers: new Map([["content-type", "application/json"]]),
  json: async () => ({ ok: true }),
  text: async () => "ok",
});

// Create test environment with REAL AbortController, REAL timers, proper fetch mock
const createTestEnv = () => {
  const window = {};

  // Track which requests got aborted
  const abortedPaths = new Set();
  const completedPaths = new Set();

  // Real fetch mock that rejects on signal abort via addEventListener
  window.fetch = (path, opts) => {
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => {
        completedPaths.add(path);
        resolve(okResponse());
      }, 200); // 200ms delay to allow abort to happen

      // Listen for abort via signal (real AbortSignal API)
      if (opts?.signal) {
        opts.signal.addEventListener("abort", () => {
          clearTimeout(t);
          abortedPaths.add(path);
          const error = new Error("aborted");
          error.name = "AbortError";
          reject(error);
        });
      }
    });
  };

  // Set up window APIs used by web_api.js
  window.setTimeout = global.setTimeout;
  window.clearTimeout = global.clearTimeout;

  global.fetch = window.fetch;
  global.window = window;

  // Initialize PABootstrap if not present
  if (!window.PABootstrap) window.PABootstrap = {};

  // Execute shipped web_api.js
  // eslint-disable-next-line no-eval
  eval(webApiFile);

  return {
    window,
    PAApi: window.PAApi,
    abortedPaths,
    completedPaths,
  };
};

// Test D1: Ordinary module request must survive when section is cancelled
test("D1: Non-section request survives when section is cancelled", async () => {
  const env = createTestEnv();

  // Ordinary module makes a request (NOT inside section loader)
  // window.PABootstrap.isSectionLoaderActive defaults to undefined, so _isSection = false
  const moduleRequest = env.PAApi.request("/api/status").catch(() => null);

  // Wait for request to be in-flight
  await new Promise((resolve) => setTimeout(resolve, 50));

  // Bootstrap tries to cancel (thinking it's cancelling a section)
  env.PAApi.abortRequest();

  // Wait for module request to complete
  await moduleRequest;

  // Module request must NOT have been aborted
  assert.equal(
    env.abortedPaths.has("/api/status"),
    false,
    "Non-section request should NOT be aborted by abortRequest()"
  );
  assert.equal(
    env.completedPaths.has("/api/status"),
    true,
    "Non-section request should complete successfully"
  );
});

// Test D1b: Section loader request IS cancelled
test("D1b: Section loader request IS cancelled by abortRequest", async () => {
  const env = createTestEnv();

  // Set flag to indicate section loader is active
  env.window.PABootstrap.isSectionLoaderActive = true;

  const sectionRequest = env.PAApi.request("/api/app").catch(() => null);

  // Wait for request to be in-flight
  await new Promise((resolve) => setTimeout(resolve, 50));

  // Bootstrap cancels the section
  env.PAApi.abortRequest();

  // Wait for request result
  await sectionRequest;

  // Section request MUST have been aborted
  assert.equal(
    env.abortedPaths.has("/api/app"),
    true,
    "Section request should be aborted by abortRequest()"
  );

  env.window.PABootstrap.isSectionLoaderActive = false;
});

// Test D1c: E-Stop is never cancellable
test("D1c: E-Stop is never cancellable by abortRequest", async () => {
  const env = createTestEnv();

  // E-Stop should never be cancellable, even if we're in a section context
  env.window.PABootstrap.isSectionLoaderActive = true;

  const estopRequest = env.PAApi.estopPostForm("/api/estop", new URLSearchParams()).catch(
    () => null
  );

  // Wait for request to be in-flight
  await new Promise((resolve) => setTimeout(resolve, 50));

  // Try to abort (should NOT affect E-Stop)
  env.PAApi.abortRequest();

  // Wait for E-Stop result
  await estopRequest;

  // E-Stop must NOT have been aborted
  assert.equal(
    env.abortedPaths.has("/api/estop"),
    false,
    "E-Stop should NEVER be aborted"
  );
  assert.equal(
    env.completedPaths.has("/api/estop"),
    true,
    "E-Stop should complete successfully"
  );

  env.window.PABootstrap.isSectionLoaderActive = false;
});
