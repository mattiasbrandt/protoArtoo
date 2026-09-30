// =============================================================================
// test/test_web/test_foot_drive_capabilities.js
//
// The Foot Drive page shows the wheel controller's readings because the fitted
// Foot Drive declares that it reports them, never because it recognises the
// controller (ADR 0042: "a per-member UI possible without the page hard-coding
// member knowledge in JavaScript"; #304, #446).
//
// One invariant, driven in both directions with the ids swapped against the
// words, so neither "always show the card" nor "show it for the hoverboard"
// survives: the page decides on the capability word of the one supported,
// included Foot Drive in GET /api/identity/components, and on nothing else.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// DRIVE_CAP_REPORTS_FEEDBACK, from include/drive_capabilities.h.
const REPORTS_FEEDBACK = 0x01;

// A lineup whose one fitted Foot Drive is `fittedId` declaring `capabilities`;
// the other Foot Drive rows are roadmap and carry nothing.
const lineup = (fittedId, capabilities) => ({
  categories: [{ id: "foot_drive", name: "Foot Drive", selectable: 1, member_key: null, active_member: null }],
  parts: ["hoverboard", "sabertooth_2x25", "flipsky_mini_v6_vesc"].map((id) => ({
    id,
    category: "foot_drive",
    status: id === fittedId ? "supported" : "roadmap",
    included: id === fittedId,
    capabilities: id === fittedId ? capabilities : 0,
  })),
});

// Answers the card's `hidden` rather than the card: the DOM stub answers any
// property, `then` included, so an element returned from an async function is
// awaited as a promise that never settles.
const wheelControllerCardHiddenAfterLoad = async (fittedId, capabilities) => {
  const env = loadPageModule("drive.js", {
    respond: (path) => (path === "/api/identity/components" ? { data: lineup(fittedId, capabilities) } : {}),
  });
  const card = env.element("wheel-controller-card");
  // As data/drive.html ships it: hidden until the lineup says otherwise.
  card.hidden = true;
  await env.runSection("foot-drive");
  return card.hidden;
};

test("the wheel controller's card follows the fitted Foot Drive's word, not its name", async () => {
  assert.strictEqual(await wheelControllerCardHiddenAfterLoad("sabertooth_2x25", REPORTS_FEEDBACK), false,
    "a fitted Foot Drive that declares feedback shows the wheel controller's card, whatever it is called");

  assert.strictEqual(await wheelControllerCardHiddenAfterLoad("hoverboard", 0), true,
    "a fitted Foot Drive that declares no feedback gets no card, even when it is the hoverboard");
});
