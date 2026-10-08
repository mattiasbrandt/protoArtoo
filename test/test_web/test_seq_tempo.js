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
  // The phase is where beat 0 falls within one beat, so it is compared round
  // the beat: 499 and 5 are both a click at 500 on a 500 ms beat.
  const off = (((result.phaseMs - 500) % 500) + 750) % 500 - 250;
  assert.ok(Math.abs(off) <= 8, `the grid sits ${off} ms off the clicks`);
});

// FIX 4 (#14): FIX 2 still left the grid early by up to one hop, by where in
// its frame a click happened to fall. Over clicks at every position in a frame
// the grid has to be centred on them, not early on average.
test("an analyzed grid is centred on the clicks wherever they fall in a frame", () => {
  const T = load();
  const offs = [];
  for (let k = 0; k < 8; k++) {
    const first = 0.5 + (k * 512) / 8 / 44100; // eight positions across one hop
    const result = T.analyze(clickTrack(120, 12, first));
    const off = (((result.phaseMs - first * 1000) % 500) + 750) % 500 - 250;
    offs.push(off);
  }
  const mean = offs.reduce((a, b) => a + b, 0) / offs.length;
  assert.ok(Math.abs(mean) <= 2, `the grid sits ${mean.toFixed(1)} ms off on average: ${offs}`);
  offs.forEach((off) => assert.ok(Math.abs(off) <= 7, `a grid ${off} ms off the clicks: ${offs}`));
});

// FIX 3, bounded (#14): the climb fixes the halving's rounding, one lag, and
// must not walk on. Unbounded it walked three lags onto another peak on a real
// track (130.8 BPM read 141.4). A correlation that keeps rising away from
// where the halving landed is the case.
test("the climb after the halving moves one lag at most", () => {
  const T = load();
  const rising = (lag) => lag; // every step up looks better
  assert.equal(T.climb(rising, 40, 20, 80), 41);
  const falling = (lag) => -lag;
  assert.equal(T.climb(falling, 40, 20, 80), 39);
  const peak = (lag) => -Math.abs(lag - 41);
  assert.equal(T.climb(peak, 40, 20, 80), 41, "a peak one lag off is still reached");
  assert.equal(T.climb(rising, 80, 20, 80), 80, "never past the search window");
});

// TEMPO LEVELS (#14): the builder picks between the tempo heard and its half,
// two thirds, three halves and double, so a folded reading is a choice on
// screen rather than a number that looks right.
test("an analyzed tempo comes with its levels, the heard one at full strength", () => {
  const result = load().analyze(clickTrack(120, 20, 0.25));
  assert.deepEqual([...result.levels.map((l) => `${l.num}/${l.den}`)], ["1/2", "2/3", "1/1", "3/2", "2/1"]);
  const heard = result.levels[2];
  assert.equal(heard.bpm, result.bpm);
  assert.equal(heard.strength, 1);
  assert.equal(result.levels[0].bpm, Math.round(result.bpm * 5) / 10);
  assert.equal(result.levels[4].bpm, Math.round(result.bpm * 20) / 10);
  // A click every half second repeats every second too, and does not repeat
  // at two thirds of a beat: the half level is strong, the 3/2 weak.
  assert.ok(result.levels[0].strength > 0.5, `half: ${result.levels[0].strength}`);
  assert.ok(result.levels[3].strength < result.levels[0].strength, JSON.stringify(result.levels));
});

test("a track with no steady beat offers no levels", () => {
  const silent = { sampleRate: 44100, length: 441000, duration: 10, numberOfChannels: 1, getChannelData: () => new Float32Array(441000) };
  const result = load().analyze(silent);
  assert.equal(result.bpm, 0);
  assert.equal(result.levels.length, 0);
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
