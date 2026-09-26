// =============================================================================
// dome.js
//
// Dome page controller.
// - Live RC dome target status (read-only)
// - Dome motor configuration load/save
// - Shared API helper error handling
// =============================================================================
(() => {
  const domeFeedback = document.getElementById("dome-feedback");
  const domeDisabledCard = document.getElementById("dome-disabled-card");
  const domeHardwareState = document.getElementById("dome-hardware-state");
  const domeSpeedDisplay = document.getElementById("dome-speed-display");
  const domeRotationState = document.getElementById("dome-rotation-state");
  const domeLiveFill = document.getElementById("dome-live-fill");

  const domeNeutral = document.getElementById("dome-neutral");
  const domeMinPulse = document.getElementById("dome-min-pulse");
  const domeMaxPulse = document.getElementById("dome-max-pulse");
  const domeSpeedLimit = document.getElementById("dome-speed-limit");
  const reloadEscButton = document.getElementById("reload-esc-button");
  const escFeedback = document.getElementById("esc-feedback");

  const domeRndEnable = document.getElementById("dome-rnd-enable");
  const domeRndSpeed = document.getElementById("dome-rnd-speed");
  const domeRndPauseMin = document.getElementById("dome-rnd-pause-min");
  const domeRndPauseMax = document.getElementById("dome-rnd-pause-max");
  const domeRndMoveMs = document.getElementById("dome-rnd-move-ms");
  const reloadRndButton = document.getElementById("reload-rnd-button");
  const rndFeedback = document.getElementById("rnd-feedback");

  let domeHardwareEnabled = true;
  // Only WHETHER the droid has sent a reading yet, not what it said about web
  // control: this surface has no control web control gates, so the value
  // itself is the plate's to report (#348).
  let statusHeard = false;

  const FEEDBACK_BASE_CLASS = "feedback";

  const showFeedback = (el, text, level = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = level ? `${FEEDBACK_BASE_CLASS} ${level}` : FEEDBACK_BASE_CLASS;
  };

  // The three readouts this surface used to paint as colored pills are now
  // words, and the setPillState that painted them is gone with them. None of
  // the three was a health signal, which is the only thing that may take a
  // signal color (CONTEXT.md "Status Color"):
  //
  //   the dome motor switched on or off in Configuration is an AVAILABILITY FAMILY,
  //   "change it here", and those are told apart by treatment and never by hue
  //   - it read green when on and amber when off, and amber promises the
  //     builder something is wrong rather than that they made a choice;
  //
  //   which way the dome is being turned is a VALUE - it read green forward
  //     and amber reverse, so turning left looked like a symptom;
  //
  //   whether this browser may command the droid is a CHOSEN POSTURE and
  //     takes no color at all. It is also the Status Plate's CONTROL chip, so
  //     what is left here is the half the plate cannot carry: that the consent
  //     is the feet's and the dome turns either way.
  const setText = (el, text) => {
    if (el) el.textContent = text;
  };


  const clampSpeed = (value) => {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return 0;
    return Math.max(-1, Math.min(1, parsed));
  };


  const renderDomeTargetSpeed = (speed) => {
    const normalized = clampSpeed(speed);
    const percent = Math.round(normalized * 100);
    const widthPct = Math.abs(percent) / 2;

    if (domeSpeedDisplay) domeSpeedDisplay.textContent = `${percent}%`;

    // Which side of centre the bar fills is what says the direction; the bar's
    // own color is one color, declared in the stylesheet, the same one Foot
    // Drive's live output bars take. It used to be mixed towards green going
    // forward and towards amber going back, which made one of the two
    // directions look like a fault.
    if (domeLiveFill) {
      domeLiveFill.style.width = `${widthPct}%`;
      if (widthPct < 0.5) {
        domeLiveFill.style.opacity = "0";
        domeLiveFill.style.left = "50%";
      } else if (percent >= 0) {
        domeLiveFill.style.opacity = "1";
        domeLiveFill.style.left = "50%";
      } else {
        domeLiveFill.style.opacity = "1";
        domeLiveFill.style.left = `calc(50% - ${widthPct}%)`;
      }
    }

    // The word, and only the word. The signed percentage sits beside it in
    // #dome-speed-display, and printing the number in both put the same fact on
    // the plate twice - once as "-42%" and once as "Reverse 42%".
    if (Math.abs(percent) < 2) {
      setText(domeRotationState, "Idle");
    } else if (percent > 0) {
      setText(domeRotationState, "Forward");
    } else {
      setText(domeRotationState, "Reverse");
    }
  };

  const updateDomeControlsEnabled = () => {
    const configEnabled = domeHardwareEnabled;
    window.PAApi.gateControls(
      [domeNeutral, domeMinPulse, domeMaxPulse, domeSpeedLimit, reloadEscButton,
       domeRndEnable, domeRndSpeed, domeRndPauseMin, domeRndPauseMax, domeRndMoveMs, reloadRndButton],
      configEnabled,
    );

    domeDisabledCard?.classList.toggle("hidden", domeHardwareEnabled);

    setText(
      domeHardwareState,
      domeHardwareEnabled ? "switched on" : "switched off in Configuration",
    );

    if (!domeHardwareEnabled) {
      showFeedback(domeFeedback, "Dome ESC is switched off. Switch it on in Configuration.", "warning");
    } else if (!statusHeard) {
      showFeedback(domeFeedback, window.PALiveReading.FINDING_OUT);
    } else {
      showFeedback(domeFeedback, "Dome ready.");
    }
  };

  const setDomeHardwareEnabled = (enabled) => {
    domeHardwareEnabled = enabled;
    if (!enabled) {
      renderDomeTargetSpeed(0);
    }
    updateDomeControlsEnabled();
  };

  const resolveDomeEnabledFromStatus = (payload) => {
    if (typeof payload?.domeEnabled === "boolean") {
      return payload.domeEnabled;
    }
    if (typeof payload?.components?.domeEsc?.enabled === "boolean") {
      return payload.components.domeEsc.enabled;
    }
    return null;
  };

  const resolveDomeTargetSpeed = (payload) => {
    const direct = Number(payload?.domeTargetSpeed);
    if (!Number.isFinite(direct)) return 0;
    return clampSpeed(direct);
  };

  const renderReading = (reading) => {
    const payload = reading.status;
    statusHeard = payload !== null;

    const statusDomeEnabled = resolveDomeEnabledFromStatus(payload);
    if (typeof statusDomeEnabled === "boolean") {
      domeHardwareEnabled = statusDomeEnabled;
    }

    renderDomeTargetSpeed(resolveDomeTargetSpeed(payload));
    updateDomeControlsEnabled();
  };

  const renderEscConfigSnapshot = (data) => {
    const domeEsc = data?.domeEsc || {};
    const components = data?.components || {};

    if (domeNeutral && domeEsc.neutralUs !== undefined) domeNeutral.value = domeEsc.neutralUs;
    if (domeMinPulse && domeEsc.minPulseUs !== undefined) domeMinPulse.value = domeEsc.minPulseUs;
    if (domeMaxPulse && domeEsc.maxPulseUs !== undefined) domeMaxPulse.value = domeEsc.maxPulseUs;
    if (domeSpeedLimit && domeEsc.speedLimitPct !== undefined) domeSpeedLimit.value = domeEsc.speedLimitPct;

    if (domeRndEnable && domeEsc.rndEnable !== undefined) domeRndEnable.checked = domeEsc.rndEnable;
    if (domeRndSpeed && domeEsc.rndSpeedPct !== undefined) domeRndSpeed.value = domeEsc.rndSpeedPct;
    if (domeRndPauseMin && domeEsc.rndPauseMin !== undefined) domeRndPauseMin.value = domeEsc.rndPauseMin;
    if (domeRndPauseMax && domeEsc.rndPauseMax !== undefined) domeRndPauseMax.value = domeEsc.rndPauseMax;
    if (domeRndMoveMs && domeEsc.rndMoveMs !== undefined) domeRndMoveMs.value = domeEsc.rndMoveMs;

    setDomeHardwareEnabled(Boolean(components.domeEsc?.enabled));
  };


  const loadEscConfig = async ({ handle = null } = {}) => {
    if (!window.PAApi) throw new Error("API helper unavailable");
    showFeedback(escFeedback, "Loading motor settings...");

    try {
      const api = handle || window.PAApi;
      const result = await api.get("/api/config");
      renderEscConfigSnapshot(result.data);
      const ts = new Date().toLocaleTimeString();
      showFeedback(escFeedback, `Motor settings loaded at ${ts}`, "success");
      showFeedback(rndFeedback, `Loaded at ${ts}`, "success");
    } catch (error) {
      showFeedback(escFeedback, `Failed to load motor settings: ${window.PAApi.messageFor(error)}`, "error");
      throw error;
    }
  };

  // What goes out is what the builder typed: the droid holds the ranges and
  // the pulse order, and a value it will not take comes back as a refusal
  // worded by PAApi.messageFor() (ADR 0068, amended 2026-09-26).
  const typed = (input) => String(input?.value ?? "").trim();

  const escPayload = () => ({
    domeEscNeutralUs: typed(domeNeutral),
    domeEscMinPulseUs: typed(domeMinPulse),
    domeEscMaxPulseUs: typed(domeMaxPulse),
    domeEscSpeedLimitPct: typed(domeSpeedLimit),
  });

  const saveEscConfig = async () => {
    if (!window.PAApi) return;
    if (!domeHardwareEnabled) {
      showFeedback(escFeedback, "Dome settings unavailable: enable DOME — Dome ESC in Configuration.", "warning");
      return;
    }

    showFeedback(escFeedback, "Saving...");

    try {
      await window.PAApi.postForm(
        "/api/config",
        escPayload(),
        { timeoutMs: 3000 },
      );

      showFeedback(escFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(escFeedback, `Failed to save motor settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const rndPayload = () => ({
    domeEscRndEnable: domeRndEnable?.checked ? "true" : "false",
    domeEscRndSpeedPct: typed(domeRndSpeed),
    domeEscRndPauseMin: typed(domeRndPauseMin),
    domeEscRndPauseMax: typed(domeRndPauseMax),
    domeEscRndMoveMs: typed(domeRndMoveMs),
  });

  const saveRndDomeConfig = async () => {
    if (!window.PAApi) return;
    if (!domeHardwareEnabled) {
      showFeedback(rndFeedback, "Random dome controls unavailable: enable DOME — Dome ESC in Configuration.", "warning");
      return;
    }

    showFeedback(rndFeedback, "Saving...");

    try {
      await window.PAApi.postForm(
        "/api/config",
        rndPayload(),
        { timeoutMs: 3000 },
      );

      showFeedback(rndFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(rndFeedback, `Failed to save random movement settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const debouncedSave = window.PAUtils.debounce(saveEscConfig, 500);
  const debouncedRndSave = window.PAUtils.debounce(saveRndDomeConfig, 500);

  domeNeutral?.addEventListener("input", debouncedSave);
  domeMinPulse?.addEventListener("input", debouncedSave);
  domeMaxPulse?.addEventListener("input", debouncedSave);
  domeSpeedLimit?.addEventListener("input", debouncedSave);
  reloadEscButton?.addEventListener("click", loadEscConfig);

  domeRndEnable?.addEventListener("input", debouncedRndSave);
  domeRndSpeed?.addEventListener("input", debouncedRndSave);
  domeRndPauseMin?.addEventListener("input", debouncedRndSave);
  domeRndPauseMax?.addEventListener("input", debouncedRndSave);
  domeRndMoveMs?.addEventListener("input", debouncedRndSave);
  reloadRndButton?.addEventListener("click", loadEscConfig);

  // The dome's live state rides the Live Reading, which owns the stream or the
  // one fallback poll for the whole shell (data/live_reading.js).
  window.PALiveReading.subscribe(renderReading);

  // -------------------------------------------------------------------------
  // Boot — load config then start status subscription
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API load as a section so the bootstrap
  // can show recovery state if the config fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const SECTIONS = [
    ["dome-configuration", loadEscConfig, "dome configuration"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadEscConfig().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/live_reading.js": "live updates",
      "/shell.js": "page layout",
      "/dome.js": "dome control",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  renderDomeTargetSpeed(0);
  updateDomeControlsEnabled();
  startPageLoad();
})();
