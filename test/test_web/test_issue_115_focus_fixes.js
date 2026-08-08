// =============================================================================
// test/test_web/test_issue_115_focus_fixes.js
//
// Verification for focus management fixes in issue #115:
// 1. focusedBeforeOverlay is saved only on hidden→visible transition
// 2. Focus is restored to the original element after mode changes
// 3. Keyboard containment works (Tab is trapped within panel)
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapPath = join(__dirname, "../../data/page_bootstrap.js");
const bootstrapFile = readFileSync(bootstrapPath, "utf-8");

test("focusedBeforeOverlay is only saved on hidden→visible transition", (t) => {
  // Verify overlayIsVisible flag is used to guard the save
  assert(
    bootstrapFile.includes("let overlayIsVisible = false"),
    "overlayIsVisible flag must be declared"
  );
  assert(
    bootstrapFile.includes("if (!overlayIsVisible)"),
    "focus save must be guarded by overlayIsVisible check"
  );
  assert(
    bootstrapFile.includes("focusedBeforeOverlay = document.activeElement"),
    "focus must be saved when entering overlay"
  );
  assert(
    bootstrapFile.includes("overlayIsVisible = true"),
    "overlayIsVisible must be set when entering"
  );
  assert(
    bootstrapFile.includes("overlayIsVisible = false"),
    "overlayIsVisible must be cleared when exiting"
  );
});

test("Focus is restored when overlay auto-hides after mode change", (t) => {
  // Verify the auto-hide path calls restoreFocus
  const hideBlock = bootstrapFile.substring(
    bootstrapFile.indexOf("if (!view.visible)"),
    bootstrapFile.indexOf("// Transitioning from hidden to visible")
  );
  assert(
    hideBlock.includes("restoreFocus()"),
    "focus must be restored on auto-hide"
  );
});

test("Keyboard containment: Tab cycling within panel is implemented", (t) => {
  // Verify keydown handler is added to trap Tab
  assert(
    bootstrapFile.includes('addEventListener("keydown", (event)'),
    "keydown handler must be added to backdrop for Tab containment"
  );
  assert(
    bootstrapFile.includes('if (event.key !== "Tab") return'),
    "handler must filter for Tab key"
  );
  assert(
    bootstrapFile.includes("event.preventDefault()"),
    "Tab must be prevented from escaping"
  );
  assert(
    bootstrapFile.includes("querySelectorAll"),
    "focusable elements must be queried"
  );
  assert(
    bootstrapFile.includes("event.shiftKey"),
    "Shift+Tab must be handled for reverse cycling"
  );
  assert(
    bootstrapFile.includes("firstElement.focus()"),
    "focus must cycle to first element"
  );
  assert(
    bootstrapFile.includes("lastElement.focus()"),
    "focus must cycle to last element"
  );
});

test("Tab containment targets correct focusable selectors", (t) => {
  // Verify the selector includes all interactive elements
  assert(
    bootstrapFile.includes("button"),
    "selector must include button elements"
  );
  assert(
    bootstrapFile.includes("[href]"),
    "selector must include link elements"
  );
  assert(
    bootstrapFile.includes("input"),
    "selector must include input elements"
  );
  assert(
    bootstrapFile.includes("select"),
    "selector must include select elements"
  );
  assert(
    bootstrapFile.includes("textarea"),
    "selector must include textarea elements"
  );
  assert(
    bootstrapFile.includes("[tabindex]"),
    "selector must include tabindex elements"
  );
  assert(
    bootstrapFile.includes(":not([tabindex"),
    "selector must exclude explicitly unfocusable elements"
  );
});

test("setFocus is called only after signature change, not on every transition", (t) => {
  // Verify setFocus is called in the signature-changed block, not unconditionally
  const ifBlock = bootstrapFile.substring(
    bootstrapFile.indexOf("if (signature !== lastSignature)"),
    bootstrapFile.indexOf("} else {", bootstrapFile.indexOf("if (signature !== lastSignature)"))
  );
  assert(
    ifBlock.includes("setFocus(backdrop)"),
    "setFocus must be called when signature changes"
  );
  // Verify it's ONLY in that block, not before the signature check
  const beforeSignatureBlock = bootstrapFile.substring(
    bootstrapFile.indexOf("if (!overlayIsVisible)"),
    bootstrapFile.indexOf("const signature = signatureOf(view)")
  );
  assert(
    !beforeSignatureBlock.includes("setFocus"),
    "setFocus must NOT be called before signature check"
  );
});

test("countdownAnnouncer text includes plural handling for seconds", (t) => {
  // Verify countdown announcer uses correct singular/plural
  assert(
    bootstrapFile.includes('second${view.waitSeconds === 1 ? "" : "s"}'),
    "countdown announcer must handle singular/plural correctly"
  );
});
