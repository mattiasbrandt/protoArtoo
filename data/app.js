// =============================================================================
// app.js
//
// Home dashboard controller.
// - Uses shared status stream (SSE-first, polling fallback)
// - Provides truthful mode/mood command UX with rollback on failure
// - Renders system health, component status, live logs, and quick audio controls
// =============================================================================
(() => {
  const healthSummary = document.getElementById("health-summary");
  const logConsole = document.getElementById("log-console");
  const logPaused = document.getElementById("log-paused");
  const componentStatusCard = document.getElementById("component-status-card");
  const componentStatusGrid = document.getElementById("component-status-grid");

  const opmodeDrive = document.getElementById("opmode-drive");
  const opmodeStationary = document.getElementById("opmode-stationary");
  const opmodeFeedback = document.getElementById("opmode-feedback");

  const moodDomeNote = document.getElementById("mood-dome-note");
  const moodFeedback = document.getElementById("mood-feedback");

  const soundFeedback = document.getElementById("sound-feedback");
  const sleepToggle = document.getElementById("sleep-toggle");
  const sleepOverlay = document.getElementById("sleep-overlay");
  const sleepOverlayWake = document.getElementById("sleep-overlay-wake");
  const sleepFeedback = document.getElementById("sleep-feedback");
  const staleBanner = document.getElementById('status-stale-banner');
  const setStale = (stale) => { if (staleBanner) staleBanner.style.display = stale ? '' : 'none'; };
  const snapshotWebControl = document.getElementById("snapshot-web-control");
  const snapshotMode = document.getElementById("snapshot-mode");
  const snapshotEstop = document.getElementById("snapshot-estop");
  const snapshotMood = document.getElementById("snapshot-mood");

  let lastStatus = null;
  let modePending = false;
  let moodPending = false;
  let pollFailCount = 0;
  let sleepPending = false;
  let isSleeping = false;

  const INDICATOR_TEXT = {
    'h-sbus':      'ht-sbus',
    'h-wifi':      'ht-wifi',
    'h-fs':        'ht-fs',
    'h-heap':      'ht-heap',
    'h-dome-link': 'ht-dome-link',
    'h-sound':     'ht-sound',
  };

  const COMPONENT_LABELS = [
    ["arm1", "🦾", "Left Arm"],
    ["arm2", "🦾", "Right Arm"],
    ["aux1", "🦾", "Aux Servo 1"],
    ["aux2", "🦾", "Aux Servo 2"],
    ["aux3", "🦾", "Aux Servo 3"],
    ["dome", "🔄", "Dome Motor"],
    ["rcCh1", "🕹️", "RC Channel 1"],
    ["rcCh2", "🕹️", "RC Channel 2"],
    ["rcCh3", "🕹️", "RC Channel 3"],
    ["rcCh4", "🕹️", "RC Channel 4"],
    ["rcCh5", "🕹️", "RC Channel 5"],
    ["rcCh6", "🕹️", "RC Channel 6"],
    ["s1Hoverboard", "🔌", "Hoverboard Drive"],
    ["s2Sound", "🔊", "Sound Module"],
    ["s3DomeCtrl", "🔌", "Dome Controller"],
  ];

  const MOOD_LABELS = {
    0: "Idle 😐",
    10: "Quiet 😴",
    11: "Full-Awake 😄",
    13: "Mid-Awake 🙂",
    14: "Awake+ ✨",
  };

  const PILL_CLASS_MAP = {
    ok: "pill-ok",
    warn: "pill-warn",
    error: "pill-error",
    info: "pill-info",
  };

  const showFeedback = (el, message, level = "") => {
    if (!el) return;
    if (!el.dataset.baseClass) {
      el.dataset.baseClass = el.className || "feedback";
    }
    el.textContent = message;
    el.className = level ? `${el.dataset.baseClass} ${level}` : el.dataset.baseClass;
  };

  const setSleepPending = (pending) => {
    sleepPending = pending;
    [sleepToggle, sleepOverlayWake].forEach((el) => {
      if (!el) return;
      el.disabled = pending;
      el.classList.toggle("is-pending", pending);
      el.setAttribute("aria-disabled", pending ? "true" : "false");
    });
  };

  const setSleepUi = (sleeping) => {
    isSleeping = !!sleeping;
    if (sleepToggle) {
      sleepToggle.textContent = isSleeping ? "⏻ Wake" : "⏻ Sleep";
      sleepToggle.title = isSleeping ? "Wake droid subsystems" : "Park droid subsystems";
      sleepToggle.classList.toggle("danger", isSleeping);
      sleepToggle.classList.toggle("accent", !isSleeping);
      sleepToggle.setAttribute("aria-pressed", isSleeping.toString());
    }
    if (sleepOverlay) {
      sleepOverlay.classList.toggle("active", isSleeping);
      sleepOverlay.setAttribute("aria-hidden", (!isSleeping).toString());
    }
    document.body.classList.toggle("sleep-mode-active", isSleeping);
  };


  const setIndicator = (id, state) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.className = `indicator ${state}`;
    const textEl = INDICATOR_TEXT[id] ? document.getElementById(INDICATOR_TEXT[id]) : null;
    if (textEl) {
      const labels = { ok: 'OK', warn: 'WARN', fail: 'FAIL', off: 'OFF' };
      textEl.textContent = labels[state] || state;
    }
  };

  const renderHealth = (payload) => {
    const anyRcEnabled = !!(
      payload.rcCh1 || payload.rcCh2 || payload.rcCh3 ||
      payload.rcCh4 || payload.rcCh5 || payload.rcCh6
    );
    if (!anyRcEnabled) {
      setIndicator("h-sbus", "off");
    } else {
      setIndicator("h-sbus", payload.sbusSignalLost || payload.sbusHwFailsafe ? "fail" : "ok");
    }

    setIndicator("h-wifi", (payload.wifiConnected || payload.wifiClientConnected) ? "ok" : "warn");
    setIndicator("h-fs", payload.littleFsReady ? "ok" : "fail");
    setIndicator("h-heap", payload.heapFree > 120000 ? "ok" : payload.heapFree > 80000 ? "warn" : "fail");

    if (payload.dome_link) {
      const s = payload.dome_link.state;
      setIndicator(
        "h-dome-link",
        s === "connected" ? "ok" :
        s === "lost" ? "fail" :
        s === "not_seen" ? "warn" : "off"
      );
    } else {
      setIndicator("h-dome-link", "off");
    }

    setIndicator("h-sound", payload.s2Sound && typeof payload.s2Sound === "object" ? "ok" : "off");

    if (!healthSummary) return;

    const heapFreeKb = Math.round(payload.heapFree / 1024);

    let heapLabel = "Healthy ✅";
    let heapState = "ok";
    if (heapFreeKb < 80) {
      heapLabel = "Critical ❌";
      heapState = "error";
    } else if (heapFreeKb < 120) {
      heapLabel = "Low ⚠️";
      heapState = "warn";
    }

    let wifiLabel = "Unknown ❌";
    let wifiState = "error";
    if ((payload.wifiConnected || payload.wifiClientConnected) && payload.wifiRssi !== 0) {
      if (payload.wifiRssi >= -67) {
        wifiLabel = `Excellent ✅ (${payload.wifiRssi} dBm)`;
        wifiState = "ok";
      } else if (payload.wifiRssi >= -75) {
        wifiLabel = `Good ✅ (${payload.wifiRssi} dBm)`;
        wifiState = "ok";
      } else if (payload.wifiRssi >= -85) {
        wifiLabel = `Fair ⚠️ (${payload.wifiRssi} dBm)`;
        wifiState = "warn";
      } else {
        wifiLabel = `Poor ❌ (${payload.wifiRssi} dBm)`;
        wifiState = "error";
      }
    }

    healthSummary.innerHTML =
      `<div class="health-summary-row">Memory: <span class="health-summary-value is-${heapState}">${heapFreeKb} KB ${heapLabel}</span></div>` +
      `<div class="health-summary-row">WiFi: <span class="health-summary-value is-${wifiState}">${wifiLabel}</span></div>` +
      `<div class="health-summary-note">Detailed memory headroom telemetry is available on Setup → Diagnostics.</div>`;
  };

  const renderComponentStatus = (payload) => {
    if (!componentStatusCard || !componentStatusGrid) return;

    const active = COMPONENT_LABELS.filter(([key]) => key in payload);
    if (active.length === 0) {
      componentStatusCard.classList.add("hidden");
      componentStatusGrid.innerHTML = "";
      return;
    }

    componentStatusCard.classList.remove("hidden");
    componentStatusGrid.innerHTML = active.map(([key, icon, label]) => {
      const entry = payload[key];
      let state = entry ? "enabled" : "disabled";
      let detail = entry ? "✅ Enabled" : "⏸️ Disabled";
      if (entry && typeof entry === "object") {
        state = entry.state || "enabled";
        detail = entry.detail || "✅ Enabled";
      }
      return `
        <div class="status-item">
          <dt>${icon} ${label}</dt>
          <dd>${state.replace(/_/g, " ")}</dd>
          <div class="desc mt-6">${detail}</div>
        </div>`;
    }).join("");
  };

  const renderOpMode = (payload) => {
    if (!opmodeDrive || !opmodeStationary) return;
    const isStationary = !!payload.stationary;
    opmodeDrive.classList.toggle("active", !isStationary);
    opmodeStationary.classList.toggle("active", isStationary);
    opmodeDrive.setAttribute("aria-pressed", (!isStationary).toString());
    opmodeStationary.setAttribute("aria-pressed", isStationary.toString());
  };

  const renderActiveMood = (payload) => {
    const activeMood = payload.activeMood || 0;
    document.querySelectorAll(".mood-btn").forEach((btn) => {
      const match = btn.dataset.cmd?.match(/:SE(\d+)/);
      const btnMood = match ? Number.parseInt(match[1], 10) : 0;
      const isActive = btnMood !== 0 && btnMood === activeMood;
      btn.classList.toggle("active", isActive);
      btn.setAttribute("aria-pressed", isActive.toString());
    });
  };

  const renderMoodDomeNote = (payload) => {
    if (!moodDomeNote) return;
    const domeConnected = payload?.dome_link?.state === "connected";
    moodDomeNote.classList.toggle("visible", !domeConnected);
  };

  const setStatusPill = (el, text, state = "info", compact = true) => {
    if (!el) return;
    const sizeClass = compact ? "status-pill status-pill-compact" : "status-pill";
    el.textContent = text;
    el.className = `${sizeClass} ${PILL_CLASS_MAP[state] || PILL_CLASS_MAP.info}`;
  };

  const renderMissionSnapshot = (payload) => {
    const isStationary = !!payload.stationary;
    const moodText = MOOD_LABELS[payload.activeMood] || `Mood ${payload.activeMood || 0}`;

    setStatusPill(
      snapshotWebControl,
      payload.webControlEnabled ? "🕹️ Web control: Enabled" : "🕹️ Web control: Disabled",
      payload.webControlEnabled ? "ok" : "warn",
    );
    setStatusPill(
      snapshotMode,
      isStationary ? "🧭 Mode: Stationary" : "🧭 Mode: Driving",
      isStationary ? "warn" : "ok",
    );
    setStatusPill(
      snapshotEstop,
      payload.estop ? "🛑 E-Stop: Latched" : "🛑 E-Stop: Clear",
      payload.estop ? "error" : "ok",
    );
    setStatusPill(snapshotMood, `🎭 Mood: ${moodText}`, "info");
  };

  const applyStatus = (payload) => {
    lastStatus = payload;
    pollFailCount = 0;
    setStale(false);
    renderHealth(payload);
    renderComponentStatus(payload);
    renderMissionSnapshot(payload);
    renderOpMode(payload);
    renderMoodDomeNote(payload);
    renderActiveMood(payload);
    setSleepUi(!!payload.sleepMode);
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { cache: "no-store", timeoutMs: 3000 });
    applyStatus(result.data);
  };

  const toggleSleepWake = async (forceWake = false) => {
    if (!window.PAApi || sleepPending) return;
    const targetSleep = forceWake ? false : !isSleeping;
    setSleepPending(true);
    showFeedback(sleepFeedback, targetSleep ? "Entering sleep mode..." : "Waking droid...");

    try {
      await window.PAApi.postForm(targetSleep ? "/api/sleep" : "/api/wake", {}, { timeoutMs: 3000 });
      await refreshStatusOnce();
      showFeedback(sleepFeedback, targetSleep ? "Sleep mode enabled" : "Droid awake", "success");
    } catch (error) {
      showFeedback(
        sleepFeedback,
        `Sleep toggle failed: ${window.PAApi.messageFor(error)}`,
        "error"
      );
      if (lastStatus) setSleepUi(!!lastStatus.sleepMode);
    } finally {
      setSleepPending(false);
    }
  };

  const setModePending = (pending) => {
    modePending = pending;
    if (opmodeDrive) opmodeDrive.disabled = pending;
    if (opmodeStationary) opmodeStationary.disabled = pending;
    document.querySelectorAll(".opmode-btn").forEach((btn) => {
      btn.classList.toggle("is-pending", pending);
      btn.setAttribute("aria-disabled", pending ? "true" : "false");
    });
  };

  const setMoodPending = (pending) => {
    moodPending = pending;
    document.querySelectorAll(".mood-btn").forEach((btn) => {
      btn.disabled = pending;
      btn.classList.toggle("is-pending", pending);
      btn.setAttribute("aria-disabled", pending ? "true" : "false");
    });
  };

  const setMode = async (mode) => {
    if (!window.PAApi || modePending) return;
    setModePending(true);
    showFeedback(opmodeFeedback, `Setting mode to ${mode}...`);

    try {
      await window.PAApi.postForm("/api/mode", { mode }, { timeoutMs: 3000 });
      await refreshStatusOnce();
      showFeedback(opmodeFeedback, "Mode updated", "success");
    } catch (error) {
      if (lastStatus) renderOpMode(lastStatus);
      showFeedback(opmodeFeedback, `Mode update failed: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      setModePending(false);
    }
  };

  const setMood = async (moodId) => {
    if (!window.PAApi || moodPending) return;
    setMoodPending(true);
    showFeedback(moodFeedback, `Applying mood SE${moodId}...`);

    try {
      await window.PAApi.postForm("/api/mood", { mood: String(moodId) }, { timeoutMs: 3000 });
      await refreshStatusOnce();
      showFeedback(moodFeedback, "Mood updated", "success");
    } catch (error) {
      if (lastStatus) renderActiveMood(lastStatus);
      showFeedback(moodFeedback, `Mood update failed: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      setMoodPending(false);
    }
  };

  const appendLogLine = (text) => {
    if (!logConsole) return;

    const raw = logConsole.textContent;
    if (raw.length > 0 && raw !== "Waiting for logs…") {
      const lines = raw.split("\n");
      if (lines.length > 250) {
        logConsole.textContent = `${lines.slice(lines.length - 200).join("\n")}\n`;
      }
    } else {
      logConsole.textContent = "";
    }

    logConsole.textContent += `${text}\n`;

    const threshold = 50;
    const atBottom =
      logConsole.scrollTop + logConsole.clientHeight >= logConsole.scrollHeight - threshold;
    if (atBottom) {
      logConsole.scrollTop = logConsole.scrollHeight;
      logPaused?.classList.remove("visible");
    } else {
      logPaused?.classList.add("visible");
    }
  };

  logConsole?.addEventListener("scroll", () => {
    const threshold = 50;
    const atBottom =
      logConsole.scrollTop + logConsole.clientHeight >= logConsole.scrollHeight - threshold;
    if (atBottom) logPaused?.classList.remove("visible");
  });

  const postSound = async (params) => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.postForm("/api/audio", params, { timeoutMs: 3000 });
      const payload = result.data;
      const ok = payload && typeof payload === "object" ? !!payload.ok : true;
      const msg = ok ? "Done" : (payload?.error || "Sound command failed");
      showFeedback(soundFeedback, msg, ok ? "success" : "error");
      if (ok) {
        window.setTimeout(() => {
          if (soundFeedback) soundFeedback.textContent = "";
        }, 2000);
      }
    } catch (error) {
      showFeedback(soundFeedback, window.PAApi.messageFor(error), "error");
    }
  };

  opmodeDrive?.addEventListener("click", () => setMode("driving"));
  opmodeStationary?.addEventListener("click", () => setMode("stationary"));

  document.querySelectorAll(".mood-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const match = btn.dataset.cmd?.match(/:SE(\d+)/);
      if (!match) return;
      setMood(match[1]);
    });
  });

  sleepToggle?.addEventListener("click", () => toggleSleepWake(false));
  sleepOverlayWake?.addEventListener("click", () => toggleSleepWake(true));

  document.getElementById("sound-stop")?.addEventListener("click", () => postSound({ action: "stop" }));
  document.getElementById("sound-vol-up")?.addEventListener("click", () => postSound({ action: "dollar", cmd: "$+" }));
  document.getElementById("sound-vol-dn")?.addEventListener("click", () => postSound({ action: "dollar", cmd: "$-" }));

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") {
        applyStatus(payload);
        setStale(false);
      }
      if (eventType === "log") appendLogLine(payload);
      if (eventType === "stream_error") {
        appendLogLine("[connection lost \u2014 retrying…]");
        setStale(true);
      }
    });

    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatusOnce().catch(() => {
        // Fallback polling below handles temporary fetch failures.
      });
    }
  } else {
    const refreshFromFallback = () => {
      refreshStatusOnce().catch(() => {
        pollFailCount++;
        if (pollFailCount >= 2) setStale(true);
      });
    };

    refreshFromFallback();

    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshFromFallback();
    }, 3000);

    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshFromFallback();
      }
    });
  }
  setSleepUi(false);
})();
