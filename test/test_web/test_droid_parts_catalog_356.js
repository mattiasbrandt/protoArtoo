// The generated parts catalog, as the browser actually receives it (#356).
//
// data/droid_parts.js is data, not logic: the app layers its own display over
// it and never writes back. So what is worth asserting is the shape a consumer
// will reach for and the two distinctions that are easy to flatten by accident
// - a declared unknown against an empty complement, and a row that declares no
// control path against one that says nothing drives it yet. Both flatten into
// "falsy" if nobody looks, and both mean something different to a builder.
//
// The module is executed rather than pattern-matched, per test_web/README.md:
// what the page gets is window.DroidParts, not the file's characters.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");
const modulePath = path.join(root, "data", "droid_parts.js");
const source = fs.readFileSync(modulePath, "utf8");

const context = { window: {} };
vm.runInNewContext(source, context);
const catalog = context.window.DroidParts;

const byId = new Map((catalog?.parts ?? []).map((part) => [part.id, part]));

test("the module publishes the catalog under one global", () => {
  assert.ok(catalog, "data/droid_parts.js did not assign window.DroidParts");
  assert.equal(catalog.source, "docs/droid-parts.yaml");
  assert.equal(catalog.generator, "tools/generate_droid_parts_catalog.py");
  assert.match(catalog.sourceSha256, /^[0-9a-f]{64}$/);
  assert.match(source, /DO NOT EDIT MANUALLY/);
});

test("every part a builder can name is here, dome and body alike", () => {
  // The browser is the side that carries the whole vocabulary: firmware gets
  // only the parts the body drives.
  for (const id of ["pie1", "panel14", "hp3Tilt", "domeBtn2", "doorFL", "utilUp"]) {
    assert.ok(byId.has(id), `${id} is missing from the browser catalog`);
  }
  assert.equal(catalog.parts.length, byId.size, "an id is declared twice");
});

test("the escape hatch is generated from its count, not kept in step by hand", () => {
  const slots = catalog.parts.filter((part) => part.section === "other_slots");
  assert.equal(slots.length, 10);
  assert.equal(slots[0].id, "other1");
  assert.equal(slots[9].id, "other10");
  // Named so a builder can pick one, and with no position word: their own
  // hardware is listed beside the body view rather than placed on it.
  assert.equal(slots[6].name, "Other part 7");
  assert.equal(slots[6].position, undefined);
});

test("a part carries the name to show and the shorthand to show beside it", () => {
  const pie = byId.get("pie1");
  assert.equal(pie.name, "Dome pie 1");
  assert.equal(pie.shorthand, "PP1");
  // Spread first: the module runs in its own vm realm, so its arrays do not
  // share a prototype with this file's and a strict deep compare fails on that
  // alone, whatever the contents are.
  assert.deepEqual([...pie.aliases], ["PP1", "Dome pie 1"]);
  assert.equal(pie.position, "rear-right");
  assert.equal(pie.bearingDeg, 150);

  // A body part has no Printed Droid shorthand, and the name is the plain
  // English one rather than the first alias blindly.
  const door = byId.get("doorFL");
  assert.equal(door.name, "Left body door");
  assert.equal(door.shorthand, undefined);
});

test("an index is emission order, and the id is the identity", () => {
  catalog.parts.forEach((part, position) => {
    assert.equal(part.index, position, `${part.id} is out of emission order`);
  });
});

test("a declared unknown is absent, never the word TBD", () => {
  const serialised = JSON.stringify(catalog);
  assert.ok(!serialised.includes("TBD"), "TBD reached the browser as a value");
  // The dome CAD names and the dome-link panel numbers are unread, not
  // missing, so their keys are simply not here.
  assert.equal("cadName" in byId.get("pie1"), false);
  assert.equal("domeLinkPanel" in byId.get("pie1"), false);
  // A cadName that IS null says the part has no CAD name, which is what marks
  // a Common Addition - a different statement from an absent key.
  assert.equal(byId.get("gripArm").cadName, null);
  assert.equal(byId.get("doorFL").cadName, "FLBreadpanDoor");
});

test("nothing-drives-it-yet and not-a-driven-thing stay apart", () => {
  assert.equal(byId.get("panel5").control, "none");
  assert.equal(byId.get("utilUp").control, "body-ledc");
  assert.equal(byId.get("pie1").control, "dome-link");
  // A dome fixture is an orientation reference and never becomes driveable,
  // so it declares no control path at all.
  assert.equal(byId.get("domeBtn1").control, null);
});

test("a design says which variant a builder starts on", () => {
  const mk4 = catalog.designs.find((design) => design.id === "mk4");
  assert.equal(mk4.defaultVariant, "complex");
  assert.equal(mk4.short, "MK4");
  const names = mk4.variants.map((variant) => variant.id);
  assert.ok(names.includes(mk4.defaultVariant), "the default is not one of the variants");

  // A design with no variants has no default either: there is nothing to
  // default to, and an empty axis would be a second control with nothing in it.
  const own = catalog.designs.find((design) => design.id === "own");
  assert.equal(own.variants, undefined);
  assert.equal(own.defaultVariant, undefined);
});

test("an unknown complement fails loudly; an empty one is a real answer", () => {
  const mk4 = catalog.designs.find((design) => design.id === "mk4");
  const simple = mk4.variants.find((variant) => variant.id === "simple");
  const complex = mk4.variants.find((variant) => variant.id === "complex");

  assert.equal(simple.seeds, null, "an unknown complement must not be an empty one");
  assert.throws(() => simple.seeds.forEach(() => {}), TypeError);

  // The default seeds a real complement, so nobody is ever pre-selected onto
  // a droid with nothing on it.
  assert.ok(complex.seeds.length > 0);
  for (const id of complex.seeds) {
    assert.ok(byId.has(id), `${id} is seeded but not declared`);
  }

  // "My own build" seeds nothing on purpose, and says so as an empty list.
  const own = catalog.designs.find((design) => design.id === "own");
  assert.deepEqual([...own.seeds], []);
});

test("a design seeds no Common Addition and no escape-hatch slot", () => {
  const seeded = new Set(
    catalog.designs.flatMap((design) =>
      (design.variants ?? [{ seeds: design.seeds }]).flatMap((variant) => variant.seeds ?? [])
    )
  );
  for (const id of ["gripArm", "gripClaw", "interArm", "interTool", "other1"]) {
    assert.ok(!seeded.has(id), `${id} belongs to no design but a design seeds it`);
  }
});
