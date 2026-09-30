// =============================================================================
// test/test_web/test_output_settings.js
//
// The segmented control data/output_settings.js keeps (#411): the one control
// a pick of what is on a wire - its servo, or its Light Type - is drawn with,
// on a Part's row of Wiring's part-first table. Servos' servo pick and
// Wiring's Output plates, the two views this file used to draw, are gone; their
// invariants live with the part-first table (test_parts_table.js).
//
// The invariant here: a press on the option already picked asks nothing, so
// a builder pressing what is shown never sends a save that changes nothing;
// a press on another hands over exactly that option.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { MiniDocument } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");

const load = () => {
  const document = new MiniDocument();
  const window = { document };
  const context = { window, document };
  context.globalThis = context;
  vm.runInNewContext(readFileSync(join(dataDir, "output_settings.js"), "utf8"), context, { filename: "output_settings.js" });
  return window.PAOutputSettings;
};

test("a press on the picked option asks nothing, and a press on another hands over that option", () => {
  const { segmented } = load();
  const picks = [];
  const bar = segmented("Left body door servo", [{ id: "mg996r", label: "MG996R" }, { id: "mg90s", label: "MG90S" }],
    "mg996r", (id) => picks.push(id));
  const option = (id) => bar.querySelectorAll("[data-value]").find((node) => node.dataset.value === id);

  assert.equal(option("mg996r").getAttribute("aria-checked"), "true");
  assert.equal(option("mg90s").getAttribute("aria-checked"), "false");

  option("mg996r").fire("click", {});
  assert.deepEqual(picks, [], "the picked one sends nothing");
  option("mg90s").fire("click", {});
  assert.deepEqual(picks, ["mg90s"]);
});
