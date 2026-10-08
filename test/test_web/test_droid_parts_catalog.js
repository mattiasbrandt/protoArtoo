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

test("an unknown complement fails loudly; an empty one is a real answer", () => {
  const mk4 = catalog.designs.find((design) => design.id === "mk4");
  const basic = mk4.variants.find((variant) => variant.id === "basic");
  const complex = mk4.variants.find((variant) => variant.id === "complex");

  // Per half (#409): a Basic dome is not yet read, a Basic body is.
  assert.equal(basic.seeds.dome, null, "an unknown complement must not be an empty one");
  assert.throws(() => basic.seeds.dome.forEach(() => {}), TypeError);
  assert.ok(Array.isArray(basic.seeds.body));

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

