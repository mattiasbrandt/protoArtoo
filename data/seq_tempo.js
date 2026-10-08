// =============================================================================
// data/seq_tempo.js
//
// Where a Sequence Tempo comes from (ADR 0058, ADR 0060, #438): the builder
// taps along to the track playing on the droid, or drops their own copy of the
// track in to be analyzed. A typed number is the third route and needs nothing
// here. Every route says how sure it is, and the builder sets the downbeat.
//
// The analyzer below is PORTED, not reimplemented, from
// r2d2-astromech-simulator v1.79.0 (175ad1b), src/js/maestro/music.js:47-114
// (musicMono, musicAnalyse). ADR 0058 took the port and its notice
// deliberately: the constants and the estimator are the reference's, and
// writing them out fresh would produce the same function under a different
// label. The measured defects below are corrected before any grid is stored, because
// a stored grid and the beat indices on its steps make a later correction
// re-resolve every beat-placed step in every saved sequence:
//   FIX 1  corr() was an unnormalised dot product, so a shorter lag summed
//          more terms and scored higher (-2.0 BPM at 128, -2.3 at 150; the
//          mechanism behind Cantina's ~200 BPM reading as 127.8). It divides
//          by the overlap length (nF - lag).
//   FIX 2  an onset was timed at its analysis window's start, about 18 ms
//          early at 44.1 kHz. It is timed at the window's centre:
//          t += win / (2 * sr).
//   FIX 3  (#438, operator-approved 2026-10-01) the octave halving rounds the
//          lag, which can land one frame off the correlation peak, and the
//          parabolic step only looks half a lag either side, so it refused to
//          walk back: 128 BPM clicks read 126.05 and 150 read 147.66. The lag
//          climbs to the local maximum of corr() before the interpolation.
//          This, not FIX 1, is what the -2.0 / -2.3 BPM figures were.
//          (#14, operator 2026-10-08) The climb is held to one lag either
//          side of where the halving landed, which is all the rounding can
//          cost. Unbounded, it walked three lags onto a different peak on a
//          real track: 130.8 BPM became 141.4.
//   FIX 4  (#14, operator 2026-10-08) FIX 2 left the grid early by up to one
//          hop, because an onset is found in the first frame that holds it,
//          wherever in that frame it falls: 2-8 ms early (mean 5.1) over 32
//          click tracks. Half a hop more centres it: -2.2 to +3.3 ms.
//
// TEMPO LEVELS (#14, operator 2026-10-08). A beat tracker cannot tell a tempo
// from its half, its double, or a three-against-two feel, and one number hides
// that it guessed. analyze() returns the tempo it heard with its half, two
// thirds, three halves and double, each with how strongly the track repeats at
// that tempo against the one it heard, and the builder picks. Faster than 190
// BPM is reached that way, not by widening the search: a wider search doubled
// tracks it reads right today.
//
// The ported part carries the reference's notice, as its licence requires:
//
//   Copyright (c) 2026 Mike Eddington
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// CONFIDENCE is 0..1 on every source, because one field carrying two scales
// would make "how sure" mean different things on one screen. The analyzer's
// own figure is best / mean(corr) (ADR 0058), stored as 1 - mean/best: a best
// lag no better than the average one is 0, twice the average is 0.5. Taps are
// 1 - 5 * (spread of the gaps / the average gap), so gaps wandering by a tenth
// of a beat read 0.5. A typed tempo is 1: the builder said so.
//
// Pure apart from the one decode, which is the browser's: the droid never
// holds this audio and no route fetches it (ADR 0046). Nothing here is sent
// anywhere; the tempo it returns is saved with the sequence like any edit.
// =============================================================================

