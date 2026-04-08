// =============================================================================
// app.js
//
// Home dashboard controller.
// - Uses shared status stream (SSE-first, polling fallback)
// - Provides truthful mode/mood command UX with rollback on failure
// - Renders system health, component status, live logs, and status snapshots
// =============================================================================
(() => {
  const logConsole = document.getElementById("log-console");
  const logPaused = document.getElementById("log-paused");
  const componentStatusCard = document.getElementById("component-status-card");
  const componentStatusGrid = document.getElementById("component-status-grid");

  const opmodeDrive = document.getElementById("opmode-drive");
  const opmodeStationary = document.getElementById("opmode-stationary");
  const opmodeFeedback = document.getElementById("opmode-feedback");

  const moodDomeNote = document.getElementById("mood-dome-note");
  const moodFeedback = document.getElementById("mood-feedback");

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

  const escapeHtml = (value) => String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");

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
    const heapBytes = Number(payload.heapFree);
    const heapKnown = Number.isFinite(heapBytes) && heapBytes >= 0;
    setIndicator("h-heap", heapKnown
      ? (heapBytes > 120000 ? "ok" : heapBytes > 80000 ? "warn" : "fail")
      : "off");
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
      const stateText = String(state).replace(/_/g, " ");
      const safeState = escapeHtml(stateText);
      const safeDetail = escapeHtml(detail);
      return `
        <div class="status-item">
          <dt>${icon} ${label}</dt>
          <dd>${safeState}</dd>
          <div class="desc mt-6">${safeDetail}</div>
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

  const LOG_MAX_LINES = 250;
  const LOG_TRIM_LINES = 200;
  const LOG_EMPTY_TEXT = "No log history available yet.";
  let logLines = [];

  const normalizeLogMessage = (line) => String(line ?? "").replace(/^\[t\+\d+ms\]\s*/i, "").trim();

  const isLogAtBottom = () => {
    if (!logConsole) return true;
    const threshold = 50;
    return logConsole.scrollTop + logConsole.clientHeight >= logConsole.scrollHeight - threshold;
  };

  const renderLogConsole = (stickToBottom = false) => {
    if (!logConsole) return;
    logConsole.textContent = logLines.length > 0 ? logLines.join("\n") : LOG_EMPTY_TEXT;
    if (stickToBottom) {
      logConsole.scrollTop = logConsole.scrollHeight;
      logPaused?.classList.remove("visible");
    } else {
      logPaused?.classList.add("visible");
    }
  };

  const setLogLines = (lines, { forceBottom = true } = {}) => {
    logLines = lines.slice(-LOG_MAX_LINES);
    const stickToBottom = forceBottom || isLogAtBottom();
    renderLogConsole(stickToBottom);
  };

  const appendLogLine = (text) => {
    if (!logConsole) return;
    const message = normalizeLogMessage(text);
    if (!message) return;

    const stickToBottom = isLogAtBottom();
    const line = message;
    logLines.push(line);
    if (logLines.length > LOG_MAX_LINES) {
      logLines = logLines.slice(logLines.length - LOG_TRIM_LINES);
    }
    renderLogConsole(stickToBottom);
  };

  const loadRecentLogs = async () => {
    if (!window.PAApi || !logConsole) return;
    try {
      const result = await window.PAApi.get("/api/logs", { cache: "no-store", timeoutMs: 3000 });
      if (logLines.length > 0) return;
      const historyLines = String(result.data ?? "")
        .split(/\r?\n/)
        .map((line) => normalizeLogMessage(line.trimEnd()))
        .filter((line) => line.length > 0);
      setLogLines(historyLines);
    } catch (error) {
      if (logLines.length === 0) {
        setLogLines([]);
      }
    }
  };

  logConsole?.addEventListener("scroll", () => {
    if (isLogAtBottom()) {
      logPaused?.classList.remove("visible");
    }
  });


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


  loadRecentLogs();

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") {
        applyStatus(payload);
        setStale(false);
      }
      if (eventType === "log") appendLogLine(payload);
      if (eventType === "stream_error") {
        appendLogLine("[connection lost — retrying…]");
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
