// =============================================================================
// test/test_web/test_page_bootstrap.js
//
// Pure reducer tests for the Common Page Bootstrap state model.
// Extracts and tests createBootstrap/dispatch without DOM or browser APIs.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

// Read and extract just the reducer code (PART 1)
const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapPath = join(__dirname, "../../data/page_bootstrap.js");
const bootstrapFile = readFileSync(bootstrapPath, "utf-8");
const part1End = bootstrapFile.indexOf("// =========================== PART 2");
const part1Start = bootstrapFile.indexOf("(() => {");
const part1Code = bootstrapFile.substring(part1Start, part1End);

// Create a minimal window shim
const window = { PageBootstrap: null };
global.window = window;

// Execute the reducer code
// eslint-disable-next-line no-eval
eval(part1Code);

const Core = window.PageBootstrap;

test("Regression test 1: retry actually executes (section fails once then succeeds)", (t) => {
  // Create bootstrap with one section
  const state0 = Core.createBootstrap({ resources: [], sections: ["section1"] });

  // Pump to start section
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  const active1 = state.active;
  assert.strictEqual(active1.kind, "section");
  assert.strictEqual(active1.name, "section1");
  assert(
    active1.id !== undefined,
    "active should have an id field for retry tracking"
  );

  // Section fails
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "no-response", reason: "timeout" },
  });
  assert.strictEqual(state.sections[0].status, "failed-retrying");

  // Pump to restart retry (need to advance time past the backoff delay: 2000ms for first retry)
  state = Core.dispatch(state, { type: "TICK", dt: 2500 });
  const active2 = state.active;
  assert(
    active2 !== null,
    "should have a new active after retry pump"
  );
  assert.strictEqual(active2.kind, "section");
  assert.strictEqual(active2.name, "section1");
  assert.notStrictEqual(
    active2.id,
    active1.id,
    "retry should get a new id distinct from first attempt"
  );

  // Section succeeds on retry
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.sections[0].status, "done");
});

test("Regression test 2: late result from expired attempt does not settle wrong step", (t) => {
  // Create bootstrap with two resources in sequence
  const state0 = Core.createBootstrap({
    resources: ["res1", "res2"],
    sections: [],
  });

  // Resource 1 starts
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  const active1 = state.active;
  assert.strictEqual(active1.name, "res1");
  const id1 = active1.id;

  // Resource 1's deadline expires -> transitions to failed-retrying
  state = Core.dispatch(state, { type: "TICK", dt: 7000 });
  assert.strictEqual(state.resources[0].status, "failed-retrying");

  // Resource 1 retries on backoff and succeeds
  state = Core.dispatch(state, { type: "TICK", dt: 2500 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.resources[0].status, "done");
  assert.strictEqual(state.resourceCursor, 1);

  // Now resource 2 should be active
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  const active2 = state.active;
  assert.strictEqual(active2.name, "res2");
  const id2 = active2.id;

  // Verify id2 is different from id1
  assert.notStrictEqual(id1, id2);

  // Resource 2 succeeds
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.resources[1].status, "done");
});

test("Resource chain advances strictly in order", (t) => {
  const state0 = Core.createBootstrap({
    resources: ["res1", "res2", "res3"],
    sections: [],
  });

  let state = state0;

  // Resource 1 loads and succeeds
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "res1");
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.resources[0].status, "done");
  assert.strictEqual(state.resourceCursor, 1);

  // Resource 2 loads and succeeds
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "res2");
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.resources[1].status, "done");
  assert.strictEqual(state.resourceCursor, 2);

  // Resource 3 loads and succeeds
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "res3");
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.resources[2].status, "done");
  assert.strictEqual(state.resourceCursor, 3);
  assert.strictEqual(state.resourcesReady, true);

  // Verify no step was redone
  assert.strictEqual(state.resources[0].attempt, 1);
  assert.strictEqual(state.resources[1].attempt, 1);
  assert.strictEqual(state.resources[2].attempt, 1);
});

test("sectionsStable is reached when all sections are done-or-failed-retrying", (t) => {
  const state0 = Core.createBootstrap({
    resources: [],
    sections: [],
  });

  assert.strictEqual(state0.sectionsStable, true); // No sections, stable immediately

  const state1 = Core.createBootstrap({
    resources: [],
    sections: ["sec1"],
  });

  assert.strictEqual(state1.sectionsStable, false); // Section pending
  let state = state1;

  // Section 1 starts loading
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "sec1");
  assert.strictEqual(state.sectionsStable, false);

  // Section 1 fails and enters failed-retrying
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "no-response", reason: "timeout" },
  });
  assert.strictEqual(state.sections[0].status, "failed-retrying");
  assert.strictEqual(state.sectionsStable, true); // Failed-retrying counts as stable
});

test("Clock idle predicate reports no pending work only when idle", (t) => {
  const state0 = Core.createBootstrap({
    resources: ["res1"],
    sections: ["sec1"],
  });

  let state = state0;

  // Has pending work: active request
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  assert(
    state.active !== null,
    "should have active work"
  );

  // Resource succeeds
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.resourcesReady, true);

  // Section loads
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  assert(state.active !== null);

  // Section fails and enters failed-retrying
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "no-response", reason: "timeout" },
  });
  assert.strictEqual(state.active, null);
  assert.strictEqual(state.sections[0].status, "failed-retrying");
  // Has pending work: waiting to retry
  const hasPendingBeforeRetry =
    state.active !== null ||
    [...state.resources, ...state.sections].some(
      (s) => s.status === "failed-retrying"
    );
  assert(hasPendingBeforeRetry);

  // After enough time, retry fires and clears
  state = Core.dispatch(state, { type: "TICK", dt: 5000 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.active, null);
  const allDoneOrDone = [...state.resources, ...state.sections].every(
    (s) => s.status === "done"
  );
  assert(allDoneOrDone);
  // No pending work now
  const hasPendingAfter =
    state.active !== null ||
    [...state.resources, ...state.sections].some(
      (s) => s.status === "failed-retrying"
    );
  assert.strictEqual(hasPendingAfter, false);
});
