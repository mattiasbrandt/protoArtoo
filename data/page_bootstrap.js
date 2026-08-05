// =============================================================================
// data/page_bootstrap.js
//
// Common Page Bootstrap: the shared page-load recovery state model, used by
// every controller page. See docs/page-load-recovery-architecture.md
// ("Common Page Bootstrap interface") for the contract this implements, and
// ADR 0016 (busy/no-response wire contract) plus ADR 0019 (single active
// request slot, rollout order) for the decisions behind it.
//
// This module is a PURE reducer: no DOM, no fetch, no timers, no logging. A
// host owns the real clock and network and calls dispatch(state, action). That
// purity is what makes the model testable under injected faults without a
// device, and what keeps every page's recovery behavior identical.
// =============================================================================
(() => {
  // Browser Request Priority. Estop never enters the queue at all (see
  // SUBMIT_ESTOP); the rest are ordered here, FIFO within a priority.
  const PRIORITY = { ESTOP: 0, COMMAND: 1, STARTUP: 2, BACKGROUND: 3 };

  // Recovery Retry Interval: the server's fixed Retry-After for a busy
  // refusal (ADR 0016). Used only when the response carried no usable header.
  const DEFAULT_BUSY_RETRY_MS = 5000;

  // A no-response outcome has no server-given interval to honor, so it uses
  // growing pauses instead of a fixed one.
  const NO_RESPONSE_BASE_BACKOFF_MS = 2000;
  const NO_RESPONSE_MAX_BACKOFF_MS = 30000;

  // Operation Deadline for ordinary page work. Matches web_api.js's
  // DEFAULT_TIMEOUT_MS so the client-side deadline and the request timeout
  // describe the same boundary rather than fighting each other.
  const OPERATION_DEADLINE_MS = 6000;

  const backoffFor = (attempt) => {
    const ms = NO_RESPONSE_BASE_BACKOFF_MS * Math.pow(2, Math.max(0, attempt - 1));
    return Math.min(ms, NO_RESPONSE_MAX_BACKOFF_MS);
  };

  // ---------------------------------------------------------------------------
  // Outcome classification
  //
  // Retry policy has exactly two failure shapes: 'busy' (server said so, honor
  // its interval) and 'no-response' (nothing usable came back, back off). The
  // originating ApiError kind/status is carried through as `reason` purely so
  // the view can say something specific; it never changes a transition.
  // ---------------------------------------------------------------------------
  const classifyOutcome = (error, retryAfterMs = null) => {
    if (!error) return { kind: "success" };

    const kind = error.kind || "unknown";
    const status = error.status || 0;

    if (kind === "http" && status === 503) {
      return {
        kind: "busy",
        reason: "busy",
        retryAfterMs: retryAfterMs ?? error.retryAfterMs ?? DEFAULT_BUSY_RETRY_MS,
      };
    }

    // timeout and network are the honest no-response cases. Other http
    // statuses and malformed JSON are also "no usable result", so they follow
    // the same backoff path -- but keep their own reason so the view can
    // distinguish "device rejected it" from "nothing came back".
    return { kind: "no-response", reason: kind, status };
  };

  // ---------------------------------------------------------------------------
  // State construction
  // ---------------------------------------------------------------------------
  const makeStep = (name) => ({
    name,
    status: "pending",
    attempt: 0,
    nextAt: null,
    reason: null,
  });

  const createBootstrap = ({ resources = [], sections = [] } = {}) =>
    // Settle the derived flags immediately, so a page declaring no sections
    // (or nothing at all) is already stable rather than waiting for a result
    // that will never arrive.
    recomputeSectionsStable(baseBootstrap(resources, sections));

  const baseBootstrap = (resources, sections) => ({
    now: 0,
    visible: true,
    nextId: 1,

    // Resource Step Recovery: resources load one at a time in declared order
    // through a single cursor. A failure pauses the cursor and retries only
    // that step; completed steps are never redone.
    resources: resources.map(makeStep),
    resourceCursor: 0,
    resourcesReady: resources.length === 0,

    // Section Recovery: once resources are ready, sections are independent --
    // one section's failure neither blocks nor resets another.
    sections: sections.map(makeStep),
    sectionsStable: false,

    // Page Startup Order: live updates start only once resources are ready
    // AND sections are stable.
    liveUpdatesStarted: false,

    queue: [],
    active: null,
    estopLog: [],
    commandLog: [],
  });

  // ---------------------------------------------------------------------------
  // Structural helpers
  //
  // Every helper returns new objects rather than mutating, so a host can hold
  // a previous state for comparison and rendering diffs stay honest.
  // ---------------------------------------------------------------------------
  const listKey = (kind) => (kind === "resource" ? "resources" : "sections");

  const replaceStep = (state, kind, name, changes) => {
    const key = listKey(kind);
    return {
      ...state,
      [key]: state[key].map((step) => (step.name === name ? { ...step, ...changes } : step)),
    };
  };

  const findStep = (state, kind, name) => state[listKey(kind)].find((step) => step.name === name);

  const takeId = (state) => [state.nextId, { ...state, nextId: state.nextId + 1 }];

  // ---------------------------------------------------------------------------
  // Scheduling
  // ---------------------------------------------------------------------------
  const dispatchableWork = (state) => {
    // Queued work first (priority, then FIFO), then the resource cursor while
    // resources are still loading, then the next due section.
    const ready = state.queue
      .filter((item) => item.nextAt === null || item.nextAt <= state.now)
      .sort((a, b) => a.priority - b.priority || a.id - b.id);
    if (ready.length > 0) return ready[0];

    if (!state.resourcesReady) {
      const step = state.resources[state.resourceCursor];
      const due =
        step &&
        (step.status === "pending" ||
          (step.status === "failed-retrying" && step.nextAt <= state.now));
      // If the cursor step is loading, or failed and not yet due, nothing else
      // may start -- that is what makes resource loading strictly ordered.
      return due ? { id: null, priority: PRIORITY.STARTUP, kind: "resource", name: step.name } : null;
    }

    const dueSection = state.sections.find(
      (s) => s.status === "pending" || (s.status === "failed-retrying" && s.nextAt <= state.now)
    );
    return dueSection
      ? { id: null, priority: PRIORITY.BACKGROUND, kind: "section", name: dueSection.name }
      : null;
  };

  const startWork = (state, work) => {
    let next = state;
    let id = work.id;
    if (id === null) {
      [id, next] = takeId(next);
    }

    // A dequeued user command has no step-list entry; its bookkeeping lives in
    // commandLog, written when it was submitted.
    if (work.kind === "resource" || work.kind === "section") {
      const step = findStep(next, work.kind, work.name);
      next = replaceStep(next, work.kind, work.name, {
        status: "loading",
        attempt: step.attempt + 1,
      });
    }

    return {
      ...next,
      queue: next.queue.filter((item) => item.id !== id),
      active: {
        id,
        kind: work.kind,
        name: work.name,
        priority: work.priority,
        startedAt: next.now,
        deadlineAt: next.now + OPERATION_DEADLINE_MS,
      },
    };
  };

  // Start the next piece of work immediately rather than waiting for the next
  // tick, so a fast succession of results does not look stalled.
  const pump = (state) => {
    if (!state.visible || state.active) return state;
    const work = dispatchableWork(state);
    return work ? startWork(state, work) : state;
  };

  const applyOutcome = (state, active, outcome) => {
    if (outcome.kind === "success") {
      let next = replaceStep(state, active.kind, active.name, {
        status: "done",
        nextAt: null,
        reason: null,
      });
      if (active.kind === "resource") {
        const cursor = next.resourceCursor + 1;
        next = {
          ...next,
          resourceCursor: cursor,
          resourcesReady: cursor >= next.resources.length,
        };
      }
      return next;
    }

    const step = findStep(state, active.kind, active.name);
    const retryDelay =
      outcome.kind === "busy"
        ? outcome.retryAfterMs ?? DEFAULT_BUSY_RETRY_MS
        : backoffFor(step.attempt);

    return replaceStep(state, active.kind, active.name, {
      status: "failed-retrying",
      nextAt: state.now + retryDelay,
      reason: outcome.reason || outcome.kind,
    });
  };

  // Sections are "stable" when every one is either done or visibly waiting to
  // retry -- not when all have succeeded. A page with one permanently failing
  // section must still start live updates.
  const recomputeSectionsStable = (state) => {
    const stable = state.sections.every(
      (s) => s.status === "done" || s.status === "failed-retrying"
    );
    return {
      ...state,
      sectionsStable: stable,
      liveUpdatesStarted:
        state.liveUpdatesStarted || (stable && state.resourcesReady),
    };
  };

  const settleActive = (state, outcome) => {
    const active = state.active;
    let next = applyOutcome({ ...state, active: null }, active, outcome);
    next = recomputeSectionsStable(next);
    return pump(next);
  };

  // ---------------------------------------------------------------------------
  // Reducer
  // ---------------------------------------------------------------------------
  const dispatch = (prev, action) => {
    switch (action.type) {
      case "TICK": {
        let state = { ...prev, now: prev.now + action.dt };

        // Operation Deadline: an active request past its deadline with no
        // result is a no-response outcome. This is the same boundary the wire
        // contract draws between busy and no-response.
        if (state.active && state.now >= state.active.deadlineAt) {
          if (state.active.kind === "command") {
            state = finishCommand(state, { kind: "no-response", reason: "timeout" });
          } else {
            state = settleActive(state, { kind: "no-response", reason: "timeout" });
          }
          return state;
        }

        // Hidden Tab Pause: while hidden no NEW work starts, but the single
        // in-flight request is left to finish within its deadline rather than
        // being aborted just because visibility changed.
        return pump(state);
      }

      case "RESULT": {
        if (!prev.active) return prev; // nothing in flight to resolve
        if (prev.active.kind === "command") return finishCommand(prev, action.outcome);
        return settleActive(prev, action.outcome);
      }

      case "DECLARE_SECTIONS": {
        // Page scripts declare their own sections as they execute, which is
        // during resource loading -- before any section work may start. Only
        // additive, and only while no section has run yet, so this can never
        // discard in-progress or completed section state.
        const known = new Set(prev.sections.map((s) => s.name));
        const added = action.names.filter((name) => !known.has(name)).map(makeStep);
        if (added.length === 0) return prev;
        if (prev.sections.some((s) => s.status !== "pending")) return prev;
        // recomputeSectionsStable re-derives the flag from the new list, so a
        // page that was momentarily stable with nothing to do becomes unstable
        // again as soon as it declares real work.
        return recomputeSectionsStable({
          ...prev,
          sections: [...prev.sections, ...added],
        });
      }

      case "RETRY_NOW": {
        // Operator-facing "Retry now": pull the named waiting step forward
        // regardless of its scheduled time.
        let state = prev;
        for (const kind of ["resource", "section"]) {
          const step = findStep(state, kind, action.name);
          if (step && step.status === "failed-retrying") {
            state = replaceStep(state, kind, action.name, { nextAt: state.now });
          }
        }
        return pump(state);
      }

      case "VISIBILITY": {
        if (!action.visible) {
          // Queued-but-not-started work is discarded rather than replayed on
          // return; it is recomputed from current step state when shown again.
          return { ...prev, visible: false, queue: [] };
        }
        return pump({ ...prev, visible: true });
      }

      case "SUBMIT_ESTOP": {
        // Latching Estop bypasses the queue and the single active slot
        // entirely. It is never queued and never auto-retried.
        const [id, state] = takeId(prev);
        return {
          ...state,
          estopLog: [...state.estopLog, { id, name: action.name, at: state.now }],
        };
      }

      case "SUBMIT_COMMAND": {
        // User commands go ahead of automatic work but never preempt whatever
        // is already active.
        const [id, state] = takeId(prev);
        const entry = { id, name: action.name, status: "pending", at: state.now };
        const withLog = { ...state, commandLog: [...state.commandLog, entry] };

        if (!withLog.active) {
          return startWork(withLog, {
            id,
            priority: PRIORITY.COMMAND,
            kind: "command",
            name: action.name,
          });
        }
        return {
          ...withLog,
          queue: [
            ...withLog.queue,
            { id, priority: PRIORITY.COMMAND, kind: "command", name: action.name, nextAt: null },
          ],
        };
      }

      case "COMMAND_RESULT": {
        if (!prev.active || prev.active.kind !== "command") return prev;
        return finishCommand(prev, action.outcome);
      }

      default:
        return prev;
    }
  };

  // A failed user command is just failed -- never requeued, never auto-retried.
  const finishCommand = (state, outcome) => {
    const active = state.active;
    const next = {
      ...state,
      active: null,
      commandLog: state.commandLog.map((entry) =>
        entry.id === active.id ? { ...entry, status: outcome.kind } : entry
      ),
    };
    return pump(next);
  };

  window.PageBootstrap = {
    PRIORITY,
    DEFAULT_BUSY_RETRY_MS,
    OPERATION_DEADLINE_MS,
    createBootstrap,
    classifyOutcome,
    dispatch,
  };
})();
