// The editor's own modules, run under Node, so a draft is analysed, checked
// and retimed by exactly the code the droid's page runs (data/, in the page's
// load order). Usage: node seq_node.js <data-dir> <command> [args]; a sequence
// or a tempo arrives as JSON on stdin and the answer leaves as JSON on stdout.
//   analyze <pcm.f32> <sampleRate>   SeqTempo.analyze on mono float32 PCM
//   check                           Protocol Check verdict + the Rehearsal
//   resolve                         t written from each beat, as a save does
//   retime                          SeqTempo.retime: every step to its beat
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const [dataDir, command, ...args] = process.argv.slice(2);
const MODULES = [
  "droid_parts.js", "servo_motion.js", "dome_command_map.js", "dome_lights.js",
  "seq_protocol_check.js", "seq_gesture.js", "seq_rehearsal.js", "seq_tempo.js",
];
const sandbox = { window: {}, console };
vm.createContext(sandbox);
for (const name of MODULES) {
  vm.runInContext(fs.readFileSync(path.join(dataDir, name), "utf8"), sandbox, { filename: name });
}
const { SeqProtocolCheck: check, SeqRehearsal: rehearsal, SeqTempo: tempo } = sandbox.window;
const plain = (value) => JSON.parse(JSON.stringify(value));
const stdin = () => JSON.parse(fs.readFileSync(0, "utf8"));

const commands = {
  analyze() {
    const raw = fs.readFileSync(args[0]);
    const sr = Number(args[1]);
    const data = new Float32Array(raw.buffer, raw.byteOffset, raw.length / 4);
    const buf = { sampleRate: sr, length: data.length, duration: data.length / sr, numberOfChannels: 1, getChannelData: () => data };
    const r = tempo.analyze(buf);
    return { bpm: r.bpm, phaseMs: r.phaseMs, confidence: r.confidence, durationMs: r.durationMs, onsetsMs: r.onsetsMs, levels: r.levels };
  },
  check() {
    const seq = stdin();
    const verdict = check.validateSequence(seq);
    const r = rehearsal.rehearse(seq, {});
    return {
      protocolCheck: verdict,
      rehearsal: { findings: r.findings, gaps: r.gaps, figures: r.figures },
    };
  },
  resolve() {
    return check.resolveBeats(stdin(), { written: true });
  },
  retime() {
    const r = tempo.retime(stdin());
    return r ? { seq: r.seq, landed: r.landed, total: r.total } : { error: "the sequence has no valid tempo to retime to" };
  },
};

if (!commands[command]) {
  console.error(`unknown command ${command}; one of ${Object.keys(commands).join(", ")}`);
  process.exit(2);
}
process.stdout.write(JSON.stringify(plain(commands[command]())));
