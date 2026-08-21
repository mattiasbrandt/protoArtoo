// =============================================================================
// test/test_web/test_issue_115_focus_fixes.js
//
// Focus management for the recovery overlay (issue #115):
//   1. focus is saved once, on the hidden->visible transition
//   2. focus moves into the overlay when it appears, and back out when it goes
//   3. Tab is contained inside the overlay and wraps at both ends
//
// Every assertion here observes what the shipped code in data/page_bootstrap.js
// DID: which element ended up focused, whether the keydown was consumed, what
// the announcer says. The Tab tests run the real keydown handler that
// ensureBackdrop() installs, so breaking its `event.key !== "Tab"` guard fails
// this file (issue #146 - the previous version asserted on source text and
// sailed straight through that mutation).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import {
  MockElement,
  loadRecoveryView,
  stateShowingRecovery,
  stateHidingRecovery,
  keyEvent,
  dispatchEvent,
} from "./helpers/recovery_dom.js";

// Puts the overlay on screen and hands back the live backdrop plus the
// focusable controls currently inside it.
const showOverlay = () => {
  const env = loadRecoveryView();
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  const backdrop = env.backdrop();
  assert.ok(backdrop, "render must mount the recovery backdrop");
  return { ...env, state, backdrop };
};

const FOCUSABLE_QUERY =
  'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])';

// Adds a control of the given tag inside the overlay. The shipped handler
// re-queries the backdrop on every keydown, so appending here is exactly what
// a panel with more than one control looks like at event time.
const addControl = (document, backdrop, tag, attributes = {}) => {
  const node = document.createElement(tag);
  Object.entries(attributes).forEach(([name, value]) => node.setAttribute(name, value));
  backdrop.appendChild(node);
  return node;
};

// -----------------------------------------------------------------------------
// Keyboard containment
// -----------------------------------------------------------------------------

test("Tab from the last focusable in the overlay wraps to the first", (t) => {
  const { document, backdrop } = showOverlay();
  addControl(document, backdrop, "input");

  const focusables = backdrop.querySelectorAll(FOCUSABLE_QUERY);
  assert.ok(focusables.length >= 2, "the panel must hold at least two controls to wrap between");

  const first = focusables[0];
  const last = focusables[focusables.length - 1];
  document.activeElement = last;
  // The overlay already focused the Retry now button when it opened, so count
  // the move this keydown causes rather than the total.
  const firstFocusesBefore = first.focusCount;

  const event = dispatchEvent(backdrop, "keydown", keyEvent("Tab"));

  assert.equal(event.defaultPrevented, true, "Tab on the last control must be consumed");
  assert.equal(document.activeElement, first, "focus must wrap to the first control");
  assert.equal(first.focusCount, firstFocusesBefore + 1, "the wrap must move focus exactly once");
});

test("Shift+Tab from the first focusable in the overlay wraps to the last", (t) => {
  const { document, backdrop } = showOverlay();
  addControl(document, backdrop, "input");

  const focusables = backdrop.querySelectorAll(FOCUSABLE_QUERY);
  const first = focusables[0];
  const last = focusables[focusables.length - 1];
  document.activeElement = first;

  const event = dispatchEvent(backdrop, "keydown", keyEvent("Tab", { shiftKey: true }));

  assert.equal(event.defaultPrevented, true, "Shift+Tab on the first control must be consumed");
  assert.equal(document.activeElement, last, "focus must wrap to the last control");
  assert.equal(last.focusCount, 1, "the last control must be focused exactly once");
});

test("Tab from a control in the middle of the overlay is left to the browser", (t) => {
  const { document, backdrop } = showOverlay();
  const middle = addControl(document, backdrop, "input");
  addControl(document, backdrop, "select");

  const focusables = backdrop.querySelectorAll(FOCUSABLE_QUERY);
  assert.notEqual(focusables[focusables.length - 1], middle, "the fixture must not sit on an edge");
  document.activeElement = middle;

  const event = dispatchEvent(backdrop, "keydown", keyEvent("Tab"));

  assert.equal(event.defaultPrevented, false, "a non-edge Tab must not be consumed");
  assert.equal(document.activeElement, middle, "the handler must not move focus itself");
});

