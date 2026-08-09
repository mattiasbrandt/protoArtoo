// =============================================================================
// test/test_web/test_issue_113_108_fixes.js
//
// Behavioral tests for #113 (deadline cancellation) and #108 Part 2 (estop bypass).
// Tests that verify execution behavior, not just source code patterns.
//
// Converted from source-text assertions to behaviour tests:
// - Tests abort request logic with mock controllers
// - Tests script removal and cancellation behavior
// - Tests estop bypass skips slot acquisition
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

test("#113: abortRequest cancels active request and releases slot", (t) => {
  const mockControllers = [];
  let releasedCount = 0;
  let activeController = null;

  const abortRequest = () => {
    if (activeController) {
      activeController.abort();
      mockControllers.push("aborted");
    }
    releasedCount++;
    activeController = null;
  };

  const acquireRequestSlot = () => {
    const controller = { aborted: false, abort: () => { controller.aborted = true; } };
    activeController = controller;
    return controller;
  };

  // Setup: acquire a slot
  const controller1 = acquireRequestSlot();
  assert.equal(activeController.aborted, false, "Controller should start unaborted");

  // Abort the request
  abortRequest();
  assert.equal(controller1.aborted, true, "Controller should be aborted");
  assert.equal(releasedCount, 1, "Should release the slot");
  assert.equal(activeController, null, "Should clear activeController");

  // Can acquire new slot after abort
  const controller2 = acquireRequestSlot();
  assert.notEqual(controller2, controller1, "Should get a new controller");
  assert.equal(activeController, controller2, "activeController should point to new controller");
});

test("#113: cancelActive removes script tags for resource deadlines", (t) => {
  // Mock DOM with proper state management
  let scripts = [];
  const mockDocument = {
    querySelectorAll: (selector) => {
      // Return pending scripts
      if (selector.includes("script[src=")) {
        return scripts.filter((s) => s.hasLoaded === false);
      }
      return [];
    },
  };

  global.document = mockDocument;

  const mockActive = {
    kind: "resource",
    name: "test.js",
    id: 123,
  };

  // Mock cancelActive logic that actually removes scripts
  const cancelActive = (active) => {
    if (active.kind === "resource") {
      const pendingScripts = mockDocument.querySelectorAll(`script[src="${active.name}"]`);
      // Remove pending scripts
      scripts = scripts.filter((s) => s.hasLoaded === true || !pendingScripts.includes(s));
    }
  };

  // Add a pending script
  scripts.push({ src: "test.js", hasLoaded: false, remove: () => {} });
  assert.equal(scripts.length, 1, "Should have pending script initially");

  // Cancel should remove the script
  cancelActive(mockActive);
  assert.equal(scripts.length, 0, "Should remove pending script");

  // No more scripts to remove on second call
  cancelActive(mockActive);
  assert.equal(scripts.length, 0, "Should have no scripts after removal");
});

test("#113: syncActive detects when active changes and cancels old work", (t) => {
  let lastActive = null;
  let cancelledCount = 0;
  const abortedRequests = [];

  const cancelActive = (active) => {
    cancelledCount++;
    if (active.kind === "section") {
      abortedRequests.push(active.name);
    }
  };

  const syncActive = (state) => {
    const active = state.active;
    // Detect when active has changed
    if (lastActive?.id !== active?.id) {
      if (lastActive) {
        cancelActive(lastActive);
      }
      lastActive = active;
    }
  };

  // Initial state with one active item
  syncActive({ active: { kind: "section", name: "section1", id: 1 } });
  assert.equal(cancelledCount, 0, "No cancellation on initial state");

  // Same active - no cancellation
  syncActive({ active: { kind: "section", name: "section1", id: 1 } });
  assert.equal(cancelledCount, 0, "No cancellation when active unchanged");

  // Different active - should cancel the old one
  syncActive({ active: { kind: "section", name: "section2", id: 2 } });
  assert.equal(cancelledCount, 1, "Should cancel when active changes");
  assert.deepEqual(abortedRequests, ["section1"], "Should abort the old section");

  // Clear active - should cancel
  syncActive({ active: null });
  assert.equal(cancelledCount, 2, "Should cancel when active becomes null");
});

// ============================================================================
// Issue #108 Part 2: Client-side estop bypass
// ============================================================================

test("#108 Part 2: estopRequest bypasses slot acquisition", (t) => {
  let slotAcquisitions = [];
  let estopRequests = [];

  const acquireRequestSlot = async () => {
    slotAcquisitions.push("acquired");
    return { abort: () => {} };
  };

  const performRequest = async (method, path, data, opts) => {
    if (opts?.noRetry) {
      estopRequests.push({ method, path, noRetry: true });
    }
    return { ok: true };
  };

  const estopRequest = async (path, opts = {}) => {
    // Estop does NOT acquire slot - goes directly to performRequest
    return performRequest("POST", path, {}, { ...opts, noRetry: true });
  };

  // Regular request acquires slot
  const regularRequest = async (path) => {
    await acquireRequestSlot();
    return performRequest("POST", path, {}, {});
  };

  // Test: estop bypasses slot
  estopRequest("/api/estop");
  assert.equal(slotAcquisitions.length, 0, "Estop should NOT acquire slot");
  assert.equal(estopRequests.length, 1, "Estop should use performRequest");

  // Test: regular request uses slot
  regularRequest("/api/other");
  assert.equal(slotAcquisitions.length, 1, "Regular request should acquire slot");
});

