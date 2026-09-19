// =============================================================================
// test/test_web/test_configuration.js
//
// Configuration (data/configuration.js), on the surface as the browser runs it
// (helpers/configuration_surface.js).
//
// The LED strip's line names the Output that carries it by what the board
// prints beside it - GPIO 5 on the FireBeetle 2, ARM4 on the Artoo - read from
// the firmware's own Output entries: the one whose ledStripPin is the routed
// aux_led_pin (ADR 0033 Amendment 2026-09-19). The page keeps no list of its
// own, so a route the firmware reports no Output for is not dressed up with a
// name this page made up.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { ready } from "./helpers/configuration_surface.js";

const withStripOutputs = (pin) => (config) => ({
  ...config,
  components: {
    ...config.components,
    // The ids and the pairing of pin to Output follow no pattern on purpose.
    w9: { enabled: true, label: "GPIO 5", address: "ledc:4", ledStripPin: 2, enabledField: "e9", typeField: "t9", type: "rgb" },
    w3: { enabled: false, label: "GPIO 51", address: "ledc:5", ledStripPin: 3, enabledField: "e3", typeField: "t3", type: "none" },
  },
  aux_led_pin: pin,
});

test("the LED strip's line names the Output that carries it, as the board prints it", async () => {
  const probe = await ready();
  const env = await ready({ config: withStripOutputs(2)(probe.config) });
  const route = env.parsed.getElementById("aux-led-route-status").textContent;
  const badge = env.parsed.getElementById("aux-led-route-badge").textContent;

  assert.equal(route, "On GPIO 5");
  assert.equal(badge, "GPIO 5");
});

test("a route the firmware reports no Output for is not given a name", async () => {
  const probe = await ready();
  const env = await ready({ config: withStripOutputs(1)(probe.config) });

  assert.equal(env.parsed.getElementById("aux-led-route-status").textContent, "Not routed");
  assert.doesNotMatch(env.parsed.getElementById("aux-led-route-badge").textContent, /AUX|ARM|GPIO/);
});
