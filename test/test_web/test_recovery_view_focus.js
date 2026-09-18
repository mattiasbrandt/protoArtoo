// =============================================================================
// test/test_web/test_recovery_view_focus.js
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

import { loadRecoveryView, shellFrame, stateShowingRecovery, stateHidingRecovery } from "./helpers/recovery_dom.js";

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

test("The surface comes back into the tab order when the view goes", (t) => {
  const env = loadRecoveryView();
  const frame = shellFrame(env.document);
  const showing = stateShowingRecovery(env.Core);
  env.RecoveryView.render(showing);
  assert.equal(frame.surfaces[0].inert, true, "the fixture must start with the view up");

  env.RecoveryView.render(stateHidingRecovery(env.Core, showing));

  assert.equal(frame.surfaces[0].inert, false, "a settled page must be usable again");
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

// -----------------------------------------------------------------------------
// Countdown announcement
// -----------------------------------------------------------------------------

