// =============================================================================
// test/test_web/test_wifi_save_and_apply.js
//
// Save and "apply and reboot" on WiFi reach the droid when pressed.
//
// Both went through PABootstrap.submitCommand, which 857f4787 removed as
// unused while these two presses still called it: each threw a TypeError and
// sent nothing, so a builder could not change the droid's WiFi from the web
// (#355 finding 2). The stub bootstrap here has no submitCommand either, the
// same as the shipped one, so a press that still reached for it would fail.
//
// Runs the shipped data/wifi.js.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

const mount = () =>
  loadPageModule("wifi.js", {
    respond: (path) => {
      if (path === "/api/wifi") return { wifi: { mode: "client", staSsid: "bench" } };
      if (path === "/api/config") return { wifi: { mode: "client", staSsid: "bench", pendingApply: true } };
      return {};
    },
    overrides: { PAAssetsReady: true },
  });

// What a browser does with a press: every handler the module bound runs.
const press = (env, id, type, event = {}) => {
  const handlers = env.element(id).__listeners.filter((l) => l.type === type);
  assert.ok(handlers.length > 0, `wifi.js bound no "${type}" handler to #${id}`);
  return Promise.all(handlers.map(({ handler }) => handler(event)));
};

const loaded = async () => {
  const env = mount();
  await env.settle();
  for (const name of env.sectionNames()) await env.runSection(name);
  await env.settle();
  return env;
};

test("Save sends the settings to the droid, and the page stays put", async () => {
  const env = await loaded();
  let prevented = false;
  await press(env, "wifi-settings-form", "submit", { preventDefault: () => { prevented = true; } });
  await env.settle();

  assert.ok(prevented, "the form does not navigate away");
  assert.ok(
    env.requests.some((r) => r.method === "POST" && r.path === "/api/wifi"),
    `Save must reach POST /api/wifi; the page sent ${JSON.stringify(env.requests.map((r) => `${r.method} ${r.path}`))}`,
  );
});

test("Apply and reboot sends the reboot once there is something to apply", async () => {
  const env = await loaded();
  await press(env, "wifi-apply-reboot-button", "click");
  await env.settle();

  assert.ok(
    env.requests.some((r) => r.method === "POST" && r.path === "/api/reboot"),
    `Apply must reach POST /api/reboot; the page sent ${JSON.stringify(env.requests.map((r) => `${r.method} ${r.path}`))}`,
  );
});
