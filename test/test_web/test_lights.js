// =============================================================================
// test/test_web/test_lights.js
//
// Lights (data/lights.js), on the surface as the browser runs it
// (helpers/lights_surface.js). Two invariants, and neither is the ticket's
// acceptance list typed out: what a heading says, which words a chip wears and
// what order the plates come in are the operator's to look at, and they are not
// here.
//
//   - THIS PAGE NAMES NO OUTPUT. Where a lead plugs in is Wiring's answer
//     (CONTEXT.md "Lights", ADR 0067). The page reads the Outputs - it has to,
//     to know what lights a Part - so it is one line of code away from printing
//     one, and three iterations of this surface were rejected for doing exactly
//     that with board labels and "AUX" on screen.
//
//   - A LIVE FRAME NEVER TAKES A PICK OUT OF A BUILDER'S HAND. The status
//     stream repeats itself every few seconds; this page shipped a version that
//     redrew on every frame, which reset a half-made mode-and-color choice.
//     That is a defect this repo has shipped, and it is invisible to a suite
//     that only renders once.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { boot, droid } from "./helpers/lights_surface.js";

const ready = async (options) => {
  const env = boot(options);
  await env.runSections();
  await env.settle();
  return env;
};

test("Lights names no Output the firmware reported, and no pin, label or address", async () => {
  const answer = droid();
  const env = await ready({ answer });
  const shown = env.text();

  // Everything the droid called its Outputs, and the words the operator ruled
  // off this surface, read from the answer rather than restated.
  const names = Object.values(answer.config.components)
    .flatMap((entry) => [entry.label, entry.address, entry.enabledField, entry.typeField])
    .filter(Boolean);
  for (const name of [...names, "AUX", "aux_led_pin"]) {
    assert.ok(!shown.includes(name), `Lights shows "${name}", which is Wiring's answer and not this page's`);
  }

  // And it is drawing the lights, so the absence above is a rule rather than an
  // empty page: the Part on the lit lead knows what lights it.
  assert.match(shown, /Data Panel/);
  assert.match(shown, /LED STRIP|LED strip/);
});

test("a status frame does not take a half-made pick out of a builder's hand", async () => {
  const env = await ready();
  const plate = env.plateFor("psiFront");
  assert.ok(plate, "the dome's Front PSI is on the page");

  const chip = (label) => env.plateFor("psiFront").querySelectorAll(".light-mode")
    .find((node) => node.textContent === label);
  const picked = () => env.plateFor("psiFront").querySelectorAll(".light-mode")
    .find((node) => node.getAttribute("aria-checked") === "true")?.textContent;

  chip("Alarm").fire("click", {});
  assert.equal(picked(), "Alarm", "the chip a builder pressed is the chip that is marked");

  // The same reading again, which is what the stream sends between changes.
  env.pushStatus({ auxLed: { pin: 2, r: 0, g: 90, b: 255, effect: "solid", available: true } });
  await env.settle();
  assert.equal(picked(), "Alarm", "an unchanged frame redrew the plate and lost the pick");

  // And a frame that really does say something new: the strip's own reading
  // changed, so the page redraws - and still owes the builder their pick.
  env.pushStatus({ auxLed: { pin: 2, r: 255, g: 0, b: 0, effect: "blink", available: true } });
  await env.settle();
  assert.equal(picked(), "Alarm", "a redraw reset a pick the builder had already made");
});

// A dome light is commandable only where the dome answers to its name. The two
// halves of that sentence live in two files that drift independently - the
// catalog's aliases (docs/droid-parts.yaml) and the dome's own target list
// (data/seq_protocol_check.js, mirroring src/protocol_check.cpp) - so a light
// can gain a control the dome would refuse, or lose one it would take, without
// either file looking wrong on its own. The Magic Panel is the case that keeps
// it honest: it is a dome light the dome has no DL: target for.
test("a dome light takes a command only where the dome answers to its name", async () => {
  const env = await ready();
  const chips = (partId) => env.plateFor(partId).querySelectorAll(".light-mode");

  assert.ok(chips("psiFront").length > 0, "the Front PSI is a DL: target, so it takes a command");
  assert.equal(chips("magicPanel").length, 0, "the dome answers to no name for the Magic Panel");

  // And the command carries the name the dome knows it by, not the Part id.
  chips("psiFront").find((node) => node.textContent === "Alarm").fire("click", {});
  env.flushTimers();
  await env.settle();

  const sent = env.posts.find((post) => post.path === "/api/dome/cmd");
  assert.ok(sent, "picking asks the dome, with no button to press after it");
  assert.match(sent.body.cmd, /^DL:FPSI:ALARM:/);
});
