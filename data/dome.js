// =============================================================================
// dome.js
//
// Dome page controller.
// - Rotation slider (hold-to-run, return-to-stop)
// - Dome motor configuration load/save
// - Shared API helper error handling
// =============================================================================
(() => {
  const domeSlider = document.getElementById("dome-slider");
  const domeSpeedDisplay = document.getElementById("dome-speed-display");
  const domeFeedback = document.getElementById("dome-feedback");

  const domeDisabledCard = document.getElementById("dome-disabled-card");

  const domeNeutral = document.getElementById("dome-neutral");
  const domeMinPulse = document.getElementById("dome-min-pulse");
  const domeMaxPulse = document.getElementById("dome-max-pulse");
  const domeSpeedLimit = document.getElementById("dome-speed-limit");
  const reloadEscButton = document.getElementById("reload-esc-button");
  const escFeedback = document.getElementById("esc-feedback");

  let saveTimeout = null;
  let domeHardwareEnabled = true;
  let webControlEnabled = false;
  const showFeedback = (el, text, level = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = level ? `feedback ${level}` : "feedback";
  };

  const debounce = (fn, ms) => (...args) => {
    window.clearTimeout(saveTimeout);
    saveTimeout = window.setTimeout(() => fn(...args), ms);
  };

  const setDomeHardwareEnabled = (enabled) => {
    domeHardwareEnabled = enabled;
    domeDisabledCard?.classList.toggle("hidden", enabled);

    const controls = [
      domeSlider,
      domeNeutral,
      domeMinPulse,
      domeMaxPulse,
      domeSpeedLimit,
      reloadEscButton,
    ];
    controls.forEach((control) => {
      if (!control) return;
      control.disabled = !enabled;
      control.setAttribute("aria-disabled", enabled ? "false" : "true");
    });

    if (!enabled && domeSlider) {
      domeSlider.value = "0";
      if (domeSpeedDisplay) domeSpeedDisplay.textContent = "0%";
    }
  };

  const postDomeCommand = async (speed) => {
    if (!window.PAApi) return;
    if (!domeHardwareEnabled) {
      showFeedback(domeFeedback, "Dome controls unavailable: enable DOME in Setup.", "warning");
      return;
    }
    if (!webControlEnabled) {
      showFeedback(domeFeedback, "Dome unavailable: web control is disabled.", "warning");
      return;
    }
    try {
      await window.PAApi.postForm("/api/dome", { speed: String(speed) }, { timeoutMs: 2500 });
      showFeedback(domeFeedback, "Dome command sent.", "success");
    } catch (error) {
      showFeedback(domeFeedback, `Dome command failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  if (domeSlider) {
    let domeDebounceTimer = null;

    domeSlider.addEventListener("input", (event) => {
      const speed = Number.parseInt(event.target.value, 10);
      if (domeSpeedDisplay) domeSpeedDisplay.textContent = `${speed}%`;

      if (domeDebounceTimer) window.clearTimeout(domeDebounceTimer);
      domeDebounceTimer = window.setTimeout(() => {
        postDomeCommand(speed / 100);
      }, 100);
    });

    domeSlider.addEventListener("change", (event) => {
      if (Number.parseInt(event.target.value, 10) !== 0) {
        event.target.value = 0;
        if (domeSpeedDisplay) domeSpeedDisplay.textContent = "0%";
        postDomeCommand(0);
      }
    });
  }

  const renderEsc = (payload) => {
    const dome = payload?.dome || {};
    const components = payload?.components || {};

    if (domeNeutral) domeNeutral.value = dome.neutralUs;
    if (domeMinPulse) domeMinPulse.value = dome.minPulseUs;
    if (domeMaxPulse) domeMaxPulse.value = dome.maxPulseUs;
    if (domeSpeedLimit) domeSpeedLimit.value = dome.speedLimitPct;

    setDomeHardwareEnabled(Boolean(components.dome?.enabled));

    showFeedback(escFeedback, `Motor settings loaded at ${new Date().toLocaleTimeString()}`, "success");
  };

  const loadEscConfig = async () => {
    if (!window.PAApi) return;
    showFeedback(escFeedback, "Loading motor settings...");

    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 3000 });
      renderEsc(result.data);
    } catch (error) {
      showFeedback(escFeedback, `Failed to load motor settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const saveEscConfig = async () => {
    if (!window.PAApi) return;
    if (!domeHardwareEnabled) {
      showFeedback(escFeedback, "Dome settings unavailable: enable DOME in Setup.", "warning");
      return;
    }
    showFeedback(escFeedback, "Saving...");

    try {
      await window.PAApi.postForm("/api/config", {
        domeNeutralUs: domeNeutral?.value || "1500",
        domeMinPulseUs: domeMinPulse?.value || "1000",
        domeMaxPulseUs: domeMaxPulse?.value || "2000",
        domeSpeedLimitPct: domeSpeedLimit?.value || "100",
      }, { timeoutMs: 3000 });

      showFeedback(escFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(escFeedback, `Failed to save motor settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const debouncedSave = debounce(saveEscConfig, 500);

  domeNeutral?.addEventListener("input", debouncedSave);
  domeMinPulse?.addEventListener("input", debouncedSave);
  domeMaxPulse?.addEventListener("input", debouncedSave);
  domeSpeedLimit?.addEventListener("input", debouncedSave);
  reloadEscButton?.addEventListener("click", loadEscConfig);

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType !== "status") return;
      webControlEnabled = !!payload.webControlEnabled;
      setDomeHardwareEnabled(Boolean(payload.dome));
    });
  }

  loadEscConfig();
})();