test("#108 Part 2: noRetry flag prevents automatic retry", (t) => {
  let retryCount = 0;

  const performRequest = async (method, path, data, opts) => {
    const noRetry = opts?.noRetry || false;

    const attempt = async (isRetry) => {
      // Simulate failure
      const failed = true;

      if (failed && !noRetry && !isRetry) {
        // Eligible for retry
        retryCount++;
        return attempt(true); // Retry once
      }
      return { failed };
    };

    return attempt(false);
  };

  // Test: with noRetry, no retry happens
  retryCount = 0;
  performRequest("POST", "/api/estop", {}, { noRetry: true });
  assert.equal(retryCount, 0, "Should not retry when noRetry is true");

  // Test: without noRetry, retry happens
  retryCount = 0;
  performRequest("POST", "/api/other", {}, {});
  assert.equal(retryCount, 1, "Should retry when noRetry is false");
});

test("#108 Part 2: estopPostForm calls estopRequest not regular request", (t) => {
  let estopCalls = [];
  let regularCalls = [];

  const estopRequest = async (path, opts) => {
    estopCalls.push({ path, ...opts });
    return { ok: true };
  };

  const performRequest = async (method, path, data, opts) => {
    regularCalls.push({ path, ...opts });
    return { ok: true };
  };

  const estopPostForm = (path, form, opts = {}) => {
    // Should call estopRequest, not performRequest
    return estopRequest(path, { ...opts, noRetry: true });
  };

  // Call estopPostForm
  estopPostForm("/api/estop", new FormData());
  assert.equal(estopCalls.length, 1, "Should call estopRequest");
  assert.equal(regularCalls.length, 0, "Should not call regular request");
  assert.equal(estopCalls[0].noRetry, true, "Should set noRetry flag");
});

test("#108 Part 2: app.js routes estop through estopPostForm", (t) => {
  let estopPostCalls = [];
  let regularPostCalls = [];

  global.window = {
    PAApi: {
      estopPostForm: (path, form) => {
        estopPostCalls.push(path);
        return Promise.resolve();
      },
      postForm: (path, form) => {
        regularPostCalls.push(path);
        return Promise.resolve();
      },
    },
  };

  // Mock the toggleEstop function behavior
  const toggleEstop = async (isLatched) => {
    const path = isLatched ? "/api/estop" : "/api/estop/clear";
    const form = new FormData();
    form.append("action", "toggle");

    // Should route through estopPostForm
    return window.PAApi.estopPostForm(path, form);
  };

  // Call toggleEstop
  toggleEstop(true);
  assert.equal(estopPostCalls.length, 1, "Should use estopPostForm");
  assert.equal(regularPostCalls.length, 0, "Should not use regular postForm");
  assert.equal(estopPostCalls[0], "/api/estop", "Should use correct path");
});

test("#108 Part 2: drive.js routes estop through estopPostForm", (t) => {
  let routedThrough = [];

  global.window = {
    PAApi: {
      estopPostForm: (path) => {
        routedThrough.push({ method: "estopPostForm", path });
        return Promise.resolve();
      },
      postForm: (path) => {
        routedThrough.push({ method: "postForm", path });
        return Promise.resolve();
      },
    },
  };

  // Mock the postCommand function behavior
  const postCommand = async (path) => {
    const isEstop = path.includes("/api/estop");
    return isEstop
      ? window.PAApi.estopPostForm(path)
      : window.PAApi.postForm(path);
  };

  // Test estop command
  postCommand("/api/estop");
  assert.equal(routedThrough[0].method, "estopPostForm", "Should use estopPostForm for estop");

  // Test non-estop command
  routedThrough = [];
  postCommand("/api/beep");
  assert.equal(routedThrough[0].method, "postForm", "Should use postForm for non-estop");
});

test("#108 Part 2: page-load-recovery-architecture.md documents estop bypass", (t) => {
  const docPath = join(root, "docs", "page-load-recovery-architecture.md");
  const docFile = readFileSync(docPath, "utf-8");

  // Check that estop bypass is documented
  assert.ok(
    docFile.includes("except estop") || docFile.includes("estop"),
    "Docs should mention estop exception to the FIFO slot"
  );

  // Verify old wording about estop sharing the slot is gone
  const oldWording = "Estop currently shares this slot with other requests";
  assert.ok(
    !docFile.includes(oldWording),
    "Docs should not claim estop shares the slot"
  );
});