(() => {
  "use strict";

  // ---------------------------------------------------------------------------
  // The port (music.js:47-114), with FIX 1 and FIX 2 marked where they land.
  // ---------------------------------------------------------------------------

  // A mono mix of every channel (musicMono, music.js:47-54).
  const mono = (buf) => {
    const n = buf.length;
    const out = new Float32Array(n);
    for (let c = 0; c < buf.numberOfChannels; c++) {
      const d = buf.getChannelData(c);
      for (let i = 0; i < n; i++) out[i] += d[i] / buf.numberOfChannels;
    }
    return out;
  };

  // The tempo levels offered beside the one heard, as num/den of it, slowest
  // first: half, two thirds, the tempo heard, three halves, double.
  const LEVELS = [[1, 2], [2, 3], [1, 1], [3, 2], [2, 1]];

  // FIX 3, bounded (#14): from `lag`, step to a neighbour while it correlates
  // higher, never more than one lag from where it started.
  const climb = (corr, lag, lagLo, lagHi) => {
    const from = lag;
    for (;;) {
      if (lag + 1 <= Math.min(lagHi, from + 1) && corr(lag + 1) > corr(lag)) lag++;
      else if (lag - 1 >= Math.max(lagLo, from - 1) && corr(lag - 1) > corr(lag)) lag--;
      else return lag;
    }
  };

  // musicAnalyse (music.js:55-114). Takes anything shaped like an AudioBuffer
  // and returns the grid in milliseconds: bpm to one decimal (0 when there is
  // no steady beat), where beat 0 sits, every beat inside the track, the
  // onsets it found, and the confidence described in the header.
  const analyze = (buf) => {
    const sr = buf.sampleRate;
    const samples = mono(buf);
    const hop = 512;
    const win = 1024;
    const nF = Math.max(1, Math.floor((samples.length - win) / hop));
    const energy = new Float32Array(nF);
    for (let f = 0; f < nF; f++) {
      let e = 0;
      const o = f * hop;
      for (let i = 0; i < win; i++) {
        const v = samples[o + i] || 0;
        e += v * v;
      }
      energy[f] = Math.sqrt(e / win);
    }
    // onset envelope: rising energy only
    const flux = new Float32Array(nF);
    for (let f = 1; f < nF; f++) flux[f] = Math.max(0, energy[f] - energy[f - 1]);
    // adaptive threshold: mean + 1.5 sigma over a +/-0.5 s window
    const W = Math.round((0.5 * sr) / hop);
    const onsets = [];
    const minGap = (0.22 * sr) / hop; // >= 220 ms between beats
    // FIX 2: an onset is timed at its window's centre, not its start.
    // FIX 4: and half a hop on, the middle of the frame it was found in.
    const centre = win / (2 * sr) + hop / (2 * sr);
    let last = -1e9;
    for (let f = 1; f < nF - 1; f++) {
      if (flux[f] < flux[f - 1] || flux[f] < flux[f + 1]) continue;
      let m = 0;
      let s = 0;
      let n = 0;
      for (let k = Math.max(0, f - W); k < Math.min(nF, f + W); k++) {
        m += flux[k];
        n++;
      }
      m /= n;
      for (let k = Math.max(0, f - W); k < Math.min(nF, f + W); k++) {
        const d = flux[k] - m;
        s += d * d;
      }
      s = Math.sqrt(s / n);
      if (flux[f] > m + 1.5 * s && f - last >= minGap) {
        onsets.push((f * hop) / sr + centre);
        last = f;
      }
    }
    // tempo: autocorrelate the smoothed envelope, 60-190 BPM, preferring the
    // smallest lag whose correlation holds up.
    const fluxS = new Float32Array(nF);
    for (let f = 0; f < nF; f++) {
      fluxS[f] = (flux[Math.max(0, f - 1)] + 2 * flux[f] + flux[Math.min(nF - 1, f + 1)]) / 4;
    }
    const fps = sr / hop;
    const lagLo = Math.max(2, Math.round((fps * 60) / 190));
    const lagHi = Math.round((fps * 60) / 60);
    // FIX 1: normalise by overlap length, or a shorter lag wins for no musical reason.
    const corr = (lag) => {
      let acc = 0;
      for (let f = 0; f < nF - lag; f++) acc += fluxS[f] * fluxS[f + lag];
      return nF - lag > 0 ? acc / (nF - lag) : 0;
    };
    let best = 0;
    let bestLag = 0;
    let sum = 0;
    let lags = 0;
    for (let lag = lagLo; lag <= lagHi && lag < nF; lag++) {
      const acc = corr(lag);
      sum += acc;
      lags += 1;
      if (acc > best) {
        best = acc;
        bestLag = lag;
      }
    }
    // The confidence is read off the scan before the octave halving, over the
    // same lags the best was chosen from (ADR 0058: best / mean(corr)).
    const meanCorr = lags > 0 ? sum / lags : 0;
    const confidence = best > 0 ? Math.max(0, Math.min(1, 1 - meanCorr / best)) : 0;
    while (bestLag >= lagLo * 2) {
      const half = Math.round(bestLag / 2);
      if (corr(half) >= 0.6 * best) {
        bestLag = half;
        best = corr(half);
      } else break;
    }
    // FIX 3 (#438): halving rounds the lag, which can land one frame off the
    // peak; the interpolation below only looks half a lag either side, so
    // climb first -- one lag either side and no further (#14).
    bestLag = climb(corr, bestLag, lagLo, lagHi);
    best = corr(bestLag);
    // parabolic interpolation over the neighbouring lags recovers the
    // fractional peak
    let lagF = bestLag;
    if (bestLag > lagLo && bestLag < lagHi) {
      const y1 = corr(bestLag - 1);
      const y2 = corr(bestLag);
      const y3 = corr(bestLag + 1);
      const den = y1 - 2 * y2 + y3;
      if (den !== 0) {
        const d = (0.5 * (y1 - y3)) / den;
        if (Math.abs(d) <= 0.5) lagF = bestLag + d;
      }
    }
    const bpm = bestLag ? Math.round(((60 * fps) / lagF) * 10) / 10 : 0;
    // phase: try each onset as beat zero, score grid hits against onsets
    let phase = 0;
    if (bpm && onsets.length) {
      const period = 60 / bpm;
      let bestScore = -1;
      for (const cand of onsets.slice(0, 24)) {
        const p = cand % period;
        let score = 0;
        for (const o of onsets) {
          const d = Math.abs((((o - p) % period) + period) % period);
          score += Math.min(d, period - d) < 0.05 ? 1 : 0;
        }
        if (score > bestScore) {
          bestScore = score;
          phase = p;
        }
      }
    }
    const beats = [];
    if (bpm) {
      const period = 60 / bpm;
      for (let t = phase; t < buf.duration; t += period) beats.push(Math.round(t * 1000));
    }
    // The tempo levels: corr() read between whole lags, against the tempo heard.
    const corrAt = (lag) => {
      const lo = Math.floor(lag);
      if (lo < 1 || lo + 1 >= nF) return 0;
      return corr(lo) + (corr(lo + 1) - corr(lo)) * (lag - lo);
    };
    const heard = bpm ? corrAt(lagF) : 0;
    const levels = heard > 0
      ? LEVELS.map(([num, den]) => {
          const at = Math.round(((bpm * num) / den) * 10) / 10;
          // The tempo heard is the yardstick, so it reads 1 exactly, not
          // whatever its rounded BPM reads back at.
          const strength = num === den ? 1 : Math.round((corrAt((60 * fps) / at) / heard) * 100) / 100;
          return { bpm: at, num, den, strength };
        })
      : [];
    return {
      bpm,
      phaseMs: Math.round(phase * 1000),
      beatsMs: beats,
      onsetsMs: onsets.map((t) => Math.round(t * 1000)),
      durationMs: Math.round(buf.duration * 1000),
      confidence: bpm ? Math.round(confidence * 1000) / 1000 : 0,
      levels,
    };
  };

  // ---------------------------------------------------------------------------
  // Tapping along (ADR 0058: the route that always works). `tapsMs` are the
  // press times, in ms from the start edge of the track on the droid -- or from
  // the first tap, when the builder started the track some other way. The
  // first tap is the downbeat (ADR 0060), so it is where beat 0 sits.
  // ---------------------------------------------------------------------------
  const TAPS_MIN = 4;

  const tap = (tapsMs) => {
    const taps = (Array.isArray(tapsMs) ? tapsMs : []).filter((t) => Number.isFinite(t));
    if (taps.length < TAPS_MIN) return null;
    const gaps = [];
    for (let i = 1; i < taps.length; i++) gaps.push(taps[i] - taps[i - 1]);
    const mean = gaps.reduce((a, b) => a + b, 0) / gaps.length;
    if (!(mean > 0)) return null;
    const spread = Math.sqrt(gaps.reduce((a, g) => a + (g - mean) * (g - mean), 0) / gaps.length);
    return {
      bpm: Math.round((60000 / mean) * 10) / 10,
      phaseMs: Math.max(0, Math.round(taps[0])),
      confidence: Math.round(Math.max(0, Math.min(1, 1 - (5 * spread) / mean)) * 1000) / 1000,
    };
  };

  // ---------------------------------------------------------------------------
  // The track's fingerprint: FNV-1a over the file's bytes, as 8 hex digits.
  // It only has to tell "the same file" from "a different one" when the
  // builder drops a copy in again, and it runs where crypto.subtle does not --
  // the droid is served over plain HTTP, which is not a secure context.
  // ---------------------------------------------------------------------------
  const fingerprint = (bytes) => {
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    let h = 0x811c9dc5;
    for (let i = 0; i < data.length; i++) {
      h ^= data[i];
      h = Math.imul(h, 0x01000193) >>> 0;
    }
    return h.toString(16).padStart(8, "0");
  };

  // Decode a dropped file and analyze it. The decode is the browser's own; a
  // file it cannot read rejects, and the caller says so.
  const analyzeFile = async (file) => {
    const bytes = await file.arrayBuffer();
    const hash = fingerprint(new Uint8Array(bytes));
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) throw new Error("This browser cannot read audio files");
    const ctx = new Ctx();
    try {
      const buf = await ctx.decodeAudioData(bytes.slice(0));
      return { ...analyze(buf), hash };
    } finally {
      if (ctx.close) ctx.close();
    }
  };

  // ---------------------------------------------------------------------------
  // The tempo block each route stores (ADR 0058). The downbeat is the
  // builder's: a tapped tempo's is the first tap, an analyzed one starts on the
  // grid's first beat until the builder moves it (moveDownbeat()).
  // ---------------------------------------------------------------------------
  const tappedTempo = (result) => ({
    bpm: result.bpm,
    phase: result.phaseMs,
    barLen: 4,
    barPhase: 0,
    source: "tapped",
    confidence: result.confidence,
  });

  const analyzedTempo = (result) => ({
    bpm: result.bpm,
    phase: result.phaseMs,
    barLen: 4,
    barPhase: 0,
    duration: result.durationMs,
    hash: result.hash,
    source: "analysed",
    confidence: result.confidence,
  });

  // The downbeat moved to grid beat `beatNumber` (1 = the first beat): beat 0,
  // and so bar 1, sits there from now on. Returns null when that beat is past
  // where a sequence can reach.
  const moveDownbeat = (tempo, beatNumber) => {
    const check = window.SeqProtocolCheck;
    if (!tempo || !check || !(Number.isInteger(beatNumber) && beatNumber >= 1)) return null;
    const phase = check.tempoBeatMs(tempo, beatNumber - 1);
    const moved = { ...tempo, phase, barPhase: 0 };
    return check.validateTempo(moved).ok ? moved : null;
  };

  // ---------------------------------------------------------------------------
  // The grid a builder picks from (ADR 0060). Beats are counted from beat 0,
  // where the builder's downbeat sits; bars from the builder's downbeat too,
  // so the twelfth beat in 4/4 is bar 3, beat 4 -- never "bar 12", the
  // reference's label defect. A beat before `barPhase` is a pickup, bar 0.
  // ---------------------------------------------------------------------------
  // The last beat a step can sit on (SEQ_TEMPO_BEAT_MAX, include/seq_tempo.h).
  const BEAT_MAX = 1200;

  const barLenOf = (tempo) => (Number.isInteger(tempo?.barLen) && tempo.barLen > 0 ? tempo.barLen : 4);

  const beatName = (tempo, index) => {
    const barLen = barLenOf(tempo);
    const from = index - (Number(tempo?.barPhase) || 0);
    const bar = from < 0 ? 0 : Math.floor(from / barLen) + 1;
    const inBar = (((from % barLen) + barLen) % barLen) + 1;
    return { index, bar, beat: inBar, strong: inBar === 1 };
  };

  // A beat in words, as the editor says it wherever it names one: beside a
  // step that is on it, and on the line a drag lands by.
  const beatWords = (tempo, index) => {
    const name = beatName(tempo, index);
    return name.bar === 0 ? `pickup beat ${name.beat}` : `bar ${name.bar}, beat ${name.beat}`;
  };

  // Every beat from beat 0 through the bar that holds `untilMs`, grouped by
  // bar. `untilMs` is how far the routine reaches; the list runs to the end
  // of that bar so the last step always has a beat after it to move to.
  const bars = (tempo, untilMs) => {
    const check = window.SeqProtocolCheck;
    if (!check || !tempo || !check.validateTempo(tempo).ok) return [];
    const out = [];
    for (let index = 0; index <= BEAT_MAX; index++) {
      const name = beatName(tempo, index);
      if (name.beat === 1 && check.tempoBeatMs(tempo, index) > untilMs && out.length > 0) break;
      if (!out.length || out[out.length - 1].bar !== name.bar) out.push({ bar: name.bar, beats: [] });
      out[out.length - 1].beats.push(name);
    }
    return out;
  };

  // Retime to the grid (ADR 0060): every step to its nearest beat, as a copy,
  // with the receipt. A step inside a loop body is timed from its pass and
  // cannot sit on a beat, so it never lands, and the receipt counts it among
  // the steps that did not: a bulk edit that mostly missed must say so rather
  // than count itself a success, which is what the reference's own test did.
  // The caller keeps what it had, which is the one-press undo.

  const retime = (seq) => {
    const check = window.SeqProtocolCheck;
    if (!check || !seq || !Array.isArray(seq.steps) || !seq.tempo || !check.validateTempo(seq.tempo).ok) {
      return null;
    }
    const tempo = seq.tempo;
    const beatMs = 60000 / (Math.round(tempo.bpm * 10) / 10);
    const phase = Number(tempo.phase) || 0;
    let landed = 0;
    let total = 0;
    const branch = (steps) => {
      const inLoop = check.loopBodySteps(steps);
      return steps.map((step, i) => {
        total += 1;
        if (!step || inLoop.has(i)) return step;
        const nearest = Math.min(BEAT_MAX, Math.max(0, Math.round(((Number(step.t) || 0) - phase) / beatMs)));
        landed += 1;
        return { ...step, beat: nearest, t: check.tempoBeatMs(tempo, nearest) };
      });
    };
    const out = { ...seq, steps: branch(seq.steps) };
    if (Array.isArray(seq.closeSteps)) out.closeSteps = branch(seq.closeSteps);
    return { seq: out, landed, total };
  };

  window.SeqTempo = Object.freeze({
    TAPS_MIN,
    LEVELS,
    climb,
    analyze,
    analyzeFile,
    tap,
    fingerprint,
    tappedTempo,
    analyzedTempo,
    moveDownbeat,
    beatName,
    beatWords,
    bars,
    retime,
  });
})();
