// =============================================================================
// test/test_web/helpers/fake_droid.js
//
// The one fake droid every surface suite describes its Outputs with (#415). An
// Output reaches the browser as two halves - a Servo Output row from
// GET /api/servo/outputs and a components{} entry from GET /api/config - and
// data/outputs.js is the one place they are joined. So the halves are made
// here, once, from one description, and a suite that wants an Output unwired,
// lit or unknown to the config says so rather than typing out its own copy of
// either answer.
//
// The config's ids and save fields follow no pattern tied to the label or the
// address on purpose: a page that derived one from another would pass against
// the real firmware's names and still be wrong.
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
 * One Servo Output row as GET /api/servo/outputs answers it
 * (src/web/api_config.cpp handleServoOutputsGet(), docs/api.md). A fresh row
 * is unmeasured, with the band's own ends standing in for a calibration nobody
 * has made, and no pulse on it.
 */
export const servoRow = (address, name, extra = {}) => ({
  address,
  name,
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
});

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
 * GET /api/config's Output entries for a set of rows, the way
 * src/web/api_config.cpp reports them: the row's name as the board's label,
 * wired, carrying an MG996R, saved under fields of their own.
 *
 * Every Output also reports its Motion Profile and the three fields that save
 * it (#414): time to full throw, time to get up to speed and the ease, at the
 * firmware's defaults - and what it does at power-up, limp, with its field.
 *
 * `say` changes what the config says about an Output, by address: any of
 * `enabled`, `type`, `label`, `lightCapable` (which also names a
 * `ledCountField`), `ledCount`, `throwMs`, `accelMs`, `ease`, `boot`, or `null` for an
 * Output the config does not describe at all (an expander channel only the
 * servo table knows).
 */
export const configOutputs = (rows, say = {}) =>
  Object.fromEntries(
    rows
      .map((row, index) => [row, index])
      .filter(([row]) => row.name !== "" && say[row.address] !== null)
      .map(([row, index]) => {
        const { lightCapable = false, ...rest } = say[row.address] || {};
        return [
          `out${index}`,
          {
            label: row.name,
            address: row.address,
            enabled: true,
            type: "mg996r",
            enabledField: `wired${index}`,
            typeField: `servo${index}`,
            throwField: `full${index}`,
            accelField: `rise${index}`,
            easeField: `shape${index}`,
            bootField: `wake${index}`,
            throwMs: 1000,
            accelMs: 250,
            ease: "none",
            boot: "limp",
            ...(lightCapable ? { lightCapable: true, ledCountField: `leds${index}`, ledCount: 1 } : {}),
            ...rest,
          },
        ];
      })
  );

/**
 * Apply a POST /api/config form to the Output entries it names, as the
 * firmware does, and say whether it named any: a wired tick, a type, a
 * light's LED count or a Motion Profile field under the field each entry gave
 * for it.
 */
export const applyOutputSave = (components, form) => {
  let named = false;
  Object.values(components).forEach((entry) => {
    if (!entry || typeof entry.address !== "string") return;
    if (entry.enabledField in form) {
      entry.enabled = form[entry.enabledField] === "true";
      named = true;
    }
    if (entry.typeField in form) {
      entry.type = form[entry.typeField];
      named = true;
    }
    if (entry.ledCountField && entry.ledCountField in form) {
      entry.ledCount = Number(form[entry.ledCountField]);
      named = true;
    }
    [["throwField", "throwMs", Number], ["accelField", "accelMs", Number], ["easeField", "ease", String],
      ["bootField", "boot", String]]
      .forEach(([fieldKey, valueKey, read]) => {
        if (entry[fieldKey] && entry[fieldKey] in form) {
          entry[valueKey] = read(form[entry[fieldKey]]);
          named = true;
        }
      });
  });
  return named;
};

/**
 * The shipped data/outputs.js, run on its own. `api` hands it the PAApi it
 * reads and saves through when a caller gives it no handle; it is asked at the
 * moment of the request, so it may name an object made after this one.
 */
export const outputsModule = (api = () => undefined) => {
  const window = {};
  Object.defineProperty(window, "PAApi", { get: api });
  const context = { window, console, URLSearchParams };
  vm.runInNewContext(readFileSync(join(dataDir, "outputs.js"), "utf-8"), context, { filename: "outputs.js" });
  return window.PAOutputs;
};
