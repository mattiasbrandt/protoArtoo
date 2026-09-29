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
//   - a Body Controller this image was not built for says how to get it - an
//     upload of its own build, with the route to Firmware - where it used to
//     sit greyed with nothing to do about it (operator, 2026-09-19 on #371).
//   - the Radio Controller's Not fitted is one answer: no receiver and every
//     RC channel off, and no later save from this page turns a channel back on
//     from a tick it still held; a droid holding it reads Not fitted in both
//     homes (CONTEXT.md "Radio Controller", operator 2026-09-29 on #369).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { ready, configured } from "./helpers/configuration_surface.js";

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
  // What carries a light is the Output's own stored type now, set on Wiring
  // (data/output_settings.js, ADR 0067). Configuration used to derive
  // aux_led_pin from its own AUX rows and send it on every save; there is no
  // droid-wide route left to send, and a save carrying one would be a second
  // store for a fact the types already hold (#413).
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

test("a Body Controller this image was not built for carries the route to its own build", async () => {
  for (const [board, other] of [["artoo_esp32", "firebeetle2"], ["firebeetle2", "artoo_pcb"]]) {
    const env = await ready({ board });
    const plateOf = (id) => env.plate("body_controller", id);
    const running = board === "artoo_esp32" ? "artoo_pcb" : "firebeetle2";
    const route = (plate) => plate.children.find((child) => child.tagName === "A") || null;

    assert.equal(route(plateOf(other))?.getAttribute("href"), "#firmware", `${board}: the other board says where to upload`);
    assert.equal(route(plateOf(running)), null, `${board}: the board it runs on has nothing to switch to`);
  }
});

test("the Radio Controller's Not fitted turns every channel off, and no later save turns one back on", async () => {
  const env = await ready();
  const channels = [1, 2, 3, 4, 5, 6].map((n) => `enableRcCh${n}`);
  env.press(env.plate("radio_controller", "not-fitted")).fire("click", {});
  assert.equal(env.posts.length, 1, "the pick was sent without waiting");
  assert.equal(env.posts[0].get("rcInputMode"), "not_fitted");
  channels.forEach((field) => assert.equal(env.posts[0].get(field), "false", `${field} goes off with it`));
  await env.settle();

  // Every save sends every tick this page holds: one it still held on would
  // switch a channel back on under a droid with no radio.
  env.press(env.plate("dome_controller", "not-fitted")).fire("click", {});
  await env.settle();
  channels.forEach((field) => assert.equal(env.posts.at(-1).get(field), "false", `${field} stays off`));
});

test("a droid with no radio fitted reads Not fitted in both homes", async () => {
  const config = configured();
  config.rc = { inputMode: "not_fitted", sbus: { recvCh2: false } };
  const env = await ready({ config });
  assert.ok(env.plate("radio_controller", "not-fitted").classList.contains("is-chosen"));
  assert.equal(env.plate("radio_controller", "hotrc_ds650").classList.contains("is-chosen"), false);
  assert.equal(env.railAnswer("rc"), "Not fitted");
});
