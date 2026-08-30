// =============================================================================
// test/test_web/test_issue_189_network_down_slot.js
//
// The new "sys_net_down" system sound slot (#189) must round-trip correctly
// between the server's /api/audio/tracks JSON field name and the client's
// SYSTEM_SOUNDS key -- a typo or dropped entry on either side leaves the
// input silently unset, with no error surfaced anywhere. Also covers the
// CHIRP catalog binding badge, a second, independent site
// (SLOT_BINDING_TARGETS derived from the same SYSTEM_SOUNDS entry) driven by
// a different data path (data.chirp_bindings).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

test("sound.js hydrates the Network Link Lost track input from the server field", async () => {
  const env = loadPageModule("sound.js", {
    respond: (path) => {
      if (path === "/api/audio/tracks") return { data: { sys_net_down: 55 } };
      return { data: {} };
    },
  });

  await env.settle();

  assert.strictEqual(
    env.element("sys-track-input-sys_net_down").value,
    55,
    "loadTracks() must find the input by the exact server field name for the new system slot"
  );
});

test("sound.js leaves the Network Link Lost input untouched when the server omits the field", async () => {
  const env = loadPageModule("sound.js", { respond: () => ({ data: {} }) });

  await env.settle();

  assert.strictEqual(
    env.element("sys-track-input-sys_net_down").value,
    "",
    "an absent field must not be confused with an explicit 0 -- the input stays at its default"
  );
});

test("sound.js renders the CHIRP binding badge for the Network Link Lost slot", async () => {
  const env = loadPageModule("sound.js", {
    respond: (path) => {
      if (path === "/api/audio") {
        return { data: { capabilities: 0x21, link_ok: true } }; // status query + catalog
      }
      if (path === "/api/audio/tracks") {
        return {
          data: {
            sys_net_down: 55,
            chirp_bindings: { sys_net_down: { bank: 2, page: "b", index: 9 } },
          },
        };
      }
      return { data: {} };
    },
  });

  await env.runSection("audio-status");
  await env.settle();

  assert.strictEqual(
    env.element("chirp-binding-sys_net_down").textContent,
    "CHIRP B2B #9",
    "the badge must resolve through the same key the new SYSTEM_SOUNDS entry declares"
  );
});
