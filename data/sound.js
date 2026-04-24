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
    { label: "Boot Sound ($B)", cmd: "$B", key: "startup", editable: true },
    { label: "Random On", cmd: "$R", key: null, editable: false },
    { label: "Random Off", cmd: "$O", key: null, editable: false },
    { label: "Stop / Chatter Off", cmd: "$s", key: null, editable: false },
  ];

  const SYSTEM_SOUNDS = [
    { label: "Boot Complete (auto)", key: "sys_boot" },
    { label: "Mode → Normal", key: "sys_mode_n" },
    { label: "Mode → Slow", key: "sys_mode_s" },
    { label: "Mode → Turbo", key: "sys_mode_t" },
    { label: "Drives engaged", key: "sys_drv_on" },
    { label: "Dome enabled", key: "sys_dome_on" },
  ];

  const NAMED_SLOT_TARGETS = NAMED_SOUNDS
    .filter((sound) => sound.editable && Boolean(sound.key))
    .map((sound) => ({
      key: sound.key,
      label: `${sound.label} (${sound.key})`,
    }));
  const SYSTEM_SLOT_TARGETS = SYSTEM_SOUNDS.map((sound) => ({
    key: sound.key,
    label: `${sound.label} (${sound.key})`,
  }));
  const SLOT_BINDING_TARGETS = [
    ...NAMED_SLOT_TARGETS.map((target) => ({ key: target.key, label: `Named · ${target.label}` })),
    ...SYSTEM_SLOT_TARGETS.map((target) => ({ key: target.key, label: `System · ${target.label}` })),
  ];
  const CATEGORY_SOUNDS = [
    {
      label: "General",
      loKey: "snd_cat_gen_lo",
      hiKey: "snd_cat_gen_hi",
      hint: "Bank 1 (1A_general) uses tracks 1-24.",
    },
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
  const SLOT_TARGET_PREFIX = "slot:";
  const CATEGORY_TARGET_PREFIX = "category:";
  const CATALOG_MAP_TARGETS = [
    ...NAMED_SLOT_TARGETS.map((target) => ({
      value: `${SLOT_TARGET_PREFIX}${target.key}`,
      label: target.label,
      group: "Map to named slot",
    })),
    ...SYSTEM_SLOT_TARGETS.map((target) => ({
      value: `${SLOT_TARGET_PREFIX}${target.key}`,
      label: target.label,
      group: "Map to system slot",
    })),
    ...CATEGORY_SOUNDS.map((category) => ({
      value: `${CATEGORY_TARGET_PREFIX}${category.loKey}`,
      label: `Category · ${category.label}`,
      group: "Map to category",
    })),
  ];
  const SLOT_TARGET_LABEL_BY_KEY = Object.fromEntries(
    SLOT_BINDING_TARGETS.map((target) => [target.key, target.label])
  );
  const CATEGORY_BY_LO_KEY = Object.fromEntries(
    CATEGORY_SOUNDS.map((category) => [category.loKey, category])
  );
  const CATEGORY_SUGGESTION_KEYWORDS = {
    snd_cat_gen_lo: ["general", "beep", "boop"],
    snd_cat_chat_lo: ["chatty", "chat"],
    snd_cat_hap_lo: ["happy"],
    snd_cat_proc_lo: ["processing", "process"],
    snd_cat_sad_lo: ["sad"],
    snd_cat_sent_lo: ["sentimental"],
    snd_cat_hum_lo: ["humming", "hum"],
    snd_cat_scrm_lo: ["scream"],
    snd_cat_ooh_lo: ["surprised", "ooh"],
    snd_cat_alrm_lo: ["alert", "alarm"],
    snd_cat_snrk_lo: ["snarky", "pfft", "razzberry"],
    snd_cat_whis_lo: ["whistle"],
  };
  const MOOD_MAP_MOODS = [
    { key: "quiet", label: "Quiet 🤐" },
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
  const AUDIO_CAP_CURRENT_TRACK = 0x08;
  const AUDIO_CAP_QUERY_SAFE_PLAYING = 0x10;

  const AUDIO_CAP_CATALOG = 0x20;
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

  const SOUND_UI_ALWAYS_ENABLED_IDS = new Set([
    "sound-mode-advanced",
    "sound-mode-compact",
    "named-sound-filter",
    "btn-poll-status",
  ]);
  const feedbackTimers = new WeakMap();
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
  const chirpCatalogCard = document.getElementById("chirp-catalog-card");
  const catalogRows = document.getElementById("catalog-rows");
  const catalogStatus = document.getElementById("catalog-status");
  const catalogFeedback = document.getElementById("catalog-feedback");
  const catalogFilterInput = document.getElementById("catalog-filter");
  const catalogBankTabs = document.getElementById("catalog-bank-tabs");
  const catalogRefreshBtn = document.getElementById("btn-catalog-refresh");
  const catalogSuggestBtn = document.getElementById("btn-catalog-apply-suggestions");
  const catalogBulkToggleBtn = document.getElementById("btn-catalog-bulk");
  const catalogBulkBar = document.getElementById("catalog-bulk-bar");
  const catalogBulkCount = document.getElementById("catalog-bulk-count");
  const catalogBulkTarget = document.getElementById("catalog-bulk-target");
  const catalogBulkMapBtn = document.getElementById("btn-catalog-bulk-map");
  const catalogBulkClearBtn = document.getElementById("btn-catalog-bulk-clear");
  const catalogBulkCancelBtn = document.getElementById("btn-catalog-bulk-cancel");
  const catalogSelectAll = document.getElementById("catalog-select-all");
  const catalogSelectCol = document.getElementById("catalog-col-select");
  let lastCapabilities = null; // null = not yet received
  let moduleStatusRefreshTimer = null;
  let catalogSupported = false;
  let catalogReady = false;
  let catalogBanks = [];
  let catalogEntries = [];
  let catalogBankFilter = 0;
  let catalogFetchInFlight = false;
  let catalogRefreshInFlight = false;
  let catalogAutoLoadAttempted = false;
  let catalogBulkMode = false;
  let chirpBindings = {};
  let chirpCategoryBindings = {};
  const catalogSelectedKeys = new Set();
  let catalogCategoryRanges = [];
  let catalogSuggestedCategoryMappings = [];

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
        : "";
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
    const supportsCatalog = (caps & AUDIO_CAP_CATALOG) !== 0;
    const showManualPoll = supportsStatusQuery && !supportsSafePlayingQuery;

    catalogSupported = supportsCatalog;
    setElementVisible(chirpCatalogCard, supportsCatalog);
    if (!supportsCatalog) {
      catalogReady = false;
      catalogBanks = [];
      catalogEntries = [];
      catalogBankFilter = 0;
      catalogAutoLoadAttempted = false;
      catalogBulkMode = false;
      catalogSelectedKeys.clear();
      chirpCategoryBindings = {};
      catalogSuggestedCategoryMappings = [];
      if (catalogRows) catalogRows.innerHTML = "";
      if (catalogBankTabs) catalogBankTabs.innerHTML = "";
      if (catalogBulkTarget) catalogBulkTarget.value = "";
      if (catalogStatus) catalogStatus.textContent = "Catalog unavailable for this backend.";
    } else if (!catalogReady && catalogEntries.length === 0 && !catalogAutoLoadAttempted) {
      catalogAutoLoadAttempted = true;
      if (catalogStatus && !catalogStatus.textContent) {
        catalogStatus.textContent = "Catalog not loaded yet. Click Refresh Catalog.";
      }
      loadCatalog();
    }
    syncCatalogBulkUi();
    applyChirpBindingBadges();

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
      modQueryNote.textContent = "";
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

  const showFeedback = (el, msg, ok, timeoutMs = 2500) => {
    if (!el) return;
    const priorTimer = feedbackTimers.get(el);
    if (priorTimer) {
      window.clearTimeout(priorTimer);
      feedbackTimers.delete(el);
    }
    if (!el.dataset.baseClass) {
      el.dataset.baseClass = el.className || "feedback";
    }
    el.textContent = msg;
    el.className = `${el.dataset.baseClass} ${ok ? "success" : "error"}`;
    if (timeoutMs <= 0) {
      return;
    }
    const timer = window.setTimeout(() => {
      el.textContent = "";
      el.className = el.dataset.baseClass || "feedback";
      feedbackTimers.delete(el);
    }, timeoutMs);
    feedbackTimers.set(el, timer);
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
      if (SOUND_UI_ALWAYS_ENABLED_IDS.has(control.id)) return;
      control.disabled = !enabled;
      if (control.tagName === "BUTTON") {
        control.setAttribute("aria-disabled", enabled ? "false" : "true");
      }
    });
    if (enabled) {
      refreshCategoryTestButtons();
    }
    syncMoodMapControlState();
    syncCatalogBulkUi();
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

  const postTrack = async (key, track, feedbackEl, binding = null) => {
    if (!window.PAApi) return false;
    if (!soundHardwareEnabled) {
      showFeedback(feedbackEl || globalFb, "Track updates unavailable: enable S2 — Sound in Setup.", false);
      return false;
    }
    try {
      const payload = { key, track };
      if (binding?.bank && binding?.page) {
        payload.bank = binding.bank;
        payload.page = binding.page;
      }
      const result = await window.PAApi.postForm("/api/audio/tracks", payload, { timeoutMs: 3000 });
      return feedbackFromSaveResponse(feedbackEl, result);
    } catch (error) {
      feedbackSaveFailure(feedbackEl, error);
      return false;
    }
  };

  const findCategoryByLoKey = (loKey) =>
    CATEGORY_SOUNDS.find((category) => category.loKey === loKey) || null;

  const postCategoryRange = async (
    loKey,
    hiKey,
    lo,
    hi,
    feedbackEl,
    binding = null,
    quiet = false,
    clearBinding = false
  ) => {
    if (!window.PAApi) return false;
    if (!soundHardwareEnabled) {
      if (!quiet) {
        showFeedback(feedbackEl || globalFb, "Category updates unavailable: enable S2 — Sound in Setup.", false);
      }
      return false;
    }
    try {
      const payload = { lo_key: loKey, hi_key: hiKey, lo, hi };
      if (binding?.bank && binding?.page) {
        payload.bank = binding.bank;
        payload.page = binding.page;
      }
      if (clearBinding) {
        payload.clear_binding = 1;
      }
      const result = await window.PAApi.postForm(
        "/api/audio/category-range",
        payload,
        { timeoutMs: 3000 }
      );
      if (quiet) return Boolean(result.data?.ok);
      return feedbackFromSaveResponse(feedbackEl || globalFb, result);
    } catch (error) {
      if (!quiet) feedbackSaveFailure(feedbackEl || globalFb, error);
      return false;
    }
  };

  const postPlayBanked = async (bank, page, index, feedbackEl, label = "Catalog") => {
    if (!window.PAApi) return false;
    if (!soundHardwareEnabled) {
      showFeedback(feedbackEl || globalFb, "Playback unavailable: enable S2 — Sound in Setup.", false);
      return false;
    }
    try {
      const result = await window.PAApi.postForm(
        "/api/audio/play-banked",
        { bank, page, index },
        { timeoutMs: 3000 }
      );
      const ok = Boolean(result.data?.ok);
      showFeedback(feedbackEl || globalFb, ok ? `${label} played` : (result.data?.error || "Failed"), ok);
      return ok;
    } catch (error) {
      showFeedback(feedbackEl || globalFb, `Playback failed: ${getApiErrorMessage(error)}`, false);
      return false;
    }
  };

  const getSlotBinding = (key) => {
    const raw = chirpBindings?.[key];
    if (!raw) return null;
    const bank = Number.parseInt(raw.bank, 10);
    const index = Number.parseInt(raw.index, 10);
    const page = String(raw.page ?? "").trim().toUpperCase();
    if (!Number.isFinite(bank) || !Number.isFinite(index) || bank < 1 || index < 1 || page.length !== 1) {
      return null;
    }
    return { bank, page, index };
  };

  const formatBindingLabel = (binding) => `CHIRP B${binding.bank}${binding.page} #${binding.index}`;

  const playMappedSlot = async (key, fallbackTrack, feedbackEl, label) => {
    const binding = getSlotBinding(key);
    if (catalogSupported && binding) {
      return postPlayBanked(binding.bank, binding.page, binding.index, feedbackEl, label);
    }
    return postAudio({ action: "play", track: fallbackTrack }, feedbackEl || globalFb, label);
  };

  const applyChirpBindingBadges = () => {
    const keys = new Set(SLOT_BINDING_TARGETS.map((entry) => entry.key));
    keys.forEach((key) => {
      const badge = document.getElementById(`chirp-binding-${key}`);
      if (!badge) return;
      const binding = getSlotBinding(key);
      if (!catalogSupported || !binding) {
        badge.textContent = "";
        badge.classList.add("hidden");
        return;
      }
      badge.textContent = formatBindingLabel(binding);
      badge.classList.remove("hidden");
    });
  };

  const catalogEntryKey = (entry) => {
    const bank = Number.parseInt(String(entry?.bank ?? "0"), 10);
    const index = Number.parseInt(String(entry?.index ?? "0"), 10);
    const page = String(entry?.page ?? "A").trim().toUpperCase() || "A";
    return `${Number.isFinite(bank) ? bank : 0}:${page}:${Number.isFinite(index) ? index : 0}`;
  };

  const populateCatalogTargetSelect = (select, placeholderText = "Choose target…") => {
    if (!select) return;
    const currentValue = select.value;
    select.innerHTML = "";

    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = placeholderText;
    select.appendChild(placeholder);

    const groups = new Map();
    CATALOG_MAP_TARGETS.forEach((target) => {
      if (!groups.has(target.group)) groups.set(target.group, []);
      groups.get(target.group).push(target);
    });

    groups.forEach((targets, groupLabel) => {
      const optGroup = document.createElement("optgroup");
      optGroup.label = groupLabel;
      targets.forEach((target) => {
        const option = document.createElement("option");
        option.value = target.value;
        option.textContent = target.label;
        optGroup.appendChild(option);
      });
      select.appendChild(optGroup);
    });

    if (currentValue) {
      select.value = currentValue;
    }
  };

  const getCurrentConfiguredTrack = (key) => {
    if (!key) return null;
    const namedInput = document.getElementById(`track-input-${key}`);
    const systemInput = document.getElementById(`sys-track-input-${key}`);
    const raw = namedInput?.value ?? systemInput?.value ?? null;
    const value = Number.parseInt(String(raw ?? ""), 10);
    if (!Number.isFinite(value) || value < 0 || value > TRACK_MAX) return null;
    return value;
  };

  const mapCatalogEntriesToTarget = async (entries, target, feedbackEl) => {
    if (!target) {
      showFeedback(feedbackEl || catalogFeedback, "Select a target before mapping.", false);
      return false;
    }

    if (!Array.isArray(entries) || entries.length === 0) {
      showFeedback(feedbackEl || catalogFeedback, "Select at least one catalog entry.", false);
      return false;
    }

    if (target.startsWith(SLOT_TARGET_PREFIX)) {
      if (entries.length !== 1) {
        showFeedback(
          feedbackEl || catalogFeedback,
          "Slot targets accept one sound at a time. Use a Category target for multi-select mapping.",
          false
        );
        return false;
      }
      const entry = entries[0];
      const index = Number.parseInt(String(entry.index), 10);
      const bank = Number.parseInt(String(entry.bank), 10);
      const page = String(entry.page || "A").trim().toUpperCase();
      if (Number.isNaN(index) || index < 1 || Number.isNaN(bank) || bank < 1 || page.length !== 1) {
        showFeedback(feedbackEl || catalogFeedback, "Catalog entry is invalid.", false);
        return false;
      }
      const key = target.slice(SLOT_TARGET_PREFIX.length);
      return postTrack(key, index, feedbackEl || catalogFeedback, { bank, page });
    }

    if (target.startsWith(CATEGORY_TARGET_PREFIX)) {
      const loKey = target.slice(CATEGORY_TARGET_PREFIX.length);
      const category = findCategoryByLoKey(loKey);
      if (!category) {
        showFeedback(feedbackEl || catalogFeedback, "Unknown category target.", false);
        return false;
      }
      let minIndex = Number.MAX_SAFE_INTEGER;
      let maxIndex = 0;
      let selectedBank = 0;
      let selectedPage = "A";
      for (const entry of entries) {
        const index = Number.parseInt(String(entry.index), 10);
        const bank = Number.parseInt(String(entry.bank), 10);
        const page = String(entry.page || "A").trim().toUpperCase();
        if (Number.isNaN(bank) || bank < 1 || page.length !== 1) {
          showFeedback(feedbackEl || catalogFeedback, "Catalog entry is invalid.", false);
          return false;
        }
        if (selectedBank === 0) {
          selectedBank = bank;
          selectedPage = page;
        } else if (selectedBank !== bank || selectedPage !== page) {
          showFeedback(
            feedbackEl || catalogFeedback,
            "Category mapping requires selected rows from the same bank/page.",
            false
          );
          return false;
        }
        if (Number.isNaN(index) || index < 1 || index > TRACK_MAX) {
          showFeedback(feedbackEl || catalogFeedback, `Category mapping requires index 1-${TRACK_MAX}.`, false);
          return false;
        }
        if (index < minIndex) minIndex = index;
        if (index > maxIndex) maxIndex = index;
      }
      return postCategoryRange(
        category.loKey,
        category.hiKey,
        minIndex,
        maxIndex,
        feedbackEl || catalogFeedback,
        { bank: selectedBank, page: selectedPage }
      );
    }

    showFeedback(feedbackEl || catalogFeedback, "Unknown mapping target.", false);
    return false;
  };

  const clearCatalogTargetMapping = async (target, feedbackEl) => {
    if (!target) {
      showFeedback(feedbackEl || catalogFeedback, "Select a target before clearing.", false);
      return false;
    }

    if (target.startsWith(SLOT_TARGET_PREFIX)) {
      const key = target.slice(SLOT_TARGET_PREFIX.length);
      const currentTrack = getCurrentConfiguredTrack(key);
      if (currentTrack === null) {
        showFeedback(feedbackEl || catalogFeedback, "Cannot clear slot mapping: track value is unavailable.", false);
        return false;
      }
      return postTrack(key, currentTrack, feedbackEl || catalogFeedback);
    }

    if (target.startsWith(CATEGORY_TARGET_PREFIX)) {
      const loKey = target.slice(CATEGORY_TARGET_PREFIX.length);
      const category = findCategoryByLoKey(loKey);
      if (!category) {
        showFeedback(feedbackEl || catalogFeedback, "Unknown category target.", false);
        return false;
      }
      return postCategoryRange(
        category.loKey,
        category.hiKey,
        0,
        0,
        feedbackEl || catalogFeedback,
        null,
        false,
        true
      );
    }

    showFeedback(feedbackEl || catalogFeedback, "Unknown clear target.", false);
    return false;
  };

  const getCatalogMappedTargets = (entry) => {
    const mapped = [];
    const entryBank = Number.parseInt(String(entry?.bank ?? "0"), 10);
    const entryIndex = Number.parseInt(String(entry?.index ?? "0"), 10);
    const entryPage = String(entry?.page ?? "A").trim().toUpperCase();
    if (Number.isNaN(entryBank) || Number.isNaN(entryIndex) || entryBank < 1 || entryIndex < 1 || entryPage.length !== 1) {
      return mapped;
    }

    Object.entries(chirpBindings || {}).forEach(([slotKey, raw]) => {
      const bank = Number.parseInt(raw?.bank, 10);
      const index = Number.parseInt(raw?.index, 10);
      const page = String(raw?.page ?? "").trim().toUpperCase();
      if (bank === entryBank && index === entryIndex && page === entryPage) {
        mapped.push({
          kind: "slot",
          slotKey,
          label: SLOT_TARGET_LABEL_BY_KEY[slotKey] || `Slot · ${slotKey}`,
        });
      }
    });

    catalogCategoryRanges.forEach((range) => {
      const binding = chirpCategoryBindings?.[range.loKey] || null;
      const boundBank = Number.parseInt(binding?.bank, 10);
      const boundPage = String(binding?.page ?? "").trim().toUpperCase();
      const effectiveBank = Number.isFinite(boundBank) && boundBank >= 1 ? boundBank : 1;
      const effectivePage = boundPage.length === 1 ? boundPage : "A";
      if (entryBank === effectiveBank && entryPage === effectivePage &&
          entryIndex >= range.lo && entryIndex <= range.hi) {
        mapped.push({
          kind: "category",
          loKey: range.loKey,
          label: `Category · ${range.label}`,
        });
      }
    });
    return mapped;
  };

  const catalogMappedTargetValue = (mappedTarget) => (
    mappedTarget.kind === "slot"
      ? `${SLOT_TARGET_PREFIX}${mappedTarget.slotKey}`
      : `${CATEGORY_TARGET_PREFIX}${mappedTarget.loKey}`
  );

  const clearCatalogEntryMappings = async (entry, feedbackEl) => {
    const mappedTargets = getCatalogMappedTargets(entry);
    if (!mappedTargets.length) {
      showFeedback(feedbackEl || catalogFeedback, "No CHIRP mappings found for this sound.", false);
      return false;
    }
    let cleared = 0;
    for (const mappedTarget of mappedTargets) {
      const ok = await clearCatalogTargetMapping(catalogMappedTargetValue(mappedTarget), feedbackEl || catalogFeedback);
      if (ok) cleared += 1;
    }
    return cleared > 0;
  };

  const buildMappedTargetsElement = (entry) => {
    const mappedTargets = getCatalogMappedTargets(entry);
    if (!mappedTargets.length) return null;
    const wrap = document.createElement("div");
    wrap.className = "catalog-mapped-tags";
    mappedTargets.forEach((mappedTarget) => {
      const targetValue = catalogMappedTargetValue(mappedTarget);
      const badge = document.createElement("button");
      badge.type = "button";
      badge.className = "catalog-mapped-tag";
      badge.textContent = `${mappedTarget.label} ×`;
      badge.title = `Clear ${mappedTarget.label} mapping`;
      badge.setAttribute("aria-label", `Clear ${mappedTarget.label} mapping`);
      badge.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
      badge.setAttribute("aria-disabled", badge.disabled ? "true" : "false");
      badge.addEventListener("click", async () => {
        if (catalogRefreshInFlight || !soundHardwareEnabled) return;
        const ok = await clearCatalogTargetMapping(targetValue, catalogFeedback);
        if (ok) await loadTracks();
      });
      wrap.appendChild(badge);
    });
    return wrap;
  };


  const normalizeCatalogDirLabel = (value) =>
    String(value ?? "").toLowerCase().replace(/[^a-z0-9]+/g, " ").trim();

  const guessCategoryLoKeyForDirName = (dirName) => {
    const normalized = normalizeCatalogDirLabel(dirName);
    if (!normalized) return null;
    const compact = normalized.replace(/\s+/g, "");
    for (const [loKey, keywords] of Object.entries(CATEGORY_SUGGESTION_KEYWORDS)) {
      if (keywords.some((token) => normalized.includes(token) || compact.includes(token.replace(/\s+/g, "")))) {
        return loKey;
      }
    }
    return null;
  };

  const buildSuggestedCategoryMappings = () => {
    const suggestions = [];
    const usedLoKeys = new Set();
    if (!catalogSupported || !catalogReady) return suggestions;

    const safeEntries = Array.isArray(catalogEntries) ? catalogEntries : [];
    const safeBanks = Array.isArray(catalogBanks) ? catalogBanks : [];

    safeBanks.forEach((bankRow) => {
      const loKey = guessCategoryLoKeyForDirName(bankRow?.dir);
      if (!loKey || usedLoKeys.has(loKey)) return;
      const category = CATEGORY_BY_LO_KEY[loKey];
      if (!category) return;

      const bank = Number.parseInt(String(bankRow?.bank ?? "0"), 10);
      const page = String(bankRow?.page ?? "A").trim().toUpperCase();
      if (!Number.isFinite(bank) || bank < 1 || page.length !== 1) return;

      const indexes = safeEntries
        .filter((entry) => Number.parseInt(String(entry?.bank ?? "0"), 10) === bank &&
                         String(entry?.page ?? "A").trim().toUpperCase() === page)
        .map((entry) => Number.parseInt(String(entry?.index ?? "0"), 10))
        .filter((index) => Number.isFinite(index) && index >= 1 && index <= TRACK_MAX);

      let lo = 0;
      let hi = 0;
      if (indexes.length > 0) {
        lo = Math.min(...indexes);
        hi = Math.max(...indexes);
      } else {
        const count = Number.parseInt(String(bankRow?.count ?? "0"), 10);
        if (!Number.isFinite(count) || count < 1) return;
        lo = 1;
        hi = Math.min(count, TRACK_MAX);
      }
      if (lo < 1 || hi < lo) return;

      suggestions.push({
        loKey,
        hiKey: category.hiKey,
        label: category.label,
        bank,
        page,
        lo,
        hi,
        sourceDir: String(bankRow?.dir ?? ""),
      });
      usedLoKeys.add(loKey);
    });

    return suggestions;
  };

  const syncCatalogSuggestionUi = () => {
    catalogSuggestedCategoryMappings = buildSuggestedCategoryMappings();
    const suggestionCount = catalogSuggestedCategoryMappings.length;

    if (catalogSuggestBtn) {
      catalogSuggestBtn.textContent = suggestionCount > 0
        ? `🧭 Apply suggestions (${suggestionCount})`
        : "🧭 Apply suggestions";
      const enabled = catalogSupported && catalogReady && soundHardwareEnabled && !catalogRefreshInFlight && suggestionCount > 0;
      catalogSuggestBtn.disabled = !enabled;
      catalogSuggestBtn.setAttribute("aria-disabled", enabled ? "false" : "true");
    }

    if (catalogStatus && catalogReady && !catalogRefreshInFlight) {
      const baseText = catalogStatus.dataset.baseText || catalogStatus.textContent || "";
      if (suggestionCount > 0) {
        catalogStatus.textContent = `${baseText} ${suggestionCount} suggestion(s) ready.`.trim();
      } else {
        catalogStatus.textContent = baseText;
      }
    }
  };

  const applySuggestedCategoryMappings = async () => {
    if (!catalogSupported || !catalogReady) {
      showFeedback(catalogFeedback, "Load catalog before applying suggestions.", false);
      return false;
    }
    if (catalogRefreshInFlight) {
      showFeedback(catalogFeedback, "Wait for catalog refresh to finish.", false);
      return false;
    }

    const suggestions = buildSuggestedCategoryMappings();
    if (!suggestions.length) {
      showFeedback(catalogFeedback, "No category suggestions found from bank directory names.", false);
      return false;
    }

    let applied = 0;
    let failed = 0;
    for (const suggestion of suggestions) {
      const ok = await postCategoryRange(
        suggestion.loKey,
        suggestion.hiKey,
        suggestion.lo,
        suggestion.hi,
        catalogFeedback,
        { bank: suggestion.bank, page: suggestion.page },
        true
      );
      if (ok) {
        applied += 1;
      } else {
        failed += 1;
      }
    }

    if (applied > 0) {
      await loadTracks();
    }
    if (failed === 0) {
      showFeedback(catalogFeedback, `Applied ${applied} suggested category mappings.`, true);
      return true;
    }
    showFeedback(catalogFeedback, `Applied ${applied} suggestions; ${failed} failed.`, false);
    return applied > 0;
  };

  const syncCatalogBulkUi = (visibleEntries = null) => {
    const visible = Array.isArray(visibleEntries) ? visibleEntries : getVisibleCatalogEntries();
    const bulkVisible = catalogSupported && catalogBulkMode;

    setElementVisible(catalogBulkBar, bulkVisible);
    setElementVisible(catalogSelectCol, bulkVisible);

    if (catalogBulkToggleBtn) {
      catalogBulkToggleBtn.textContent = bulkVisible ? "✕ Done" : "☑ Bulk";
      catalogBulkToggleBtn.setAttribute("aria-pressed", bulkVisible ? "true" : "false");
      catalogBulkToggleBtn.disabled = !catalogSupported || catalogRefreshInFlight || !soundHardwareEnabled;
      catalogBulkToggleBtn.setAttribute("aria-disabled", catalogBulkToggleBtn.disabled ? "true" : "false");
    }

    if (catalogBulkCount) {
      catalogBulkCount.textContent = `${catalogSelectedKeys.size} selected`;
    }

    if (catalogSelectAll) {
      const visibleKeys = visible.map((entry) => catalogEntryKey(entry));
      const selectedVisibleCount = visibleKeys.filter((key) => catalogSelectedKeys.has(key)).length;
      catalogSelectAll.checked = visibleKeys.length > 0 && selectedVisibleCount === visibleKeys.length;
      catalogSelectAll.indeterminate = selectedVisibleCount > 0 && selectedVisibleCount < visibleKeys.length;
      catalogSelectAll.disabled = !bulkVisible || visibleKeys.length === 0 || catalogRefreshInFlight || !soundHardwareEnabled;
    }

    if (catalogBulkTarget) {
      catalogBulkTarget.disabled = !bulkVisible || catalogRefreshInFlight || !soundHardwareEnabled;
    }
    if (catalogBulkMapBtn) {
      const canMap = bulkVisible && soundHardwareEnabled && !catalogRefreshInFlight && catalogSelectedKeys.size > 0 && Boolean(catalogBulkTarget?.value);
      catalogBulkMapBtn.disabled = !canMap;
      catalogBulkMapBtn.setAttribute("aria-disabled", canMap ? "false" : "true");
    }
    if (catalogBulkClearBtn) {
      const canClear = bulkVisible && catalogSelectedKeys.size > 0 && !catalogRefreshInFlight;
      catalogBulkClearBtn.disabled = !canClear;
      catalogBulkClearBtn.setAttribute("aria-disabled", canClear ? "false" : "true");
    }
    if (catalogBulkCancelBtn) {
      const canCancel = bulkVisible && !catalogRefreshInFlight;
      catalogBulkCancelBtn.disabled = !canCancel;
      catalogBulkCancelBtn.setAttribute("aria-disabled", canCancel ? "false" : "true");
    }
    syncCatalogSuggestionUi();
  };

  const setCatalogBulkMode = (enabled) => {
    const next = Boolean(enabled && catalogSupported && !catalogRefreshInFlight && soundHardwareEnabled);
    if (next === catalogBulkMode) {
      syncCatalogBulkUi();
      return;
    }
    catalogBulkMode = next;
    if (!catalogBulkMode) {
      catalogSelectedKeys.clear();
      if (catalogBulkTarget) catalogBulkTarget.value = "";
      if (catalogSelectAll) {
        catalogSelectAll.checked = false;
        catalogSelectAll.indeterminate = false;
      }
    }
    renderCatalogRows();
    syncCatalogBulkUi();
  };

  const setCatalogActionLock = (locked) => {
    const refreshRunning = Boolean(locked);
    if (catalogRefreshBtn) {
      catalogRefreshBtn.disabled = refreshRunning || !soundHardwareEnabled || !catalogSupported;
      catalogRefreshBtn.setAttribute("aria-disabled", catalogRefreshBtn.disabled ? "true" : "false");
    }
    if (catalogFilterInput) {
      catalogFilterInput.disabled = refreshRunning || !catalogSupported || !soundHardwareEnabled;
    }
    if (catalogStatus && refreshRunning) {
      catalogStatus.textContent = "Refreshing catalog... this can take around 1 minute for 100+ entries.";
    }
    renderCatalogBankTabs();
    renderCatalogRows();
    syncCatalogBulkUi();
  };

  const buildCatalogTargetSelect = () => {
    const select = document.createElement("select");
    select.className = "sound-track-input-md catalog-map-select";
    select.setAttribute("aria-label", "Select mapping target");
    populateCatalogTargetSelect(select, "Choose target…");
    return select;
  };

  const renderCatalogBankTabs = () => {
    if (!catalogBankTabs) return;
    catalogBankTabs.innerHTML = "";
    if (!catalogSupported) return;

    const tabs = [{ bank: 0, label: "All banks" }, ...catalogBanks.map((bank) => ({
      bank: Number(bank.bank) || 0,
      label: `B${bank.bank}${bank.page || "A"}`
    }))];

    tabs.forEach((tab) => {
      const button = document.createElement("button");
      button.className = `btn sound-btn-compact catalog-bank-tab${catalogBankFilter === tab.bank ? " accent" : ""}`;
      button.type = "button";
      button.textContent = tab.label;
      button.setAttribute("role", "tab");
      button.setAttribute("aria-selected", catalogBankFilter === tab.bank ? "true" : "false");
      button.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
      button.setAttribute("aria-disabled", button.disabled ? "true" : "false");
      button.addEventListener("click", () => {
        if (catalogRefreshInFlight || !soundHardwareEnabled) return;
        catalogBankFilter = tab.bank;
        renderCatalogBankTabs();
        renderCatalogRows();
      });
      catalogBankTabs.appendChild(button);
    });
  };

  const getVisibleCatalogEntries = () => {
    const query = catalogFilterInput?.value?.trim().toLowerCase() ?? "";
    return catalogEntries.filter((entry) => {
      if (catalogBankFilter !== 0 && Number(entry.bank) !== catalogBankFilter) return false;
      if (!query) return true;
      const haystack = `${entry.name ?? ""} ${entry.bank ?? ""}${entry.page ?? ""} ${entry.index ?? ""}`.toLowerCase();
      return haystack.includes(query);
    });
  };

  const renderCatalogRows = () => {
    if (!catalogRows) return;
    catalogRows.innerHTML = "";

    const columnCount = catalogBulkMode ? 6 : 5;
    if (!catalogSupported) {
      syncCatalogBulkUi([]);
      return;
    }

    if (!catalogReady) {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";
      const td = document.createElement("td");
      td.colSpan = columnCount;
      td.className = "desc";
      td.textContent = "Catalog not loaded yet. Click Refresh Catalog.";
      tr.appendChild(td);
      catalogRows.appendChild(tr);
      syncCatalogBulkUi([]);
      return;
    }

    const visibleEntries = getVisibleCatalogEntries();
    if (!visibleEntries.length) {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";
      const td = document.createElement("td");
      td.colSpan = columnCount;
      td.className = "desc";
      td.textContent = "No catalog entries match the current filter.";
      tr.appendChild(td);
      catalogRows.appendChild(tr);
      syncCatalogBulkUi([]);
      return;
    }

    // All row content inserted via textContent and createElement — XSS-safe.
    visibleEntries.forEach((entry) => {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";
      const entryKey = catalogEntryKey(entry);

      const tdSelect = document.createElement("td");
      tdSelect.className = "catalog-select-col";
      if (!catalogBulkMode) {
        tdSelect.classList.add("hidden");
      } else {
        const rowSelect = document.createElement("input");
        rowSelect.type = "checkbox";
        rowSelect.className = "catalog-select-checkbox";
        rowSelect.checked = catalogSelectedKeys.has(entryKey);
        rowSelect.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
        rowSelect.setAttribute("aria-label", `Select ${entry.name || `(index ${entry.index})`}`);
        rowSelect.addEventListener("change", () => {
          if (rowSelect.checked) {
            catalogSelectedKeys.add(entryKey);
          } else {
            catalogSelectedKeys.delete(entryKey);
          }
          syncCatalogBulkUi(visibleEntries);
        });
        tdSelect.appendChild(rowSelect);
      }

      const tdBank = document.createElement("td");
      tdBank.className = "sound-mono";
      tdBank.textContent = `B${entry.bank}${entry.page}`;

      const tdIndex = document.createElement("td");
      tdIndex.className = "sound-mono";
      tdIndex.textContent = String(entry.index);

      const tdName = document.createElement("td");
      tdName.textContent = entry.name || `(index ${entry.index})`;
      const mappedTargetsEl = buildMappedTargetsElement(entry);
      if (mappedTargetsEl) {
        tdName.appendChild(mappedTargetsEl);
      }
      const tdMap = document.createElement("td");
      tdMap.className = "catalog-map-cell";
      const targetSelect = buildCatalogTargetSelect();
      targetSelect.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
      tdMap.appendChild(targetSelect);

      const tdActions = document.createElement("td");
      tdActions.className = "sound-actions-cell catalog-actions-cell";
      const actionRow = document.createElement("div");
      actionRow.className = "sound-action-row";

      const mapButton = createActionButton({
        label: "💾 Map",
        title: "Save mapping to selected target",
        ariaLabel: `Map ${entry.name || entry.index} to selected target`,
        className: "btn sound-btn-compact",
        onClick: async () => {
          if (catalogRefreshInFlight) return;
          const ok = await mapCatalogEntriesToTarget([entry], targetSelect.value, catalogFeedback);
          if (ok) await loadTracks();
        },
      });
      mapButton.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
      mapButton.setAttribute("aria-disabled", mapButton.disabled ? "true" : "false");

      const clearButton = createActionButton({
        label: "Clear",
        title: "Clear all mappings shown on this sound",
        ariaLabel: `Clear mapped targets for ${entry.name || entry.index}`,
        className: "btn sound-btn-compact",
        onClick: async () => {
          if (catalogRefreshInFlight) return;
          const ok = await clearCatalogEntryMappings(entry, catalogFeedback);
          if (ok) await loadTracks();
        },
      });
      clearButton.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
      clearButton.setAttribute("aria-disabled", clearButton.disabled ? "true" : "false");

      const playButton = createActionButton({
        label: "▶ Play",
        title: "Play this catalog entry",
        ariaLabel: `Play catalog entry ${entry.name || entry.index}`,
        className: "btn sound-btn-play",
        onClick: () => {
          if (catalogRefreshInFlight) return;
          postPlayBanked(Number(entry.bank), String(entry.page || "A").toUpperCase(), Number(entry.index), catalogFeedback, "Catalog");
        },
      });
      playButton.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
      playButton.setAttribute("aria-disabled", playButton.disabled ? "true" : "false");

      actionRow.appendChild(mapButton);
      actionRow.appendChild(clearButton);
      actionRow.appendChild(playButton);
      tdActions.appendChild(actionRow);

      tr.appendChild(tdSelect);
      tr.appendChild(tdBank);
      tr.appendChild(tdIndex);
      tr.appendChild(tdName);
      tr.appendChild(tdMap);
      tr.appendChild(tdActions);
      catalogRows.appendChild(tr);
    });

    syncCatalogBulkUi(visibleEntries);
  };

  const loadCatalog = async () => {
    if (!window.PAApi || !catalogSupported) return false;
    if (catalogFetchInFlight) return catalogReady;
    catalogFetchInFlight = true;
    try {
      const result = await window.PAApi.get("/api/audio/catalog", { timeoutMs: 12000 });
      const data = result.data || {};
      catalogReady = Boolean(data.ready);
      catalogBanks = Array.isArray(data.banks) ? data.banks : [];
      catalogEntries = Array.isArray(data.entries) ? data.entries : [];
      const validKeys = new Set(catalogEntries.map((entry) => catalogEntryKey(entry)));
      [...catalogSelectedKeys].forEach((key) => {
        if (!validKeys.has(key)) {
          catalogSelectedKeys.delete(key);
        }
      });

      if (catalogStatus) {
        if (!catalogReady) {
          catalogStatus.textContent = "Catalog not loaded yet. Click Refresh Catalog.";
        } else {
          const bank1PageCount = catalogBanks.filter((bankRow) =>
            Number.parseInt(String(bankRow?.bank ?? "0"), 10) === 1
          ).length;
          let statusText = `${catalogEntries.length} entries across ${catalogBanks.length} bank(s).`;
          if (bank1PageCount === 1) {
            statusText += " CHIRP reports one active Bank 1 page per refresh.";
          }
          catalogStatus.dataset.baseText = statusText;
          catalogStatus.textContent = statusText;
        }
      }

      renderCatalogBankTabs();
      renderCatalogRows();
      return catalogReady;
    } catch (error) {
      if (catalogStatus) {
        delete catalogStatus.dataset.baseText;
        catalogStatus.textContent = "Catalog load failed.";
      }
      showFeedback(catalogFeedback, `Catalog load failed: ${getApiErrorMessage(error)}`, false);
      return false;
    } finally {
      catalogFetchInFlight = false;
    }
  };
  const refreshCatalog = async () => {
    if (!window.PAApi || !catalogSupported) return false;
    if (catalogRefreshInFlight) {
      showFeedback(catalogFeedback, "Catalog refresh already running", false);
      return false;
    }

    catalogRefreshInFlight = true;
    setCatalogActionLock(true);
    showFeedback(catalogFeedback, "Catalog refresh queued. This can take around 1 minute for large banks.", true, 0);

    try {
      const result = await window.PAApi.postForm("/api/audio/catalog/refresh", {}, { timeoutMs: 3000 });
      if (!result.data?.ok) {
        showFeedback(catalogFeedback, result.data?.error || "Refresh enqueue failed", false);
        return false;
      }

      for (let attempt = 0; attempt < 30; attempt += 1) {
        await new Promise((resolve) => window.setTimeout(resolve, 1000));
        const ready = await loadCatalog();
        if (ready) {
          showFeedback(catalogFeedback, "Catalog refreshed", true);
          return true;
        }
      }

      showFeedback(catalogFeedback, "Catalog refresh still running. Try again in a moment.", false);
      return false;
    } catch (error) {
      showFeedback(catalogFeedback, `Catalog refresh failed: ${getApiErrorMessage(error)}`, false);
      return false;
    } finally {
      catalogRefreshInFlight = false;
      setCatalogActionLock(false);
      if (!catalogReady && catalogStatus) {
        catalogStatus.textContent = "Catalog not loaded yet. Click Refresh Catalog.";
      }
      renderCatalogRows();
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
      trackNumberNote.textContent = "";
    }

    // All row content inserted via textContent and createElement — XSS-safe.
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
        const bindingBadge = document.createElement("span");
        bindingBadge.id = `chirp-binding-${sound.key}`;
        bindingBadge.className = "chirp-binding-badge hidden";
        tdTrack.appendChild(bindingBadge);

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
          if (sound.editable && sound.key) {
            const minTrack = sound.trackMin ?? 1;
            const value = Number.parseInt(rowInput?.value, 10);
            if (Number.isNaN(value) || value < minTrack || value > TRACK_MAX) {
              showFeedback(rowFeedback || globalFb, MSG.trackRange(minTrack, TRACK_MAX), false);
              return;
            }
            if (value === 0) return;

            const binding = getSlotBinding(sound.key);
            if (catalogSupported && binding) {
              playMappedSlot(sound.key, value, rowFeedback || globalFb, sound.label);
              return;
            }

            if (sound.playMode === "track") {
              postAudio({ action: "play", track: value }, globalFb, sound.label);
              return;
            }
            postAudio({ action: "dollar", cmd: sound.cmd }, globalFb, sound.label);
            return;
          }

          if (sound.cmd) {
            postAudio({ action: "dollar", cmd: sound.cmd }, globalFb, sound.label);
          }
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

    // All row content inserted via textContent and createElement — XSS-safe.
    CATEGORY_SOUNDS.forEach((category) => {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";

      const tdLabel = document.createElement("td");
      tdLabel.textContent = category.label;
      if (category.hint) {
        const hint = document.createElement("div");
        hint.className = "sound-category-hint";
        hint.textContent = category.hint;
        tdLabel.appendChild(hint);
      }

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
          const ok = await postCategoryRange(
            category.loKey,
            category.hiKey,
            minVal,
            maxVal,
            rowFeedback
          );
          if (ok) dirtyTracker.markSaved();
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
          const binding = chirpCategoryBindings?.[category.loKey];
          const bank = Number.parseInt(binding?.bank, 10);
          const page = String(binding?.page ?? "").trim().toUpperCase();
          if (catalogSupported && Number.isFinite(bank) && bank >= 1 && page.length === 1) {
            postPlayBanked(bank, page, randomTrack, globalFb, `${category.label} (${randomTrack})`);
            return;
          }
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

    // All row content inserted via textContent and createElement — XSS-safe.
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

    // All row content inserted via textContent and createElement — XSS-safe.
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
      const bindingBadge = document.createElement("span");
      bindingBadge.id = `chirp-binding-${sound.key}`;
      bindingBadge.className = "chirp-binding-badge hidden";
      tdTrack.appendChild(bindingBadge);

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
          playMappedSlot(sound.key, value, rowFeedback || globalFb, sound.label);
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
      chirpBindings = (data && typeof data.chirp_bindings === "object" && data.chirp_bindings)
        ? data.chirp_bindings
        : {};
      chirpCategoryBindings = (data && typeof data.chirp_category_bindings === "object" && data.chirp_category_bindings)
        ? data.chirp_category_bindings
        : {};
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

      catalogCategoryRanges = [];
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
        if (isCategoryRangeValid(minVal, maxVal) && minVal > 0) {
          catalogCategoryRanges.push({ label: category.label, loKey: category.loKey, lo: minVal, hi: maxVal });
        }
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
      applyChirpBindingBadges();
      renderCatalogRows();
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
  if (catalogStatus) catalogStatus.textContent = "Catalog unavailable for this backend.";
  populateCatalogTargetSelect(catalogBulkTarget, "Map checked to target…");
  renderCatalogBankTabs();
  renderCatalogRows();
  syncCatalogBulkUi([]);
  syncCatalogSuggestionUi();
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

  catalogFilterInput?.addEventListener("input", () => {
    renderCatalogRows();
  });

  catalogRefreshBtn?.addEventListener("click", async () => {
    await refreshCatalog();
  });

  catalogSuggestBtn?.addEventListener("click", async () => {
    await applySuggestedCategoryMappings();
  });

  catalogBulkToggleBtn?.addEventListener("click", () => {
    setCatalogBulkMode(!catalogBulkMode);
  });

  catalogBulkCancelBtn?.addEventListener("click", () => {
    setCatalogBulkMode(false);
  });

  catalogBulkClearBtn?.addEventListener("click", () => {
    catalogSelectedKeys.clear();
    renderCatalogRows();
    syncCatalogBulkUi();
  });

  catalogBulkTarget?.addEventListener("change", () => {
    syncCatalogBulkUi();
  });

  catalogSelectAll?.addEventListener("change", () => {
    if (!catalogBulkMode) return;
    const visibleEntries = getVisibleCatalogEntries();
    visibleEntries.forEach((entry) => {
      const key = catalogEntryKey(entry);
      if (catalogSelectAll.checked) {
        catalogSelectedKeys.add(key);
      } else {
        catalogSelectedKeys.delete(key);
      }
    });
    renderCatalogRows();
    syncCatalogBulkUi(visibleEntries);
  });

  catalogBulkMapBtn?.addEventListener("click", async () => {
    if (!catalogBulkMode || catalogRefreshInFlight) return;
    const target = catalogBulkTarget?.value || "";
    const selectedEntries = catalogEntries.filter((entry) => catalogSelectedKeys.has(catalogEntryKey(entry)));
    const ok = await mapCatalogEntriesToTarget(selectedEntries, target, catalogFeedback);
    if (!ok) return;
    catalogSelectedKeys.clear();
    await loadTracks();
    renderCatalogRows();
    syncCatalogBulkUi();
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
