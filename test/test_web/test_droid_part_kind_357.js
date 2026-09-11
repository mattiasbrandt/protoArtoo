// What a Part Kind lets a surface show, and what it takes away (#357).
//
// The catalog declares `kind` on the Parts it can classify; this module is the
// consumer that reads it. Three things are worth asserting and all three have a
// way of going quietly wrong:
//
//   - the branch is on the declared field, not on the id or the name. A module
//     that looked for "psi" in an id would pass every happy-path test and then
//     classify a builder's `other7` PSI as something that moves.
//   - a light's travel, throw and position are ABSENT. Zeroed or greyed is the
//     failure mode here: both still read as a promise about movement.
//   - the Kind advises. There is no refusal in the API to find, and the answer
//     for a lit panel is the light it carries - something to say, not a veto.
//
// Both modules are executed rather than pattern-matched, per test_web/README.md:
// what the page gets is window.DroidPartKind reading window.DroidParts, not the
// characters of either file.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");
const read = (name) => fs.readFileSync(path.join(root, "data", name), "utf8");

// One context, both modules, in the order a page loads them.
const context = { window: {} };
vm.runInNewContext(read("droid_parts.js"), context);
vm.runInNewContext(read("droid_part_kind.js"), context);

const kinds = context.window.DroidPartKind;
const catalog = context.window.DroidParts;
const parts = catalog?.parts ?? [];
const byId = new Map(parts.map((part) => [part.id, part]));

test("the module publishes its answers under one global", () => {
  assert.ok(kinds, "data/droid_part_kind.js did not assign window.DroidPartKind");
  assert.equal(kinds.LIGHT, "light");
});

test("the six lit dome parts are the ones the catalog classifies as lights", () => {
  const lights = parts.filter((part) => kinds.isLight(part));
  assert.deepEqual(
    // Spread first: the catalog module runs in its own vm realm, so its arrays
    // do not share a prototype with this file's and a strict deep compare fails
    // on that alone, whatever the contents are.
    [...lights.map((part) => part.id)].sort(),
    ["logicFront", "logicRear", "magicPanel", "psiFront", "psiRear", "upperPanel"],
  );
  // And a Part the catalog does not classify says so, rather than being
  // reported as the moving kind: an escape-hatch slot is whatever the builder
  // wired to it.
  assert.equal(kinds.kindOf(byId.get("other7")), null);
  assert.equal(kinds.kindOf(byId.get("doorFL")), null);
});

test("the kind is read off the declared field, never off an id or a name", () => {
  // Same name, same shape of id, no declared kind: not a light. This is the
  // whole reason the field exists.
  assert.equal(kinds.isLight({ id: "magicPanel", name: "Magic Panel" }), false);
  assert.equal(kinds.isLight({ id: "psiFront", name: "Front PSI" }), false);
  // And a Part whose id says nothing about light IS one when the catalog says
  // so - a builder's own hardware on a spare output.
  assert.equal(kinds.isLight({ id: "other7", name: "Other part 7", kind: "light" }), true);
});

test("a light promises no travel, and the row does not show one", () => {
  const light = byId.get("magicPanel");
  const door = byId.get("doorFL");

  for (const affordance of ["travel", "throw", "position", "release"]) {
    assert.equal(kinds.shows(light, affordance), false, `a light offered ${affordance}`);
    assert.equal(kinds.shows(door, affordance), true, `a door lost ${affordance}`);
  }
  // "How far" as a light hears it.
  assert.equal(kinds.shows(light, "brightness"), true);
  assert.equal(kinds.shows(door, "brightness"), false);

  // Absent, not zeroed: the list a row builds from does not carry the words at
  // all, so there is nothing for a surface to draw as an empty value.
  assert.deepEqual([...kinds.affordances(light)], ["brightness"]);
});

test("the kind is carried by a state class and never by a colour", () => {
  // The module's whole output for a Kind is a class name. Colour is reserved
  // for refused and for actionable (#327), and a Part being a light is neither.
  assert.equal(kinds.treatmentClass(byId.get("psiRear")), "partkind-light");
  assert.equal(kinds.treatmentClass(byId.get("doorFL")), "");
  assert.equal(kinds.treatmentClass(null), "");
});

test("a lit panel says what it carries, and says nothing about refusing", () => {
  // What a surface asks before recording a servo Output against P5. The answer
  // is the light itself, so the question can name it.
  const light = kinds.lightOn("panel5", parts);
  assert.equal(light.id, "magicPanel");
  assert.equal(light.name, "Magic Panel");

  // A panel that carries no light has nothing to say about the mapping.
  assert.equal(kinds.lightOn("panel7", parts), null);
  assert.equal(kinds.lightOn("doorFL", parts), null);

  // Every lit panel is reachable from the panel's own id, so no surface has to
  // match on a name to find one.
  for (const id of ["panel5", "panel6", "panel8", "panel9", "panel12", "panel14"]) {
    assert.ok(kinds.lightOn(id, parts), `${id} carries a light the catalog cannot find`);
  }
});

test("nothing here throws at a caller holding less than a Part", () => {
  assert.equal(kinds.kindOf(null), null);
  assert.equal(kinds.kindOf({}), null);
  assert.equal(kinds.kindOf({ kind: "" }), null);
  assert.equal(kinds.isLight(undefined), false);
  assert.equal(kinds.lightOn("panel5", undefined), null);
  assert.equal(kinds.lightOn("", parts), null);
});
