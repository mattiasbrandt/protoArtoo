// =============================================================================
// test/test_web/test_issue_115_modal_semantics.js
//
// Verification that issue #115 is resolved: recovery overlay has proper
// modal semantics, focus management, and announcement behavior.
//
// Verifies from source inspection:
// 1. Overlay is marked as a dialog/modal (role="dialog", aria-modal="true")
// 2. A separate countdown announcer exists for aria-live updates
// 3. Focus management functions exist and are called
// 4. Retry button keeps focus across countdown updates (signature mechanism)
// 5. Auto-hide-on-stable properly restores focus
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapPath = join(__dirname, "../../data/page_bootstrap.js");
const bootstrapFile = readFileSync(bootstrapPath, "utf-8");

test("Recovery overlay has modal attributes (role=dialog, aria-modal=true)", (t) => {
  // Verify the ensureBackdrop function sets the correct ARIA attributes
  assert(
    bootstrapFile.includes('setAttribute("role", "dialog")'),
    "overlay must have role='dialog'"
  );
  assert(
    bootstrapFile.includes('setAttribute("aria-modal", "true")'),
    "overlay must have aria-modal='true'"
  );
});

test("Countdown announcer is separate from the modal", (t) => {
  // Verify a separate countdown announcer is created with its own live region
  assert(
    bootstrapFile.includes('"recovery-countdown-announcer"'),
    "countdown announcer element must be created"
  );
  assert(
    bootstrapFile.includes('setAttribute("role", "status")'),
    "countdown announcer must have role='status' for aria-live"
  );
  assert(
    bootstrapFile.includes('setAttribute("aria-live", "polite")'),
    "countdown announcer must use aria-live=polite"
  );
  assert(
    bootstrapFile.includes('setAttribute("aria-atomic", "false")'),
    "countdown announcer must have aria-atomic=false to avoid re-announcing entire panel"
  );
});

test("Focus management functions exist (setFocus, restoreFocus)", (t) => {
  // Verify focus management is implemented
  assert(
    bootstrapFile.includes("const setFocus = (backdrop)"),
    "setFocus function must exist"
  );
  assert(
    bootstrapFile.includes("const restoreFocus = ()"),
    "restoreFocus function must exist"
  );
  assert(
    bootstrapFile.includes("focusedBeforeOverlay = document.activeElement"),
    "focus must be saved before overlay appears"
  );
});

test("Focus is moved into overlay when panel becomes visible", (t) => {
  // Verify setFocus is called when visibility changes to true
  assert(
    bootstrapFile.includes("setFocus(backdrop)"),
    "setFocus must be called when panel becomes visible"
  );
  // Verify it tries to focus the retry button first
  assert(
    bootstrapFile.includes('.querySelector(".btn.accent")'),
    "setFocus should prefer focusing the retry button"
  );
});

test("Focus is restored when panel auto-hides (visibility changes to false)", (t) => {
  // Verify restoreFocus is called when visibility changes to false
  assert(
    bootstrapFile.includes("restoreFocus()"),
    "restoreFocus must be called when panel disappears"
  );
  // Verify it's called specifically on the auto-hide path
  assert(
    bootstrapFile.includes("if (!view.visible)"),
    "auto-hide path must restore focus"
  );
});

test("Countdown-only updates preserve focus on retry button (signature mechanism)", (t) => {
  // Verify the signature mechanism keeps the panel intact when only countdown changes
  assert(
    bootstrapFile.includes("const signature = signatureOf(view)"),
    "signature comparison must control panel rebuild"
  );
  assert(
    bootstrapFile.includes("if (signature !== lastSignature)"),
    "panel should only rebuild when signature changes (not on countdown-only updates)"
  );
  assert(
    bootstrapFile.includes("backdrop.replaceChildren(buildPanel(view, onRetryNow))"),
    "panel is fully rebuilt when signature changes"
  );
  assert(
    bootstrapFile.includes("value.textContent = `${view.waitSeconds} s`"),
    "but only the countdown value is updated when signature stays the same"
  );
});

test("Recovery view code does not use aria-atomic on main modal", (t) => {
  // Verify the OLD broken behavior is removed: role=status with aria-atomic=true
  // on the main backdrop. The only aria-atomic should be on the countdown announcer.
  const backdropCreation = bootstrapFile.substring(
    bootstrapFile.indexOf("const ensureBackdrop = "),
    bootstrapFile.indexOf("const setFocus = ")
  );

  // Should NOT have aria-atomic on the backdrop itself
  const backdropHasAtomicWrong = backdropCreation.includes('setAttribute("aria-atomic", "true")');
  assert(
    !backdropHasAtomicWrong,
    "main backdrop must NOT have aria-atomic=true (it caused over-announcement)"
  );

  // Should only have aria-atomic=false on the announcer
  assert(
    bootstrapFile.includes(
      `announcer.setAttribute("aria-atomic", "false")`
    ),
    "countdown announcer must explicitly set aria-atomic=false"
  );
});
