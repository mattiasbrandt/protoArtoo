// =============================================================================
// servo.js
//
// Servos page controller — arm servo controls, AUX output controls, and
// servo calibration (ARM1/ARM2 and AUX servo channels).
// Polls /api/status every second to show which outputs are enabled.
// Sends open/close/stop/position commands via POST /api/servo.
// Loads and saves calibration via GET/POST /api/config (auto-save on change).
// Component types (mg996r/mg90s/rgb/none) are read from /api/config to render
// type-appropriate controls per AUX channel.
// =============================================================================
(() => {
  const armControlsCard      = document.getElementById("arm-controls-card");
  const armControlsContainer = document.getElementById("arm-controls-container");
  const noArmsCard           = document.getElementById("no-arms-card");
  const armFeedback          = document.getElementById("arm-feedback");

  const auxControlsCard      = document.getElementById("aux-controls-card");
  const auxControlsContainer = document.getElementById("aux-controls-container");
  const auxFeedback          = document.getElementById("aux-feedback");

  const servoCalibCard       = document.getElementById("servo-calib-card");
  const arm1CalibSection     = document.getElementById("arm1-calib-section");
  const arm2CalibSection     = document.getElementById("arm2-calib-section");
  const aux1CalibSection     = document.getElementById("aux1-calib-section");
  const aux2CalibSection     = document.getElementById("aux2-calib-section");
  const aux3CalibSection     = document.getElementById("aux3-calib-section");

  const calibForm            = document.getElementById("calib-form");
  const arm1OpenUs           = document.getElementById("arm1-open-us");
  const arm1CloseUs          = document.getElementById("arm1-close-us");
  const arm2OpenUs           = document.getElementById("arm2-open-us");
  const arm2CloseUs          = document.getElementById("arm2-close-us");
  const aux1OpenUs           = document.getElementById("aux1-open-us");
  const aux1CloseUs          = document.getElementById("aux1-close-us");
  const aux2OpenUs           = document.getElementById("aux2-open-us");
  const aux2CloseUs          = document.getElementById("aux2-close-us");
  const aux3OpenUs           = document.getElementById("aux3-open-us");
  const aux3CloseUs          = document.getElementById("aux3-close-us");
  const arm1TestUs           = document.getElementById("arm1-test-us");
  const arm2TestUs           = document.getElementById("arm2-test-us");
  const aux1TestUs           = document.getElementById("aux1-test-us");
  const aux2TestUs           = document.getElementById("aux2-test-us");
  const aux3TestUs           = document.getElementById("aux3-test-us");
  const calibFeedback        = document.getElementById("calib-feedback");
  const reloadCalibBtn       = document.getElementById("reload-calib-btn");

  // Component types loaded from /api/config — determines AUX rendering
  let auxTypes = { aux1: "none", aux2: "none", aux3: "none" };

  // Debounce utility for auto-save
  let saveTimeout = null;
  const debounce = (fn, ms) => {
    return (...args) => {
      clearTimeout(saveTimeout);
      saveTimeout = setTimeout(() => fn(...args), ms);
    };
  };

  // -------------------------------------------------------------------------
  // Servo command helpers
  // -------------------------------------------------------------------------
  const postServoAction = async (arm, action, feedbackEl) => {
    const fb = feedbackEl || armFeedback;
    try {
      const body = new URLSearchParams({ arm, action });
      const res = await fetch("/api/servo", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      const label = `${arm} ${action}`;
      if (!res.ok) {
        const err = await res.json().catch(() => null);
        setFeedback(fb, `${label} failed: ${err?.error || res.status}`, "error");
      } else {
        setFeedback(fb, `${label} sent at ${new Date().toLocaleTimeString()}`, "success");
      }
    } catch (_e) {
      setFeedback(fb, "Network error sending servo command", "error");
    }
  };

  const postServoPosition = async (arm, pulseUs, feedbackEl) => {
    const fb = feedbackEl || armFeedback;
    try {
      const body = new URLSearchParams({ arm, action: "position", positionUs: String(pulseUs) });
      const res = await fetch("/api/servo", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      const label = `${arm} → ${pulseUs} µs`;
      if (!res.ok) {
        const err = await res.json().catch(() => null);
        setFeedback(fb, `Test ${label} failed: ${err?.error || res.status}`, "error");
      } else {
        setFeedback(fb, `▶ Test ${label} at ${new Date().toLocaleTimeString()}`, "success");
      }
    } catch (_e) {
      setFeedback(fb, "Network error sending test position", "error");
    }
  };

  const setFeedback = (el, text, cls = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = cls ? `feedback ${cls}` : "feedback";
  };

  // -------------------------------------------------------------------------
  // renderArmControls() — ARM1/ARM2 open/close/stop buttons
  // -------------------------------------------------------------------------
  const ARM_DEFS = [
    { id: "arm1", name: "Left Arm",  label: "ARM1" },
    { id: "arm2", name: "Right Arm", label: "ARM2" },
  ];

  let renderedArmIds = null;

  const renderArmControls = (payload) => {
    const enabled = ARM_DEFS.filter((a) => a.id in payload);
    const ids = enabled.map((a) => a.id).join(",");

    if (noArmsCard)      noArmsCard.classList.toggle("hidden", enabled.length > 0);
    if (armControlsCard) armControlsCard.classList.toggle("hidden", enabled.length === 0);

    if (enabled.length === 0) return;

    if (armControlsContainer && ids !== renderedArmIds) {
      renderedArmIds = ids;
      armControlsContainer.innerHTML = enabled.map((arm) => {
        const detail = payload[arm.id]?.detail || "";
        return `
          <div class="arm-control-row" id="row-${arm.id}">
            <span class="arm-name">${arm.name}</span>
            <span class="arm-position text-dim" id="pos-${arm.id}">${detail}</span>
            <button class="btn" data-arm="${arm.id}" data-action="open"  type="button">📂 Open</button>
            <button class="btn" data-arm="${arm.id}" data-action="close" type="button">📁 Close</button>
            <button class="btn" data-arm="${arm.id}" data-action="stop"  type="button">⏹️ Stop</button>
          </div>`;
      }).join("");

      armControlsContainer.querySelectorAll("[data-arm]").forEach((btn) => {
        btn.addEventListener("click", () =>
          postServoAction(btn.dataset.arm, btn.dataset.action, armFeedback));
      });
    } else if (armControlsContainer) {
      enabled.forEach((arm) => {
        const el = document.getElementById(`pos-${arm.id}`);
        if (el) el.textContent = payload[arm.id]?.detail || "";
      });
    }
  };

  // -------------------------------------------------------------------------
  // renderAuxControls() — AUX1/2/3 type-appropriate controls
  // -------------------------------------------------------------------------
  const AUX_DEFS = [
    { id: "aux1", name: "AUX 1", label: "AUX1" },
    { id: "aux2", name: "AUX 2", label: "AUX2" },
    { id: "aux3", name: "AUX 3", label: "AUX3" },
  ];

  let renderedAuxIds = null;

  const renderAuxControls = (payload) => {
    const enabled = AUX_DEFS.filter((a) => a.id in payload);
    if (auxControlsCard) auxControlsCard.classList.toggle("hidden", enabled.length === 0);
    if (enabled.length === 0) return;

    const ids = enabled.map((a) => `${a.id}:${auxTypes[a.id]}`).join(",");
    if (auxControlsContainer && ids !== renderedAuxIds) {
      renderedAuxIds = ids;
      auxControlsContainer.innerHTML = enabled.map((aux) => {
        const type = auxTypes[aux.id] || "none";
        const detail = payload[aux.id]?.detail || "";
        if (type === "rgb") {
          return `
            <div class="arm-control-row" id="row-${aux.id}">
              <span class="arm-name">${aux.name}</span>
              <span class="arm-position text-dim">RGB LED strip — control coming soon</span>
            </div>`;
        }
        if (type === "mg996r" || type === "mg90s") {
          return `
            <div class="arm-control-row" id="row-${aux.id}">
              <span class="arm-name">${aux.name}</span>
              <span class="arm-position text-dim" id="pos-${aux.id}">${detail}</span>
              <button class="btn" data-arm="${aux.id}" data-action="open"  type="button">📂 Open</button>
              <button class="btn" data-arm="${aux.id}" data-action="close" type="button">📁 Close</button>
              <button class="btn" data-arm="${aux.id}" data-action="stop"  type="button">⏹️ Stop</button>
            </div>`;
        }
        return "";  // none — nothing connected, skip
      }).join("");

      auxControlsContainer.querySelectorAll("[data-arm]").forEach((btn) => {
        btn.addEventListener("click", () =>
          postServoAction(btn.dataset.arm, btn.dataset.action, auxFeedback));
      });
    } else if (auxControlsContainer) {
      enabled.forEach((aux) => {
        const el = document.getElementById(`pos-${aux.id}`);
        if (el) el.textContent = payload[aux.id]?.detail || "";
      });
    }
  };

  // -------------------------------------------------------------------------
  // renderCalibSections() — show/hide calibration sections per enabled state + type
  // -------------------------------------------------------------------------
  const renderCalibSections = (payload) => {
    const arm1Present = "arm1" in payload;
    const arm2Present = "arm2" in payload;
    const aux1Present = "aux1" in payload;
    const aux2Present = "aux2" in payload;
    const aux3Present = "aux3" in payload;

    const isServoType = (t) => t === "mg996r" || t === "mg90s";

    const aux1Servo = aux1Present && isServoType(auxTypes.aux1);
    const aux2Servo = aux2Present && isServoType(auxTypes.aux2);
    const aux3Servo = aux3Present && isServoType(auxTypes.aux3);

    const anyCalib = arm1Present || arm2Present || aux1Servo || aux2Servo || aux3Servo;

    if (servoCalibCard)   servoCalibCard.classList.toggle("hidden", !anyCalib);
    if (arm1CalibSection) arm1CalibSection.classList.toggle("hidden", !arm1Present);
    if (arm2CalibSection) arm2CalibSection.classList.toggle("hidden", !arm2Present);
    if (aux1CalibSection) aux1CalibSection.classList.toggle("hidden", !aux1Servo);
    if (aux2CalibSection) aux2CalibSection.classList.toggle("hidden", !aux2Servo);
    if (aux3CalibSection) aux3CalibSection.classList.toggle("hidden", !aux3Servo);
  };

  // -------------------------------------------------------------------------
  // Status poll
  // -------------------------------------------------------------------------
  let lastPayload = null;

  const poll = async () => {
    try {
      const res = await fetch("/api/status", { cache: "no-store" });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      lastPayload = await res.json();
      renderArmControls(lastPayload);
      renderAuxControls(lastPayload);
      renderCalibSections(lastPayload);
    } catch (_e) {}
  };

  // -------------------------------------------------------------------------
  // Calibration load / auto-save
  // -------------------------------------------------------------------------
  const setCalibFeedback = (text, cls = "") => {
    if (!calibFeedback) return;
    calibFeedback.textContent = text;
    calibFeedback.className = cls ? `feedback ${cls}` : "feedback";
  };

  const loadCalib = async () => {
    setCalibFeedback("Loading calibration...");
    try {
      const res = await fetch("/api/config", { cache: "no-store" });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const cfg = await res.json();

      // Arm calibration
      if (arm1OpenUs)  arm1OpenUs.value  = cfg.arm1OpenUs  ?? 2000;
      if (arm1CloseUs) arm1CloseUs.value = cfg.arm1CloseUs ?? 1000;
      if (arm2OpenUs)  arm2OpenUs.value  = cfg.arm2OpenUs  ?? 2000;
      if (arm2CloseUs) arm2CloseUs.value = cfg.arm2CloseUs ?? 1000;

      // AUX calibration
      if (aux1OpenUs)  aux1OpenUs.value  = cfg.aux1OpenUs  ?? 2000;
      if (aux1CloseUs) aux1CloseUs.value = cfg.aux1CloseUs ?? 1000;
      if (aux2OpenUs)  aux2OpenUs.value  = cfg.aux2OpenUs  ?? 2000;
      if (aux2CloseUs) aux2CloseUs.value = cfg.aux2CloseUs ?? 1000;
      if (aux3OpenUs)  aux3OpenUs.value  = cfg.aux3OpenUs  ?? 2000;
      if (aux3CloseUs) aux3CloseUs.value = cfg.aux3CloseUs ?? 1000;

      // Pre-populate test inputs at neutral
      if (arm1TestUs) arm1TestUs.value = 1500;
      if (arm2TestUs) arm2TestUs.value = 1500;
      if (aux1TestUs) aux1TestUs.value = 1500;
      if (aux2TestUs) aux2TestUs.value = 1500;
      if (aux3TestUs) aux3TestUs.value = 1500;

      const components = cfg?.components || {};
      auxTypes.aux1 = String(components.aux1?.type || "none");
      auxTypes.aux2 = String(components.aux2?.type || "none");
      auxTypes.aux3 = String(components.aux3?.type || "none");
      // Re-render AUX controls now that types are known
      if (lastPayload) {
        renderAuxControls(lastPayload);
        renderCalibSections(lastPayload);
      }

      setCalibFeedback(`Calibration loaded at ${new Date().toLocaleTimeString()}`, "success");
    } catch (_e) {
      setCalibFeedback("Failed to load calibration", "error");
    }
  };

  // Auto-save calibration
  const saveCalib = async () => {
    setCalibFeedback("Saving...");
    try {
      const body = new URLSearchParams();
      if (arm1OpenUs  && !arm1CalibSection?.classList.contains("hidden")) {
        body.set("arm1OpenUs",  arm1OpenUs.value);
        body.set("arm1CloseUs", arm1CloseUs.value);
      }
      if (arm2OpenUs  && !arm2CalibSection?.classList.contains("hidden")) {
        body.set("arm2OpenUs",  arm2OpenUs.value);
        body.set("arm2CloseUs", arm2CloseUs.value);
      }
      if (aux1OpenUs  && !aux1CalibSection?.classList.contains("hidden")) {
        body.set("aux1OpenUs",  aux1OpenUs.value);
        body.set("aux1CloseUs", aux1CloseUs.value);
      }
      if (aux2OpenUs  && !aux2CalibSection?.classList.contains("hidden")) {
        body.set("aux2OpenUs",  aux2OpenUs.value);
        body.set("aux2CloseUs", aux2CloseUs.value);
      }
      if (aux3OpenUs  && !aux3CalibSection?.classList.contains("hidden")) {
        body.set("aux3OpenUs",  aux3OpenUs.value);
        body.set("aux3CloseUs", aux3CloseUs.value);
      }
      const res = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      if (!res.ok) {
        const err = await res.json().catch(() => null);
        throw new Error(err?.error || `HTTP ${res.status}`);
      }
      setCalibFeedback(`Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (e) {
      setCalibFeedback(`Save failed: ${e.message}`, "error");
    }
  };

  const debouncedSave = debounce(saveCalib, 500);

  // Attach auto-save listeners to all calibration inputs
  const calibInputs = [arm1OpenUs, arm1CloseUs, arm2OpenUs, arm2CloseUs,
                       aux1OpenUs, aux1CloseUs, aux2OpenUs, aux2CloseUs,
                       aux3OpenUs, aux3CloseUs];
  calibInputs.forEach((input) => {
    if (input) {
      input.addEventListener("input", debouncedSave);
    }
  });

  if (reloadCalibBtn) reloadCalibBtn.addEventListener("click", loadCalib);

  // -------------------------------------------------------------------------
  // Test buttons — send SERVO_CMD_POSITION immediately (does not save)
  // -------------------------------------------------------------------------
  const wireTestBtn = (btnId, armId, getUs, fb) => {
    const btn = document.getElementById(btnId);
    if (btn) btn.addEventListener("click", () =>
      postServoPosition(armId, Number(getUs()), fb || armFeedback));
  };

  wireTestBtn("arm1-test-btn",       "arm1", () => arm1TestUs?.value  || 1500);
  wireTestBtn("arm1-open-test-btn",  "arm1", () => arm1OpenUs?.value  || 2000);
  wireTestBtn("arm1-close-test-btn", "arm1", () => arm1CloseUs?.value || 1000);
  wireTestBtn("arm2-test-btn",       "arm2", () => arm2TestUs?.value  || 1500);
  wireTestBtn("arm2-open-test-btn",  "arm2", () => arm2OpenUs?.value  || 2000);
  wireTestBtn("arm2-close-test-btn", "arm2", () => arm2CloseUs?.value || 1000);
  wireTestBtn("aux1-test-btn",       "aux1", () => aux1TestUs?.value  || 1500, auxFeedback);
  wireTestBtn("aux1-open-test-btn",  "aux1", () => aux1OpenUs?.value  || 2000, auxFeedback);
  wireTestBtn("aux1-close-test-btn", "aux1", () => aux1CloseUs?.value || 1000, auxFeedback);
  wireTestBtn("aux2-test-btn",       "aux2", () => aux2TestUs?.value  || 1500, auxFeedback);
  wireTestBtn("aux2-open-test-btn",  "aux2", () => aux2OpenUs?.value  || 2000, auxFeedback);
  wireTestBtn("aux2-close-test-btn", "aux2", () => aux2CloseUs?.value || 1000, auxFeedback);
  wireTestBtn("aux3-test-btn",       "aux3", () => aux3TestUs?.value  || 1500, auxFeedback);
  wireTestBtn("aux3-open-test-btn",  "aux3", () => aux3OpenUs?.value  || 2000, auxFeedback);
  wireTestBtn("aux3-close-test-btn", "aux3", () => aux3CloseUs?.value || 1000, auxFeedback);

  // -------------------------------------------------------------------------
  // Boot
  // -------------------------------------------------------------------------
  loadCalib();
  poll();
  const pollTimer = window.setInterval(poll, 1000);

  window.addEventListener("beforeunload", () => window.clearInterval(pollTimer));
})();
