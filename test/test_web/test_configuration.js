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
// What IS this page's, and is new, is the other half of that move: a light's
// settings belong to the Output that carries it, and a save from here carrying
// a droid-wide light field would fight the surface that owns it, one
// overwriting the other with whatever it last read. The two names below are
// the retired ones (#413), which is exactly what a regression would bring
// back. That is a harness-only fact - two writers for one field looks like
// nothing on screen - which is what earns it a test
// (test/test_web/README.md).
//
// The dome's IP (#369) is the other: a declared Setting on this page that
// saves on its own. What only the harness sees is that it goes out under the
// Setting's own name, alone - a bad address riding the component save would
// refuse the toggles with it - and that a refusal reaches the builder in the
// Setting's words from the shipped table, with what they typed still there.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "node:fs";

import { ready } from "./helpers/configuration_surface.js";

// The shipped PAApi, for its refusal wording and the error its transport
// throws; the surface harness's own PAApi is a stand-in.
const shippedApi = () => {
  const window = {};
  vm.runInNewContext(readFileSync(new URL("../../data/web_api.js", import.meta.url), "utf8"), { window, URLSearchParams });
  return window.PAApi;
};

test("Configuration's save carries the components and nothing of the LED strip", async () => {
  const env = await ready();

  // A Component Picker pick, which is what makes this page save.
  env.window.PAConfiguration.applyComponentPick({ toggleIds: ["enable-drive"], enabled: false });
  await env.settle();

  const save = env.posts.find((post) => post.path === "/api/config");
  assert.ok(save, "a pick saves");
  assert.equal(save.get("enableDrive"), "false", "the pick itself is carried");
  for (const field of ["aux_led_count", "aux_led_pin"]) {
    assert.equal(save.get(field), null, `Configuration writes ${field}, a droid-wide light field no surface owns any more`);
  }
});

test("The dome's IP saves alone as its Setting, and a refusal says why in its words", async () => {
  const env = await ready();
  const api = shippedApi();
  const settings = env.parsed.getElementById("protor2link-settings");
  const input = env.parsed.getElementById("protor2link-wifi-peer-ip");
  const feedback = env.parsed.getElementById("protor2link-feedback");
  assert.equal(settings.hidden, false, "the Dome Controller is fitted, so its link settings show");

  // The droid refuses what its Ipv4 rule refuses, the way it answers
  // (src/config_settings.cpp): the field, out-of-range, no accepts.
  const accept = env.window.PAApi.postForm;
  env.window.PAApi.messageFor = api.messageFor;
  env.window.PAApi.postForm = async (path, body) => {
    const sent = body instanceof URLSearchParams ? body.get("protoR2linkWifiPeerIp") : body.protoR2linkWifiPeerIp;
    if (sent === "10.0.0") {
      env.posts.push({ path, get: (key) => (key === "protoR2linkWifiPeerIp" ? sent : null) });
      throw new api.ApiError("protoR2linkWifiPeerIp must be empty or a valid IPv4 address", {
        kind: "http", status: 400, field: "protoR2linkWifiPeerIp", reason: "out-of-range",
      });
    }
    return accept(path, body);
  };

  input.value = " 10.0.0 ";
  input.fire("change");
  await env.settle();
  const save = env.posts.at(-1);
  assert.equal(save.path, "/api/config");
  assert.equal(save.get("protoR2linkWifiPeerIp"), "10.0.0", "it goes out under the Setting's name, trimmed");
  assert.equal(save.get("enableProtoR2link"), null, "and alone: a refused address must not take the toggles with it");
  assert.equal(feedback.textContent, "Dome's IP address must be empty or an address like 192.168.4.2");
  assert.equal(input.value, "10.0.0", "what the builder typed stays in the box beside the reason");

  // Not fitted: the link's settings go with it.
  env.window.PAConfiguration.applyComponentPick({ toggleIds: ["enable-protor2link"], enabled: false });
  await env.settle();
  assert.equal(settings.hidden, true, "a Dome Controller not fitted has no link settings on screen");
});
