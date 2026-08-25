// =============================================================================
// test/test_web/test_handle_coverage_159.js
//
// Behavioral tests for handle-based request routing (#159).
// Proves that migrated loaders in dome.js, shell.js, wifi.js, and rc.js
// route their section loads THROUGH the supplied Section Request Handle and
// do not bypass it to PAApi.
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
  wifi: {
    ap_ssid: "droid-ap",
    ap_password: "password123",
    sta_ssid: "home-wifi",
    sta_password: "home123",
    mode: "sta",
  },
};

const IDENTITY_PAYLOAD = {
  droidName: "R2-D2",
  mdnsUseName: true,
  board: "artoo_esp32",
  board_capabilities: { PA_CAP_NATIVE_WIFI: true },
  build_flags: { PA_HEAP_PROFILE: false },
};

const WIFI_DIAG_PAYLOAD = {
  mode: "sta",
  signal_strength: -50,
  channel: 6,
  ip_address: "192.168.1.100",
  gateway: "192.168.1.1",
  dns: "8.8.8.8",
};

const RC_MAP_PAYLOAD = {
  mode: "standard_pwm",
  map: [0, 1, 2, 3, 4, 5],
};

const RC_DIAG_PAYLOAD = {
  ch0: { raw: 1500, failsafe: false },
  ch1: { raw: 1500, failsafe: false },
  ch2: { raw: 1500, failsafe: false },
  ch3: { raw: 1500, failsafe: false },
  ch4: { raw: 1500, failsafe: false },
  ch5: { raw: 1500, failsafe: false },
};

const ACTIONS_PAYLOAD = [
  { token: "DOME:CW", name: "Dome Clockwise" },
  { token: "DOME:CCW", name: "Dome Counter-Clockwise" },
];

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
// Dome Page Tests
// =============================================================================

test("dome-configuration loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("dome.js", {
    respond: (path) => {
      if (path === "/api/config") return { data: CONFIG_PAYLOAD };
      return { data: {} };
    },
  });
  const { calls, handle } = makeRecordingHandle(CONFIG_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("dome-configuration", { handle });

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
// Shell Page Tests
// =============================================================================

test("shell-identity loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("shell.js", {
    respond: (path) => {
      if (path === "/api/identity") return { data: IDENTITY_PAYLOAD };
      return { data: {} };
    },
  });
  const { calls, handle } = makeRecordingHandle(IDENTITY_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("shell-identity", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/identity"],
    "the loader must issue GET /api/identity through the handle"
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
// WiFi Page Tests
// =============================================================================

test("wifi-identity loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("wifi.js", {
    respond: (path) => {
      if (path === "/api/identity") return { data: IDENTITY_PAYLOAD };
      if (path === "/api/config") return { data: CONFIG_PAYLOAD };
      if (path === "/api/wifi") return { data: WIFI_DIAG_PAYLOAD };
      return { data: {} };
    },
  });
  const { calls, handle } = makeRecordingHandle(IDENTITY_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("wifi-identity", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/identity"],
    "the loader must issue GET /api/identity through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(directBefore),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});

test("wifi-config loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("wifi.js", {
    respond: (path) => {
      if (path === "/api/identity") return { data: IDENTITY_PAYLOAD };
      if (path === "/api/config") return { data: CONFIG_PAYLOAD };
      if (path === "/api/wifi") return { data: WIFI_DIAG_PAYLOAD };
      return { data: {} };
    },
  });
  const { calls, handle } = makeRecordingHandle(CONFIG_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("wifi-config", { handle });

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

test("wifi-diagnostics loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("wifi.js", {
    respond: (path) => {
      if (path === "/api/identity") return { data: IDENTITY_PAYLOAD };
      if (path === "/api/config") return { data: CONFIG_PAYLOAD };
      if (path === "/api/wifi") return { data: WIFI_DIAG_PAYLOAD };
      return { data: {} };
    },
  });
  const { calls, handle } = makeRecordingHandle(WIFI_DIAG_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("wifi-diagnostics", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/wifi"],
    "the loader must issue GET /api/wifi through the handle"
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
// RC Page Tests
// =============================================================================

test("rc-mode-mapping loader issues both requests through the handle, not PAApi", async () => {
  const env = loadPageModule("rc.js", {
    respond: (path) => {
      if (path === "/api/config") return { data: CONFIG_PAYLOAD };
      if (path === "/api/rc/map") return { data: RC_MAP_PAYLOAD };
      if (path === "/api/rc") return { data: RC_DIAG_PAYLOAD };
      if (path === "/api/actions") return { data: ACTIONS_PAYLOAD };
      return { data: {} };
    },
  });

  // Create a handle that tracks both calls
  const calls = [];
  const handle = {
    get: (path, ...rest) => {
      calls.push({ method: "GET", path });
      if (path === "/api/config") return Promise.resolve({ data: CONFIG_PAYLOAD });
      if (path === "/api/rc/map") return Promise.resolve({ data: RC_MAP_PAYLOAD });
      return Promise.resolve({ data: {} });
    },
    postForm: (path, ...rest) => {
      calls.push({ method: "POST", path });
      return Promise.resolve({ data: {} });
    },
    postJson: (path, ...rest) => {
      calls.push({ method: "POST", path });
      return Promise.resolve({ data: {} });
    },
    estopPostForm: (path, ...rest) => {
      calls.push({ method: "POST", path });
      return Promise.resolve({ data: {} });
    },
  };

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("rc-mode-mapping", { handle });

  // The handle saw BOTH requests...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/config", "/api/rc/map"],
    "the loader must issue both GET /api/config and GET /api/rc/map through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(directBefore),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});

test("rc-diagnostics loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("rc.js", {
    respond: (path) => {
      if (path === "/api/config") return { data: CONFIG_PAYLOAD };
      if (path === "/api/rc/map") return { data: RC_MAP_PAYLOAD };
      if (path === "/api/rc") return { data: RC_DIAG_PAYLOAD };
      if (path === "/api/actions") return { data: ACTIONS_PAYLOAD };
      return { data: {} };
    },
  });
  const { calls, handle } = makeRecordingHandle(RC_DIAG_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("rc-diagnostics", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/rc"],
    "the loader must issue GET /api/rc through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(directBefore),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});

test("rc-action-targets loader issues its request through the handle, not PAApi", async () => {
  const env = loadPageModule("rc.js", {
    respond: (path) => {
      if (path === "/api/config") return { data: CONFIG_PAYLOAD };
      if (path === "/api/rc/map") return { data: RC_MAP_PAYLOAD };
      if (path === "/api/rc") return { data: RC_DIAG_PAYLOAD };
      if (path === "/api/actions") return { data: ACTIONS_PAYLOAD };
      return { data: {} };
    },
  });
  const { calls, handle } = makeRecordingHandle(ACTIONS_PAYLOAD);

  // Loading the module issues its own traffic; only requests made from this
  // point on belong to the loader under test.
  const directBefore = env.pathsRequested().length;

  await env.runSection("rc-action-targets", { handle });

  // The handle saw the request...
  assert.deepStrictEqual(
    calls.map((c) => c.path),
    ["/api/actions"],
    "the loader must issue GET /api/actions through the handle"
  );
  // ...and nothing went around it. This is the half that catches a loader
  // which ignores the handle and reaches for window.PAApi directly.
  assert.deepStrictEqual(
    env.pathsRequested().slice(directBefore),
    [],
    "the loader must not bypass the handle and call PAApi directly"
  );
});
