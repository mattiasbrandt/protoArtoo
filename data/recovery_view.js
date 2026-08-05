// =============================================================================
// data/recovery_view.js
//
// Page Recovery View: renders the Common Page Bootstrap's state as the
// operator-facing panel, so a page that is still loading or waiting to retry
// says so instead of sitting silently. Four states, per the approved design:
//
//   loading      required resources still arriving, nothing has failed
//   busy         the controller refused the request; honor its Retry-After
//   no-response  nothing came back on the first attempt
//   retrying     still nothing back, intervals are growing
//
// Reads bootstrap state and writes DOM. It derives everything it shows from
// that state -- it holds no recovery state of its own, so what the operator
// sees can never drift from what the bootstrap is actually doing.
// See ADR 0016 and docs/page-load-recovery-architecture.md.
// =============================================================================
(() => {
  const BACKDROP_ID = "page-recovery-backdrop";

  // Beyond this attempt count the wait is long enough that the operator needs
  // to be told the intervals are growing, not just that a retry is pending.
  const BACKOFF_VISIBLE_AFTER_ATTEMPT = 1;

  const REASON_DETAIL = {
    timeout: "Connection timed out. Attempting to reconnect.",
    network: "Connection to the controller was lost. Attempting to reconnect.",
    http: "The controller rejected the request. Retrying.",
    "bad-json": "The controller sent an incomplete reply. Retrying.",
  };

  // ---------------------------------------------------------------------------
  // Deriving what to show
  // ---------------------------------------------------------------------------

  // The step the operator cares about: whatever is blocking progress right
  // now. A waiting resource outranks a waiting section, because nothing else
  // can proceed until required resources land.
  const blockingStep = (state) => {
    if (!state.resourcesReady) {
      const step = state.resources[state.resourceCursor];
      if (step) return { step, kind: "resource" };
    }
    const waiting = state.sections.find((s) => s.status === "failed-retrying");
    if (waiting) return { step: waiting, kind: "section" };
    const loading = state.sections.find((s) => s.status === "loading");
    if (loading) return { step: loading, kind: "section" };
    return null;
  };

  const deriveView = (state) => {
    // Once required resources are in and every section has settled, the page
    // is usable -- get out of the operator's way even if a section is still
    // retrying in the background.
    if (state.resourcesReady && state.sectionsStable) return { visible: false };

    const blocking = blockingStep(state);
    if (!blocking) return { visible: false };

    const { step, kind } = blocking;

    if (step.status !== "failed-retrying") {
      return { visible: true, mode: "loading", stepName: step.name, kind };
    }

    const waitMs = Math.max(0, (step.nextAt ?? state.now) - state.now);
    const mode =
      step.reason === "busy"
        ? "busy"
        : step.attempt > BACKOFF_VISIBLE_AFTER_ATTEMPT
          ? "retrying"
          : "no-response";

    return {
      visible: true,
      mode,
      stepName: step.name,
      kind,
      attempt: step.attempt,
      reason: step.reason,
      waitMs,
      // Round up so a 4.2s wait reads "5 s" and reaches "1 s" before firing,
      // rather than sitting on "0 s" while nothing visibly happens.
      waitSeconds: Math.ceil(waitMs / 1000),
    };
  };

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  const el = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const countdownPanel = (label, seconds, sublabel) => {
    const panel = el("div", "recovery-countdown-panel");
    if (label) panel.appendChild(el("div", "recovery-countdown-label", label));
    panel.appendChild(el("div", "recovery-countdown-value", `${seconds} s`));
    if (sublabel) panel.appendChild(el("div", "recovery-countdown-label", sublabel));
    return panel;
  };

  const retryButton = (onRetryNow, stepName) => {
    const actions = el("div", "recovery-actions");
    const button = el("button", "btn accent", "Retry now");
    button.type = "button";
    button.addEventListener("click", () => onRetryNow(stepName));
    actions.appendChild(button);
    return actions;
  };

  const buildPanel = (view, onRetryNow) => {
    const panel = el("div", "recovery-panel");

    if (view.mode === "busy") {
      const banner = el("div", "recovery-refused-banner");
      banner.appendChild(el("span", "indicator warn"));
      banner.appendChild(el("span", null, "REQUEST REFUSED"));
      panel.appendChild(banner);
      panel.appendChild(el("div", "recovery-status-reason", "Controller busy"));
      panel.appendChild(
        el(
          "p",
          "recovery-message",
          "Controller is handling other requests. Try again in a moment."
        )
      );
      panel.appendChild(countdownPanel("Retry interval", view.waitSeconds));
      panel.appendChild(retryButton(onRetryNow, view.stepName));
      return panel;
    }

    const header = el("div", "recovery-header");
    const indicatorClass =
      view.mode === "loading" ? "indicator info" : view.mode === "retrying" ? "indicator fail" : "indicator warn";
    header.appendChild(el("span", indicatorClass));

    const headerText = el("div");
    if (view.mode === "loading") {
      headerText.appendChild(el("div", "recovery-status-reason", "Loading page resources"));
      headerText.appendChild(
        el("div", "recovery-status-detail", "Preparing the controller page")
      );
    } else {
      headerText.appendChild(
        el("div", "recovery-status-reason", "No response from controller")
      );
      headerText.appendChild(
        el(
          "div",
          "recovery-status-detail",
          view.mode === "retrying"
            ? "Still waiting. Retrying with increasing intervals."
            : REASON_DETAIL[view.reason] || "Attempting to reconnect."
        )
      );
    }
    header.appendChild(headerText);
    panel.appendChild(header);

    if (view.mode === "loading") {
      const step = el("p", "recovery-step");
      step.appendChild(el("span", "recovery-spinner"));
      step.appendChild(document.createTextNode(`Loading: ${view.stepName}`));
      panel.appendChild(step);
      panel.appendChild(
        el(
          "p",
          "recovery-message",
          "Completed resources stay loaded. Page data loads once required files are in."
        )
      );
      return panel;
    }

    panel.appendChild(
      countdownPanel(
        null,
        view.waitSeconds,
        view.mode === "retrying"
          ? `Next attempt\nAttempt ${view.attempt} (backoff)`
          : `Next attempt\nAttempt ${view.attempt}`
      )
    );

    if (view.mode === "retrying") {
      panel.appendChild(
        el(
          "p",
          "recovery-message",
          "Retry intervals are increasing so the controller is not overwhelmed."
        )
      );
    }

    panel.appendChild(retryButton(onRetryNow, view.stepName));
    return panel;
  };

  // ---------------------------------------------------------------------------
  // Mount / render
  // ---------------------------------------------------------------------------
  const ensureBackdrop = () => {
    let backdrop = document.getElementById(BACKDROP_ID);
    if (backdrop) return backdrop;

    backdrop = el("div", "recovery-backdrop");
    backdrop.id = BACKDROP_ID;
    // Announced politely: this updates on a timer, and an assertive live
    // region would interrupt the operator on every countdown tick.
    backdrop.setAttribute("role", "status");
    backdrop.setAttribute("aria-live", "polite");
    backdrop.setAttribute("aria-atomic", "true");
    document.body.appendChild(backdrop);
    return backdrop;
  };

  // Signature kept stable across renders so the countdown can repaint without
  // rebuilding the panel and stealing focus from the Retry now button.
  const signatureOf = (view) =>
    view.visible ? `${view.mode}|${view.kind}|${view.stepName}|${view.attempt ?? 0}` : "hidden";

  let lastSignature = null;

  const render = (state, { onRetryNow = () => {} } = {}) => {
    const view = deriveView(state);
    const backdrop = ensureBackdrop();

    if (!view.visible) {
      backdrop.classList.remove("active");
      document.body.classList.remove("recovery-active");
      backdrop.replaceChildren();
      lastSignature = "hidden";
      return view;
    }

    const signature = signatureOf(view);
    if (signature !== lastSignature) {
      backdrop.replaceChildren(buildPanel(view, onRetryNow));
      lastSignature = signature;
    } else {
      const value = backdrop.querySelector(".recovery-countdown-value");
      if (value) value.textContent = `${view.waitSeconds} s`;
    }

    backdrop.classList.add("active");
    document.body.classList.add("recovery-active");
    return view;
  };

  window.PARecoveryView = {
    deriveView,
    render,
  };
})();
