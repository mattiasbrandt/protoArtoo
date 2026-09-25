// =============================================================================
// test/test_web/helpers/fake_droid.js
//
// The one fake droid every surface suite describes its Outputs with (#415). An
// Output reaches the browser as its row from GET /api/servo/outputs, with
// every setting a builder makes on it, and goes back as that row through
// POST /api/config's `outputs` (ADR 0068). So a row is made here, once, from
// one description, and a suite that wants an Output unwired, lit or with no
// wired tick at all says so rather than typing out its own copy.
//
// A row's stored id follows no pattern tied to the label or the address on
// purpose: a page that derived one from another would pass against the real
// firmware's ids and still be wrong.
//
// outputsModule() runs the shipped data/outputs.js in a context of its own,
// for a suite that loads one surface file alone (helpers/page_module_env.js):
// the surface asks it for the Outputs exactly as it does in a browser.
// =============================================================================

import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../../data");

/**
 * A status frame as GET /api/status and the status stream carry one. The six
 * core fields come from the first unconditional chunk of buildStatusJson()
 * (src/web/web_server.cpp), so a real frame has all of them or none, and the
 * Live Reading ignores a frame missing one (data/live_reading.js). `changes`
 * lands on top.
 */
export const statusFrame = (changes = {}) => ({
  estop: false,
  sbusHwFailsafe: false,
  sbusSignalLost: false,
  webDriveExpired: false,
  webControlEnabled: false,
  sleepMode: false,
  ...changes,
});

// A stored id nobody could work out from the address or the label.
const storedIdFor = (address) =>
  `wire${[...address].reduce((hash, c) => (hash * 31 + c.charCodeAt(0)) % 9973, 7)}`;

/**
 * One Servo Output row as GET /api/servo/outputs answers it
 * (src/web/api_config.cpp handleServoOutputsGet(), docs/api.md). A fresh row
 * is unmeasured, with the band's own ends standing in for a calibration nobody
 * has made, and no pulse on it. A row the board prints a name for is one of the
 * board's own Outputs: it has a stored id and a wired tick, ticked, and carries
 * an MG996R. One with no name is an expander's channel: no id and no tick, so
 * it is always wired.
 *
 * Every row reports its Motion Profile at the firmware's defaults - time to
 * full throw, time to get up to speed and the ease - and what it does at
 * power-up, limp (#414).
 */
export const servoRow = (address, name, extra = {}) => {
  const board = name !== "";
  return {
    address,
    name,
    ...(board ? { id: storedIdFor(address) } : {}),
    switchable: board,
    wired: true,
    lightCapable: false,
    throwMs: 1000,
    accelMs: 250,
    ease: "none",
    boot: "limp",
    parts: [],
    bandLoUs: 1000,
    bandHiUs: 2000,
    component: "mg996r",
    openUs: 2000,
    centreUs: 1500,
    closeUs: 1000,
    calibrated: false,
    held: false,
    limp: "off",
    commandedUs: null,
    targetUs: null,
    nudgesDone: 0,
    ...extra,
  };
};

/**
 * An Artoo PCB as it comes up: its five LEDC Outputs driving nothing, ARM1 to
 * ARM4 standing at neutral and ARM5 with no pulse.
 */
export const freshOutputs = () => [
  servoRow("ledc:0", "ARM1", { commandedUs: 1500, targetUs: 1500 }),
  servoRow("ledc:1", "ARM2", { commandedUs: 1500, targetUs: 1500 }),
  servoRow("ledc:3", "ARM3", { commandedUs: 1500, targetUs: 1500 }),
  servoRow("ledc:4", "ARM4", { commandedUs: 1500, targetUs: 1500 }),
  servoRow("ledc:5", "ARM5"),
];

/** Parts put on the rows, by address: `{ "ledc:0": ["doorFL"] }`. */
export const withParts = (assignments, outputs = freshOutputs()) => {
  Object.entries(assignments).forEach(([address, parts]) => {
    outputs.find((each) => each.address === address).parts = parts.slice();
  });
  return outputs;
};

/**
 * Say what a set of rows holds, by address, in the words a builder's settings
 * use: any of `wired`, `type` (what is on the wire), `lightCapable` (a light
 * may go on it, which also gives it an LED count), `ledCount`, `throwMs`,
 * `accelMs`, `ease`, `boot`, `id`. Returns the rows, changed in place.
 */
export const describe = (rows, say = {}) => {
  rows.forEach((row) => {
    const { type, lightCapable, ...rest } = say[row.address] || {};
    if (type !== undefined) row.component = type;
    if (lightCapable) {
      row.lightCapable = true;
      if (row.ledCount === undefined) row.ledCount = 1;
    }
    Object.assign(row, rest);
  });
  return rows;
};

/**
 * Apply a POST /api/config JSON body's `outputs` rows to the rows they name,
 * as the firmware does, and say whether it named any Output setting: its wired
 * tick, what is on its wire, a light's LED count, its Motion Profile or its
 * boot behaviour.
 */
export const applyRowSave = (rows, body) => {
  let named = false;
  (Array.isArray(body?.outputs) ? body.outputs : []).forEach((sent) => {
    const row = rows.find((each) => each.address === sent.address);
    if (!row) return;
    ["wired", "component", "ledCount", "throwMs", "accelMs", "ease", "boot"].forEach((key) => {
      if (sent[key] === undefined) return;
      row[key] = sent[key];
      named = true;
    });
  });
  return named;
};

/**
 * The shipped data/outputs.js, run on its own. `api` hands it the PAApi it
 * reads and saves through when a caller gives it no handle; it is asked at the
 * moment of the request, so it may name an object made after this one.
 * `globals` are other browser modules the page loads beside it, such as the
 * parts catalog (`{ DroidParts }`).
 */
export const outputsModule = (api = () => undefined, globals = {}) => {
  const window = { ...globals };
  Object.defineProperty(window, "PAApi", { get: api });
  const context = { window, console, URLSearchParams };
  vm.runInNewContext(readFileSync(join(dataDir, "outputs.js"), "utf-8"), context, { filename: "outputs.js" });
  return window.PAOutputs;
};
