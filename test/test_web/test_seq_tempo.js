// A tempo read off a track (data/seq_tempo.js, ADR 0058, #438).
//
// The invariant is the one-way door: an analyzed grid persists, and the steps
// placed on it carry beat indices, so a grid that sits early is a fault every
// saved sequence inherits and no later fix can quietly undo. The reference the
// analyzer is ported from timed an onset at its analysis window's start, about
// 18 ms early at 44.1 kHz; the port times it at the window's centre. A click
// track with a known click time is the measurement: the grid has to land where
// the clicks sound.
//
// Per test_web/README.md: the shipped module runs in a vm on a synthesised
// buffer shaped like the AudioBuffer the browser's decode hands it.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const load = () => {
  const sandbox = { window: {} };
  vm.createContext(sandbox);
  ["dome_lights.js", "seq_protocol_check.js", "seq_tempo.js"].forEach((name) =>
    vm.runInContext(fs.readFileSync(path.resolve(__dirname, "../../data", name), "utf8"), sandbox, { filename: name }),
  );
  return sandbox.window.SeqTempo;
};

// A click every beat from `firstSec`, each a short decaying burst.
const clickTrack = (bpm, seconds, firstSec, sampleRate = 44100) => {
  const n = seconds * sampleRate;
  const data = new Float32Array(n);
  for (let t = firstSec; t < seconds; t += 60 / bpm) {
    const start = Math.round(t * sampleRate);
    for (let i = 0; i < 400 && start + i < n; i++) data[start + i] = Math.sin(i * 0.3) * Math.exp(-i / 80);
  }
  return { sampleRate, length: n, duration: seconds, numberOfChannels: 1, getChannelData: () => data };
};

test("an analyzed grid lands where the clicks sound, not a window early", () => {
  const result = load().analyze(clickTrack(120, 10, 0.5));
  assert.ok(result.onsetsMs.length >= 8, "the clicks were not found");
  result.onsetsMs.slice(0, 8).forEach((onset, k) => {
    const click = 500 + k * 500;
    assert.ok(Math.abs(onset - click) <= 8, `the click at ${click} ms was placed at ${onset} ms`);
  });
  assert.ok(Math.abs(result.phaseMs - 500) <= 8, `beat 1 was placed at ${result.phaseMs} ms, not 500`);
});

// The same one-way door for the tempo itself (FIX 3, #438): the octave halving
// rounds the lag one frame off the correlation peak and the interpolation will
// not walk back, so 128 BPM clicks read 126.05. A grid stored at the wrong
// tempo drifts every beat-placed step further the later it sits.
test("an analyzed tempo reads the clicks' own BPM, not the halving's rounding", () => {
  const T = load();
  for (const bpm of [128, 150]) {
    const result = T.analyze(clickTrack(bpm, 30, 0.1));
    assert.ok(Math.abs(result.bpm - bpm) <= 0.3, `${bpm} BPM clicks read ${result.bpm}`);
  }
});
