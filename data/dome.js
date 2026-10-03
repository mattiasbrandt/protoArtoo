// =============================================================================
// dome.js
//
// Dome page controller.
// - Live RC dome target status (read-only)
// - Where the dome believes it points, Front is here and Go home (#445)
// - Dome motor configuration load/save, and the full turn the bearing is
//   integrated against
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

  const domeBearingValue = document.getElementById("dome-bearing-value");
  const domeBearingWord = document.getElementById("dome-bearing-word");
  const domeFrontButton = document.getElementById("dome-front-button");
  const domeHomeButton = document.getElementById("dome-home-button");

  const domeTurnState = document.getElementById("dome-turn-state");
  const domeTurnMs = document.getElementById("dome-turn-ms");
  const domeTurnPct = document.getElementById("dome-turn-pct");
  const domeTurnDir = document.getElementById("dome-turn-dir");
  const domeTurnDirButtons = Array.from(domeTurnDir?.querySelectorAll("button[data-value]") || []);
  const turnFeedback = document.getElementById("turn-feedback");

  let domeHardwareEnabled = true;
  // Only WHETHER the droid has sent a reading yet, not what it said about web
  // control: this surface has no control web control gates, so the value
  // itself is the plate's to report (#348).
  let statusHeard = false;
  // What the feedback line last said about the dome itself (updateDomeControlsEnabled()).
  let feedbackState = null;

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
       domeRndEnable, domeRndSpeed, domeRndPauseMin, domeRndPauseMax, domeRndMoveMs, reloadRndButton,
       domeFrontButton, domeHomeButton, domeTurnMs, domeTurnPct, ...domeTurnDirButtons],
      configEnabled,
    );

    domeDisabledCard?.classList.toggle("hidden", domeHardwareEnabled);

    setText(
      domeHardwareState,
      domeHardwareEnabled ? "switched on" : "switched off in Configuration",
    );

    // Written when the state it reports changes, not on every reading: the
    // line also carries the answer to Front is here and Go home, and a reading
    // a second later would otherwise wipe the droid's clause before it is read.
    const state = !domeHardwareEnabled ? "off" : !statusHeard ? "waiting" : "ready";
    if (state === feedbackState) return;
    feedbackState = state;
    if (state === "off") {
      showFeedback(domeFeedback, "Dome ESC is switched off. Switch it on in Configuration.", "warning");
    } else if (state === "waiting") {
      showFeedback(domeFeedback, "Waiting for the droid.");
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

  // ---------------------------------------------------------------------------
  // Where the dome points (#445). The number comes from the one accessor every
  // dome drawing reads (data/dome_bearing.js), with "believed" beside it in its
  // own row; unknown is a word in the number's place, never a number.
  // ---------------------------------------------------------------------------
  const renderBearing = (bearing) => {
    if (!domeBearingValue) return;
    if (bearing?.believed) {
      domeBearingValue.textContent = `${bearing.deg.toFixed(1)}°`;
      domeBearingValue.classList.remove("is-unknown");
      if (domeBearingWord) domeBearingWord.hidden = false;
    } else {
      domeBearingValue.textContent = "Unknown";
      domeBearingValue.classList.add("is-unknown");
      if (domeBearingWord) domeBearingWord.hidden = true;
    }
  };

  // A press goes to the droid, which decides; a refusal comes back as its one
  // clause (include/dome_bearing_act.h) and is shown as it is.
  const pressBearingAct = async (route, done) => {
    if (!window.PAApi) return;
    try {
      await window.PAApi.postForm(route, {}, { timeoutMs: 3000 });
      showFeedback(domeFeedback, done, "success");
    } catch (error) {
      showFeedback(domeFeedback, window.PAApi.messageFor(error), "warning");
    }
  };

  domeFrontButton?.addEventListener("click", () => pressBearingAct("/api/dome/front", "Front set."));
  domeHomeButton?.addEventListener("click", () => pressBearingAct("/api/dome/home", "Turning home."));

  // ---------------------------------------------------------------------------
  // The full turn: three numbers the droid cannot measure. 0 and `unset` are
  // "never recorded", so a box holding 0 is shown empty and the subtitle says
  // so until all three are set.
  // ---------------------------------------------------------------------------
  let turnDir = "unset";

  const paintTurnDir = () => {
    domeTurnDirButtons.forEach((button) => {
      const on = button.dataset.value === turnDir;
      button.classList.toggle("active", on);
      button.setAttribute("aria-pressed", on ? "true" : "false");
      button.setAttribute("aria-checked", on ? "true" : "false");
    });
  };

  const paintTurnState = () => {
    const set = Number(domeTurnMs?.value) > 0 && Number(domeTurnPct?.value) > 0 && turnDir !== "unset";
    setText(domeTurnState, set ? "set" : "not set");
  };

  const renderTurnSnapshot = (domeEsc) => {
    const shown = (value) => (Number(value) > 0 ? String(value) : "");
    if (domeTurnMs && domeEsc.fullTurnMs !== undefined) domeTurnMs.value = shown(domeEsc.fullTurnMs);
    if (domeTurnPct && domeEsc.fullTurnPct !== undefined) domeTurnPct.value = shown(domeEsc.fullTurnPct);
    if (domeEsc.positiveTurn !== undefined) turnDir = String(domeEsc.positiveTurn);
    paintTurnDir();
    paintTurnState();
  };

  // What the builder typed; an empty box is left out rather than sent as 0.
  const turnPayload = (extra = {}) => {
    const payload = { ...extra };
    if (typed(domeTurnMs) !== "") payload.domeEscFullTurnMs = typed(domeTurnMs);
    if (typed(domeTurnPct) !== "") payload.domeEscFullTurnPct = typed(domeTurnPct);
    return payload;
  };

  const saveTurn = async (extra = {}) => {
    if (!window.PAApi || !domeHardwareEnabled) return;
    const payload = turnPayload(extra);
    if (Object.keys(payload).length === 0) return;
    showFeedback(turnFeedback, "Saving...");
    try {
      await window.PAApi.postForm("/api/config", payload, { timeoutMs: 3000 });
      paintTurnState();
      showFeedback(turnFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(turnFeedback, `Failed to save the full turn: ${window.PAApi.messageFor(error, payload)}`, "error");
    }
  };

  domeTurnDirButtons.forEach((button) => {
    button.addEventListener("click", () => {
      if (button.dataset.value === turnDir) return;
      turnDir = button.dataset.value;
      paintTurnDir();
      saveTurn({ domeEscPositiveTurn: turnDir });
    });
  });

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
    renderTurnSnapshot(domeEsc);

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
      showFeedback(turnFeedback, `Loaded at ${ts}`, "success");
    } catch (error) {
      showFeedback(escFeedback, `Failed to load motor settings: ${window.PAApi.messageFor(error)}`, "error");
      showFeedback(turnFeedback, "Failed to load the full turn.", "error");
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
  const debouncedTurnSave = window.PAUtils.debounce(() => saveTurn(), 500);

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

  domeTurnMs?.addEventListener("input", debouncedTurnSave);
  domeTurnPct?.addEventListener("input", debouncedTurnSave);

  // The dome's live state rides the Live Reading, which owns the stream or the
  // one fallback poll for the whole shell (data/live_reading.js).
  window.PALiveReading.subscribe(renderReading);
  window.PADomeBearing?.subscribe(renderBearing);

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
      "/dome_bearing.js": "where the dome points",
      "/shell.js": "page layout",
      "/dome.js": "dome control",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  renderDomeTargetSpeed(0);
  updateDomeControlsEnabled();
  startPageLoad();
})();
