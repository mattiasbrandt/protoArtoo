// =============================================================================
// data/sound.js
//
// Sound page controller — named sound commands, volume, direct play,
// random range configuration. All audio commands go through /api/audio.
// Named track assignments are loaded from and saved to /api/audio/tracks.
// =============================================================================
(() => {
  const TRACK_MAX = 999;
  const NAMED_SOUNDS = [
    { label: "Scream", cmd: "$S", key: "scream", editable: true },
    { label: "Short Circuit", cmd: "$F", key: "faint", editable: true },
    { label: "Doo-doo", cmd: null, key: "doodoo", editable: true, playMode: "track", trackMin: 0 },
    { label: "Failure", cmd: null, key: "failure", editable: true, playMode: "track", trackMin: 0 },
    { label: "Leia Message", cmd: "$L", key: "leia", editable: true },
    { label: "Short Cantina", cmd: "$c", key: "cantina_s", editable: true },
    { label: "Star Wars Theme", cmd: "$W", key: "sw_theme", editable: true },
    { label: "Disco", cmd: null, key: "disco", editable: true, playMode: "track", trackMin: 0 },
    { label: "Mahna Mahna", cmd: null, key: "mahna", editable: true, playMode: "track", trackMin: 0 },
    { label: "In Love", cmd: null, key: "inlove", editable: true, playMode: "track", trackMin: 0 },
    { label: "Macho Man", cmd: null, key: "macho", editable: true, playMode: "track", trackMin: 0 },
    { label: "Gangnam Style", cmd: null, key: "gangnam", editable: true, playMode: "track", trackMin: 0 },
    { label: "Uptown Funk", cmd: null, key: "uptown", editable: true, playMode: "track", trackMin: 0 },
    { label: "Celebration", cmd: null, key: "celebr", editable: true, playMode: "track", trackMin: 0 },
    { label: "Stayin' Alive", cmd: null, key: "stayin", editable: true, playMode: "track", trackMin: 0 },
    { label: "Harlem Shake", cmd: null, key: "harlem", editable: true, playMode: "track", trackMin: 0 },
    { label: "PBJ Time", cmd: null, key: "pbjtime", editable: true, playMode: "track", trackMin: 0 },
    { label: "Imperial March", cmd: "$M", key: "imp_march", editable: true },
    { label: "Long Cantina", cmd: "$C", key: "cantina_l", editable: true },
    { label: "Boot Sound", cmd: "$B", key: "startup", editable: true },
    { label: "Random On", cmd: "$R", key: null, editable: false },
    { label: "Random Off", cmd: "$O", key: null, editable: false },
    { label: "Stop / Chatter Off", cmd: "$s", key: null, editable: false },
  ];

  const SYSTEM_SOUNDS = [
    { label: "Boot complete", key: "sys_boot" },
    { label: "Mode → Normal", key: "sys_mode_n" },
    { label: "Mode → Slow", key: "sys_mode_s" },
    { label: "Mode → Turbo", key: "sys_mode_t" },
    { label: "Drives engaged", key: "sys_drv_on" },
    { label: "Dome enabled", key: "sys_dome_on" },
  ];
  const CATEGORY_SOUNDS = [
    { label: "General", loKey: "snd_cat_gen_lo", hiKey: "snd_cat_gen_hi" },
    { label: "Chatty", loKey: "snd_cat_chat_lo", hiKey: "snd_cat_chat_hi" },
    { label: "Happy", loKey: "snd_cat_hap_lo", hiKey: "snd_cat_hap_hi" },
    { label: "Processing", loKey: "snd_cat_proc_lo", hiKey: "snd_cat_proc_hi" },
    { label: "Sad", loKey: "snd_cat_sad_lo", hiKey: "snd_cat_sad_hi" },
    { label: "Sentimental", loKey: "snd_cat_sent_lo", hiKey: "snd_cat_sent_hi" },
    { label: "Humming", loKey: "snd_cat_hum_lo", hiKey: "snd_cat_hum_hi" },
    { label: "Scream", loKey: "snd_cat_scrm_lo", hiKey: "snd_cat_scrm_hi" },
    { label: "Surprised", loKey: "snd_cat_ooh_lo", hiKey: "snd_cat_ooh_hi" },
    { label: "Alert", loKey: "snd_cat_alrm_lo", hiKey: "snd_cat_alrm_hi" },
    { label: "Snarky", loKey: "snd_cat_snrk_lo", hiKey: "snd_cat_snrk_hi" },
    { label: "Whistle", loKey: "snd_cat_whis_lo", hiKey: "snd_cat_whis_hi" },
  ];
  const MOOD_MAP_MOODS = [
    { key: "quiet", label: "Quiet" },
    { key: "mid", label: "Mid-Awake" },
    { key: "full", label: "Full-Awake" },
    { key: "awakeplus", label: "Awake+" },
  ];
  const MOOD_MAP_DEFAULTS = {
    quiet: 0x0048,
    mid: 0x004F,
    full: 0x090F,
    awakeplus: 0x0F8F,
  };

  const CMD_MARKER = "—";

  // AudioDriver capability bits — must match audio_driver.h AUDIO_CAP_* constants
  const AUDIO_CAP_STATUS_QUERY = 0x01;
  const AUDIO_CAP_DEVICE_TYPE = 0x02;
  const AUDIO_CAP_TRACK_COUNT = 0x04;
  const AUDIO_CAP_CURRENT_TRACK = 0x08;
  const AUDIO_CAP_QUERY_SAFE_PLAYING = 0x10;

  const tbody = document.getElementById("named-sound-rows");
  const systemTbody = document.getElementById("system-sound-rows");
  const categoryTbody = document.getElementById("category-sound-rows");
  const moodMapTbody = document.getElementById("mood-map-rows");
  const moodMapFb = document.getElementById("mood-map-feedback");
  const moodMapSaveBtn = document.getElementById("btn-mood-map-save");
  const soundStateBadge = document.getElementById("sound-state-badge");
  const soundDisabledCard = document.getElementById("sound-disabled-card");
  const globalFb = document.getElementById("global-feedback");
  const volSlider = document.getElementById("vol-slider");
  const volDisplay = document.getElementById("vol-display");
  const namedSoundFilterInput = document.getElementById("named-sound-filter");
  const namedSoundFilterCount = document.getElementById("named-sound-filter-count");
  const soundModeAdvancedBtn = document.getElementById("sound-mode-advanced");
  const soundModeCompactBtn = document.getElementById("sound-mode-compact");
  const soundModeFeedback = document.getElementById("sound-mode-feedback");
  const SOUND_VIEW_MODE_KEY = "pa.sound.viewMode";
  const SOUND_VIEW_MODE_ADVANCED = "advanced";
  const SOUND_VIEW_MODE_COMPACT = "compact";

  const MSG = {
    categoryRangeInvalid: "Use 0/0 or 1–999 with Min ≤ Max",
    valuesMustBe1To999: "Values must be 1–999",
    minMustBeLeMax: "Min must be ≤ Max",
    saveFailed: "Save failed",
    saved: "Saved",
    unsaved: "Unsaved",
    trackRange: (min, max) => `${min}–${max}`,
    trackRangeZeroToMax: `0–${TRACK_MAX}`,
  };

  const namedDirtyTrackers = new Map();
  const categoryDirtyTrackers = new Map();
  const systemDirtyTrackers = new Map();

  let soundHardwareEnabled = true;
  let moodMapApiAvailable = true;
  let moodMapLoaded = false;

  // Module status elements
  const modDriver = document.getElementById("mod-driver");
  const modLink = document.getElementById("mod-link");
  const modDevice = document.getElementById("mod-device");
  const modPlayState = document.getElementById("mod-play-state");
  const modTotalTracks = document.getElementById("mod-total-tracks");
  const modCurrentTrack = document.getElementById("mod-current-track");
  const modDeviceRow = document.getElementById("mod-device-row");
  const modCurrentTrackRow = document.getElementById("mod-current-track-row");
  const modStatusTable = document.getElementById("mod-status-table");
  const modPollSection = document.getElementById("mod-poll-section");
  const modQueryNote = document.getElementById("mod-query-note");
  const modNoQueryNotice = document.getElementById("mod-no-query-notice");
  const btnPoll = document.getElementById("btn-poll-status");
  const modStatusFb = document.getElementById("mod-status-feedback");
  const trackNumberNote = document.getElementById("track-number-note");
  let lastCapabilities = null; // null = not yet received
  let moduleStatusRefreshTimer = null;

  const setElementVisible = (element, visible) => {
    if (!element) return;
    element.classList.toggle("hidden", !visible);
  };

  const setModeButtonState = (button, active) => {
    if (!button) return;
    button.classList.toggle("accent", active);
    button.setAttribute("aria-pressed", active ? "true" : "false");
  };

  const applySoundWorkspaceMode = (mode, persist = true) => {
    const normalizedMode = mode === SOUND_VIEW_MODE_COMPACT
      ? SOUND_VIEW_MODE_COMPACT
      : SOUND_VIEW_MODE_ADVANCED;

    document.body.classList.toggle("sound-mode-compact", normalizedMode === SOUND_VIEW_MODE_COMPACT);
    setModeButtonState(soundModeAdvancedBtn, normalizedMode === SOUND_VIEW_MODE_ADVANCED);
    setModeButtonState(soundModeCompactBtn, normalizedMode === SOUND_VIEW_MODE_COMPACT);

    if (soundModeFeedback) {
      soundModeFeedback.textContent = normalizedMode === SOUND_VIEW_MODE_COMPACT
        ? "Compact mode active. Advanced tuning cards are hidden."
        : "Advanced mode active. Full editing cards are visible.";
    }

    if (!persist) return;
    try {
      window.localStorage?.setItem(SOUND_VIEW_MODE_KEY, normalizedMode);
    } catch (_err) {
      // Non-fatal when storage is unavailable.
    }
  };

  const loadSoundWorkspaceMode = () => {
    try {
      const storedMode = window.localStorage?.getItem(SOUND_VIEW_MODE_KEY);
      if (storedMode === SOUND_VIEW_MODE_COMPACT || storedMode === SOUND_VIEW_MODE_ADVANCED) {
        return storedMode;
      }
    } catch (_err) {
      // Ignore storage read failures and use default.
    }
    return SOUND_VIEW_MODE_ADVANCED;
  };

  const resetModuleStatusAutoRefresh = (caps) => {
    if (moduleStatusRefreshTimer !== null) {
      window.clearInterval(moduleStatusRefreshTimer);
      moduleStatusRefreshTimer = null;
    }

    if ((caps & AUDIO_CAP_QUERY_SAFE_PLAYING) !== 0) {
      moduleStatusRefreshTimer = window.setInterval(() => {
        if (document.visibilityState === "hidden") return;
        updateModuleStatus();
      }, 2000);
    }
  };

  const applyCapabilityUI = (caps) => {
    const supportsStatusQuery = (caps & AUDIO_CAP_STATUS_QUERY) !== 0;
    const supportsDeviceType = (caps & AUDIO_CAP_DEVICE_TYPE) !== 0;
    const supportsCurrentTrack = (caps & AUDIO_CAP_CURRENT_TRACK) !== 0;
    const supportsSafePlayingQuery = (caps & AUDIO_CAP_QUERY_SAFE_PLAYING) !== 0;
    const showManualPoll = supportsStatusQuery && !supportsSafePlayingQuery;

    setElementVisible(modDeviceRow, supportsStatusQuery && supportsDeviceType);
    setElementVisible(modCurrentTrackRow, supportsStatusQuery && supportsCurrentTrack);
    setElementVisible(modPollSection, showManualPoll);
    setElementVisible(btnPoll, showManualPoll);
    setElementVisible(modStatusTable, supportsStatusQuery);
    if (modNoQueryNotice) setElementVisible(modNoQueryNotice, !supportsStatusQuery);

    if (!modQueryNote) return;
    if (!supportsStatusQuery) {
      modQueryNote.textContent = "Status queries not supported by this module.";
      return;
    }
    if (supportsSafePlayingQuery) {
      modQueryNote.textContent = "Module status updates automatically every 2 s.";
      return;
    }
    modQueryNote.textContent = "Status is cached from boot. Use Poll to refresh — only poll when not playing.";
  };

  const updateModuleStatus = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/audio", { timeoutMs: 3000 });
      const d = result.data;

      if (d.capabilities !== undefined && d.capabilities !== null) {
        const caps = Number(d.capabilities) & 0xFF;
        const capabilitiesChanged = lastCapabilities !== caps;
        lastCapabilities = caps;
        applyCapabilityUI(caps);
        if (capabilitiesChanged) resetModuleStatusAutoRefresh(caps);
      }

      if (modDriver) modDriver.textContent = d.driver ?? "—";
      if (modLink) {
        const ok = Boolean(d.link_ok);
        modLink.textContent = ok ? "OK" : "No response";
        modLink.dataset.state = ok ? "ok" : "error";
      }
      if (modDevice) modDevice.textContent = d.device ?? "—";
      if (modPlayState) modPlayState.textContent = d.play_state ?? "—";
      if (modTotalTracks) modTotalTracks.textContent = d.total_tracks ?? "—";
      if (modCurrentTrack) modCurrentTrack.textContent = d.current_track ?? "—";

      // Update the badge with the real module-reported play state.
      // Only override when the module is actually responding; if link_ok is
      // false the user needs to see "No module response", not a stale "Idle".
      if (soundStateBadge && soundHardwareEnabled) {
        const linkOk = Boolean(d.link_ok);
        if (!linkOk) {
          soundStateBadge.textContent = "No module response";
          soundStateBadge.dataset.state = "error";
        } else if (d.play_state === "playing") {
          soundStateBadge.textContent = "🔊 Playing";
          soundStateBadge.dataset.state = "playing";
        } else if (d.play_state === "paused") {
          soundStateBadge.textContent = "⏸ Paused";
          soundStateBadge.dataset.state = "idle";
        } else {
          soundStateBadge.textContent = "✅ Idle";
          soundStateBadge.dataset.state = "idle";
        }
      }
    } catch (_err) {
      if (modLink) {
        modLink.textContent = "Fetch error";
        modLink.dataset.state = "error";
      }
    }
  };

  const showFeedback = (el, msg, ok) => {
    if (!el) return;
    el.textContent = msg;
    el.className = `feedback ${ok ? "success" : "error"}`;
    window.setTimeout(() => {
      if (!el) return;
      el.textContent = "";
      el.className = "feedback";
    }, 2500);
  };

  const getApiErrorMessage = (error) => window.PAApi?.messageFor(error) || String(error);

  const feedbackFromSaveResponse = (feedbackEl, response, successMessage = MSG.saved) => {
    const ok = Boolean(response.data?.ok);
    showFeedback(feedbackEl, ok ? successMessage : (response.data?.error || MSG.saveFailed), ok);
    return ok;
  };

  const feedbackSaveFailure = (feedbackEl, error, prefix = MSG.saveFailed) => {
    showFeedback(feedbackEl, `${prefix}: ${getApiErrorMessage(error)}`, false);
  };

  const setMoodMapStatus = (msg, ok) => {
    if (!moodMapFb) return;
    if (!msg) {
      moodMapFb.textContent = "";
      moodMapFb.className = "feedback mt-8";
      return;
    }
    moodMapFb.textContent = msg;
    moodMapFb.className = `feedback mt-8 ${ok ? "success" : "error"}`;
  };

  const getMoodMapCheckbox = (moodKey, categoryIndex) =>
    document.getElementById(`mood-map-${moodKey}-${categoryIndex}`);

  const decodeMoodMaskToUi = (moodKey, maskValue) => {
    CATEGORY_SOUNDS.forEach((_, index) => {
      const checkbox = getMoodMapCheckbox(moodKey, index);
      if (!checkbox) return;
      checkbox.checked = (maskValue & (1 << index)) !== 0;
    });
  };

  const encodeMoodMaskFromUi = (moodKey) => {
    let mask = 0;
    CATEGORY_SOUNDS.forEach((_, index) => {
      const checkbox = getMoodMapCheckbox(moodKey, index);
      if (checkbox?.checked) mask |= (1 << index);
    });
    return mask;
  };

  const syncMoodMapControlState = () => {
    const enabled = soundHardwareEnabled && moodMapApiAvailable;
    moodMapTbody?.querySelectorAll('input[type="checkbox"]').forEach((checkbox) => {
      checkbox.disabled = !enabled;
    });
    if (moodMapSaveBtn) {
      moodMapSaveBtn.disabled = !enabled;
      moodMapSaveBtn.setAttribute("aria-disabled", enabled ? "false" : "true");
    }
  };

  const setSoundHardwareEnabled = (enabled) => {
    soundHardwareEnabled = enabled;
    soundDisabledCard?.classList.toggle("hidden", enabled);

    const controls = document.querySelectorAll(
      '.card:not(#sound-disabled-card) button, .card:not(#sound-disabled-card) input, .card:not(#sound-disabled-card) select, .card:not(#sound-disabled-card) textarea'
    );
    controls.forEach((control) => {
      control.disabled = !enabled;
      if (control.tagName === "BUTTON") {
        control.setAttribute("aria-disabled", enabled ? "false" : "true");
      }
    });
    if (enabled) {
      refreshCategoryTestButtons();
    }
    syncMoodMapControlState();
  };

  const postAudio = async (params, feedbackEl, label = 'Sound command') => {
    if (!window.PAApi) return false;
    if (!soundHardwareEnabled) {
      showFeedback(feedbackEl || globalFb, "Sound controls unavailable: enable S2 — Sound in Setup.", false);
      return false;
    }
    try {
      const result = await window.PAApi.postForm("/api/audio", params, { timeoutMs: 3000 });
      const ok = Boolean(result.data?.ok);
      showFeedback(feedbackEl, ok ? `${label} sent` : (result.data?.error || "Failed"), ok);
      return ok;
    } catch (error) {
      showFeedback(feedbackEl, `Command failed: ${getApiErrorMessage(error)}`, false);
      return false;
    }
  };

  const postTrack = async (key, track, feedbackEl) => {
    if (!window.PAApi) return false;
    if (!soundHardwareEnabled) {
      showFeedback(feedbackEl || globalFb, "Track updates unavailable: enable S2 — Sound in Setup.", false);
      return false;
    }
    try {
      const result = await window.PAApi.postForm("/api/audio/tracks", { key, track }, { timeoutMs: 3000 });
      return feedbackFromSaveResponse(feedbackEl, result);
    } catch (error) {
      feedbackSaveFailure(feedbackEl, error);
      return false;
    }
  };

  const syncVolumeLabel = () => {
    if (!volSlider || !volDisplay) return;
    volDisplay.textContent = String(volSlider.value);
  };

  const isCategoryRangeValid = (minVal, maxVal) => {
    if (Number.isNaN(minVal) || Number.isNaN(maxVal)) return false;
    if (minVal === 0 && maxVal === 0) return true;
    return minVal >= 1 && maxVal >= 1 && minVal <= maxVal && maxVal <= TRACK_MAX;
  };

  const updateCategoryTestButtonState = (button, minVal, maxVal) => {
    if (!button) return;
    const enabled = soundHardwareEnabled && isCategoryRangeValid(minVal, maxVal) && minVal !== 0;
    button.disabled = !enabled;
    button.setAttribute("aria-disabled", enabled ? "false" : "true");
  };

  const findCategoryPlayButton = (minInput) =>
    minInput?.closest("tr")?.querySelector(".sound-btn-play");

  const refreshCategoryTestButtons = () => {
    CATEGORY_SOUNDS.forEach((category) => {
      const minInput = document.getElementById(`cat-min-${category.loKey}`);
      const maxInput = document.getElementById(`cat-max-${category.hiKey}`);
      const playButton = findCategoryPlayButton(minInput);
      const minVal = Number.parseInt(minInput?.value, 10);
      const maxVal = Number.parseInt(maxInput?.value, 10);
      updateCategoryTestButtonState(playButton, minVal, maxVal);
    });
  };

  const updateNamedSoundFilterCount = (visibleRows, totalRows) => {
    if (!namedSoundFilterCount) return;
    namedSoundFilterCount.textContent = `${visibleRows} of ${totalRows} sounds shown`;
  };

  const applyNamedSoundFilter = () => {
    if (!tbody) return;
    const query = namedSoundFilterInput?.value?.trim().toLowerCase() ?? "";
    const rows = Array.from(tbody.querySelectorAll("tr"));
    if (!rows.length) {
      updateNamedSoundFilterCount(0, 0);
      return;
    }

    let visibleRows = 0;
    rows.forEach((row) => {
      const label = (row.dataset.soundLabel ?? "").toLowerCase();
      const command = (row.dataset.soundCmd ?? "").toLowerCase();
      const matches = !query || label.includes(query) || command.includes(query);
      row.classList.toggle("hidden-row", !matches);
      if (matches) visibleRows += 1;
    });

    updateNamedSoundFilterCount(visibleRows, rows.length);
  };

  const createActionCell = () => {
    const tdActions = document.createElement("td");
    tdActions.className = "sound-actions-cell";

    const actionsWrap = document.createElement("div");
    actionsWrap.className = "sound-action-row";
    tdActions.appendChild(actionsWrap);

    return { tdActions, actionsWrap };
  };

  const createActionButton = ({ label, title, ariaLabel, className, onClick }) => {
    const button = document.createElement("button");
    button.className = className;
    button.textContent = label;
    button.title = title;
    button.setAttribute("aria-label", ariaLabel);
    button.addEventListener("click", onClick);
    return button;
  };

  const createInlineFeedback = () => {
    const feedback = document.createElement("span");
    feedback.className = "sound-feedback-inline";
    feedback.setAttribute("role", "status");
    feedback.setAttribute("aria-live", "polite");
    feedback.setAttribute("aria-atomic", "true");
    return feedback;
  };

  const createDirtyMarker = () => {
    const marker = document.createElement("span");
    marker.className = "sound-dirty-pill hidden";
    marker.textContent = MSG.unsaved;
    return marker;
  };

  const createRowDirtyTracker = ({ row, inputs, marker }) => {
    const normalizedValue = (input) => String(input?.value ?? "");

    const update = () => {
      const dirty = inputs.some((input) => normalizedValue(input) !== (input.dataset.savedValue ?? ""));
      row.classList.toggle("sound-row-dirty", dirty);
      marker.classList.toggle("hidden", !dirty);
    };

    const markSaved = () => {
      inputs.forEach((input) => {
        input.dataset.savedValue = normalizedValue(input);
      });
      update();
    };

    inputs.forEach((input) => {
      input.addEventListener("input", update);
      input.addEventListener("change", update);
    });

    markSaved();
    return { markSaved, update };
  };

  const createNumberInput = ({ id, min, max, className, ariaLabel, datasetKey = null, placeholder = null, value = null }) => {
    const input = document.createElement("input");
    input.type = "number";
    input.min = String(min);
    input.max = String(max);
    input.className = className;
    if (id) input.id = id;
    if (datasetKey) input.dataset.key = datasetKey;
    if (placeholder !== null) input.placeholder = placeholder;
    if (value !== null) input.value = String(value);
    input.setAttribute("aria-label", ariaLabel);
    return input;
  };

  const buildNamedSoundRows = () => {
    if (!tbody) return;
    tbody.innerHTML = "";
    namedDirtyTrackers.clear();

    if (trackNumberNote) {
      trackNumberNote.textContent = "Track numbers are module-specific — set these to match your installed module's layout. T08 music rows allow 0 for silent/no-op play.";
    }

    NAMED_SOUNDS.forEach((sound) => {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";
      tr.dataset.soundLabel = sound.label;
      tr.dataset.soundCmd = sound.cmd ?? "";

      const tdLabel = document.createElement("td");
      tdLabel.textContent = sound.label;

      const tdCmd = document.createElement("td");
      tdCmd.className = "sound-mono";
      tdCmd.textContent = sound.cmd ?? CMD_MARKER;

      const tdTrack = document.createElement("td");
      const { tdActions, actionsWrap } = createActionCell();

      let rowInput = null;
      let rowFeedback = null;
      let dirtyMarker = null;
      let dirtyTracker = null;

      if (sound.editable && sound.key) {
        const minTrack = sound.trackMin ?? 1;
        rowInput = createNumberInput({
          id: `track-input-${sound.key}`,
          min: minTrack,
          max: TRACK_MAX,
          className: "sound-track-input-sm",
          ariaLabel: `${sound.label} track number`,
          datasetKey: sound.key,
        });
        tdTrack.appendChild(rowInput);

        rowFeedback = createInlineFeedback();
        dirtyMarker = createDirtyMarker();
        dirtyTracker = createRowDirtyTracker({ row: tr, inputs: [rowInput], marker: dirtyMarker });
        namedDirtyTrackers.set(sound.key, dirtyTracker);

        const saveButton = createActionButton({
          label: "💾 Save",
          title: "Save track number",
          ariaLabel: `Save ${sound.label} track number`,
          className: "btn sound-btn-compact",
          onClick: async () => {
            const value = Number.parseInt(rowInput.value, 10);
            if (Number.isNaN(value) || value < minTrack || value > TRACK_MAX) {
              showFeedback(rowFeedback, MSG.trackRange(minTrack, TRACK_MAX), false);
              return;
            }
            const ok = await postTrack(sound.key, value, rowFeedback);
            if (ok) dirtyTracker?.markSaved();
          },
        });
        actionsWrap.appendChild(saveButton);
      } else {
        tdTrack.textContent = "—";
      }

      const playButton = createActionButton({
        label: "▶ Play",
        title: sound.playMode === "track"
          ? `Play configured track for ${sound.label}`
          : `Play ${sound.cmd}`,
        ariaLabel: `Play ${sound.label}`,
        className: "btn sound-btn-play",
        onClick: () => {
          if (sound.playMode === "track") {
            const minTrack = sound.trackMin ?? 1;
            const value = Number.parseInt(rowInput?.value, 10);
            if (Number.isNaN(value) || value < minTrack || value > TRACK_MAX) {
              showFeedback(rowFeedback || globalFb, MSG.trackRange(minTrack, TRACK_MAX), false);
              return;
            }
            if (value === 0) return;
            postAudio({ action: "play", track: value }, globalFb, sound.label);
            return;
          }
          postAudio({ action: "dollar", cmd: sound.cmd }, globalFb, sound.label);
        },
      });
      actionsWrap.appendChild(playButton);

      if (dirtyMarker) tdActions.appendChild(dirtyMarker);
      if (rowFeedback) tdActions.appendChild(rowFeedback);

      tr.appendChild(tdLabel);
      tr.appendChild(tdCmd);
      tr.appendChild(tdTrack);
      tr.appendChild(tdActions);
      tbody.appendChild(tr);
    });

    applyNamedSoundFilter();
  };

  const buildCategorySoundRows = () => {
    if (!categoryTbody) return;
    categoryTbody.innerHTML = "";
    categoryDirtyTrackers.clear();

    CATEGORY_SOUNDS.forEach((category) => {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";

      const tdLabel = document.createElement("td");
      tdLabel.textContent = category.label;

      const tdMin = document.createElement("td");
      const minInput = createNumberInput({
        id: `cat-min-${category.loKey}`,
        min: 0,
        max: TRACK_MAX,
        value: 0,
        className: "sound-track-input-sm",
        ariaLabel: `${category.label} minimum track`,
      });
      tdMin.appendChild(minInput);

      const tdMax = document.createElement("td");
      const maxInput = createNumberInput({
        id: `cat-max-${category.hiKey}`,
        min: 0,
        max: TRACK_MAX,
        value: 0,
        className: "sound-track-input-sm",
        ariaLabel: `${category.label} maximum track`,
      });
      tdMax.appendChild(maxInput);

      const { tdActions, actionsWrap } = createActionCell();
      const rowFeedback = createInlineFeedback();
      const dirtyMarker = createDirtyMarker();
      const dirtyTracker = createRowDirtyTracker({ row: tr, inputs: [minInput, maxInput], marker: dirtyMarker });
      categoryDirtyTrackers.set(category.loKey, dirtyTracker);

      const saveButton = createActionButton({
        label: "💾 Save",
        title: "Save category range",
        ariaLabel: `Save ${category.label} range`,
        className: "btn sound-btn-compact",
        onClick: async () => {
          const minVal = Number.parseInt(minInput.value, 10);
          const maxVal = Number.parseInt(maxInput.value, 10);
          if (!isCategoryRangeValid(minVal, maxVal)) {
            showFeedback(rowFeedback, MSG.categoryRangeInvalid, false);
            return;
          }
          if (!window.PAApi) return;
          if (!soundHardwareEnabled) {
            showFeedback(rowFeedback, "Category updates unavailable: enable S2 — Sound in Setup.", false);
            return;
          }
          try {
            const response = await window.PAApi.postForm(
              "/api/audio/category-range",
              { lo_key: category.loKey, hi_key: category.hiKey, lo: minVal, hi: maxVal },
              { timeoutMs: 3000 }
            );
            const ok = feedbackFromSaveResponse(rowFeedback, response);
            if (ok) dirtyTracker.markSaved();
          } catch (error) {
            feedbackSaveFailure(rowFeedback, error);
          }
        },
      });

      const playButton = createActionButton({
        label: "▶ Play",
        title: `Play random ${category.label} track`,
        ariaLabel: `Play ${category.label}`,
        className: "btn sound-btn-play",
        onClick: () => {
          const minVal = Number.parseInt(minInput.value, 10);
          const maxVal = Number.parseInt(maxInput.value, 10);
          if (!isCategoryRangeValid(minVal, maxVal)) {
            showFeedback(rowFeedback, MSG.categoryRangeInvalid, false);
            return;
          }
          if (minVal === 0) return;
          const randomTrack = minVal + Math.floor(Math.random() * (maxVal - minVal + 1));
          postAudio({ action: "play", track: randomTrack }, globalFb, `${category.label} (${randomTrack})`);
        },
      });

      const syncPlayButtonState = () => {
        const minVal = Number.parseInt(minInput.value, 10);
        const maxVal = Number.parseInt(maxInput.value, 10);
        updateCategoryTestButtonState(playButton, minVal, maxVal);
      };
      minInput.addEventListener("input", syncPlayButtonState);
      maxInput.addEventListener("input", syncPlayButtonState);
      syncPlayButtonState();

      actionsWrap.appendChild(saveButton);
      actionsWrap.appendChild(playButton);
      tdActions.appendChild(dirtyMarker);
      tdActions.appendChild(rowFeedback);

      tr.appendChild(tdLabel);
      tr.appendChild(tdMin);
      tr.appendChild(tdMax);
      tr.appendChild(tdActions);
      categoryTbody.appendChild(tr);
    });
  };


  const buildMoodMapRows = () => {
    if (!moodMapTbody) return;
    moodMapTbody.innerHTML = "";

    CATEGORY_SOUNDS.forEach((category, index) => {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";

      const tdLabel = document.createElement("td");
      tdLabel.textContent = category.label;
      tr.appendChild(tdLabel);

      MOOD_MAP_MOODS.forEach((mood) => {
        const td = document.createElement("td");
        td.className = "sound-center";
        const checkbox = document.createElement("input");
        checkbox.type = "checkbox";
        checkbox.id = `mood-map-${mood.key}-${index}`;
        checkbox.setAttribute("aria-label", `${category.label} enabled for ${mood.label}`);
        td.appendChild(checkbox);
        tr.appendChild(td);
      });

      moodMapTbody.appendChild(tr);
    });

    syncMoodMapControlState();
  };
  const buildSystemSoundRows = () => {
    if (!systemTbody) return;
    systemTbody.innerHTML = "";
    systemDirtyTrackers.clear();

    SYSTEM_SOUNDS.forEach((sound) => {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";

      const tdLabel = document.createElement("td");
      tdLabel.textContent = sound.label;

      const tdTrack = document.createElement("td");
      const input = createNumberInput({
        id: `sys-track-input-${sound.key}`,
        min: 0,
        max: TRACK_MAX,
        className: "sound-track-input-sm",
        ariaLabel: `${sound.label} track number`,
        datasetKey: sound.key,
        placeholder: "(silent / not set)",
      });
      tdTrack.appendChild(input);

      const { tdActions, actionsWrap } = createActionCell();
      const rowFeedback = createInlineFeedback();
      const dirtyMarker = createDirtyMarker();
      const dirtyTracker = createRowDirtyTracker({ row: tr, inputs: [input], marker: dirtyMarker });
      systemDirtyTrackers.set(sound.key, dirtyTracker);

      const saveButton = createActionButton({
        label: "💾 Save",
        title: "Save track number",
        ariaLabel: `Save ${sound.label} track number`,
        className: "btn sound-btn-compact",
        onClick: async () => {
          const value = Number.parseInt(input.value, 10);
          if (Number.isNaN(value) || value < 0 || value > TRACK_MAX) {
            showFeedback(rowFeedback, MSG.trackRangeZeroToMax, false);
            return;
          }
          const ok = await postTrack(sound.key, value, rowFeedback);
          if (ok) dirtyTracker.markSaved();
        },
      });

      const playButton = createActionButton({
        label: "▶ Play",
        title: `Play configured track for ${sound.label}`,
        ariaLabel: `Play ${sound.label}`,
        className: "btn sound-btn-play",
        onClick: () => {
          const value = Number.parseInt(input.value, 10);
          if (Number.isNaN(value) || value < 0 || value > TRACK_MAX) {
            showFeedback(rowFeedback, MSG.trackRangeZeroToMax, false);
            return;
          }
          if (value === 0) return;
          postAudio({ action: "play", track: value }, globalFb, sound.label);
        },
      });

      actionsWrap.appendChild(saveButton);
      actionsWrap.appendChild(playButton);
      tdActions.appendChild(dirtyMarker);
      tdActions.appendChild(rowFeedback);

      tr.appendChild(tdLabel);
      tr.appendChild(tdTrack);
      tr.appendChild(tdActions);
      systemTbody.appendChild(tr);
    });
  };

  const loadTracks = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/audio/tracks", { timeoutMs: 3000 });
      const data = result.data;

      NAMED_SOUNDS.forEach((sound) => {
        if (!sound.editable || !sound.key) return;
        const input = document.getElementById(`track-input-${sound.key}`);
        if (input && data[sound.key] !== undefined) {
          input.value = data[sound.key];
        }
        namedDirtyTrackers.get(sound.key)?.markSaved();
      });

      SYSTEM_SOUNDS.forEach((sound) => {
        const input = document.getElementById(`sys-track-input-${sound.key}`);
        if (input && data[sound.key] !== undefined) {
          input.value = data[sound.key];
        }
        systemDirtyTrackers.get(sound.key)?.markSaved();
      });

      CATEGORY_SOUNDS.forEach((category) => {
        const minInput = document.getElementById(`cat-min-${category.loKey}`);
        const maxInput = document.getElementById(`cat-max-${category.hiKey}`);
        if (minInput && data[category.loKey] !== undefined) {
          minInput.value = data[category.loKey];
        }
        if (maxInput && data[category.hiKey] !== undefined) {
          maxInput.value = data[category.hiKey];
        }
        categoryDirtyTrackers.get(category.loKey)?.markSaved();
        const playButton = findCategoryPlayButton(minInput);
        const minVal = Number.parseInt(minInput?.value, 10);
        const maxVal = Number.parseInt(maxInput?.value, 10);
        updateCategoryTestButtonState(playButton, minVal, maxVal);
      });

      const randMin = document.getElementById("rand-min");
      const randMax = document.getElementById("rand-max");
      if (randMin && data.rand_min !== undefined) randMin.value = data.rand_min;
      if (randMax && data.rand_max !== undefined) randMax.value = data.rand_max;

      const intQuiet = document.getElementById("int-quiet");
      const intMid = document.getElementById("int-mid");
      const intFull = document.getElementById("int-full");
      const intAwake = document.getElementById("int-awake");
      if (intQuiet && data.snd_int_quiet !== undefined) intQuiet.value = data.snd_int_quiet;
      if (intMid && data.snd_int_mid !== undefined) intMid.value = data.snd_int_mid;
      if (intFull && data.snd_int_full !== undefined) intFull.value = data.snd_int_full;
      if (intAwake && data.snd_int_awake !== undefined) intAwake.value = data.snd_int_awake;

      if (volSlider && data.volume !== undefined) {
        volSlider.value = data.volume;
        syncVolumeLabel();
      }
    } catch (_error) {
      // No dedicated feedback surface for initial track hydration.
    }
  };

  const loadMoodMap = async () => {
    if (!window.PAApi) return false;
    try {
      const result = await window.PAApi.get("/api/audio/mood-map", { timeoutMs: 3000 });
      const data = result.data || {};

      MOOD_MAP_MOODS.forEach((mood) => {
        const raw = Number.parseInt(data[mood.key], 10);
        const fallback = MOOD_MAP_DEFAULTS[mood.key] ?? 0;
        const mask = Number.isNaN(raw) ? fallback : (raw & 0x0FFF);
        decodeMoodMaskToUi(mood.key, mask);
      });

      moodMapApiAvailable = true;
      moodMapLoaded = true;
      setMoodMapStatus("", true);
    } catch (error) {
      const endpointMissing =
        error instanceof window.PAApi.ApiError && error.kind === "http" && error.status === 404;
      if (endpointMissing) {
        MOOD_MAP_MOODS.forEach((mood) => {
          const fallback = MOOD_MAP_DEFAULTS[mood.key] ?? 0;
          decodeMoodMaskToUi(mood.key, fallback);
        });
        moodMapApiAvailable = false;
        moodMapLoaded = false;
        setMoodMapStatus("Mood mapping unavailable", false);
      } else {
        moodMapApiAvailable = true;
        setMoodMapStatus(`Mood mapping load failed: ${window.PAApi.messageFor(error)}`, false);
      }
    }
    syncMoodMapControlState();
    return moodMapLoaded;
  };

  const renderStatus = (data) => {
    const s2Enabled = Boolean(data.s2Sound);
    setSoundHardwareEnabled(s2Enabled);
    // Note: the sound-state-badge is intentionally NOT updated here from
    // data.s2Sound.state — that field reflects firmware's guess (audioActive),
    // not confirmed module state. Badge is updated by updateModuleStatus()
    // which uses the real module-reported play state from GET /api/audio.
    if (!soundStateBadge) return;
    if (!s2Enabled) {
      soundStateBadge.textContent = "Disabled";
      soundStateBadge.dataset.state = "disabled";
    }
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
  };

  buildNamedSoundRows();
  buildCategorySoundRows();
  buildMoodMapRows();
  buildSystemSoundRows();
  applySoundWorkspaceMode(loadSoundWorkspaceMode(), false);
  loadTracks();
  loadMoodMap();
  syncVolumeLabel();

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderStatus(payload);
    });

    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatusOnce().catch(() => {
        // Retry via next SSE event.
      });
    }
  } else {
    const refreshFromFallback = () => {
      refreshStatusOnce().catch(() => {
        // Retry next cycle.
      });
    };

    refreshFromFallback();
    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshFromFallback();
    }, 2000);

    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshFromFallback();
      }
    });
  }

  namedSoundFilterInput?.addEventListener("input", () => {
    applyNamedSoundFilter();
  });

  soundModeAdvancedBtn?.addEventListener("click", () => {
    applySoundWorkspaceMode(SOUND_VIEW_MODE_ADVANCED);
  });

  soundModeCompactBtn?.addEventListener("click", () => {
    applySoundWorkspaceMode(SOUND_VIEW_MODE_COMPACT);
  });
  volSlider?.addEventListener("input", syncVolumeLabel);
  volSlider?.addEventListener("change", () => {
    postAudio({ action: "volume", level: volSlider.value }, globalFb, 'Volume');
  });

  document.getElementById("btn-stop")
    ?.addEventListener("click", () => postAudio({ action: "stop" }, globalFb, 'Stop'));

  document.getElementById("btn-random-on")
    ?.addEventListener("click", () => postAudio({ action: "dollar", cmd: "$R" }, globalFb, 'Random mode on'));

  document.getElementById("btn-random-off")
    ?.addEventListener("click", () => postAudio({ action: "dollar", cmd: "$O" }, globalFb, 'Random mode off'));
  const directFb = document.getElementById("direct-feedback");
  document.getElementById("btn-direct-play")?.addEventListener("click", () => {
    const value = Number.parseInt(document.getElementById("direct-track")?.value, 10);
    if (!soundHardwareEnabled) {
      showFeedback(directFb, "Direct play unavailable: enable S2 — Sound in Setup.", false);
      return;
    }
    if (!value || value < 1 || value > 65535) {
      showFeedback(directFb, "Track must be 1–65535", false);
      return;
    }
    postAudio({ action: "play", track: value }, directFb, `Track ${value}`);
  });

  const randFb = document.getElementById("rand-feedback");
  document.getElementById("btn-rand-save")?.addEventListener("click", async () => {
    const minVal = Number.parseInt(document.getElementById("rand-min")?.value, 10);
    if (!soundHardwareEnabled) {
      showFeedback(randFb, "Random range unavailable: enable S2 — Sound in Setup.", false);
      return;
    }
    const maxVal = Number.parseInt(document.getElementById("rand-max")?.value, 10);

    if (!minVal || minVal < 1 || minVal > TRACK_MAX || !maxVal || maxVal < 1 || maxVal > TRACK_MAX) {
      showFeedback(randFb, MSG.valuesMustBe1To999, false);
      return;
    }
    if (minVal > maxVal) {
      showFeedback(randFb, MSG.minMustBeLeMax, false);
      return;
    }

    if (!window.PAApi) return;
    try {
      const [r1, r2] = await Promise.all([
        window.PAApi.postForm("/api/audio/tracks", { key: "rand_min", track: minVal }, { timeoutMs: 3000 }),
        window.PAApi.postForm("/api/audio/tracks", { key: "rand_max", track: maxVal }, { timeoutMs: 3000 }),
      ]);
      const ok = Boolean(r1.data?.ok && r2.data?.ok);
      showFeedback(randFb, ok ? "Range saved" : MSG.saveFailed, ok);
    } catch (error) {
      feedbackSaveFailure(randFb, error);
    }
  });

  const intFb = document.getElementById("int-feedback");
  const INT_FIELDS = [
    { id: "int-quiet", key: "snd_int_quiet" },
    { id: "int-mid", key: "snd_int_mid" },
    { id: "int-full", key: "snd_int_full" },
    { id: "int-awake", key: "snd_int_awake" },
  ];

  document.getElementById("btn-int-save")?.addEventListener("click", async () => {
    if (!soundHardwareEnabled) {
      showFeedback(intFb, "Interval updates unavailable: enable S2 — Sound in Setup.", false);
      return;
    }
    for (const field of INT_FIELDS) {
      const value = Number.parseInt(document.getElementById(field.id)?.value, 10);
      if (Number.isNaN(value) || value < 0 || value > 3600) {
        showFeedback(intFb, `${field.key}: must be 0–3600`, false);
        return;
      }
    }

    if (!window.PAApi) return;
    try {
      const results = await Promise.all(INT_FIELDS.map((field) => {
        const value = Number.parseInt(document.getElementById(field.id)?.value, 10);
        return window.PAApi.postForm("/api/audio/tracks", { key: field.key, track: value }, { timeoutMs: 3000 });
      }));
      const ok = results.every((entry) => entry.data?.ok);
      showFeedback(intFb, ok ? "Intervals saved" : MSG.saveFailed, ok);
    } catch (error) {
      feedbackSaveFailure(intFb, error);
    }
  });

  moodMapSaveBtn?.addEventListener("click", async () => {
    if (!soundHardwareEnabled) {
      setMoodMapStatus("Mood mapping unavailable: enable S2 — Sound in Setup.", false);
      return;
    }
    if (!moodMapApiAvailable) {
      setMoodMapStatus("Mood mapping unavailable", false);
      return;
    }
    if (!moodMapLoaded && moodMapApiAvailable) {
      await loadMoodMap();
    }
    if (!moodMapLoaded) {
      setMoodMapStatus("Mood mapping not loaded yet; retry after connection recovers.", false);
      return;
    }
    if (!window.PAApi) return;

    const payload = {};
    MOOD_MAP_MOODS.forEach((mood) => {
      payload[mood.key] = encodeMoodMaskFromUi(mood.key);
    });

    try {
      const result = await window.PAApi.postForm("/api/audio/mood-map", payload, { timeoutMs: 3000 });
      const ok = Boolean(result.data?.ok);
      setMoodMapStatus(ok ? "Mood mapping saved" : (result.data?.error || MSG.saveFailed), ok);
    } catch (error) {
      setMoodMapStatus(`Mood mapping save failed: ${getApiErrorMessage(error)}`, false);
    }
    syncMoodMapControlState();
  });

  // Initial module status fetch from cached data (no UART query on load)
  updateModuleStatus();
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState !== "hidden") {
      updateModuleStatus();
    }
  });

  // Poll button — sends a POST /api/audio/query which runs queryModuleState()
  // in AudioTask, then re-fetches /api/audio after 1.5 s to show the result.
  // For manual-poll backends, only poll while not playing to avoid UART disruption.
  if (btnPoll) {
    btnPoll.addEventListener("click", async () => {
      btnPoll.disabled = true;
      btnPoll.textContent = "Polling…";
      try {
        await window.PAApi.postForm("/api/audio/query", {});
        window.setTimeout(() => {
          updateModuleStatus()
            .then(() => {
              showFeedback(modStatusFb, "Status updated", true);
            })
            .catch(() => {
              showFeedback(modStatusFb, "Fetch failed after poll", false);
            })
            .finally(() => {
              btnPoll.disabled = false;
              btnPoll.textContent = "Poll status";
            });
        }, 1600);
      } catch (err) {
        btnPoll.disabled = false;
        btnPoll.textContent = "Poll status";
        showFeedback(modStatusFb, `Poll failed: ${window.PAApi.messageFor(err)}`, false);
      }
    });
  }
})();
