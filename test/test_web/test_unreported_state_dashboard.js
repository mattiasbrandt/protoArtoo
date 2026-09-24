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
import { servoRow, configOutputs, outputsModule, statusFrame } from "./helpers/fake_droid.js";

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

// The Dashboard reads its config, and the Outputs' names with it, through the
// shipped data/outputs.js its page loads first (#415).
const dashboard = (status = {}, { config = CONFIG } = {}) => {
  let env = null;
  env = loadPageModule("app.js", {
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
      PAOutputs: outputsModule(() => env.window.PAApi),
      PAUi: { setupActionHtml: (action) => `${action} in <a href="/setup.html">Setup</a>` },
    },
  });
  return env;
};

test("a WiFi signal nothing measured is not printed as a very strong one", async () => {
  // wifiRssi is zero whenever the droid is not joined to a network as a
  // station (deriveWiFiConnectivityFields). Zero dBm would be the strongest
  // reading the scale has, so printing it is the readout lying loudest.
  const joined = dashboard({ wifiRssi: -54 });
  await joined.window.PALiveReading.read();
  await joined.settle();
  assert.equal(joined.element("readout-wifi").innerHTML, "-54<small>dBm</small>");
  assert.match(joined.element("readout-wifi-detail").textContent, /joined/);

  const alone = dashboard({ wifiRssi: 0 });
  await alone.window.PALiveReading.read();
  await alone.settle();
  assert.equal(alone.element("readout-wifi").innerHTML, "--", "nothing measured prints as nothing");
  assert.match(alone.element("readout-wifi-detail").textContent, /Nothing to measure/);
});

// A Dashboard fed whole frames through the stream and the Live Reading, so a
// frame can leave a field out.
const mountDashboard = () => {
  const env = loadPageModule("app.js", { respond: () => ({ data: {} }) });
  return { env, send: (changes) => env.pushStatus(statusFrame(changes)), unknown: env.window.PALiveReading.UNKNOWN };
};

test("a mood the droid has not reported is not printed as mood zero", () => {
  const known = mountDashboard();
  known.send({ activeMood: 13 });
  assert.equal(known.env.element("snapshot-mood").textContent, "Mid-Awake");
  assert.equal(known.env.element("mood-now").textContent, "Mid-Awake");

  const silent = mountDashboard();
  silent.send({});
  assert.equal(
    silent.env.element("snapshot-mood").textContent,
    silent.unknown,
    "a frame that carried no mood is not a droid reporting mood zero",
  );
  assert.equal(silent.env.element("mood-now").textContent, silent.unknown);

  // A number this surface has no name for is not printed either: a raw
  // identifier reaches an operator only through the mapping table (ADR 0059).
  const unknown = mountDashboard();
  unknown.send({ activeMood: 77 });
  assert.equal(unknown.env.element("snapshot-mood").textContent, unknown.unknown);
  assert.doesNotMatch(unknown.env.element("snapshot-mood").textContent, /77/);
});

// An Output the firmware did not report is not on the Dashboard either. Which
// Outputs exist and what each is called is GET /api/config's answer (every
// components{} entry carrying an address); the card names them from it, and a
// status key it cannot place is not dressed up as an Output with a name this
// page made up (ADR 0033 Amendment 2026-09-19). The fake droid's ids follow no
// pattern, and `aux1` is the old protoArtoo word a page might still know.
test("the component card names Outputs as the firmware reported them, and no others", async () => {
  const components = configOutputs([servoRow("ledc:0", "GPIO 49")]);
  const [id] = Object.keys(components);
  const config = { ...CONFIG, components };
  const env = dashboard(
    { [id]: { state: "ready", detail: "Target 1500 us" }, aux1: { state: "ready", detail: "Servo channel enabled" } },
    { config },
  );
  await env.window.PALiveReading.read();
  await env.runSection("app-log-level");
  await env.settle();

  const card = env.element("component-status-grid").innerHTML;
  assert.match(card, /GPIO 49/, "the Output is named as its board prints it");
  assert.doesNotMatch(card, /AUX|aux1/i, "and nothing the firmware did not report is listed");
});
