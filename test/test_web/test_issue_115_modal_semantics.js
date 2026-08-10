// =============================================================================
// test/test_web/test_issue_115_modal_semantics.js
//
// Modal and live-region semantics of the recovery overlay (issue #115): the
// backdrop must present as a dialog, and the countdown must be announced
// through a separate polite live region so a screen reader hears the seconds
// tick without the whole panel being re-read every second.
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

test("The overlay presents as a modal dialog", (t) => {
  const { backdrop } = showOverlay();

  assert.equal(backdrop.getAttribute("role"), "dialog");
  assert.equal(backdrop.getAttribute("aria-modal"), "true");
  assert.equal(
    backdrop.getAttribute("aria-label"),
    "Page recovery overlay",
    "a dialog with no accessible name is announced as an unlabelled group"
  );
  assert.equal(
    backdrop.getAttribute("tabindex"),
    "-1",
    "the backdrop must be programmatically focusable without becoming a Tab stop"
  );
});

test("The countdown is announced through a separate polite live region", (t) => {
  const { backdrop } = showOverlay();
  const announcer = announcerOf(backdrop);

  assert.equal(announcer.getAttribute("role"), "status");
  assert.equal(announcer.getAttribute("aria-live"), "polite");
  assert.equal(
    announcer.getAttribute("aria-atomic"),
    "false",
    "atomic announcements would re-read the whole panel on every countdown tick"
  );
});

test("The dialog itself carries no aria-atomic", (t) => {
  const { backdrop } = showOverlay();

  // aria-atomic on the backdrop is what caused the over-announcement this
  // issue was filed for: every countdown update re-read the entire panel.
  assert.equal(
    backdrop.getAttribute("aria-atomic"),
    null,
    "only the announcer may carry aria-atomic"
  );
});

test("The live region is offscreen rather than hidden", (t) => {
  const { backdrop } = showOverlay();
  const announcer = announcerOf(backdrop);

  // display:none or visibility:hidden would stop it being announced at all;
  // offscreen positioning keeps it in the accessibility tree.
  assert.equal(announcer.style.position, "absolute");
  assert.equal(announcer.style.left, "-10000px");
  assert.notEqual(announcer.style.display, "none");
  assert.notEqual(announcer.style.visibility, "hidden");
});

test("The live region survives a panel rebuild", (t) => {
  const env = loadRecoveryView();
  env.RecoveryView.render(stateShowingRecovery(env.Core));
  const backdrop = env.backdrop();
  const announcer = announcerOf(backdrop);

  // A second failed attempt changes the panel signature and rebuilds it.
  env.RecoveryView.render(stateShowingRecovery(env.Core, { attempts: 2 }));

  assert.equal(
    announcerOf(backdrop),
    announcer,
    "replacing the live region node would drop the announcement the reader is tracking"
  );
});

test("The live region survives the overlay being dismissed", (t) => {
  const env = loadRecoveryView();
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  const backdrop = env.backdrop();
  const announcer = announcerOf(backdrop);

  env.RecoveryView.render(stateHidingRecovery(env.Core, state));

  assert.equal(announcerOf(backdrop), announcer, "the live region must outlive the panel content");
  assert.deepEqual(backdrop.children, [announcer], "nothing but the live region may remain");
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

test("Dialog semantics are applied once and stay applied across renders", (t) => {
  const env = loadRecoveryView();
  const state = stateShowingRecovery(env.Core);
  env.RecoveryView.render(state);
  const backdrop = env.backdrop();

  env.RecoveryView.render(stateHidingRecovery(env.Core, state));
  env.RecoveryView.render(stateShowingRecovery(env.Core, { attempts: 2 }));

  assert.equal(env.backdrop(), backdrop, "the overlay must be reused, not rebuilt per render");
  assert.equal(backdrop.getAttribute("role"), "dialog");
  assert.equal(backdrop.getAttribute("aria-modal"), "true");
  assert.equal(
    backdrop.querySelectorAll(".recovery-countdown-announcer").length,
    1,
    "repeated renders must not stack up duplicate live regions"
  );
});
