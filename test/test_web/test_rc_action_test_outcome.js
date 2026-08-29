// =============================================================================
// test/test_web/test_rc_action_test_outcome.js
//
// #220: POST /api/actions/test now reports a real outcome (queued |
// queue-full | unavailable) instead of always ok:true, and the RC page's
// action-test picker must stop rendering "Dispatched" for anything but a
// genuine queued success.
//
// data/rc.js's action picker is built entirely from innerHTML strings
// (renderActionPicker/renderActionRow), and its post-render wiring re-queries
// that HTML with querySelectorAll - a real DOM operation the shared
// page-module test harness (test/test_web/helpers/page_module_env.js)
// deliberately does not implement (its querySelectorAll stub always returns
// [], since nothing in this test suite parses innerHTML into a queryable
// tree). Clicking the rendered "Test" button is therefore not reachable
// through that harness, or any harness that does not add a real HTML parser.
//
// actionTestFeedbackForOutcome() is the actual outcome->feedback mapping
// runActionTest() uses (data/rc.js, marked ACTION TEST OUTCOME (#220)
// BEGIN/END) - not a reimplementation - extracted and evaluated standalone,
// the same technique test/test_web/helpers/page_module_env.js already uses
// to pull PART 1 out of page_bootstrap.js by marker comment.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const BEGIN_MARKER = "// ==== ACTION TEST OUTCOME (#220) BEGIN ====";
const END_MARKER = "// ==== ACTION TEST OUTCOME (#220) END ====";

const loadActionTestFeedbackForOutcome = () => {
  const source = readFileSync(new URL("../../data/rc.js", import.meta.url), "utf8");
  const start = source.indexOf(BEGIN_MARKER);
  const end = source.indexOf(END_MARKER);
  assert.ok(
    start >= 0 && end > start,
    "data/rc.js must carry the ACTION TEST OUTCOME (#220) BEGIN/END markers"
  );
  const block = source.slice(start, end);

  const context = { module: { exports: undefined } };
  vm.createContext(context);
  vm.runInContext(`${block}\nmodule.exports = actionTestFeedbackForOutcome;`, context, {
    filename: "data/rc.js#actionTestFeedbackForOutcome",
  });
  assert.strictEqual(typeof context.module.exports, "function");
  return context.module.exports;
};

test("queued outcome is the only one that renders the success pill", () => {
  const feedbackFor = loadActionTestFeedbackForOutcome();
  const feedback = feedbackFor("queued");
  assert.strictEqual(feedback.kind, "success");
  assert.strictEqual(feedback.text, "Dispatched");
});

test("queue-full outcome renders an error pill, never Dispatched", () => {
  const feedbackFor = loadActionTestFeedbackForOutcome();
  const feedback = feedbackFor("queue-full");
  assert.strictEqual(feedback.kind, "error");
  assert.notStrictEqual(feedback.text, "Dispatched");
});

test("unavailable outcome renders an error pill, never Dispatched", () => {
  const feedbackFor = loadActionTestFeedbackForOutcome();
  const feedback = feedbackFor("unavailable");
  assert.strictEqual(feedback.kind, "error");
  assert.notStrictEqual(feedback.text, "Dispatched");
});

// The trust-boundary rule (a missing/unrecognized key must never read as a
// confident positive): an outcome this mapping does not recognize - absent
// (older/mismatched firmware), null, or a future addition - must render as
// an error, not silently fall back to the pre-#220 "always Dispatched" bug
// this ticket exists to fix.
test("a missing or unrecognized outcome never reads as success", () => {
  const feedbackFor = loadActionTestFeedbackForOutcome();
  assert.strictEqual(feedbackFor(undefined).kind, "error");
  assert.strictEqual(feedbackFor(null).kind, "error");
  assert.strictEqual(feedbackFor("").kind, "error");
  assert.strictEqual(feedbackFor("some-future-outcome").kind, "error");
});

test("every outcome maps to operator-visible text distinct from the others", () => {
  const feedbackFor = loadActionTestFeedbackForOutcome();
  const texts = new Set([
    feedbackFor("queued").text,
    feedbackFor("queue-full").text,
    feedbackFor("unavailable").text,
    feedbackFor("some-future-outcome").text,
  ]);
  assert.strictEqual(texts.size, 4, "outcomes must not collapse onto the same operator-visible text");
});
