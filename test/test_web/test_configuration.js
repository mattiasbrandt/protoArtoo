// =============================================================================
// test/test_web/test_configuration.js
//
// Configuration (data/configuration.js), on the surface as the browser runs it
// (helpers/configuration_surface.js).
//
// The two suites this file used to carry asserted that Configuration's LED
// strip line named the Output carrying the strip, by what the board prints
// beside it. That surface is gone: the strip left this page for Lights, and
// Lights names no Output at all (ADR 0067). Neither test relocated, because
// neither subject exists:
//
//   - naming an Output by its board label is still asserted where Outputs are
//     still named, on Wiring's and Servos' plates
//     (test_output_settings.js "the plates are the Outputs the firmware
//     reports"), so re-pointing it here would have been a second copy;
//   - "a route the firmware reports no Output for is not given a name" became
//     the stronger rule on the new surface - this page names no Output ever -
//     and is asserted in test_lights.js.
//
// What IS this page's, and is new, is the other half of that move: the strip's
// length is Lights' field now, and a save from here that still carried
// aux_led_count would fight it silently, one surface overwriting the other with
// whatever it last read. That is a harness-only fact - two writers for one
// field looks like nothing on screen - which is what earns it a test
// (test/test_web/README.md).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { ready } from "./helpers/configuration_surface.js";

test("Configuration's save carries the components and nothing of the LED strip", async () => {
  const env = await ready();

  // A Component Picker pick, which is what makes this page save.
  env.window.PAConfiguration.applyComponentPick({ toggleId: "enable-drive", enabled: false });
  await env.settle();

  const save = env.posts.find((post) => post.path === "/api/config");
  assert.ok(save, "a pick saves");
  assert.equal(save.get("enableDrive"), "false", "the pick itself is carried");
  for (const field of ["aux_led_count", "aux_led_pin"]) {
    assert.equal(save.get(field), null, `Configuration still writes ${field}, which is Lights' to save`);
  }
});
