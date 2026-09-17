# Worker slice gate

`tools/slice_verify.py` is the mechanical PASS/FAIL floor for a branch against a
base ref. `python3 tools/slice_verify.py --help` is the flag reference; this file
is the contract: what the block must contain, who may waive what, and what
counts as evidence.

**Worker slice gate:** after committing a slice, workers must run
`python3 tools/slice_verify.py --base <base-ref>` (plus any `--fenced` pathspecs
and the `--mutations` patches the coordinator's brief specifies) and paste its
full block verbatim into the issue status comment — including the opening
provenance lines (blob hashes of both verifier scripts, HEAD sha, DIRTY
marker, merge-base, diff size, toolchain). The coordinator does not re-run the
gate behind every slice. Per slice it checks the block's **provenance against
the branch** - HEAD sha against the tip, **the block's merge-base against the
base's current tip** (`git rev-parse <base>`, never `git merge-base <base>
HEAD`: a slice whose base moved under it is internally consistent and would
otherwise pass, verified against a tree that no longer exists), diff size, both
script blob hashes against the files on disk, the DIRTY marker against a clean
tree,
every changed web production JS file present in the mutation table, and no
waiver ACK it did not grant - which takes seconds and catches a block that is
not of this branch. The gate itself is run **once per wave, on the merged
tree**, with the union of the wave's fences: that run is the anti-fabrication
net, and it has to happen anyway because line numbers and stragglers move on
merge. Divergence at either point marks the slice unverified, and a failed
provenance check is the trigger to re-run the full gate on that one slice.

**Why it changed (2026-09-17).** Across epic #175 the coordinator re-ran the
full gate behind **18** accepted slices and found **0** divergences, while each
re-run cost a second copy of the most expensive thing in the repo - the
mutation stage alone runs the whole web suite once per patch, 28 times on a
slice like #346 - serialised behind a machine-wide build lock. Every rejection
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
design. Editing `tools/slice_verify.py` or `tools/mutation_verify.py` inside
a slice fails the gate; `--expect-gate-edit` is for coordinator-sanctioned
gate work only. The waiver flags — `--expect-gate-edit`,
`--expect-no-new-tests`, `--expect-no-mutations` — are granted by the
coordinator in the brief, never self-granted by a worker, and every ACK is
visible in the block.

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
