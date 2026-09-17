// =============================================================================
// test/test_web/test_health_grid_402.js
//
// The Dashboard's health grid, driven through the shipped data/app.js against
// the shipped data/health_signals.js.
//
// #402 was seen before it was reasoned about: a Dashboard served with no event
// stream read "7 signals - 5 degraded - 2 not reporting" with five rows lit
// amber saying "Stale data". Nothing was degraded; the controller had simply
// not been heard from. These tests pin the two ends of that: a payload that
// said nothing lights nothing, and a stream that drops after a good frame
// leaves every row exactly as the controller last reported it.
//
// #402 left this surface one banner saying the stream had broken, beside the
// Status Plate's own freshness line - the same fact in two aria-live regions on
// one screen, announced twice. #399 removed the banner and kept the plate's
// line, which is the better of the two because it carries the age. What was
// asserted here as "the banner appears" is now asserted as "this surface writes
// nothing at all about the stream", against a DECOY value planted in the
// retired element: the harness answers getElementById for any id, so a test
// that only read it back would pass against the code that still wrote to it.
// The plate's line itself is driven for real in test_status_plate_346.js, which
// boots the shipped shell.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { createRequire } from "node:module";

import { loadPageModule } from "./helpers/page_module_env.js";

const require = createRequire(import.meta.url);
// The shipped model, executed for real - not a stand-in for it.
const healthSignals = require("../../data/health_signals.js");

const ROW_IDS = ["h-sbus", "h-wifi", "h-fs", "h-heap", "h-dome-link", "h-sound", "h-dome-esc"];

const HEALTHY_FRAME = Object.freeze({
  rcCh1: 1500,
  sbusSignalLost: false,
  sbusHwFailsafe: false,
  wifiConnected: true,
  wifiRssi: -52,
  littleFsReady: true,
  heapLargest8bit: 90000,
  dome_link: { state: "connected", transport: "uart" },
  audio: { state: "idle" },
  domeEnabled: true,
  domeEsc: { state: "idle" },
  estop: false,
  sleepMode: false,
});

// Mounts the Dashboard on the SSE path and hands back the stream callback, so
// a test can deliver a frame and then break the stream the way the browser
// does. Pass `model: null` to mount a page whose health module never loaded.
const mountDashboard = ({ model = healthSignals } = {}) => {
  let deliver = null;
  const env = loadPageModule("app.js", {
    respond: () => ({ data: {} }),
    overrides: {
      PAHealthSignals: model,
      PAStatusStream: {
        isSupported: () => true,
        subscribe: (handler) => {
          deliver = handler;
          return () => {};
        },
        getLastStatus: () => null,
      },
    },
  });
  assert.ok(deliver, "app.js must subscribe to the status stream when it is supported");
  return {
    env,
    stateOf: (id) => String(env.element(id).className).replace("indicator ", ""),
    textOf: (id) => String(env.element(`ht-${id.slice(2)}`).textContent),
    summary: () => String(env.element("health-summary").textContent),
    // A decoy in the element the banner used to be. Nothing on this surface may
    // write to it; the pre-removal setStale wrote "" or "none" over exactly
    // this, so the assertion is red against the code that carried the banner.
    plantDecoy: () => {
      env.element("status-stale-banner").style.display = "DECOY";
      env.element("status-stale-banner").textContent = "DECOY";
    },
    decoyIntact: () =>
      env.element("status-stale-banner").style.display === "DECOY" &&
      env.element("status-stale-banner").textContent === "DECOY",
    send: (type, payload) => deliver(type, payload),
  };
};

test("a Dashboard that has heard nothing lights nothing", () => {
  const dash = mountDashboard();

  dash.send("status", {});

  ROW_IDS.forEach((id) => {
    assert.equal(dash.stateOf(id), "off", `${id} must be unlit when the payload said nothing`);
  });
  assert.equal(dash.summary(), "7 signals · 7 not reporting");
});

test("a stream that drops leaves every row on the state the controller reported", () => {
  const dash = mountDashboard();
  dash.plantDecoy();

  dash.send("status", HEALTHY_FRAME);
  ROW_IDS.forEach((id) => assert.equal(dash.stateOf(id), "ok", `${id} should start nominal`));
  assert.equal(dash.summary(), "7 signals · 7 ok");
  assert.ok(dash.decoyIntact(), "a good frame wrote to the retired stale banner");

  dash.send("stream_error", "");

  // The stream breaking is the Status Plate's one freshness line to report, and
  // this surface adds nothing to it. The rows keep the state the controller
  // last sent, say nothing about age, and change no colour.
  assert.ok(
    dash.decoyIntact(),
    "this surface wrote its own freshness claim; the plate already carries the one for the screen",
  );
  ROW_IDS.forEach((id) => {
    assert.equal(dash.stateOf(id), "ok", `${id} must keep the state the controller reported`);
    assert.doesNotMatch(dash.textOf(id), /stale/i, `${id} must not tell the operator about staleness`);
  });
  assert.equal(dash.summary(), "7 signals · 7 ok");
});

test("a fallback poll that keeps failing writes no freshness claim either", async () => {
  // The other half of the same removal: with no event stream the surface polls,
  // and the second failed poll in a row used to raise the same banner. The
  // poll's own function is captured and driven here, which is what the shipped
  // background poll does on its cadence.
  const env = loadPageModule("app.js", {
    respond: (path) =>
      path === "/api/status"
        ? Promise.reject(new Error("no response from controller"))
        : { data: {} },
    overrides: {
      PAHealthSignals: healthSignals,
      PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    },
  });
  await env.settle();
  assert.ok(env.intervals.length > 0, "app.js must install a fallback poll when the stream is unsupported");
  env.element("status-stale-banner").style.display = "DECOY";

  for (let round = 0; round < 3; round += 1) {
    env.intervals.forEach((timer) => timer.fn());
    await env.settle();
  }

  assert.equal(
    env.element("status-stale-banner").style.display,
    "DECOY",
    "a run of failed polls raised a banner this surface no longer carries",
  );
});

test("a health module that never loaded reads not-reporting, not degraded", () => {
  const dash = mountDashboard({ model: null });

  dash.send("status", HEALTHY_FRAME);

  ROW_IDS.forEach((id) => {
    assert.equal(dash.stateOf(id), "off", `${id} must be unlit when nothing evaluated it`);
    assert.equal(dash.textOf(id), "Health model missing");
  });
});

test("a degraded reading the controller did send still lights amber", () => {
  const dash = mountDashboard();

  // Largest allocatable block between the warn and fail floors: reported,
  // degraded, and something the builder can act on.
  dash.send("status", { ...HEALTHY_FRAME, heapLargest8bit: 13000 });

  assert.equal(dash.stateOf("h-heap"), "warn");
  assert.equal(dash.textOf("h-heap"), "Low");
  assert.equal(dash.summary(), "7 signals · 6 ok · 1 degraded");
});
