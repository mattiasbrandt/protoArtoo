// =============================================================================
// test/test_web/test_unreported_state_dashboard.js
//
// A reading nobody took is not printed as a reading. The Dashboard's renderers
// are driven through helpers/page_module_env.js with the SHIPPED
// health_signals.js, droid_parts.js and droid_build.js published into the same
// context rather than modelled.
//
// Thinned from the #399 Surface Anatomy checklist (#406): the anatomy itself -
// icons, cells, section heads - is read on the screen, not here.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { loadPageModule } from "./helpers/page_module_env.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const HEALTHY = Object.freeze({
  estop: false,
  sbusHwFailsafe: false,
  sbusSignalLost: false,
  webDriveExpired: false,
  webControlEnabled: true,
  sleepMode: false,
  stationary: false,
  speedLimitMax: 600,
  drive: { state: "idle" },
  rcCh1: { state: "active" },
  dome_link: { state: "connected", uart_owner: "dome" },
  audio: { state: "idle", link_ok: true, rx_status: "available" },
});

// Publishes a shipped browser module's globals without modelling them.
const publish = (files) => {
  const bag = { window: {} };
  bag.globalThis = bag;
  bag.console = { log: () => {}, warn: () => {}, error: () => {} };
  Object.assign(bag, { JSON, Math, Date, Number, String, Boolean, Array, Object, Set, Map, Promise, Error, RegExp, isNaN, parseInt, parseFloat });
  files.forEach((file) => vm.runInNewContext(readData(file), bag, { filename: file }));
  return bag.window;
};

const MODELS = publish(["health_signals.js", "droid_parts.js", "droid_build.js"]);

const CONFIG = {
  system: { logLevel: 3 },
  droidBuild: {
    domeDesign: "mk4",
    domeVariant: "complex",
    bodyDesign: "mk4",
    bodyVariant: "complex",
    fitted: ["pie1", "pie2", "doorFL"],
  },
};

const dashboard = (status = {}, { config = CONFIG } = {}) =>
  loadPageModule("app.js", {
    respond: (path) => {
      if (path === "/api/status") return { data: { ...HEALTHY, ...status } };
      if (path === "/api/config") return { data: config };
      if (path === "/api/logs") return { data: "" };
      return { data: {} };
    },
    overrides: {
      PAHealthSignals: MODELS.PAHealthSignals,
      DroidParts: MODELS.DroidParts,
      DroidBuild: MODELS.DroidBuild,
      PAUi: { setupActionHtml: (action) => `${action} in <a href="/setup.html">Setup</a>` },
    },
  });

test("a WiFi signal nothing measured is not printed as a very strong one", async () => {
  // wifiRssi is zero whenever the droid is not joined to a network as a
  // station (deriveWiFiConnectivityFields). Zero dBm would be the strongest
  // reading the scale has, so printing it is the readout lying loudest.
  const joined = dashboard({ wifiRssi: -54 });
  await joined.runSection("app-initial-status");
  await joined.settle();
  assert.equal(joined.element("readout-wifi").innerHTML, "-54<small>dBm</small>");
  assert.match(joined.element("readout-wifi-detail").textContent, /joined/);

  const alone = dashboard({ wifiRssi: 0 });
  await alone.runSection("app-initial-status");
  await alone.settle();
  assert.equal(alone.element("readout-wifi").innerHTML, "--", "nothing measured prints as nothing");
  assert.match(alone.element("readout-wifi-detail").textContent, /nothing measured/);
});

// A Dashboard fed by the stream alone, so a frame can simply leave a field out.
const mountDashboard = () => {
  let deliver = null;
  const env = loadPageModule("app.js", {
    respond: () => ({ data: {} }),
    overrides: {
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
  return { env, send: (payload) => deliver("status", payload) };
};

test("a mood the droid has not reported reads Not reported, not mood zero", () => {
  const known = mountDashboard();
  known.send({ activeMood: 13 });
  assert.equal(known.env.element("snapshot-mood").textContent, "Mid-Awake");
  assert.equal(known.env.element("mood-now").textContent, "Mid-Awake");

  const silent = mountDashboard();
  silent.send({});
  assert.equal(
    silent.env.element("snapshot-mood").textContent,
    "Not reported",
    "a frame that carried no mood is not a droid reporting mood zero",
  );
  assert.equal(silent.env.element("mood-now").textContent, "Not reported");

  // A number this surface has no name for is not printed either: a raw
  // identifier reaches an operator only through the mapping table (ADR 0059).
  const unknown = mountDashboard();
  unknown.send({ activeMood: 77 });
  assert.equal(unknown.env.element("snapshot-mood").textContent, "Not reported");
  assert.doesNotMatch(unknown.env.element("snapshot-mood").textContent, /77/);
});
