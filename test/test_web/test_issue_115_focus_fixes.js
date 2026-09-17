// =============================================================================
// test/test_web/test_issue_115_focus_fixes.js
//
// Focus management for the recovery overlay (issue #115):
//   1. focus is saved once, on the hidden->visible transition
//   2. focus moves into the overlay when it appears, and back out when it goes
//   3. keyboard reach stops at the failed surface and nowhere else
//
// (3) was "Tab is contained inside the overlay and wraps at both ends" until
// #359. The wrap was a focus trap, and a trap around the panel is also a trap
// away from the Latching Estop the Operator Shell renders on the chrome -- the
// one control an operator reaches for when a surface will not load. ADR 0048
// had already put this view inside the work area; the containment moved with
// it, onto the surface, as `inert`. #115's concern is unchanged: Tab must not
// wander into a page that is not there.
//
// Every assertion here observes what the shipped code in data/page_bootstrap.js
// DID: which element ended up focused, which node it took out of the tab order,
// what the announcer says.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import {
  MockElement,
  loadRecoveryView,
  shellFrame,
  stateShowingRecovery,
  stateHidingRecovery,
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

// Puts the overlay on screen inside the shell's frame, which is where it is
// drawn in the product: the work area holds the surface, the chrome sits
// around it. Hands back the frame so a test can read what the view did to it.
const showOverlayInShell = ({ surfaces = 1 } = {}) => {
  const env = loadRecoveryView();
  const frame = shellFrame(env.document, { surfaces });
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  const backdrop = env.backdrop();
  assert.ok(backdrop, "render must mount the recovery backdrop");
  return { ...env, frame, state, backdrop };
};

// -----------------------------------------------------------------------------
// Keyboard containment
//
// The failed surface leaves the tab order; nothing else does.
// -----------------------------------------------------------------------------

test("The surface the view is reporting on leaves the tab order", (t) => {
  const { frame } = showOverlayInShell();

  assert.equal(
    frame.surfaces[0].inert,
    true,
    "Tab must not wander into a page that is not there (#115)"
  );
});

test("The chrome around the work area stays in the tab order", (t) => {
  const { frame } = showOverlayInShell();

  // The topbar carries the Latching Estop and the plate carries its ESTOP
  // cell. A surface that cannot load is exactly when an operator reaches for
  // one of them (#359, ADR 0048).
  for (const [name, node] of [
    ["the topbar", frame.top],
    ["the nav rail", frame.nav],
    ["the Status Plate", frame.status],
  ]) {
    assert.ok(!node.inert, `${name} must stay reachable while a surface is failing to load`);
  }
});

test("The view is mounted inside the work area, not over the whole page", (t) => {
  const { frame, backdrop } = showOverlayInShell();

  assert.equal(
    backdrop.parentElement,
    frame.content,
    "the Page Recovery View renders inside the content region (ADR 0048)"
  );
});

test("The surface comes back into the tab order when the view goes", (t) => {
  const env = loadRecoveryView();
  const frame = shellFrame(env.document);
  const showing = stateShowingRecovery(env.Core);
  env.RecoveryView.render(showing);
  assert.equal(frame.surfaces[0].inert, true, "the fixture must start with the view up");

  env.RecoveryView.render(stateHidingRecovery(env.Core, showing));

  assert.equal(frame.surfaces[0].inert, false, "a settled page must be usable again");
});

test("A surface that mounts while the view is already up leaves the tab order too", (t) => {
  const env = loadRecoveryView();
  const frame = shellFrame(env.document);
  const showing = stateShowingRecovery(env.Core);
  env.RecoveryView.render(showing);

  // The shell attaches a surface's node before its markup arrives, so this is
  // the ordinary first-load path rather than an edge case.
  const arriving = env.document.createElement("div");
  arriving.className = "surface";
  frame.content.appendChild(arriving);
  env.RecoveryView.render(showing);

  assert.equal(arriving.inert, true, "a surface mounted under the view must not be tabbable");
});

test("A surface the shell detached while the view was up is not left inert", (t) => {
  const env = loadRecoveryView();
  const frame = shellFrame(env.document);
  const showing = stateShowingRecovery(env.Core);
  env.RecoveryView.render(showing);
  const left = frame.surfaces[0];
  assert.equal(left.inert, true, "the fixture must start with the leaving surface inert");

  // The chrome is live under this view, so the operator can navigate: the
  // shell detaches what they left and attaches what they opened. A surface
  // released only by walking the work area's current children would come back
  // dead.
  const opened = env.document.createElement("div");
  opened.className = "surface";
  frame.content.replaceChildren(opened, env.backdrop());
  env.RecoveryView.render(showing);
  env.RecoveryView.render(stateHidingRecovery(env.Core, showing));

  assert.equal(left.inert, false, "the surface the operator left must be usable on return");
  assert.equal(opened.inert, false, "and so must the one they opened");
});

test("The view intercepts no keystroke, so Tab out of the panel is the browser's", (t) => {
  const { backdrop } = showOverlayInShell();

  const keyHandlers = backdrop.eventListeners.filter((listener) => listener.event === "keydown");
  assert.deepEqual(
    keyHandlers,
    [],
    "a key handler here is a focus trap, and a trap holds focus away from the estop"
  );
});

test("Nothing outside the work area is ever taken out of the tab order", (t) => {
  // No frame at all: a document whose body is the host has no chrome to keep
  // live, and marking its children would take the whole page out.
  const env = loadRecoveryView();
  const sibling = env.document.createElement("div");
  env.document.body.appendChild(sibling);

  env.RecoveryView.render(stateShowingRecovery(env.Core));

  assert.ok(!sibling.inert, "the body's children are not a surface and must not be marked");
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
