// =============================================================================
// test/test_web/test_issue_119_refresh_in_flight.js
//
// Operator refresh while a section request is in flight must not hide the
// recovery panel. REFRESH_SECTIONS leaves the step `state.active` points at
// untouched (it is genuinely loading), resets every other named section, and
// the in-flight result settles through the single slot as usual.
//
// Executes the shipped reducer and view (PART 1 + PART 2 of
// data/page_bootstrap.js) -- nothing under test is reimplemented here.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapPath = join(__dirname, "../../data/page_bootstrap.js");
const bootstrapFile = readFileSync(bootstrapPath, "utf-8");
const part2End = bootstrapFile.indexOf("// ============================ PART 3");
const part1Start = bootstrapFile.indexOf("(() => {");
const partCode = bootstrapFile.substring(part1Start, part2End);

const window = { PageBootstrap: null, PARecoveryView: null };
global.window = window;

// eslint-disable-next-line no-eval
eval(partCode);

const Core = window.PageBootstrap;
const deriveView = (state) => window.PARecoveryView.deriveView(state);

const statuses = (state) => state.sections.map((s) => `${s.name}:${s.status}`).join(",");

test("refresh while a section is in flight keeps the panel visible and the step honest", () => {
  let state = Core.createBootstrap({ resources: [], sections: ["a", "b"] });
  state = Core.dispatch(state, { type: "TICK", dt: 0 });

  assert.strictEqual(state.active?.name, "a");
  assert.strictEqual(deriveView(state).visible, true, "panel visible before refresh");
  const activeIdBefore = state.active.id;

  state = Core.dispatch(state, { type: "REFRESH_SECTIONS", names: ["a", "b"] });

  // The in-flight request is untouched: same attempt id, same honest status.
  assert.strictEqual(state.active?.id, activeIdBefore, "active request survives refresh");
  const stepA = state.sections.find((s) => s.name === "a");
  assert.strictEqual(stepA.status, "loading", "in-flight step is not reported as pending");
  const stepB = state.sections.find((s) => s.name === "b");
  assert.strictEqual(stepB.status, "pending", "idle step is reset for re-run");

  const view = deriveView(state);
  assert.strictEqual(view.visible, true, `panel must stay visible, statuses=${statuses(state)}`);
  assert.strictEqual(view.mode, "loading");

  // Ticks inside the request's deadline change nothing: still one request in
  // flight, still a visible loading panel -- no hidden gap.
  state = Core.dispatch(state, { type: "TICK", dt: 1000 });
  assert.strictEqual(state.active?.id, activeIdBefore, "single slot: no second start");
  assert.strictEqual(deriveView(state).visible, true, "panel visible through the window");
});

test("the in-flight result settles normally and every named section re-runs", () => {
  let state = Core.createBootstrap({ resources: [], sections: ["a", "b"] });
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, { type: "REFRESH_SECTIONS", names: ["a", "b"] });

  // The in-flight request settles; its response is the refresh for that step.
  state = Core.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
  assert.strictEqual(statuses(state), "a:done,b:loading", "slot moves on to the reset step");
  assert.strictEqual(deriveView(state).visible, true, "panel visible while b re-runs");

  state = Core.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
  assert.strictEqual(statuses(state), "a:done,b:done");
  assert.strictEqual(state.sectionsStable, true);
  assert.strictEqual(deriveView(state).visible, false, "panel hides once everything settled");
});

test("refresh still re-runs a section that was already done while another is in flight", () => {
  let state = Core.createBootstrap({ resources: [], sections: ["a", "b"] });
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
  assert.strictEqual(statuses(state), "a:done,b:loading");

  state = Core.dispatch(state, { type: "REFRESH_SECTIONS", names: ["a", "b"] });
  assert.strictEqual(statuses(state), "a:pending,b:loading", "done step reset, in-flight step kept");
  assert.strictEqual(deriveView(state).visible, true);

  // b settles from its in-flight run; the reset a re-runs through the slot.
  state = Core.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
  assert.strictEqual(statuses(state), "a:loading,b:done", "reset step re-runs after the slot frees");
  const stepA = state.sections.find((s) => s.name === "a");
  assert.strictEqual(stepA.attempt, 1, "re-run starts from a fresh attempt count");
});

test("refresh with nothing in flight still resets and restarts every named section", () => {
  let state = Core.createBootstrap({ resources: [], sections: ["a", "b"] });
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
  state = Core.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
  assert.strictEqual(statuses(state), "a:done,b:done");
  assert.strictEqual(state.active, null);

  state = Core.dispatch(state, { type: "REFRESH_SECTIONS", names: ["a", "b"] });
  assert.strictEqual(statuses(state), "a:loading,b:pending", "refresh pumps the first section");
  assert.strictEqual(deriveView(state).visible, true);
});
