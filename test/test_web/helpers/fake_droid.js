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
 * full throw, time to get up to speed and the ease - its release time, never
 * (#443), and what it does at power-up, limp (#414). It also reports what the
 * droid started with (asStarted() below), as a droid nothing has been saved on
 * since: `extra` may state any of those four by hand, and what it states stands.
 */
export const servoRow = (address, name, extra = {}) => {
  const board = name !== "";
  return asStarted({
    address,
    name,
    ...(board ? { id: storedIdFor(address) } : {}),
    switchable: board,
    wired: true,
    lightCapable: false,
    throwMs: 1000,
    accelMs: 250,
    ease: "none",
    release: 0,
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
  }, extra);
};

// A Light Type on the wire, as include/output_wire.h carriesLight() reads it.
const carriesLight = (component) => component === "rgb";

/**
 * Give a row what the droid reports it STARTED with (#364,
 * src/web/api_config.cpp handleServoOutputsGet()), worked out from the row as
 * it stands - so it describes a droid nothing has been saved on since it
 * started. Changes the row in place and returns it.
 *
 *   activeWired     the wired tick read at start
 *   activeLight     the Light Type on the wire at start, null on a servo wire
 *   driven          whether ServoTask puts pulses on it: wired at start, and
 *                   no light on the wire
 *   activeLedCount  the LED count read at start, only where a light can go
 *
 * A field `stated` carries was set by hand and stands; `driven` follows a
 * stated `activeWired` or `activeLight`. A change made to a row AFTER this -
 * applyRowSave(), or a test assigning to it - leaves the four alone, which is
 * a save the droid has not restarted on. A test that changes a row to say how
 * the droid came up calls describe(), or this.
 *
 * An expander's channel has no tick, so the firmware answers `activeWired`
 * true and neither light field. It also answers `driven` false, since nothing
 * drives an expander yet. That is left off here: these fakes give an
 * expander's row a pulse (test_find_by_moving.js) and list one on Servos
 * (test_servo_calibration_test_card.js), which no firmware does yet, and
 * `driven: false` takes a row's acts away.
 */
export const asStarted = (row, stated = {}) => {
  if (!row.switchable) {
    if (!("activeWired" in stated)) row.activeWired = true;
    return row;
  }
  if (!("activeWired" in stated)) row.activeWired = row.wired;
  if (!("activeLight" in stated)) row.activeLight = carriesLight(row.component) ? row.component : null;
  if (!("driven" in stated)) row.driven = row.activeWired && row.activeLight === null;
  if (!("activeLedCount" in stated)) {
    if (row.lightCapable && typeof row.ledCount === "number") row.activeLedCount = row.ledCount;
    else delete row.activeLedCount;
  }
  return row;
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

/**
 * The same board with a body door on each of its five Outputs: every Output
 * wired, since an Output with a Part on it is wired (CONTEXT.md "Wiring",
 * #411). What a surface that lists only Outputs with a Part - Servos - is
 * booted against when a test acts on the rows rather than on the Parts.
 */
export const wiredOutputs = () =>
  withParts({
    "ledc:0": ["doorFL"],
    "ledc:1": ["doorFR"],
    "ledc:3": ["doorRL"],
    "ledc:4": ["doorRR"],
    "ledc:5": ["smallDoor"],
  });

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
 *
 * A row it names is a droid that came up that way, so what the row reports it
 * started with follows (asStarted()) - unless the entry states `activeWired`,
 * `driven`, `activeLight` or `activeLedCount` itself, which is how a test says
 * a setting was saved after the droid started.
 */
export const describe = (rows, say = {}) => {
  rows.forEach((row) => {
    if (!(row.address in say)) return;
    const { type, lightCapable, ...rest } = say[row.address];
    if (type !== undefined) row.component = type;
    if (lightCapable) {
      row.lightCapable = true;
      if (row.ledCount === undefined) row.ledCount = 1;
    }
    Object.assign(row, rest);
    asStarted(row, rest);
  });
  return rows;
};

/**
 * Apply a POST /api/config JSON body's `outputs` rows to the rows they name,
 * as the firmware does, and say whether it named any Output setting: its wired
 * tick, what is on its wire, a light's LED count, its Motion Profile, its
 * release time or its boot behaviour.
 */
export const applyRowSave = (rows, body) => {
  let named = false;
  (Array.isArray(body?.outputs) ? body.outputs : []).forEach((sent) => {
    const row = rows.find((each) => each.address === sent.address);
    if (!row) return;
    ["wired", "component", "ledCount", "throwMs", "accelMs", "ease", "release", "boot"].forEach((key) => {
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
