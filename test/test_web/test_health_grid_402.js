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
    bannerShown: () => env.element("status-stale-banner").style.display === "",
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

  dash.send("status", HEALTHY_FRAME);
  ROW_IDS.forEach((id) => assert.equal(dash.stateOf(id), "ok", `${id} should start nominal`));
  assert.equal(dash.summary(), "7 signals · 7 ok");
  assert.equal(dash.bannerShown(), false, "no banner while the stream is running");

  dash.send("stream_error", "");

  // The surface says the stream broke exactly once, in its banner, beside the
  // Status Plate's one freshness line. The rows themselves say nothing about
  // age and change no colour.
  assert.equal(dash.bannerShown(), true, "the stale banner is how this surface says the stream broke");
  ROW_IDS.forEach((id) => {
    assert.equal(dash.stateOf(id), "ok", `${id} must keep the state the controller reported`);
    assert.doesNotMatch(dash.textOf(id), /stale/i, `${id} must not tell the operator about staleness`);
  });
  assert.equal(dash.summary(), "7 signals · 7 ok");
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
