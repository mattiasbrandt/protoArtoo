// =============================================================================
// test/test_web/test_component_picker.js
//
// The Component Picker (#369, CONTEXT.md "Component Picker") on the surface it
// is drawn into: data/configuration.html with Configuration's own chain and
// guided Setup over it, and the lineup read from a controller that answers the
// way src/web/api_identity_serializers.cpp does, row for row from
// include/component_registry.inc - so a registry row added tomorrow is a card
// here too, and nothing below restates the lineup.
//
// What earns a test here, and nothing else:
//   - a roadmap card is not a control: pressing it reaches the droid never;
//   - picking is applying: the pick is on its way to the controller at once,
//     with no staging step in between;
//   - one host, two homes: Configuration's cards and guided Setup's rail read
//     one answer after a pick;
//   - a missing photograph never reads as greyed;
//   - Configuration never sends the LED strip route it no longer holds, which
//     would clear the one Wiring set;
//   - another page shows the product the droid holds, read from the saved
//     answer - the receiver by the wire its input mode speaks - as a card with
//     nothing to press: it is chosen only here (#412).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { ready } from "./helpers/configuration_surface.js";

test("a roadmap card is not a control, and pressing it reaches the droid never", async () => {
  const env = await ready();
  const roadmap = env.plate("sound", "dfplayer_mini");
  assert.ok(roadmap, "the roadmap product is on the lineup");
  assert.equal(env.press(roadmap), null, "it carries no button to press");
  assert.equal(roadmap.querySelectorAll("button").length, 0);
  roadmap.fire("click", {});
  roadmap.children.forEach((child) => child.fire("click", {}));
  await env.settle();
  assert.deepEqual(env.posts, [], "and nothing was sent");
});

test("picking is applying: the pick leaves for the controller at once, and both homes read it", async () => {
  const env = await ready();
  env.press(env.plate("sound", "mp3_trigger")).fire("click", {});
  // No timer is flushed: a pick that waited on one would be a staging buffer.
  assert.equal(env.posts.length, 1, "the pick was sent without waiting");
  assert.equal(env.posts[0].path, "/api/config");
  assert.equal(env.posts[0].get("soundMember"), "mp3_trigger");
  await env.settle();

  // Guided Setup draws this same host as its step, and its rail reads the
  // picker's answer: the two homes cannot show different choices.
  assert.ok(env.plate("sound", "mp3_trigger").classList.contains("is-chosen"));
  assert.equal(env.railAnswer("sound"), "MP3 Trigger");
});

test("a card whose photograph is missing lays out like the rest and is never greyed", async () => {
  const env = await ready({ set: "default", board: "firebeetle2" });
  const card = env.plate("sound", "chirp");
  const image = card.querySelector("img");
  assert.ok(image, "the default set asks for a photograph");
  image.onerror();
  assert.ok(card.querySelector(".component-card-art"), "the frame stays, the same size");
  assert.equal(card.querySelector("img"), null, "the broken image is gone");
  for (const family of ["availability-settled-no", "availability-change-elsewhere"]) {
    assert.equal(card.classList.contains(family), false, `a missing photo is not ${family}`);
  }
  assert.equal(env.press(card).disabled, false, "and it can still be picked");
});

test("a Configuration save never sends the LED strip route it no longer holds", async () => {
  // The route is set on Wiring now (data/output_settings.js). Configuration
  // used to derive aux_led_pin from its own AUX rows and send it on every
  // save; with those rows gone, sending it would clear the route Wiring set.
  const env = await ready();
  env.press(env.plate("dome_controller", "not-fitted")).fire("click", {});
  assert.equal(env.posts.length, 1);
  assert.equal(env.posts[0].get("aux_led_pin"), null);
});

test("another page is shown the radio and receiver the droid holds, as cards with nothing to press", async () => {
  const env = await ready();
  const picker = env.window.ComponentPicker;

  assert.equal(picker.chosenPart("radio_controller")?.id, "hotrc_ds650");
  // dual_sbus is two SBUS receivers: the receiver shown is the SBUS product.
  assert.equal(picker.chosenReceiverPart()?.id, "rc_transmitter_sbus");

  const card = picker.shownCard(picker.chosenReceiverPart());
  assert.equal(card.dataset.option, "rc_transmitter_sbus");
  assert.equal(card.querySelectorAll("button").length, 0, "shown here, never chosen here");
});
