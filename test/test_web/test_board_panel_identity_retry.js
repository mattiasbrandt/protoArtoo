// =============================================================================
// test/test_web/test_board_panel_identity_retry.js
//
// The board's picture on Configuration, and the two defects it once shipped.
// Kept under this name on purpose: the single board panel this file was
// written for is gone (#369), and the board is now a card in the Component
// Picker's Body Controller row - but what these tests hold down is the same,
// and a renamed test file reads as a deleted one to the slice gate.
//
// #202: identity - and now the lineup - can arrive after the one-shot
// deferred-asset sweep has already run, so a photograph must wait in
// data-deferred-src before that sweep and be asked for directly after it; an
// image fetch must never compete with the event stream's opening.
//
// #404: the shell takes a surface the builder has left out of the document,
// and a card redrawn then cannot find the page's drawing by id. On the legacy
// asset set, which carries drawings and no photographs, falling through to a
// photograph left the builder an empty frame where the drawing had been.
//
// Run on the real surface (helpers/configuration_surface.js): configuration.html
// with each asset set's _product_art.html inlined, so a lost symbol or a moved
// host turns this suite red.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { ready } from "./helpers/configuration_surface.js";

// The running board's card: the Body Controller this image was built for.
const boardCard = (env, id) => env.plate("body_controller", id);

test("default set: before assets-ready, the board's photograph waits in data-deferred-src", async () => {
  const env = await ready({ set: "default", board: "firebeetle2", assetsReady: false });
  const image = boardCard(env, "firebeetle2").querySelector("img");
  assert.ok(image, "the default set asks for the board's photograph");
  assert.equal(image.dataset.deferredSrc, "/firebeetle2.webp");
  assert.equal(image.src, undefined, "and does not fetch it before the sweep");
});

test("default set: after assets-ready (a late lineup), the board's photograph is asked for directly", async () => {
  const env = await ready({ set: "default", board: "artoo_esp32", assetsReady: true });
  const image = boardCard(env, "artoo_pcb").querySelector("img");
  assert.equal(image.src, "/artoo_pcb.webp");
  assert.equal(image.dataset.deferredSrc, undefined);
});

test("legacy set: the board's drawing is still there after the builder has been on another screen", async () => {
  const env = await ready();
  const drawn = () => boardCard(env, "artoo_pcb").querySelector("use")?.getAttribute("href");
  assert.equal(drawn(), "#art-artoo_pcb", "the legacy set draws the board");

  // Every card is redrawn after a pick lands; this one lands while the
  // surface is out of the document.
  env.leave();
  env.press(env.plate("sound", "mp3_trigger")).fire("click", {});
  await env.settle();
  env.returnTo();

  assert.equal(drawn(), "#art-artoo_pcb", "the drawing is what the builder comes back to");
  assert.equal(boardCard(env, "artoo_pcb").querySelector("img"), null, "and no photograph was asked for");
});
