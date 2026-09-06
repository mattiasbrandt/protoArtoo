// =============================================================================
// test/test_web/test_failed_terminal_outcome_200_s2a.js
//
// Tests for the third section-loader outcome: failed-terminal.
// Slice 2a of issue #200: terminal failures (kind=incompatible or kind=device-error)
// have no automatic retry timer (nextAt stays null) and count toward sectionsStable
// so a permanently failed section never stops the page starting live updates.
// retryNow and refreshSections reset failed-terminal to pending.
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

// =============================================================================
// Outcome Classification Tests
// =============================================================================

test("ApiError with kind='incompatible' classifies to failed-terminal", (t) => {
  const error = new Error("identity manifest invalid");
  error.kind = "incompatible";
  error.status = 200;

  const outcome = Core.classifyOutcome(error);

  assert.strictEqual(outcome.kind, "failed-terminal");
  assert.strictEqual(outcome.reason, "incompatible");
});

test("ApiError with kind='device-error' classifies to failed-terminal", (t) => {
  const error = new Error("device-side defect");
  error.kind = "device-error";
  error.status = 500;

  const outcome = Core.classifyOutcome(error);

  assert.strictEqual(outcome.kind, "failed-terminal");
  assert.strictEqual(outcome.reason, "device-error");
});

// =============================================================================
// Terminal Failure — No Automatic Retry Timer
// =============================================================================

test("section with failed-terminal outcome ends at status='failed-terminal' with nextAt=null", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["term-section"] });

  // Pump to start section
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  const active = state.active;
  assert.strictEqual(active.kind, "section");
  assert.strictEqual(active.name, "term-section");

  // Section fails with terminal error
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "incompatible" },
  });

  const section = state.sections[0];
  assert.strictEqual(section.status, "failed-terminal");
  assert.strictEqual(section.nextAt, null, "terminal failure must have nextAt=null (no retry timer)");
  assert.strictEqual(section.reason, "incompatible");
});

test("CRITICAL: terminal failure never schedules a retry — no timer fires even far in the future", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["no-retry-section"] });

  // Pump to start section
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "no-retry-section");

  // Section fails terminally
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "device-error" },
  });

  assert.strictEqual(state.sections[0].status, "failed-terminal");
  const activeAfter = state.active;

  // Advance time far into the future (beyond any normal backoff delay)
  state = Core.dispatch(state, { type: "TICK", dt: 100000 });

  // active must remain null and the section must still be terminal
  assert.strictEqual(state.active, null, "no new active should be scheduled after a terminal failure");
  assert.strictEqual(state.sections[0].status, "failed-terminal");
  assert.strictEqual(state.sections[0].nextAt, null, "nextAt must still be null after time advance");
});

// =============================================================================
// Sections Stable with Terminal Outcome
// =============================================================================

test("sectionsStable becomes true with a failed-terminal section alongside other done sections", (t) => {
  const state0 = Core.createBootstrap({
    resources: [],
    sections: ["done-section", "term-section"],
  });

  // Start and complete first section
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });
  assert.strictEqual(state.sections[0].status, "done");

  // Start and terminally fail second section
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "incompatible" },
  });
  assert.strictEqual(state.sections[1].status, "failed-terminal");

  // Sections must be stable (no more active work scheduled)
  assert.strictEqual(state.active, null);
  assert.strictEqual(state.sectionsStable, true, "failed-terminal section must count toward stable");
});

test("sectionsStable becomes true even with only failed-terminal sections", (t) => {
  const state0 = Core.createBootstrap({
    resources: [],
    sections: ["only-term"],
  });

  // Start and fail terminally
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "device-error" },
  });

  assert.strictEqual(state.sectionsStable, true, "a single failed-terminal section must make sections stable");
  assert.strictEqual(state.active, null);
});

// =============================================================================
// Retry/Refresh Reset Terminal to Pending
// =============================================================================

test("retryNow resets failed-terminal section to pending and schedules loading", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["retry-term"] });

  // Start and fail terminally
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "incompatible" },
  });
  assert.strictEqual(state.sections[0].status, "failed-terminal");

  // Operator issues retryNow - it resets to pending and pumps (activates the work)
  state = Core.dispatch(state, {
    type: "RETRY_NOW",
    name: "retry-term",
  });

  // After RETRY_NOW and pump, the section transitions to loading
  const section = state.sections[0];
  assert.strictEqual(section.status, "loading");
  assert.strictEqual(section.attempt, 1, "first retry attempt is #1");
  assert.strictEqual(section.nextAt, null);
  assert.strictEqual(section.reason, null);
  assert.strictEqual(state.active.name, "retry-term", "section should be active");
});

test("retryNow on failed-terminal schedules the section to run immediately", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["restart"] });

  // Start and fail terminally
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  const id1 = state.active.id;
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "device-error" },
  });
  assert.strictEqual(state.active, null);

  // retryNow resets to pending
  state = Core.dispatch(state, {
    type: "RETRY_NOW",
    name: "restart",
  });

  // Pump to run the reset section
  state = Core.dispatch(state, { type: "TICK", dt: 0 });

  // active should be the section again with a fresh id
  assert.strictEqual(state.active.kind, "section");
  assert.strictEqual(state.active.name, "restart");
  assert.notStrictEqual(state.active.id, id1, "retry should get a fresh id");
});

