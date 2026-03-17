// =============================================================================
// dome.js
//
// Dome page controller — rotation slider (hold-to-run, auto-returns to stop)
// and dome motor ESC settings form with auto-save.
// =============================================================================
(() => {
  const domeSlider = document.getElementById("dome-slider");
  const domeSpeedDisplay = document.getElementById("dome-speed-display");
  const domeFeedback = document.getElementById("dome-feedback");

  const escForm = document.getElementById("esc-form");
  const domeNeutral = document.getElementById("dome-neutral");
  const domeMinPulse = document.getElementById("dome-min-pulse");
  const domeMaxPulse = document.getElementById("dome-max-pulse");
  const domeSpeedLimit = document.getElementById("dome-speed-limit");
  const reloadEscButton = document.getElementById("reload-esc-button");
  const escFeedback = document.getElementById("esc-feedback");

  // Debounce utility for auto-save
  let saveTimeout = null;
  const debounce = (fn, ms) => {
    return (...args) => {
      clearTimeout(saveTimeout);
      saveTimeout = setTimeout(() => fn(...args), ms);
    };
  };

  // -------------------------------------------------------------------------
  // Dome rotation slider
  // -------------------------------------------------------------------------
  const postDomeCommand = async (speed) => {
    try {
      const body = new URLSearchParams({ speed: String(speed) });
      const response = await fetch("/api/dome", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
    } catch (_error) {
      if (domeFeedback) {
        domeFeedback.textContent = "❌ Dome command failed";
        domeFeedback.className = "feedback error";
      }
    }
  };

  if (domeSlider) {
    let domeDebounceTimer = null;

    domeSlider.addEventListener("input", (event) => {
      const speed = parseInt(event.target.value, 10);
      if (domeSpeedDisplay) domeSpeedDisplay.textContent = `${speed}%`;
      if (domeDebounceTimer) window.clearTimeout(domeDebounceTimer);
      // Debounce rapid drag events; send at most once per 100 ms
      domeDebounceTimer = window.setTimeout(() => postDomeCommand(speed / 100), 100);
    });

    // Return-to-stop on release
    domeSlider.addEventListener("change", (event) => {
      if (parseInt(event.target.value, 10) !== 0) {
        event.target.value = 0;
        if (domeSpeedDisplay) domeSpeedDisplay.textContent = "0%";
        postDomeCommand(0);
      }
    });
  }

  // -------------------------------------------------------------------------
  // Dome motor ESC settings with auto-save
  // -------------------------------------------------------------------------
  const domeDisabledCard = document.getElementById("dome-disabled-card");

  const renderEsc = (payload) => {
    if (domeNeutral)    domeNeutral.value    = payload.domeNeutralUs;
    if (domeMinPulse)   domeMinPulse.value   = payload.domeMinPulseUs;
    if (domeMaxPulse)   domeMaxPulse.value   = payload.domeMaxPulseUs;
    if (domeSpeedLimit) domeSpeedLimit.value = payload.domeSpeedLimitPct;
    if (escFeedback) {
      escFeedback.textContent = `Motor settings loaded at ${new Date().toLocaleTimeString()}`;
      escFeedback.className = "feedback success";
    }
    if (domeDisabledCard) {
      domeDisabledCard.classList.toggle("hidden", Boolean(payload.enableDome));
    }
  };

  const loadEscConfig = async () => {
    if (escFeedback) {
      escFeedback.textContent = "Loading motor settings...";
      escFeedback.className = "feedback";
    }
    try {
      const response = await fetch("/api/config", { cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      renderEsc(await response.json());
    } catch (_error) {
      if (escFeedback) {
        escFeedback.textContent = "Failed to load motor settings";
        escFeedback.className = "feedback error";
      }
    }
  };

  // Auto-save function
  const saveEscConfig = async () => {
    if (escFeedback) {
      escFeedback.textContent = "Saving...";
      escFeedback.className = "feedback";
    }
    try {
      const body = new URLSearchParams({
        domeNeutralUs:    domeNeutral.value,
        domeMinPulseUs:   domeMinPulse.value,
        domeMaxPulseUs:   domeMaxPulse.value,
        domeSpeedLimitPct: domeSpeedLimit.value,
      });
      const response = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      if (!response.ok) {
        const errorBody = await response.json().catch(() => null);
        throw new Error(errorBody?.error || `HTTP ${response.status}`);
      }
      if (escFeedback) {
        escFeedback.textContent = `✓ Saved at ${new Date().toLocaleTimeString()}`;
        escFeedback.className = "feedback success";
      }
    } catch (error) {
      if (escFeedback) {
        escFeedback.textContent = error instanceof Error
          ? `❌ ${error.message}` : "❌ Failed to save motor settings";
        escFeedback.className = "feedback error";
      }
    }
  };

  const debouncedSave = debounce(saveEscConfig, 500);

  // Attach auto-save listeners to all inputs
  if (domeNeutral) {
    domeNeutral.addEventListener("input", debouncedSave);
  }
  if (domeMinPulse) {
    domeMinPulse.addEventListener("input", debouncedSave);
  }
  if (domeMaxPulse) {
    domeMaxPulse.addEventListener("input", debouncedSave);
  }
  if (domeSpeedLimit) {
    domeSpeedLimit.addEventListener("input", debouncedSave);
  }

  if (reloadEscButton) {
    reloadEscButton.addEventListener("click", loadEscConfig);
  }

  loadEscConfig();
})();
