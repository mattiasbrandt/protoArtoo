// The vocabulary the browser receives, and the consumer that has to read it (#358).
//
// Two things the catalog can lose quietly, both of which the suites beside this
// one pass straight over because they name `light` and `control: none` by hand.
//
//   - A Part Kind nobody reads. The catalog may declare a Kind that no browser
//     module classifies, and the failure is silent by construction: an
//     unclassified Kind falls to the moving default, so six lights would be
//     offered travel, throw and position for a device that has none. Asserting
//     "light is handled" cannot catch the second Kind arriving unread; this
//     asserts it of whatever the catalog declares today.
//   - A treatment that follows the control path. Since #358 every declared Part
//     reaches the browser whether or not anything on the body drives it, so a
//     surface that let `control` change what a row offers would quietly split
//     the catalog in two - which is the split this decision removed.
//
// Both modules are executed rather than pattern-matched, per test_web/README.md:
// what the page gets is window.DroidPartKind reading window.DroidParts.

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
const parts = context.window.DroidParts?.parts ?? [];
const byId = new Map(parts.map((part) => [part.id, part]));

test("every Part Kind the catalog declares is one the browser classifies", () => {
  const declared = [...new Set(parts.map((part) => part.kind).filter(Boolean))];
  assert.ok(declared.length > 0, "the catalog declares no Part Kind at all");

  // The moving default, which is what an unread Kind silently falls to.
  const unclassified = [...kinds.affordances({ id: "probe" })];

  for (const kind of declared) {
    const carrying = { id: "probe", kind };
    assert.equal(kinds.kindOf(carrying), kind, `${kind} is not read off the field`);
    assert.notDeepEqual(
      [...kinds.affordances(carrying)],
      unclassified,
      `Part Kind '${kind}' reaches the browser and is treated exactly as a Part ` +
        `with no Kind - nothing reads it, so every Part carrying it is offered ` +
        `what its Kind cannot promise`,
    );
  }
});

test("what drives a Part changes nothing about how a surface treats it", () => {
  // A breadpan door nothing drives yet and a utility arm the body already
  // drives are the same kind of thing to draw. Since #358 both are in the
  // vocabulary, and a treatment that read `control` would re-open the split
  // that decision closed - a Part you can see and not name.
  const undriven = byId.get("doorFL");
  const driven = byId.get("utilUp");
  assert.equal(undriven.control, "none");
  assert.equal(driven.control, "body-ledc");

  assert.deepEqual([...kinds.affordances(undriven)], [...kinds.affordances(driven)]);
  assert.equal(kinds.treatmentClass(undriven), kinds.treatmentClass(driven));

  // And a dome part the body will never drive is treated the same way again:
  // who executes a Part is the dome's question, not the row's.
  const domeDriven = byId.get("pie1");
  assert.equal(domeDriven.control, "dome-link");
  assert.deepEqual([...kinds.affordances(domeDriven)], [...kinds.affordances(driven)]);
});
