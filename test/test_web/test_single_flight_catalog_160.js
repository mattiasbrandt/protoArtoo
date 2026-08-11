// =============================================================================
// test/test_web/test_single_flight_catalog_160.js
//
// Behavioral tests for single-flight catalog promise fix (#160).
// Verifies that when loadCatalog is called while a fetch is in flight,
// the second caller gets the in-flight PROMISE instead of a stale boolean,
// ensuring failures propagate to the section loader.
//
// Pattern: Coordinator-written and measured (ref_handle_test.js).
// Kills the "returns stale boolean instead of in-flight promise" mutation.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

const AUDIO_STATUS_PAYLOAD = {
  driver: "I2S-simple",
  link_ok: true,
  device: "example",
  play_state: "idle",
  total_tracks: 10,
  current_track: 0,
  capabilities: 0x20, // AUDIO_CAP_CATALOG = 0x20
};

const AUDIO_CATALOG_PAYLOAD = {
  ready: true,
  banks: [{ bank: 0, page: 0 }],
  entries: [{ bank: 0, category: "status", displayName: "Status", position: 0 }],
};

test("single-flight: second caller joins the in-flight promise and waits for real outcome", async () => {
  // This test simulates the race: pre-load starts first (no handle),
  // then section joins while fetch is in flight (with handle).
  // Both should await the SAME promise, and failures should propagate.

  let requestInProgress = false;
  let resolveRequest = null;
  let rejectRequest = null;

  const env = loadPageModule("sound.js", {
    respond: (path) => {
      if (path === "/api/audio") return { data: AUDIO_STATUS_PAYLOAD };
      if (path === "/api/audio/catalog") {
        // Simulate a slow request: don't return immediately.
        // This gives us time to call loadCatalog a second time while
        // the first is still in flight.
        requestInProgress = true;
        return new Promise((resolve, reject) => {
          resolveRequest = () => {
            requestInProgress = false;
            resolve({ data: AUDIO_CATALOG_PAYLOAD });
          };
          rejectRequest = () => {
            requestInProgress = false;
            reject(new Error("Catalog fetch failed"));
          };
        });
      }
      return { data: {} };
    },
  });

  // First, establish that catalog is supported by running audio-status
  const audioStatusHandle = {
    get: (path) => {
      if (path === "/api/audio") return Promise.resolve({ data: AUDIO_STATUS_PAYLOAD });
      return Promise.resolve({ data: {} });
    },
    postForm: () => Promise.resolve({ data: {} }),
    postJson: () => Promise.resolve({ data: {} }),
    estopPostForm: () => Promise.resolve({ data: {} }),
  };
  await env.runSection("audio-status", { handle: audioStatusHandle });

  // Now test the race: start a pre-load (no handle)
  const preLoadPromise = env.runSection("audio-catalog");

  // The pre-load should have started the fetch but not completed it
  assert.strictEqual(requestInProgress, true, "request should be in progress after pre-load starts");

  // Create a handle to record what the section caller does
  const sectionCalls = [];
  const sectionHandle = {
    get: (path) => {
      sectionCalls.push({ method: "GET", path });
      // Don't actually fetch; return the pending request
      return new Promise((resolve, reject) => {
        // We'll resolve this when the pre-load's request resolves
        const checkDone = setInterval(() => {
          if (!requestInProgress) {
            clearInterval(checkDone);
            resolve({ data: AUDIO_CATALOG_PAYLOAD });
          }
        }, 10);
      });
    },
    postForm: () => Promise.resolve({ data: {} }),
    postJson: () => Promise.resolve({ data: {} }),
    estopPostForm: () => Promise.resolve({ data: {} }),
  };

  // While the pre-load is in flight, have the section also call loadCatalog
  // by running the section. This should get the in-flight promise, not a stale value.
  const sectionPromise = env.runSection("audio-catalog", { handle: sectionHandle });

  // At this point, exactly ONE request should be in flight (not two)
  // Verify by resolving the mock request
  resolveRequest();
  await preLoadPromise;
  await sectionPromise;

  // No additional request should have been made (they shared the in-flight promise)
  assert.deepStrictEqual(
    sectionCalls.map((c) => c.path),
    [],
    "section should not issue a new request; it should have joined the pre-load's in-flight promise"
  );
});

test("single-flight: when the in-flight fetch fails, the joiner gets the rejection", async () => {
  // This is the critical behavioral test: when the shared fetch fails,
  // both the pre-load and the section must see the failure.

  let requestInProgress = false;
  let resolveRequest = null;
  let rejectRequest = null;
  let requestCount = 0;

  const env = loadPageModule("sound.js", {
    respond: (path) => {
      if (path === "/api/audio") return { data: AUDIO_STATUS_PAYLOAD };
      if (path === "/api/audio/catalog") {
        requestCount += 1;
        requestInProgress = true;
        return new Promise((resolve, reject) => {
          resolveRequest = () => {
            requestInProgress = false;
            resolve({ data: AUDIO_CATALOG_PAYLOAD });
          };
          rejectRequest = () => {
            requestInProgress = false;
            reject(new Error("Catalog fetch failed"));
          };
        });
      }
      return { data: {} };
    },
  });

  // Establish catalog support
  const audioStatusHandle = {
    get: (path) => {
      if (path === "/api/audio") return Promise.resolve({ data: AUDIO_STATUS_PAYLOAD });
      return Promise.resolve({ data: {} });
    },
    postForm: () => Promise.resolve({ data: {} }),
    postJson: () => Promise.resolve({ data: {} }),
    estopPostForm: () => Promise.resolve({ data: {} }),
  };
  await env.runSection("audio-status", { handle: audioStatusHandle });

  // Start pre-load (no handle, will swallow its rejection via .catch(() => {}))
  const preLoadPromise = env.runSection("audio-catalog").catch(() => {
    // Pre-load rejection is swallowed (as per line 325 in sound.js)
  });

  // Wait for request to start
  await new Promise(resolve => setTimeout(resolve, 50));
  assert.strictEqual(requestInProgress, true, "request should be in progress");

  // Section caller joins the same fetch (with handle)
  const sectionHandle = {
    get: (path) => Promise.reject(new Error("Should not be called")),
    postForm: () => Promise.resolve({ data: {} }),
    postJson: () => Promise.resolve({ data: {} }),
    estopPostForm: () => Promise.resolve({ data: {} }),
  };

  const sectionPromise = env.runSection("audio-catalog", { handle: sectionHandle });

  // Reject the shared in-flight request
  rejectRequest();

  // Pre-load should complete (its rejection is caught)
  await preLoadPromise;

  // Section should also fail (it joined the same promise)
  let sectionSawFailure = false;
  try {
    await sectionPromise;
  } catch (err) {
    sectionSawFailure = true;
  }

  assert.strictEqual(sectionSawFailure, true, "section should see the failure from the joined promise");
  assert.strictEqual(requestCount, 1, "exactly one /api/audio/catalog request should have been issued");
});
