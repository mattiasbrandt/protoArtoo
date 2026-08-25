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
marker, merge-base, diff size, toolchain). The coordinator re-runs the same
command on the branch and compares blocks, provenance lines included;
divergence marks the slice unverified. The gate runs the native suite, the web
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
