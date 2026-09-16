// Sound page copy for the MP3 Trigger (#396): wiring gotchas, a missing clip,
// and a range this module cannot play.

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

const CAP_MP3 = 0x0d; // STATUS_QUERY | TRACK_COUNT | CURRENT_TRACK

const audioStatus = (extra = {}) => ({
  driver: "MP3Trigger",
  link_ok: true,
  device: "none",
  play_state: "stop",
  total_tracks: 42,
  current_track: 0,
  missing_track: 0,
  capabilities: CAP_MP3,
  ...extra,
});

test("the MP3 Trigger card names the 3.3 V jumper and the 9600 card file", async () => {
  const env = loadPageModule("sound.js", {
    respond: () => ({ data: audioStatus() }),
  });
  const note = env.element("mp3-wire-note");
  await env.runSection("audio-status", {});
  await env.settle();
  assert.ok(
    String(note.textContent).includes("3.3 V jumper"),
    `expected 3.3 V jumper on the card, got: ${note.textContent}`
  );
  assert.ok(
    String(note.textContent).includes("#BAUD 9600"),
    `expected the 9600 card file on the card, got: ${note.textContent}`
  );
});

test("a missing clip is named as a card problem, not wiring", async () => {
  const env = loadPageModule("sound.js", {
    respond: () => ({ data: audioStatus({ missing_track: 99 }) }),
  });
  const el = env.element("mp3-missing-track");
  await env.runSection("audio-status", {});
  await env.settle();
  assert.ok(
    String(el.textContent).includes("Track 99 is not on the card"),
    `expected the missing clip named, got: ${el.textContent}`
  );
  assert.ok(
    String(el.textContent).includes("not the wiring"),
    `expected the misdiagnosis named, got: ${el.textContent}`
  );
});
