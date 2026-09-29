// =============================================================================
// test/test_web/test_dashboard_sleep.js
//
// The Dashboard has two entrances to Sleep - the topbar button and the switch
// in Controls (#399, operator 2026-09-29) - and they are one act: the same
// request, behind the same pending guard. Two presses that each sent their own
// POST would put the droid to sleep and then, from the second, wake it or sleep
// it again, on a controller that sheds connections under load.
//
// Runs the REAL data/app.js with the real status stream and Live Reading
// (helpers/page_module_env.js) and asserts on the requests it made.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";
import { statusFrame } from "./helpers/fake_droid.js";

// Presses a control the way a browser does: the click handlers it registered.
const press = (element) =>
  element.__listeners.filter((listener) => listener.type === "click").forEach(({ handler }) => handler({}));

test("the Sleep switch and the topbar button are one act: one request while it is pending, and the switch follows the droid", async () => {
  let answer = null;
  const env = loadPageModule("app.js", {
    respond: (path) => {
      // The Sleep request stays in flight until the test lets it answer.
      if (path === "/api/sleep" || path === "/api/wake") {
        return new Promise((resolve) => {
          answer = resolve;
        });
      }
      return { data: {} };
    },
  });
  env.pushStatus(statusFrame({ sleepMode: false }));

  const switchEl = env.element("sleep-switch");
  const checked = [];
  switchEl.setAttribute = (name, value) => {
    if (name === "aria-checked") checked.push(value);
  };

  press(switchEl);
  // While that one is pending, neither entrance sends another.
  press(switchEl);
  press(env.element("sleep-toggle"));
  await env.settle();

  const sleeps = () => env.requests.filter((r) => r.path === "/api/sleep" || r.path === "/api/wake");
  assert.deepStrictEqual(
    sleeps().map((r) => `${r.method} ${r.path}`),
    ["POST /api/sleep"],
    "a second press, on either entrance, sent its own request while the first was pending",
  );
  assert.strictEqual(switchEl.disabled, true, "the switch takes no press while the act is pending");
  assert.deepStrictEqual(checked, [], "the press alone moved the switch; only the droid's frame may");

  answer({ data: { ok: true, sleepMode: true } });
  env.pushStatus(statusFrame({ sleepMode: true }));
  await env.settle();
  assert.strictEqual(switchEl.disabled, false);
  assert.strictEqual(checked.at(-1), "true", "the droid said it slept, and the switch did not follow");
});
