const assert = require("node:assert/strict");
const { chromium } = require("playwright");
const fs = require("fs");
const path = require("path");

const DATA_DIR = path.join(__dirname, "../../../data");

async function testBootstrapStateMachine() {
  const browser = await chromium.launch({ headless: true });
  const page = await browser.newPage();

  // Load the PageBootstrap module
  const bootstrapCode = fs.readFileSync(path.join(DATA_DIR, "page_bootstrap.js"), "utf-8");
  await page.evaluate(bootstrapCode);

  // Test 1: Initial state
  console.log("Test 1: Initial bootstrap state");
  const initialState = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    return bs.createBootstrap({
      resources: ["web_api.js", "status_stream.js"],
      sections: ["status", "controls"],
    });
  });
  assert.strictEqual(initialState.resourcesReady, false);
  assert.strictEqual(initialState.sectionsStable, false);
  console.log("  ✓ Initial state correct");

  // Test 2: Resource Step Recovery
  console.log("Test 2: Resource Step Recovery (success)");
  const afterResources = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js", "stream.js"],
      sections: ["status"],
    });

    // First resource
    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    if (!state.active || state.active.kind !== "resource" || state.active.name !== "api.js") {
      throw new Error("First resource not started");
    }

    // First resource completes and second one starts immediately
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
    if (state.resourceCursor !== 1) {
      throw new Error(`Cursor not advanced after first resource: cursor=${state.resourceCursor}`);
    }
    // RESULT action should have immediately started the second resource
    if (!state.active || state.active.name !== "stream.js") {
      throw new Error(`Second resource not started immediately: active=${state.active?.name}`);
    }

    // Second resource completes
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
    if (!state.resourcesReady || state.resourceCursor !== 2) {
      throw new Error(`Resources not marked ready: ready=${state.resourcesReady}, cursor=${state.resourceCursor}`);
    }

    return { resourcesReady: state.resourcesReady, cursor: state.resourceCursor };
  });
  assert.strictEqual(afterResources.resourcesReady, true);
  assert.strictEqual(afterResources.cursor, 2);
  console.log("  ✓ Resources loaded in order");

  // Test 3: Failed Resource Retry
  console.log("Test 3: Resource failure with automatic retry");
  const afterRetry = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: [],
    });

    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "no-response" } });

    if (!state.resources[0].nextAt || state.resources[0].attempt !== 1) {
      throw new Error("Failed resource not scheduled for retry");
    }

    // Advance time for retry
    const retryTime = state.resources[0].nextAt - state.now;
    state = bs.dispatch(state, { type: "TICK", dt: retryTime + 1 });

    if (!state.active || state.active.name !== "api.js" || state.resources[0].attempt !== 2) {
      throw new Error("Retry not triggered");
    }

    return { attempt: state.resources[0].attempt, active: state.active.name };
  });
  assert.strictEqual(afterRetry.attempt, 2);
  console.log("  ✓ Failed resource retried after backoff");

  // Test 4: Busy Response (503) with Retry-After
  console.log("Test 4: Busy (503) outcome with Retry-After header");
  const afterBusy = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: [],
    });

    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    state = bs.dispatch(state, {
      type: "RESULT",
      outcome: { kind: "busy", retryAfterMs: 5000 },
    });

    const nextAt = state.resources[0].nextAt;
    const expectedRetry = state.now + 5000;
    if (Math.abs(nextAt - expectedRetry) > 1) {
      throw new Error(`Retry-After not used: expected ${expectedRetry}, got ${nextAt}`);
    }

    return { nextAt, now: state.now };
  });
  assert.strictEqual(afterBusy.nextAt - afterBusy.now, 5000);
  console.log("  ✓ Busy outcome uses Retry-After header");

  // Test 5: Hidden Tab Pause
  console.log("Test 5: Hidden Tab Pause");
  const afterHidden = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: ["status"],
    });

    // Start work
    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    const inFlightId = state.active?.id;
    if (!inFlightId) throw new Error("No work started");

    // Hide tab
    state = bs.dispatch(state, { type: "VISIBILITY", visible: false });
    if (state.queue.length !== 0 || !state.active) {
      throw new Error("Hidden tab should discard queue but keep in-flight");
    }

    // Show tab
    state = bs.dispatch(state, { type: "VISIBILITY", visible: true });
    if (!state.visible) throw new Error("Visibility not restored");

    return { visible: state.visible, queueEmpty: state.queue.length === 0 };
  });
  assert.strictEqual(afterHidden.visible, true);
  assert.strictEqual(afterHidden.queueEmpty, true);
  console.log("  ✓ Hidden tab pauses new work, show resumes");

  // Test 6: Browser Request Priority
  console.log("Test 6: Browser Request Priority");
  const afterPriority = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: [],
    });

    // Start resource work
    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    if (state.active.kind !== "resource") throw new Error("Resource not started");

    // Submit user command
    state = bs.dispatch(state, { type: "SUBMIT_COMMAND", name: "save" });
    if (state.queue.length !== 1 || state.queue[0].priority !== bs.PRIORITY.COMMAND) {
      throw new Error("Command not queued at right priority");
    }

    // Complete resource
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
    state = bs.dispatch(state, { type: "TICK", dt: 1 }); // Next round

    // Command should be active now
    if (!state.active || state.active.kind !== "command") {
      throw new Error("Command not prioritized after resource");
    }

    return { activeKind: state.active.kind, activeName: state.active.name };
  });
  assert.strictEqual(afterPriority.activeKind, "command");
  console.log("  ✓ User commands prioritized over background work");

  // Test 7: Estop bypasses queue
  console.log("Test 7: Estop bypasses queue and active slot");
  const afterEstop = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: [],
    });

    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    const hadActive = !!state.active;

    // Estop never queued
    state = bs.dispatch(state, { type: "SUBMIT_ESTOP", name: "STOP" });
    if (state.estopLog.length !== 1 || state.queue.length !== 0) {
      throw new Error("Estop queued or not logged");
    }

    return { estopLogged: state.estopLog.length === 1, notQueued: state.queue.length === 0 };
  });
  assert.strictEqual(afterEstop.estopLogged, true);
  console.log("  ✓ Estop bypasses queue and active slot");

  // Test 8: Operation Deadline (timeout)
  console.log("Test 8: Operation Deadline (timeout)");
  const afterDeadline = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: [],
    });

    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    const deadline = state.active.deadlineAt - state.now;

    // Advance past deadline
    state = bs.dispatch(state, { type: "TICK", dt: deadline + 10 });

    if (state.active || state.resources[0].status !== "failed-retrying") {
      throw new Error("Deadline expiry not handled as failure");
    }

    return {
      noActive: !state.active,
      failed: state.resources[0].status === "failed-retrying",
      attempt: state.resources[0].attempt,
    };
  });
  assert.strictEqual(afterDeadline.noActive, true);
  assert.strictEqual(afterDeadline.failed, true);
  console.log("  ✓ Deadline expiry classified as no-response");

  // Test 9: Page Startup Order (liveUpdatesStarted)
  console.log("Test 9: Page Startup Order — live updates only after resources+sections");
  const afterStartup = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: ["status"],
    });

    // Load resource
    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });
    if (state.liveUpdatesStarted) throw new Error("Live updates started too early");

    // Load section
    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "success" } });

    return { liveStarted: state.liveUpdatesStarted, sectionsStable: state.sectionsStable };
  });
  assert.strictEqual(afterStartup.liveStarted, true);
  assert.strictEqual(afterStartup.sectionsStable, true);
  console.log("  ✓ Live updates start when resources ready AND sections stable");

  // Test 10: Growing backoff for no-response
  console.log("Test 10: Growing backoff for no-response retries");
  const afterBackoff = await page.evaluate(() => {
    const bs = window.PageBootstrap;
    let state = bs.createBootstrap({
      resources: ["api.js"],
      sections: [],
    });

    const delaysBetweenRetries = [];

    // Attempt 1
    state = bs.dispatch(state, { type: "TICK", dt: 1 });
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "no-response" } });
    delaysBetweenRetries.push(state.resources[0].nextAt - state.now);

    // Attempt 2
    state = bs.dispatch(state, { type: "TICK", dt: delaysBetweenRetries[0] + 1 });
    state = bs.dispatch(state, { type: "RESULT", outcome: { kind: "no-response" } });
    delaysBetweenRetries.push(state.resources[0].nextAt - state.now);

    // Backoff should grow
    if (delaysBetweenRetries[1] <= delaysBetweenRetries[0]) {
      throw new Error("Backoff not increasing");
    }

    return { delays: delaysBetweenRetries };
  });
  assert.ok(afterBackoff.delays[1] > afterBackoff.delays[0]);
  console.log("  ✓ Backoff increases with each attempt");

  await browser.close();
  console.log("\nAll tests passed! ✓");
}

testBootstrapStateMachine().catch((err) => {
  console.error("Test failed:", err);
  process.exit(1);
});
