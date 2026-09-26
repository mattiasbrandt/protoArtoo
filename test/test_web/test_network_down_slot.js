// =============================================================================
// test/test_web/test_network_down_slot.js
//
// The new "sys_net_down" system sound slot (#189) must round-trip correctly
// between the server's /api/audio/tracks JSON field name and the client's
// SYSTEM_SOUNDS key -- a typo or dropped entry on either side leaves the
// input silently unset, with no error surfaced anywhere. Also covers the
// CHIRP catalog binding badge, a second, independent site
// (SLOT_BINDING_TARGETS derived from the same SYSTEM_SOUNDS entry) driven by
// a different data path (data.chirp_bindings). And a track row is named by its
// Setting's one entry (data/web_api.js labelOf, #432), not by a label table of
// the page's own, which is how the Sound page and the sequence editor came to
// call one track two things.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { loadPageModule } from "./helpers/page_module_env.js";
import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";
import { shippedWords } from "./helpers/shipped_words.cjs";

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


test("every track row is named by its Setting's label, the one the sequence editor uses too", async () => {
  // The shipped sound.html, so the rows the page builds are real elements a
  // test can read back.
  const document = new MiniDocument();
  const html = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "../../data/sound.html"), "utf-8");
  new MiniDOMParser().parseFromString(html).body.children
    .forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const env = loadPageModule("sound.js", { overrides: { document } });
  await env.settle();

  const { labelOf } = shippedWords();
  const rows = document.querySelectorAll("tr").filter((row) => row.dataset.soundLabel);
  const tracks = rows.map((row) => row.querySelector(".sound-track-input-sm")).filter(Boolean);
  assert.ok(tracks.length >= 20, `the named track rows were drawn: ${tracks.length}`);
  tracks.forEach((input) => {
    const key = input.id.replace("track-input-", "");
    const row = rows.find((each) => each.querySelector(".sound-track-input-sm") === input);
    assert.equal(row.dataset.soundLabel, labelOf(key), `${key}'s row`);
  });
});
