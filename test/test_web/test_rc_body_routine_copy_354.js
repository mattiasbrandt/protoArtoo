// The RC page names the seven body routines the way the firmware does (#354).
//
// :SE30..:SE36 used to run one shared open-and-close, and data/rc.js described
// them as such ("uses ARM1 gripper motion"). They are Factory Sequences now,
// each with its own purpose in src/tasks/sequence_catalog.cpp, and a picker that
// still describes the old stub tells a builder their gripper button moves an
// arm it no longer touches. This holds the two sources together: the table the
// RC page actually builds its picker from is evaluated, and each description is
// compared with the catalog's purpose for that routine.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");

// The picker table, evaluated as the data it is rather than read as text.
const rcSequences = () => {
  const source = fs.readFileSync(path.join(root, "data/rc.js"), "utf8");
  const start = source.indexOf("const MARCDUINO_SEQUENCES = [");
  assert.ok(start >= 0, "data/rc.js no longer declares MARCDUINO_SEQUENCES");
  const end = source.indexOf("];", start);
  const literal = source.slice(source.indexOf("[", start), end + 1);
  return JSON.parse(JSON.stringify(vm.runInNewContext(literal)));
};

// Each body routine's purpose, after its ":SE<id> - " prefix.
const catalogPurposes = () => {
  const source = fs.readFileSync(path.join(root, "src/tasks/sequence_catalog.cpp"), "utf8");
  const purposes = new Map();
  const entry = /\{ "DM:SE(3[0-6])"[^\n]*\n\s*":SE3[0-6] - ((?:[^"\\]|\\.)*)" \}/g;
  let match;
  while ((match = entry.exec(source)) !== null) purposes.set(Number(match[1]), match[2]);
  return purposes;
};

test("every body routine in the RC picker is described as the firmware describes it", () => {
  const purposes = catalogPurposes();
  assert.equal(purposes.size, 7, "the catalog no longer carries DM:SE30..DM:SE36");

  const picker = rcSequences();
  assert.deepEqual(
    picker.map((entry) => entry.id),
    [30, 31, 32, 33, 34, 35, 36],
  );
  picker.forEach((entry) => {
    assert.equal(entry.description, purposes.get(entry.id), `:SE${entry.id}`);
  });
});
