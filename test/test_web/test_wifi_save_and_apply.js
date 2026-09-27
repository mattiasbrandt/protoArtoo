// =============================================================================
// test/test_web/test_wifi_save_and_apply.js
//
// Save and "apply and reboot" on WiFi reach the droid when pressed, and a
// save the droid refuses is said in the builder's words on the box it is about.
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
import vm from "node:vm";
import { readFileSync } from "node:fs";

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

// A save the droid refuses is said in the builder's words, on the box the
// refusal names. The droid's sentence names the field by its wire name
// ("staSsid is required for WiFi Client Mode") and reached the screen as it
// was, and the page found the box by searching that sentence for a field name
// (#355 finding 11, ADR 0059). The field, reason and accepts are the contract;
// each refusal here keeps the droid's sentence as a decoy, and the last one's
// sentence names no field at all, so only the field can put it on its box.
test("a refused WiFi save says what to fix in the builder's words, on the box it is about", async () => {
  const shipped = {};
  vm.runInNewContext(readFileSync(new URL("../../data/web_api.js", import.meta.url), "utf8"), {
    window: shipped,
    URLSearchParams,
  });
  const words = shipped.PAApi;
  let refusal = null;
  const env = loadPageModule("wifi.js", {
    respond: (path, opts) => {
      if (path === "/api/wifi" && opts.method === "POST") {
        throw new words.ApiError(refusal.error, { kind: "http", status: 400, ...refusal.envelope });
      }
      if (path === "/api/wifi") return { wifi: { mode: "client", staSsid: "bench" } };
      if (path === "/api/config") return { wifi: { mode: "client", staSsid: "bench", pendingApply: true } };
      return {};
    },
    overrides: { PAAssetsReady: true },
  });
  await env.settle();
  for (const name of env.sectionNames()) await env.runSection(name);
  await env.settle();
  env.window.PAApi.messageFor = words.messageFor;

  const BOXES = ["wifi-sta-ssid", "wifi-sta-password", "wifi-ap-ssid", "wifi-ap-password"];
  const invalid = new Map();
  BOXES.forEach((id) => {
    env.element(id).setAttribute = (name, value) => {
      if (name === "aria-invalid") invalid.set(id, value);
    };
  });
  const form = (mode, values) => {
    env.element("wifi-mode-client").checked = mode === "client";
    env.element("wifi-mode-standalone-ap").checked = mode === "standalone_ap";
    BOXES.forEach((id) => { env.element(id).value = values[id] ?? ""; });
  };

  const cases = [
    {
      mode: "client",
      values: { "wifi-sta-ssid": "" },
      error: "staSsid is required for WiFi Client Mode",
      envelope: { field: "staSsid", reason: "out-of-range" },
      box: "wifi-sta-ssid",
      says: "Name of the network it joins is required for WiFi Client Mode",
    },
    {
      mode: "standalone_ap",
      values: { "wifi-ap-ssid": "FieldKit", "wifi-ap-password": "short" },
      error: "apPassword must be empty or 8..63 characters",
      envelope: { field: "apPassword", reason: "out-of-range" },
      box: "wifi-ap-password",
      says: "Password of the droid's own network is too short or too long",
    },
    {
      mode: "standalone_ap",
      values: { "wifi-ap-ssid": "" },
      error: "a name is needed here",
      envelope: { field: "apSsid", reason: "out-of-range" },
      box: "wifi-ap-ssid",
      says: "Name of the droid's own network is required for Standalone AP Mode",
    },
  ];
  for (const each of cases) {
    refusal = each;
    invalid.clear();
    form(each.mode, each.values);
    await press(env, "wifi-settings-form", "submit", { preventDefault: () => {} });
    await env.settle();

    assert.equal(env.element(`${each.box}-error`).textContent, each.says, `the ${each.box} box says what to fix`);
    assert.equal(invalid.get(each.box), "true", `the ${each.box} box is marked as the one refused`);
    for (const other of BOXES.filter((id) => id !== each.box)) {
      assert.equal(env.element(`${other}-error`).textContent, "", `${other} says nothing about ${each.envelope.field}`);
    }
    const feedback = env.element("wifi-settings-feedback").textContent;
    assert.equal(feedback, each.says, "the form's line says the same");
    for (const said of [feedback, env.element(`${each.box}-error`).textContent]) {
      assert.doesNotMatch(said, /staSsid|staPassword|apSsid|apPassword|wifiMode/, "no wire name reaches the screen");
    }
  }
});
