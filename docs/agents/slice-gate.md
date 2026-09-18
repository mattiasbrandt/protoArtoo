# Worker slice gate

`tools/slice_verify.py` is the mechanical PASS/FAIL floor for a branch against a
base ref. `python3 tools/slice_verify.py --help` is the flag reference; this file
is the contract: what the block must contain, who may waive what, and what
counts as evidence.

**Worker slice gate:** after committing a slice, workers must run
`python3 tools/slice_verify.py --base <base-ref>` (plus any `--fenced` pathspecs
and the `--mutations` patches the coordinator's brief specifies) and paste its
full block verbatim into the issue status comment — including the opening
provenance lines (blob hashes of the three verifier scripts - `gate`,
`mut`, `trace` - HEAD sha, DIRTY marker, merge-base, diff size, web-only,
toolchain). The coordinator does not re-run the
gate behind every slice. Per slice it checks the block's **provenance against
the branch** - HEAD sha against the tip, **the block's merge-base against the
base's current tip** (`git rev-parse <base>`, never `git merge-base <base>
HEAD`: a slice whose base moved under it is internally consistent and would
otherwise pass, verified against a tree that no longer exists), diff size, all
three script blob hashes against the files on disk, the DIRTY marker against a clean
tree,
every changed web production JS file present in the mutation table, and no
waiver ACK it did not grant - which takes seconds and catches a block that is
not of this branch. The gate itself is run **once per wave, on the merged
tree**, with the union of the wave's fences: that run is the anti-fabrication
net, and it has to happen anyway because line numbers and stragglers move on
merge. Divergence at either point marks the slice unverified, and a failed
provenance check is the trigger to re-run the full gate on that one slice. The
merge-base check is the exception that is judged rather than failed: when the
base moved under a slice, the coordinator intersects what landed with what the
slice touches - no overlap and the per-wave merged-tree run is its proof, an
overlap (or merged work that is this slice's own subject) and the worker merges
and re-gates.

**Why it changed (2026-09-17).** Across epic #175 the coordinator re-ran the
full gate behind **18** accepted slices and found **0** divergences, while each
re-run cost a second copy of the most expensive thing in the repo - the
mutation stage alone ran the whole web suite once per patch (until #405), 28
times on a slice like #346 - serialised behind a machine-wide build lock. Every rejection
that epic produced came from reading the production diff, which is step 0 of
the critic protocol and costs nothing. The duplicate was buying a check that
the merged-tree run already performs. The gate runs the native suite, the web
suite (`make test-web` semantics: process exit code and `# cancelled` decide,
never the TAP `# fail` line), the mutation stage, the firmware build, drift
and diff checks, and fails on deleted test files or a shrinking test total. A
flat test total over production changes also fails: `data/` changes must grow
the web suite and `src/`/`include/` changes the native suite. A diff touching
web production JS must carry mutation patches via `--mutations` (files or a
directory of `*.patch`); the gate runs `tools/mutation_verify.py` itself,
requires every patch KILLED and every changed JS file hit by at least one
patch, and folds the verdict into the block — a passing block implies killed
mutations. Its diff checks compare merge-base..HEAD, so commit before running
it; build-stamped working-tree changes to `data/*version.json` are ignored by
design. Editing `tools/slice_verify.py`, `tools/mutation_verify.py` or
`tools/web_load_trace.cjs` inside a slice fails the gate; `--expect-gate-edit` is for coordinator-sanctioned
gate work only. The waiver flags — `--expect-gate-edit`,
`--expect-no-new-tests`, `--expect-no-mutations` — are granted by the
coordinator in the brief, never self-granted by a worker, and every ACK is
visible in the block.

**The mutation stage (#405).** `tools/mutation_verify.py` owns its node
processes: one `node --test` per test file, concurrency 1, 60 s each. Per patch
it runs the **likely-set** - the test files the load map says open a patched
`data/*.js` file - **shortest-first** by each file's cached wall time, and
**stops at the first clean assertion kill**. The load map is traced from the
gate's own HEAD web run by `tools/web_load_trace.cjs` (a `node --require`
preload), and cached in `/tmp/protoartoo-web-load-map.json` keyed by the
`data/` and `test/test_web/` tree ids and the Node version; standalone runs
share it and rebuild it once when it is stale. Every unknown widens to the
whole suite, never narrows. `SURVIVED` now means *no test that loads this file
killed it*, and a patch one file kills by assertion while another hangs is
`KILLED` (`test/test_web/README.md` states the rule). The mutation table is
printed in the block on PASS as well as FAIL, with a `ran` column - files run
/ likely-set size, e.g. `3/21`. `ran` depends on cached durations and can
differ between two runs of one patch; the verdict cannot. `--whole-suite` on
`mutation_verify.py` restores the one-invocation run for debugging and corpus
replay; the gate never passes it.

**Web-only diffs.** When every path in merge-base..HEAD matches
`^data/[^/]+\.(js|css|html)$`, `^test/test_web/` or `^docs/`, the block says
`web-only yes`, and the native tests, build budget and task stack chains rows
print `SKIP (web-only diff)`. `pio run -e artoo_esp32` stays: it is also the
staging check, because `tools/gzip_fsdata.py` runs on every `pio run`, runs
every `data/` JS and CSS file through esbuild and resolves the HTML includes -
the only syntax check `data/` files no web test opens ever get. It is not
`-t buildfs`: in a fresh worktree that target half-runs the framework rebuild
and leaves the machine-wide artoo framework pool pristine behind a stamp that
claims otherwise (measured on #405, 2026-09-18). Anything else in the diff -
`data/console_help.txt` (a native test reads it), `data/asset-sets/`, `src/`,
`tools/`, `platformio.ini` - is not web-only and runs every row. It is derived
from the diff; there is no flag.

**Locks.** The pio phases hold `/tmp/protoartoo-pio.lock`. The web suite and the
mutation stage hold `/tmp/protoartoo-webtest.lock` - a different lock, never
held together with the pio lock and never nested in it - so two gates' web
stages run one after the other without queueing anyone's build. Base-suite
totals are cached machine-wide in `/tmp/protoartoo-slice-verify-cache.json`,
keyed by base sha, the three verifier hashes and the Node version, and a killed
gate removes the `/tmp/slice-verify-base-*` worktree it made. Per-stage wall
times go to stderr and `--json`, never to the block.

**Evidence rules:** pasted evidence must carry process exit codes, never a
hand-summarised pass/fail line. A test that fails only by hanging or timing out
is not acceptable coverage — the failure must be an assertion. Mutation
evidence is the gate block itself (`slice_verify.py --mutations` runs the
mutation stage and fails unless every mutation is KILLED by assertion);
standalone `python3 tools/mutation_verify.py <patches>` runs are for authoring
patches, and a hand-written mutation table is never evidence. If a stated
requirement of the
ticket cannot be met, stop and report on the issue — shipping the remainder
while reporting the ticket complete is a reject, not a partial pass. Never edit
a shared test harness to accommodate the code under test; fix the code or
report the conflict.
