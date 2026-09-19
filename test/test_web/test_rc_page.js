// =============================================================================
// test/test_web/test_rc_page.js
//
// The RC page (data/rc.js) and the names it shows for what a channel does.
//
// An action about one Output - the toggles - is named by what the running
// board prints beside that Output ("GPIO 4 Toggle" on the FireBeetle 2), and
// only the firmware knows that, so the page's built-in fallback list carries
// no Output toggle at all (ADR 0033 Amendment 2026-09-19;
// tools/check_action_registry_drift.py holds the fallback to it). The
// invariant: a binding to one is named from GET /api/actions once that answer
// arrives, whichever of the page's reads finished first. Before the fix a map
// read that landed first left the binding showing its bare token for good.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

const ACTIONS = [
  { id: 1, name: "drive.action.speed", display_name: "Speed", domain: "drive", description: "", token: "drive_speed", testable: false, one_shot: false, safety_critical: false },
  { id: 6, name: "servo.action.toggle-aux1", display_name: "GPIO 4 Toggle", domain: "servo", description: "Open or close the part on GPIO 4.", token: "aux1_toggle", testable: true, one_shot: false, safety_critical: false },
];

const respond = (path) => {
  if (path === "/api/config") return { data: { rc: { inputMode: "single_sbus", sbus: { recvCh2: false } }, components: {} } };
  if (path === "/api/rc/map") {
    return {
      data: {
        mode: "single_sbus",
        map: [
          { source: "sbus1", channel: 1, action: "drive_speed" },
          { source: "sbus1", channel: 6, action: "aux1_toggle" },
        ],
      },
    };
  }
  if (path === "/api/actions") return { data: ACTIONS };
  return { data: {} };
};

test("a binding to an Output's toggle is named by the board once the firmware's list arrives", async () => {
  const env = loadPageModule("rc.js", { respond });
  await env.settle();

  // The map lands first: the fallback cannot name the toggle, so it cannot
  // be the last word either.
  await env.runSection("rc-mode-mapping");
  await env.runSection("rc-action-targets");
  await env.settle();

  const summary = env.element("rc-summary-body").innerHTML;
  assert.match(summary, /<td>GPIO 4 Toggle<\/td>/, "the binding is named as the board names the Output");
  assert.doesNotMatch(summary, /<td>aux1_toggle<\/td>/, "and never left as its stored token");
});
