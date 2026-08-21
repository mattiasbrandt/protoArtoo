// =============================================================================
// test/test_web/test_handle_coverage_154.js
//
// Behavioral tests for handle-based request routing (#154).
// Proves that migrated loaders in drive.js, servo.js, and sound.js route
// their section loads THROUGH the supplied Section Request Handle and do not
// bypass it to PAApi.
//
// Pattern: Coordinator-written and measured (ref_handle_test.js).
// Kills the "loader ignores the handle" mutation.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// Test payloads for each module
const CONFIG_PAYLOAD = {
  arm1OpenUs: 2000, arm1CloseUs: 1000, arm2OpenUs: 2000, arm2CloseUs: 1000,
  aux1OpenUs: 2000, aux1CloseUs: 1000, aux2OpenUs: 2000, aux2CloseUs: 1000,
  aux3OpenUs: 2000, aux3CloseUs: 1000,
};

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

// A stand-in for the bootstrap's handle: records what was asked of it, so the
// test can assert the loader went through it rather than around it.
const makeRecordingHandle = (payload) => {
  const calls = [];
  const record = (method) => (path, ...rest) => {
    calls.push({ method, path });
    return Promise.resolve({ data: payload });
  };
  return {
    calls,
    handle: {
      get: record("GET"),
      postForm: record("POST"),
      postJson: record("POST"),
      estopPostForm: record("POST"),
    },
  };
};

// =============================================================================
// Drive Page Tests
// =============================================================================

test("drive-configuration loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("drive.js", {
    respond: () => ({ data: CONFIG_PAYLOAD }),
  });
  const { calls, handle } = makeRecordingHandle(CONFIG_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("drive-configuration", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/config"],
    "the loader must issue GET /api/config through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(directBefore),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});

// =============================================================================
// Servo Page Tests
// =============================================================================

test("servo-calibration loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("servo.js", {
    respond: () => ({ data: CONFIG_PAYLOAD }),
  });
  const { calls, handle } = makeRecordingHandle(CONFIG_PAYLOAD);

  // Loading the module issues its own traffic (a status fetch); only requests
  // made from this point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("servo-calibration", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/config"],
    "the loader must issue GET /api/config through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(directBefore),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});

// =============================================================================
// Sound Page Tests
// =============================================================================

test("audio-status loader issues its request through the handle, not PAApi", async () => {
  // Use a payload WITHOUT catalog support (capabilities: 0) to avoid triggering
  // the automatic catalog load from applyCapabilityUI. This test focuses on the
  // audio-status section loader itself, not the auto-load side effect.
  const audioStatusPayloadNoCatalog = {
    driver: "I2S-simple",
    link_ok: true,
    device: "example",
    play_state: "idle",
    total_tracks: 10,
    current_track: 0,
    capabilities: 0, // No catalog capability
  };

  const env = loadPageModule("sound.js", {
    respond: () => ({ data: audioStatusPayloadNoCatalog }),
  });
  const { calls, handle } = makeRecordingHandle(audioStatusPayloadNoCatalog);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("audio-status", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/audio"],
    "the loader must issue GET /api/audio through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(directBefore),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});

test("audio-catalog loader issues its request through the handle, not PAApi", async () => {
  // For this test, we need to:
  // 1. Load sound.js with audio-status that INCLUDES catalog capability
  //    so catalogSupported gets set, but use a mock that doesn't trigger auto-load
  // 2. Run audio-status first (mocking only /api/audio)
  // 3. Then run audio-catalog (mocking only /api/audio/catalog)

  const audioStatusWithCatalogCap = {
    driver: "I2S-simple",
    link_ok: true,
    device: "example",
    play_state: "idle",
    total_tracks: 10,
    current_track: 0,
    capabilities: 0x20, // AUDIO_CAP_CATALOG
  };

  const env = loadPageModule("sound.js", {
    respond: (path) => {
      if (path === "/api/audio") return { data: audioStatusWithCatalogCap };
      if (path === "/api/audio/catalog") return { data: AUDIO_CATALOG_PAYLOAD };
      return { data: {} };
    },
  });

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  // First, run audio-status with the handle to establish catalogSupported.
  // This will trigger auto-load, which we need to capture.
  const audioStatusHandle = makeRecordingHandle(audioStatusWithCatalogCap).handle;
  await env.runSection("audio-status", { handle: audioStatusHandle });

  // Now run audio-catalog test: create fresh handle for catalog-specific test
  const catalogCalls = [];
  const catalogRecordHandle = {
    get: (path, ...rest) => {
      catalogCalls.push({ method: "GET", path });
      return Promise.resolve({ data: AUDIO_CATALOG_PAYLOAD });
    },
    postForm: (path, ...rest) => {
      catalogCalls.push({ method: "POST", path });
      return Promise.resolve({ data: {} });
    },
    postJson: (path, ...rest) => {
      catalogCalls.push({ method: "POST", path });
      return Promise.resolve({ data: {} });
    },
    estopPostForm: (path, ...rest) => {
      catalogCalls.push({ method: "POST", path });
      return Promise.resolve({ data: {} });
    },
  };

  // Snapshot requests before running the catalog section
  const beforeCatalog = env.pathsRequested().length;

  await env.runSection("audio-catalog", { handle: catalogRecordHandle });

  // The handle saw the catalog request...
  assert.deepStrictEqual(
    catalogCalls.map((c) => c.path),
    ["/api/audio/catalog"],
    "the loader must issue GET /api/audio/catalog through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(beforeCatalog),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});
