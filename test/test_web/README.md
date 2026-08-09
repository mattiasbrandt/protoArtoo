# Behavioral tests for shipped web modules

Two defects (#148, #149) shipped behind green suites, and two rework attempts
were rejected with green suites over unchanged behavior. The recurring failure
mode is the **vacuous test** - a test that cannot fail. Work from this
assumption: your suite is vacuous until a mutation of production code turns it
red.

## Harness pattern

- **Execute the shipped file.** `vm.runInNewContext(readFileSync("data/<module>.js"), context)`
  with a mocked `window`/`document`. Working examples:
  `test_paapi_cancellation.js` (single module) and
  `test_bootstrap_cancellation.js` (reducer + browser host + transport composed
  in one context, with a real deadline expiry driving the code path under test).
- **Real primitives.** Node's real `AbortController` and real timers. A stubbed
  signal without `addEventListener` propagation, or a `setTimeout` stub that
  never invokes its callback, makes in-flight effects unobservable - the tests
  then assert conditions that are true by construction.
- **Contract-honoring mocks.** A fetch mock must wire `opts.signal` via
  `addEventListener` and settle exactly once. Measure concurrency as
  dispatch-to-settle overlap at the fetch layer, not call time.
- **vm-hosting the browser host:** `unref()` every timer the code under test
  schedules, or its retry clock holds the `node:test` process open forever.

## Prove the suite can fail - required before reporting green

1. **Calibrate against the known-bad commit.** Extract the pre-fix files
   (`git show <bad>:data/<module>.js`) into a scratch tree beside a copy of the
   new tests and run them there. The tests covering the defect must be red; a
   test that passes on the bad commit covers nothing.
2. **Mutate production code, never assertions.** One mutation at a time, each
   reverting one fix aspect; every mutation must turn at least one test red.
   Include **stealth mutations** - behavior changes that leave every asserted
   string byte-identical (invert a flag assignment, swap a return value). These
   are what defeat source-text assertions.
3. **Pristine green on both sides.** Full suite green before mutating and after
   restoring. Commit the fix before running any script that restores files via
   `git checkout` - restore targets the last commit, and uncommitted work is
   wiped silently.

Report the calibration result and the mutation table alongside the green run.
If a mutation turns nothing red, fix the test - a gap explained away
("logically correct per the specification") is how both rejected attempts
shipped.

## Traps that shipped real bugs here

Each of these was caught in review on this repo, not hypothesized:

- Asserting that source text exists (`file.includes("Math.pow(2, attempt)")`) -
  proves characters, cannot detect unreachable code.
- Reimplementing the functions under test inside the test file - proves a
  hand-written model, says nothing about `data/`.
- Retry wrapper around a function that catches internally and never rethrows -
  the retry branch is dead code; make failure observable to the caller and test
  the observable.
- Asserting a queued request was "not aborted" when it never had a controller -
  true by construction under `MAX_CONCURRENT_REQUESTS = 1`.

## Design rule the transport now encodes

Ownership travels **with the data**: a caller that needs to cancel its request
passes its own `AbortSignal` as `opts.signal`; the bootstrap owns one
controller per section run. Ambient global flags keyed on *when* a request
happens marked bystanders as cancellable and were rejected twice on #148 -
any new cancellable path gets a handle at issue time, not a mode flag.