test("Keys other than Tab pass through the overlay untouched", (t) => {
  const { document, backdrop } = showOverlay();
  addControl(document, backdrop, "input");

  const focusables = backdrop.querySelectorAll(FOCUSABLE_QUERY);
  const last = focusables[focusables.length - 1];
  document.activeElement = last;

  for (const key of ["Enter", "Escape", "a", "ArrowDown"]) {
    const event = dispatchEvent(backdrop, "keydown", keyEvent(key));
    assert.equal(event.defaultPrevented, false, `${key} must not be consumed by the Tab handler`);
    assert.equal(document.activeElement, last, `${key} must not move focus`);
  }
});

test("Tab containment holds for button, input, select, textarea and [tabindex]", (t) => {
  const { document, backdrop } = showOverlay();

  // One of each kind the containment query claims to cover. If any kind is
  // dropped from the query, the element order below changes and the wrap
  // target changes with it.
  const input = addControl(document, backdrop, "input");
  const select = addControl(document, backdrop, "select");
  const textarea = addControl(document, backdrop, "textarea");
  const tabbableDiv = addControl(document, backdrop, "div", { tabindex: "0" });

  const focusables = backdrop.querySelectorAll(FOCUSABLE_QUERY);
  assert.deepEqual(
    focusables.slice(-4),
    [input, select, textarea, tabbableDiv],
    "every focusable kind must be inside the containment query"
  );

  document.activeElement = tabbableDiv;
  const forward = dispatchEvent(backdrop, "keydown", keyEvent("Tab"));
  assert.equal(forward.defaultPrevented, true, "Tab from the last kind must be consumed");
  assert.equal(document.activeElement, focusables[0], "Tab must wrap to the first control");

  document.activeElement = focusables[0];
  const backward = dispatchEvent(backdrop, "keydown", keyEvent("Tab", { shiftKey: true }));
  assert.equal(backward.defaultPrevented, true, "Shift+Tab from the first control must be consumed");
  assert.equal(document.activeElement, tabbableDiv, "Shift+Tab must wrap to the last control");
});

test("An element with tabindex=-1 is not a Tab stop inside the overlay", (t) => {
  const { document, backdrop } = showOverlay();
  const skipped = addControl(document, backdrop, "div", { tabindex: "-1" });

  const focusables = backdrop.querySelectorAll(FOCUSABLE_QUERY);
  assert.ok(
    !focusables.includes(skipped),
    "tabindex=-1 is programmatic focus only and must be excluded from the cycle"
  );

  document.activeElement = focusables[focusables.length - 1];
  dispatchEvent(backdrop, "keydown", keyEvent("Tab"));
  assert.equal(skipped.focusCount, 0, "a tabindex=-1 element must never be a wrap target");
});

test("Tab with no focusable content keeps focus on the backdrop", (t) => {
  const { document, backdrop } = showOverlay();

  // The loading state clears the panel; there is nothing to Tab to, and Tab
  // must still not escape the modal.
  backdrop.replaceChildren();
  document.activeElement = document.body;

  const event = dispatchEvent(backdrop, "keydown", keyEvent("Tab"));

  assert.equal(event.defaultPrevented, true, "Tab must not escape an empty overlay");
  assert.equal(document.activeElement, backdrop, "focus must fall back to the backdrop");
});

// -----------------------------------------------------------------------------
// Focus in and out of the overlay
// -----------------------------------------------------------------------------

test("Focus moves onto the Retry now button when the overlay appears", (t) => {
  const env = loadRecoveryView();
  const trigger = new MockElement("button", env.document);
  env.document.activeElement = trigger;

  env.RecoveryView.render(stateShowingRecovery(env.Core));

  const retry = env.backdrop().querySelector(".btn.accent");
  assert.ok(retry, "the panel must offer a Retry now button");
  assert.equal(env.document.activeElement, retry, "focus must land inside the modal");
  assert.equal(retry.textContent, "Retry now", "focus must land on the Retry now control");
});

