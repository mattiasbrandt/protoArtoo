// =============================================================================
// data/wiring_outputs.js
//
// Wiring's one control: which Outputs are wired, and which one carries the
// LED strip rather than a servo (operator, 2026-09-18 on #369).
// The controls and their save are data/output_settings.js's; this file only
// mounts them on this surface. It is its own file so data/wiring.js - the
// sheet generator - stays what it has always been: a reference that writes
// nothing.
// =============================================================================
(() => {
  "use strict";
  window.PAOutputSettings?.mount("wired", {
    body: document.getElementById("wiring-outputs-body"),
    feedback: document.getElementById("wiring-outputs-feedback"),
  });
})();
