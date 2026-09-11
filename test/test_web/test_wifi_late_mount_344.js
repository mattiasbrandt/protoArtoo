// =============================================================================
// test/test_web/test_wifi_late_mount_344.js
//
// The WiFi surface under the Operator Shell (#344): it mounts long after the
// session announced pa:assets-ready, so what it reports -- and when it starts
// polling -- has to come from its own sections settling rather than from a
// once-per-session event it registered for too late.
//
// Runs the shipped data/wifi.js.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// The state a surface mounted into the shell finds: the session settled some
// time ago, and pa:assets-ready is long gone.
const mountLate = () =>
  loadPageModule("wifi.js", {
    respond: (path) => {
      if (path === "/api/wifi") return { wifi: { mode: "client", staSsid: "bench" } };
      return {};
    },
    overrides: { PAAssetsReady: true },
  });

const settled = (sectionsStable) => ({
  detail: {
    sections: [{ name: "wifi-config", status: sectionsStable ? "done" : "pending" }],
    resourcesReady: true,
    sectionsStable,
  },
});

test("the load outcome is reported when this surface's sections settle", async () => {
  const env = mountLate();
  await env.settle();

  const feedback = env.element("wifi-settings-feedback");
  env.emit("window", "pa:bootstrap-change", settled(true));

  assert.match(
    feedback.textContent,
    /WiFi settings loaded/,
    "a surface that mounted after the session settled still says what it loaded",
  );
  assert.ok(env.intervals.length > 0, "and its polling actually starts");
});

test("nothing is reported while this surface's sections are still running", async () => {
  const env = mountLate();
  await env.settle();

  const feedback = env.element("wifi-settings-feedback");
  feedback.textContent = "";
  env.emit("window", "pa:bootstrap-change", settled(false));

  assert.equal(feedback.textContent, "", "an unsettled bootstrap is not a loaded page");
  assert.equal(env.intervals.length, 0, "and polling has not started");
});

test("the outcome is reported once, not on every later bootstrap change", async () => {
  const env = mountLate();
  await env.settle();

  env.emit("window", "pa:bootstrap-change", settled(true));
  const intervalsAfterFirst = env.intervals.length;

  const feedback = env.element("wifi-settings-feedback");
  feedback.textContent = "";
  env.emit("window", "pa:bootstrap-change", settled(true));

  assert.equal(feedback.textContent, "", "the second settle is not a second load");
  assert.equal(env.intervals.length, intervalsAfterFirst, "and does not start a second poll");
});