test("Focus stays inside the overlay when the panel is rebuilt for a new state", (t) => {
  const env = loadRecoveryView();
  const outside = new MockElement("button", env.document);
  env.RecoveryView.render(stateShowingRecovery(env.Core));

  // A second failed attempt changes the panel signature, so the view tears the
  // panel down and builds a new one. Focus must not be dropped on the floor.
  env.document.activeElement = outside;
  env.RecoveryView.render(stateShowingRecovery(env.Core, { attempts: 2 }));

  const backdrop = env.backdrop();
  assert.equal(
    env.document.activeElement,
    backdrop.querySelector(".btn.accent"),
    "a rebuild must put focus back on the Retry now button"
  );
});

test("Focus returns to the element that had it before the overlay appeared", (t) => {
  const env = loadRecoveryView();
  const trigger = new MockElement("button", env.document);
  env.document.activeElement = trigger;

  let state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  assert.notEqual(env.document.activeElement, trigger, "the overlay must take focus while visible");

  env.RecoveryView.render(stateHidingRecovery(env.Core, state));

  assert.equal(env.document.activeElement, trigger, "focus must be handed back to the trigger");
  assert.equal(trigger.focusCount, 1, "the trigger must be focused exactly once");
});

test("Focus is captured on the hidden->visible transition only, not on every render", (t) => {
  const env = loadRecoveryView();
  const trigger = new MockElement("button", env.document);
  env.document.activeElement = trigger;

  let state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);

  // Something inside the overlay takes focus while it is up. A re-render must
  // not adopt that as "what had focus before the overlay".
  const insider = new MockElement("button", env.document);
  env.document.activeElement = insider;
  env.RecoveryView.render(state);

  env.RecoveryView.render(stateHidingRecovery(env.Core, state));

  assert.equal(env.document.activeElement, trigger, "focus must return to the pre-overlay element");
  assert.equal(insider.focusCount, 0, "an element focused during the overlay must not be restored to");
});

test("Focus is not stolen back to the body when nothing had focus before the overlay", (t) => {
  const env = loadRecoveryView();
  // activeElement starts as document.body; restoring to it would blur whatever
  // the browser moved focus to on its own.
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  env.RecoveryView.render(stateHidingRecovery(env.Core, state));

  assert.equal(env.document.body.focusCount, 0, "the body must never be explicitly focused");
});

// -----------------------------------------------------------------------------
// Countdown announcement
// -----------------------------------------------------------------------------

test("The countdown announcer speaks the remaining seconds, singular at one", (t) => {
  const env = loadRecoveryView();
  let state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);

  const announcer = env.backdrop().querySelector(".recovery-countdown-announcer");
  assert.ok(announcer, "the overlay must carry a countdown live region");

  // Advancing time keeps the panel signature stable, so the view takes its
  // countdown-only path - the one that updates the announcer.
  const firstView = env.RecoveryView.render(env.Core.dispatch(state, { type: "TICK", dt: 500 }));
  assert.equal(firstView.waitSeconds, 2, "fixture must leave two seconds on the clock");
  assert.equal(announcer.textContent, "Next attempt in 2 seconds");

  const lastView = env.RecoveryView.render(env.Core.dispatch(state, { type: "TICK", dt: 1000 }));
  assert.equal(lastView.waitSeconds, 1, "fixture must leave one second on the clock");
  assert.equal(announcer.textContent, "Next attempt in 1 second", "one second must read singular");
});

test("The visible countdown and the announcement stay in step", (t) => {
  const env = loadRecoveryView();
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);

  const view = env.RecoveryView.render(env.Core.dispatch(state, { type: "TICK", dt: 500 }));
  const backdrop = env.backdrop();

  assert.equal(
    backdrop.querySelector(".recovery-countdown-value").textContent,
    `${view.waitSeconds} s`,
    "the visible countdown must show the derived seconds"
  );
  assert.equal(
    backdrop.querySelector(".recovery-countdown-announcer").textContent,
    `Next attempt in ${view.waitSeconds} seconds`,
    "the announcement must agree with the visible countdown"
  );
});
