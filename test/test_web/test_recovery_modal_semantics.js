// =============================================================================
// test/test_web/test_recovery_modal_semantics.js
//
// Dialog and live-region semantics of the recovery overlay (issue #115): the
// backdrop must present as a dialog, and the countdown must be announced
// through a separate polite live region so a screen reader hears the seconds
// tick without the whole panel being re-read every second.
//
// It is a dialog and not a MODAL one, since #359. aria-modal="true" tells a
// screen reader that everything outside the dialog is inert, and outside this
// one is the chrome carrying the Latching Estop, which ADR 0048 keeps live for
// exactly the surface that failed to load. What is really inert is the
// surface, and the view says so on the surface.
//
// All assertions read attributes off the element the shipped code built.
// Earlier versions wrapped those assertions in `if (backdrop) { if (announcer)
// {` - a missing element silently passed - and checked the backdrop's own
// aria-atomic by matching source text. Both are gone. Issue #146.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadRecoveryView, stateShowingRecovery, stateHidingRecovery } from "./helpers/recovery_dom.js";

const showOverlay = () => {
  const env = loadRecoveryView();
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  const backdrop = env.backdrop();
  assert.ok(backdrop, "the recovery overlay must be mounted");
  return { ...env, state, backdrop };
};

const announcerOf = (backdrop) => {
  const announcer = backdrop.querySelector(".recovery-countdown-announcer");
  assert.ok(announcer, "the overlay must carry a countdown live region");
  return announcer;
};

test("The overlay does not claim the rest of the page is inert", (t) => {
  const { backdrop } = showOverlay();

  assert.equal(
    backdrop.getAttribute("aria-modal"),
    null,
    "aria-modal would hide the chrome's Latching Estop from a screen reader (#359)"
  );
});

test("Dismissing the overlay takes the active markers off the page", (t) => {
  const env = loadRecoveryView();
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  const backdrop = env.backdrop();

  assert.ok(backdrop.classList.contains("active"), "the visible overlay must be marked active");
  assert.ok(
    env.document.body.classList.contains("recovery-active"),
    "the page must be marked so the rest of the UI can dim behind the overlay"
  );

  env.RecoveryView.render(stateHidingRecovery(env.Core, state));

  assert.ok(!backdrop.classList.contains("active"), "a dismissed overlay must not stay active");
  assert.ok(
    !env.document.body.classList.contains("recovery-active"),
    "the page must be released when the overlay goes"
  );
});

