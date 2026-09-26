// =============================================================================
// test/test_web/test_settings_ruled_by_the_droid.js
//
// What a Setting accepts is ruled on by the droid, once (CONTEXT.md "Setting";
// ADR 0068, amended 2026-09-26). A page keeps no copy of a firmware range: it
// sends what the builder typed, and a value the droid will not take comes back
// as a refusal worded by data/web_api.js.
//
// The defect this guards: page-side copies are how the browser and the
// firmware came to disagree - the Dome page clamped a typed pulse into its own
// 1000..2000 and saved a number nobody typed, and a second range table in the
// firmware had already drifted from the one that rules. So the invariant is
// what goes out: the typed value, unclamped and unblocked.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// Types into an element the way a builder does: set its value, then deliver
// the `input` its page listens for.
const type = (env, id, value) => {
  const element = env.element(id);
  element.value = value;
  element.__listeners.filter((l) => l.type === "input").forEach(({ handler }) => handler({ target: element }));
};

const configPosts = (env) =>
  env.requests.filter((request) => request.method === "POST" && request.path === "/api/config")
    .map((request) => request.opts.body);

test("the Dome page sends a pulse the droid will refuse exactly as typed", async () => {
  const env = loadPageModule("dome.js");
  await env.settle();
  type(env, "dome-min-pulse", "1000");
  type(env, "dome-max-pulse", "2000");
  type(env, "dome-speed-limit", "100");
  type(env, "dome-neutral", "999");
  await env.settle();

  const sent = configPosts(env).at(-1);
  assert.ok(sent, "a save went out");
  assert.equal(sent.domeEscNeutralUs, "999", "the typed pulse, not one moved into a range the page holds");
});

test("the Drive page sends three presets that clash, for the droid to refuse", async () => {
  const env = loadPageModule("drive.js");
  await env.settle();
  env.element("speed-preset-slow").value = "300";
  env.element("speed-preset-normal").value = "300";
  type(env, "speed-preset-turbo", "300");
  await env.settle();

  const sent = configPosts(env).at(-1);
  assert.ok(sent, "a save went out rather than being stopped by the page");
  assert.deepEqual([sent.speedPresetSlow, sent.speedPresetNormal, sent.speedPresetTurbo],
    ["300", "300", "300"]);
});
