// =============================================================================
// data/page_bootstrap.js
//
// Common Page Bootstrap: page load recovery state machine.
//
// Pure reducer over { resources, sections } implementing Resource Step
// Recovery, Section Recovery, Page Startup Order, Browser Request Priority
// (single slot), Hidden Tab Pause, and Operation Deadline per
// docs/page-load-recovery-architecture.md (ADRs 0016-0019).
//
// This module is framework-agnostic (no DOM, fetch, timers, or globals).
// A host page (e.g., wifi.html via wifi.js) owns the real clock/network and
// calls dispatch(state, action) to drive state transitions.
// =============================================================================

(() => {
  const PRIORITY = { ESTOP: 0, COMMAND: 1, STARTUP: 2, BACKGROUND: 3 };

  // Recovery Retry Interval (ADR 0016): server's fixed Busy Recovery Page
  // Retry-After. Used when an outcome is 'busy'.
  const DEFAULT_BUSY_RETRY_MS = 5000;

  // No server-given value for 'no-response', use growing backoff.
  const NO_RESPONSE_BASE_BACKOFF_MS = 2000;
  const NO_RESPONSE_MAX_BACKOFF_MS = 30000;

  // Operation Deadline: matches web_api.js DEFAULT_TIMEOUT_MS.
  const OPERATION_DEADLINE_MS = 6000;

  // Catalog Operation Deadline: longer timeout for /api/audio/catalog.
  const OPERATION_DEADLINE_CATALOG_MS = 12000;

  function backoffFor(attempt) {
    const ms = NO_RESPONSE_BASE_BACKOFF_MS * Math.pow(2, Math.max(0, attempt - 1));
    return Math.min(ms, NO_RESPONSE_MAX_BACKOFF_MS);
  }

  let nextId = 1;
  function makeId() {
    return nextId++;
  }

  // Create initial bootstrap state for a page's resources and sections.
  // resources: array of resource names (e.g., ['web_api.js', 'status_stream.js', ...])
  // sections: array of section names (e.g., ['status', 'controls', ...])
  function createBootstrap({ resources, sections }) {
    return {
      now: 0,
      visible: true,
      // Resource Step Recovery: resources load in declared order via cursor.
      // A failed resource pauses the cursor; earlier steps untouched.
      // resourcesReady flips only once every resource succeeds.
      resources: resources.map((name) => ({ name, status: "pending", attempt: 0, nextAt: null })),
      resourceCursor: 0,
      resourcesReady: false,
      // Section Recovery: independent once resources ready. One failure
      // does not block others. sectionsStable requires every section
      // done or visibly waiting to retry, not all succeeded.
      sections: sections.map((name) => ({ name, status: "pending", attempt: 0, nextAt: null })),
      sectionsStable: false,
      // Page Startup Order: /api/events starts only once resourcesReady
      // && sectionsStable.
      liveUpdatesStarted: false,
      // Browser Request Priority queue: Estop bypasses entirely (separate
      // log). Single active request slot drawn from priority order.
      queue: [],
      // At most one active bounded request at a time.
      active: null,
      // Estop log: modeled as always-immediately-actionable, never queued.
      estopLog: [],
      // User command log: never auto-retried on failure.
      commandLog: [],
      // Event log: last 12 messages for debugging.
      events: [],
    };
  }

  function log(state, message) {
    state.events = [...state.events.slice(-11), `[${state.now}ms] ${message}`];
  }

  // --- scheduling -------------------------------------------------------

  function dispatchableWork(state) {
    // Priority order: queue (highest), next resource step, next pending section.
    const ready = state.queue.filter((item) => item.nextAt === null || item.nextAt <= state.now);
    if (ready.length > 0) {
      ready.sort((a, b) => a.priority - b.priority || a.id - b.id);
      return ready[0];
    }
    if (!state.resourcesReady) {
      const step = state.resources[state.resourceCursor];
      const due =
        step && (step.status === "pending" || (step.status === "failed-retrying" && step.nextAt <= state.now));
      if (due) {
        return { id: makeId(), priority: PRIORITY.STARTUP, kind: "resource", name: step.name };
      }
      return null;
    }
    const dueSection = state.sections.find(
      (s) => s.status === "pending" || (s.status === "failed-retrying" && s.nextAt <= state.now)
    );
    if (dueSection) {
      return { id: makeId(), priority: PRIORITY.BACKGROUND, kind: "section", name: dueSection.name };
    }
    return null;
  }

  function startWork(state, work) {
    let attemptNote = "";
    if (work.kind === "resource" || work.kind === "section") {
      const step = stepList(state, work.kind).find((s) => s.name === work.name);
      step.status = "loading";
      step.attempt += 1;
      attemptNote = ` (attempt ${step.attempt})`;
    }
    state.active = {
      id: work.id,
      kind: work.kind,
      name: work.name,
      priority: work.priority,
      startedAt: state.now,
      deadlineAt: state.now + (work.catalogDeadline ? OPERATION_DEADLINE_CATALOG_MS : OPERATION_DEADLINE_MS),
    };
    state.queue = state.queue.filter((item) => item.id !== work.id);
    log(state, `start ${work.kind}:${work.name}${attemptNote}`);
  }

  function stepList(state, kind) {
    return kind === "resource" ? state.resources : state.sections;
  }

  function applyOutcome(state, active, outcome) {
    const list = stepList(state, active.kind);
    const step = list.find((s) => s.name === active.name);

    if (outcome.kind === "success") {
      step.status = "done";
      step.nextAt = null;
      log(state, `${active.kind}:${active.name} succeeded`);
      if (active.kind === "resource") {
        state.resourceCursor += 1;
        if (state.resourceCursor >= state.resources.length) {
          state.resourcesReady = true;
          log(state, "all required resources ready");
        }
      }
    } else {
      const retryDelay =
        outcome.kind === "busy"
          ? outcome.retryAfterMs ?? DEFAULT_BUSY_RETRY_MS
          : backoffFor(step.attempt);
      step.status = "failed-retrying";
      step.nextAt = state.now + retryDelay;
      log(
        state,
        `${active.kind}:${active.name} failed (${outcome.kind}); retry in ${retryDelay}ms`
      );
    }
    return step;
  }

  function recomputeSectionsStable(state) {
    state.sectionsStable = state.sections.every(
      (s) => s.status === "done" || s.status === "failed-retrying"
    );
    if (state.sectionsStable && state.resourcesReady && !state.liveUpdatesStarted) {
      state.liveUpdatesStarted = true;
      log(state, "live updates started");
    }
  }

  // --- public reducer ---------------------------------------------------

  function dispatch(prev, action) {
    const state = JSON.parse(JSON.stringify(prev)); // structural clone

    switch (action.type) {
      case "TICK": {
        state.now += action.dt;

        // Operation Deadline: request past its deadline with no result = no-response.
        if (state.active && state.now >= state.active.deadlineAt) {
          const active = state.active;
          state.active = null;
          applyOutcome(state, active, { kind: "no-response" });
          recomputeSectionsStable(state);
        }

        // Hidden Tab Pause: only start new work if visible.
        if (state.visible && !state.active) {
          const work = dispatchableWork(state);
          if (work) startWork(state, work);
        }
        return state;
      }

      case "RESULT": {
        if (!state.active) return state;
        const active = state.active;
        state.active = null;
        applyOutcome(state, active, action.outcome);
        recomputeSectionsStable(state);
        // Immediately try next work instead of waiting for TICK.
        if (state.visible) {
          const work = dispatchableWork(state);
          if (work) startWork(state, work);
        }
        return state;
      }

      case "RETRY_NOW": {
        // Operator "Retry now": pull failed-retrying step to front.
        for (const kind of ["resource", "section"]) {
          const step = stepList(state, kind).find(
            (s) => s.name === action.name && s.status === "failed-retrying"
          );
          if (step) {
            step.nextAt = state.now;
            log(state, `Retry now: ${kind}:${action.name}`);
          }
        }
        if (state.visible && !state.active) {
          const work = dispatchableWork(state);
          if (work) startWork(state, work);
        }
        return state;
      }

      case "VISIBILITY": {
        state.visible = action.visible;
        if (!action.visible) {
          // Hidden: discard queued work, let in-flight request finish.
          state.queue = [];
          log(state, "hidden: pausing new work (in-flight request, if any, keeps running)");
        } else {
          log(state, "visible: resuming from unfinished step");
          const work = dispatchableWork(state);
          if (work && !state.active) startWork(state, work);
        }
        return state;
      }

      case "SUBMIT_ESTOP": {
        // Estop bypasses queue and active slot entirely.
        state.estopLog = [...state.estopLog, { id: makeId(), name: action.name, at: state.now }];
        log(state, `ESTOP ${action.name} sent immediately (bypassed queue/active slot)`);
        return state;
      }

      case "SUBMIT_COMMAND": {
        // User command goes ahead of background work, not auto-retried.
        const id = makeId();
        state.commandLog = [...state.commandLog, { id, name: action.name, status: "pending", at: state.now }];
        if (!state.active) {
          state.active = {
            id,
            kind: "command",
            name: action.name,
            priority: PRIORITY.COMMAND,
            startedAt: state.now,
            deadlineAt: state.now + OPERATION_DEADLINE_MS,
          };
          log(state, `command:${action.name} started immediately`);
        } else {
          state.queue = [
            ...state.queue,
            { id, priority: PRIORITY.COMMAND, kind: "command", name: action.name, nextAt: null },
          ];
          log(state, `command:${action.name} queued ahead of background work`);
        }
        return state;
      }

      case "COMMAND_RESULT": {
        // Commands are never retried.
        if (state.active && state.active.kind === "command") {
          const entry = state.commandLog.find((c) => c.id === state.active.id);
          if (entry) entry.status = action.outcome.kind;
          log(state, `command:${state.active.name} -> ${action.outcome.kind} (not retried)`);
          state.active = null;
          if (state.visible) {
            const work = dispatchableWork(state);
            if (work) startWork(state, work);
          }
        }
        return state;
      }

      default:
        return state;
    }
  }

  window.PageBootstrap = {
    PRIORITY,
    DEFAULT_BUSY_RETRY_MS,
    OPERATION_DEADLINE_MS,
    OPERATION_DEADLINE_CATALOG_MS,
    createBootstrap,
    dispatch,
  };
})();