test("refreshSections resets failed-terminal to pending and schedules loading", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["refresh-term"] });

  // Start and fail terminally
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "incompatible" },
  });
  assert.strictEqual(state.sections[0].status, "failed-terminal");

  // Operator issues refreshSections - it resets to pending and pumps
  state = Core.dispatch(state, {
    type: "REFRESH_SECTIONS",
    names: ["refresh-term"],
  });

  const section = state.sections[0];
  // After refresh and pump, the section is loading
  assert.strictEqual(section.status, "loading");
  assert.strictEqual(section.reason, null);
  assert.strictEqual(state.active.name, "refresh-term");
});

test("refreshSections without names resets all failed-terminal sections", (t) => {
  const state0 = Core.createBootstrap({
    resources: [],
    sections: ["term1", "term2"],
  });

  // Start and fail first section terminally
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "incompatible" },
  });

  // Start and fail second section terminally
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "device-error" },
  });

  assert.strictEqual(state.sections[0].status, "failed-terminal");
  assert.strictEqual(state.sections[1].status, "failed-terminal");

  // Refresh all sections (both get reset to pending)
  state = Core.dispatch(state, {
    type: "REFRESH_SECTIONS",
  });

  // Both are reset to pending (though one is now loading after pump)
  assert.strictEqual(state.sections[0].status, "loading", "first reset and activated by pump");
  assert.strictEqual(state.sections[1].status, "pending", "second is reset but not yet activated");
});

// =============================================================================
// Existing Behaviors Unchanged
// =============================================================================

test("503 Busy failure still uses Retry-After backoff", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["busy-section"] });

  // Start section
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "busy", reason: "503", retryAfterMs: 3000 },
  });

  const section = state.sections[0];
  assert.strictEqual(section.status, "failed-retrying");
  assert.strictEqual(section.reason, "503");
  // nextAt should be set to some future time (now + 3000)
  assert(section.nextAt !== null && section.nextAt > 0);
});

test("timeout no-response failure still uses growing backoff", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["timeout-section"] });

  // Start section
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "no-response", reason: "timeout" },
  });

  const section = state.sections[0];
  assert.strictEqual(section.status, "failed-retrying");
  assert.strictEqual(section.reason, "timeout");
  // nextAt should be set (initial backoff 2000ms)
  assert(section.nextAt !== null && section.nextAt > 0);
});

test("success outcome still completes a section", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["success-section"] });

  // Start section
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });

  const section = state.sections[0];
  assert.strictEqual(section.status, "done");
  assert.strictEqual(section.nextAt, null);
});

// =============================================================================
// Mixed Scenarios
// =============================================================================

test("terminal failure in resource chain stops chain but marks stable", (t) => {
  const state0 = Core.createBootstrap({
    resources: ["res1", "res2"],
    sections: [],
  });

  // Start resource 1
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "res1");

  // Resource 1 fails terminally
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "incompatible" },
  });

  const res1 = state.resources[0];
  assert.strictEqual(res1.status, "failed-terminal");
  assert.strictEqual(res1.nextAt, null);
  assert.strictEqual(state.resourceCursor, 0, "resource chain should not advance past terminal failure");
  assert.strictEqual(state.sectionsStable, true);
});

test("sections stabilize even with terminal failures; live updates can start", (t) => {
  const state0 = Core.createBootstrap({
    resources: [],
    sections: ["term-section"],
  });

  // Section fails terminally
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "term-section");
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "device-error" },
  });

  // Sections are now stable - a permanently failing section still counts as stable
  // because no automatic retry will occur. The page can proceed to live updates.
  assert.strictEqual(state.sectionsStable, true);
  assert.strictEqual(state.active, null, "no more work scheduled");
  // The live updates are ready to start when both resourcesReady and sectionsStable are true.
  // With no resources, resourcesReady is true at bootstrap, and now sectionsStable is also true.
  assert.strictEqual(state.liveUpdatesStarted || (state.sectionsStable && state.resourcesReady), true);
});

test("retryNow on failed-terminal section followed by success completes it", (t) => {
  const state0 = Core.createBootstrap({ resources: [], sections: ["recovery-test"] });

  // Fail terminally
  let state = Core.dispatch(state0, { type: "TICK", dt: 0 });
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "failed-terminal", reason: "incompatible" },
  });
  assert.strictEqual(state.sections[0].status, "failed-terminal");

  // Operator retries
  state = Core.dispatch(state, {
    type: "RETRY_NOW",
    name: "recovery-test",
  });

  // Pump to run
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  assert.strictEqual(state.active.name, "recovery-test");

  // This time it succeeds
  state = Core.dispatch(state, {
    type: "RESULT",
    outcome: { kind: "success" },
  });

  assert.strictEqual(state.sections[0].status, "done");
});
