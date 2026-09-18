// =============================================================================
// tools/web_load_trace.cjs
//
// A `node --require` preload that records which shipped data/*.js files each
// web test file opens. tools/slice_verify.py runs the HEAD web suite with it,
// and that run is the load map tools/mutation_verify.py narrows by: a mutation
// of data/shell.js is only run against the test files that opened shell.js.
//
// Why a trace and not a grep: helpers load modules the test file never names.
// test/test_web/helpers/page_module_env.js always reads page_bootstrap.js, and
// a grep map of that is a false SURVIVED (#405).
//
// Mechanism: every web test observes the shipped code through
// fs.readFileSync - directly, through vm.runInNewContext of what it read, or
// through require()/import(), both of which read the source with it on Node 26
// (pinned by the canary in test/test_tools/test_gate_tools.py, so a Node
// upgrade that changes this fails there instead of silently changing a map).
//
// `node --test` runs each test file in its own child process, and the preload
// runs in the runner and in every child. A child (NODE_TEST_CONTEXT set) owns
// exactly one test file, process.argv[1], and writes its opens to a fragment
// file on exit. The runner writes nothing of its own; on exit it merges the
// fragments into $PROTOARTOO_LOAD_TRACE:
//
//   { "test/test_web/test_foo.js": ["data/page_bootstrap.js", "data/shell.js"] }
//
// A test file that opened nothing under data/ is still listed, with [], so the
// reader can tell "opens nothing" from "was never traced".
//
// It must not change what it measures: no output on stdout or stderr (node
// relays a child's stderr into the TAP stream as comments), no change to exit
// codes, and a read it cannot attribute is dropped rather than thrown on. With
// PROTOARTOO_LOAD_TRACE unset it does nothing at all.
// =============================================================================
"use strict";

const fs = require("fs");
const path = require("path");

const outPath = process.env.PROTOARTOO_LOAD_TRACE;

if (outPath) {
  const root = process.cwd();
  const dataDir = path.join(root, "data") + path.sep;
  const fragmentDir = `${outPath}.parts`;
  const isTestChild = Boolean(process.env.NODE_TEST_CONTEXT);

  if (isTestChild && process.argv[1]) {
    const testFile = path.relative(root, path.resolve(process.argv[1]));
    const opened = new Set();
    const originalReadFileSync = fs.readFileSync;

    fs.readFileSync = function tracedReadFileSync(file, ...rest) {
      try {
        const name = file instanceof URL ? file.pathname
          : typeof file === "string" ? file : null;
        if (name !== null) {
          const absolute = path.resolve(root, name);
          if (absolute.startsWith(dataDir) && absolute.endsWith(".js")) {
            opened.add(path.relative(root, absolute).split(path.sep).join("/"));
          }
        }
      } catch {
        // An unattributable read is not recorded. Throwing here would change
        // the behaviour of the test being traced; the reader widens on any
        // test file it has no entry for.
      }
      return originalReadFileSync.call(this, file, ...rest);
    };

    process.on("exit", () => {
      fs.mkdirSync(fragmentDir, { recursive: true });
      const fragment = path.join(fragmentDir, `${process.pid}.json`);
      fs.writeFileSync(fragment, JSON.stringify({ [testFile]: [...opened].sort() }));
    });
  } else if (!isTestChild) {
    // The runner. It exits after every child has, so every fragment is on disk.
    process.on("exit", () => {
      let names = [];
      try {
        names = fs.readdirSync(fragmentDir);
      } catch {
        return; // No child traced anything: write no map, and the reader widens.
      }
      const map = {};
      for (const name of names.sort()) {
        const fragment = JSON.parse(fs.readFileSync(path.join(fragmentDir, name), "utf-8"));
        Object.assign(map, fragment);
      }
      const sorted = Object.fromEntries(Object.keys(map).sort().map((key) => [key, map[key]]));
      const partial = `${outPath}.tmp`;
      fs.writeFileSync(partial, JSON.stringify(sorted, null, 2) + "\n");
      fs.renameSync(partial, outPath);
      fs.rmSync(fragmentDir, { recursive: true, force: true });
    });
  }
}
