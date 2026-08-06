"use strict";

// Verdict derivation for tools/webload_browser_capture.js.
//
// The defect these cover: "usable" was reachable only through the coordinator's
// statusReachableAt handshake, while --control-file was documented as optional.
// A standalone run could therefore never report anything but a failure, however
// healthy the page -- so any run scored on captureStatus read the invocation mode
// rather than the controller. The regression case is `standalone healthy page`.

const assert = require("assert");
const path = require("path");
const { test } = require("node:test");

const {
  deriveCaptureVerdict, captureExitCode,
} = require(path.resolve(__dirname, "../../tools/webload_browser_capture.js"));

test("standalone healthy page is usable", () => {
  const verdict = deriveCaptureVerdict({
    coordinated: false, terminalReason: "browser-ready", browserGatesPassed: true,
  });
  assert.strictEqual(verdict.captureStatus, "usable");
  assert.strictEqual(verdict.verdictBasis, "browser-gates-only");
  assert.strictEqual(captureExitCode(verdict.captureStatus), 0);
});

test("standalone run that never became ready still passes on final gates", () => {
  // The loop can hit the observation deadline without an in-loop candidate while
  // the post-loop DOM sample passes. Standalone grades on the gates, not on how
  // the window ended.
  const verdict = deriveCaptureVerdict({
    coordinated: false, terminalReason: "observation-deadline", browserGatesPassed: true,
  });
  assert.strictEqual(verdict.captureStatus, "usable");
});

test("standalone failing page is a browser failure", () => {
  const verdict = deriveCaptureVerdict({
    coordinated: false, terminalReason: "observation-deadline", browserGatesPassed: false,
  });
  assert.strictEqual(verdict.captureStatus, "browser-failure-observed");
  assert.strictEqual(captureExitCode(verdict.captureStatus), 3);
});

test("coordinated handshake plus passing gates is unchanged", () => {
  const verdict = deriveCaptureVerdict({
    coordinated: true, terminalReason: "usable", browserGatesPassed: true,
  });
  assert.strictEqual(verdict.captureStatus, "usable");
  assert.strictEqual(verdict.verdictBasis, "coordinated-status-handshake");
  assert.strictEqual(captureExitCode(verdict.captureStatus), 0);
});

test("coordinated gates passing without a handshake is not a browser failure", () => {
  const verdict = deriveCaptureVerdict({
    coordinated: true, terminalReason: "observation-deadline", browserGatesPassed: true,
  });
  assert.strictEqual(verdict.captureStatus, "controller-unconfirmed");
  // Non-passing, and inside the (0, 2, 3, 4) contract the coordinator enforces.
  assert.strictEqual(captureExitCode(verdict.captureStatus), 3);
});

test("coordinated deadline with failing gates stays a browser failure", () => {
  const verdict = deriveCaptureVerdict({
    coordinated: true, terminalReason: "observation-deadline", browserGatesPassed: false,
  });
  assert.strictEqual(verdict.captureStatus, "browser-failure-observed");
});

test("a handshake cannot pass a page whose gates regressed", () => {
  const verdict = deriveCaptureVerdict({
    coordinated: true, terminalReason: "usable", browserGatesPassed: false,
  });
  assert.strictEqual(verdict.captureStatus, "browser-failure-observed");
});

test("an external stop is stopped in both modes, whatever the gates say", () => {
  for (const coordinated of [true, false]) {
    for (const gates of [true, false]) {
      for (const reason of ["operator-interrupt", "external-termination", "cooldown-failed"]) {
        const verdict = deriveCaptureVerdict({
          coordinated, terminalReason: reason, browserGatesPassed: gates,
        });
        assert.strictEqual(
          verdict.captureStatus, "stopped",
          `${reason} coordinated=${coordinated} gates=${gates}`,
        );
        assert.strictEqual(captureExitCode(verdict.captureStatus), 4);
      }
    }
  }
});

test("verdictBasis names the mode independently of the outcome", () => {
  for (const reason of ["usable", "browser-ready", "observation-deadline", "operator-interrupt"]) {
    for (const gates of [true, false]) {
      assert.strictEqual(
        deriveCaptureVerdict({ coordinated: true, terminalReason: reason, browserGatesPassed: gates })
          .verdictBasis,
        "coordinated-status-handshake",
      );
      assert.strictEqual(
        deriveCaptureVerdict({ coordinated: false, terminalReason: reason, browserGatesPassed: gates })
          .verdictBasis,
        "browser-gates-only",
      );
    }
  }
});

test("every reachable exit code stays inside the coordinator's contract", () => {
  const statuses = new Set();
  for (const coordinated of [true, false]) {
    for (const gates of [true, false]) {
      for (const reason of ["usable", "browser-ready", "observation-deadline", "operator-interrupt"]) {
        statuses.add(
          deriveCaptureVerdict({ coordinated, terminalReason: reason, browserGatesPassed: gates })
            .captureStatus,
        );
      }
    }
  }
  // tools/webload_baseline_run.py raises on any code outside (0, 2, 3, 4); 2 is
  // the evidence-artifact failure, which this function does not produce.
  for (const status of statuses) {
    assert.ok([0, 3, 4].includes(captureExitCode(status)), `${status} maps outside the contract`);
  }
  assert.deepStrictEqual(
    [...statuses].sort(),
    ["browser-failure-observed", "controller-unconfirmed", "stopped", "usable"],
  );
});
