// =============================================================================
// data/sound.js
//
// Sound page controller — Named Track commands, volume, direct play,
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
    { label: "Network Link Lost (auto)", key: "sys_net_down" },
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

  // AudioDriver capability bits — must match audio_driver.h AUDIO_CAP_* constants.
  // Every bit here is consulted below: a capability the firmware declares and
  // nothing reads is worse than no capability, because the page then reports a
  // field the fitted module cannot actually answer.
  const AUDIO_CAP_STATUS_QUERY = 0x01;
  const AUDIO_CAP_DEVICE_TYPE = 0x02;
  const AUDIO_CAP_TRACK_COUNT = 0x04;
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

  // The sound link is the health-signal model's word and light, the same ones
  // the Status Plate, the Dashboard and Maintenance show (data/health_signals.js,
  // #422). This page reads no rx_status of its own: it hands the model the
  // module block, from the status frame or from GET /api/audio, and paints
  // the answer. The light becomes the badge's data-state.
  const SOUND_LINK_BADGE_STATES = { ok: "ok", fail: "error", off: "disabled" };
  const readSoundLink = (audio) =>
    window.PAHealthSignals.readSoundLink({ audio }, { unknown: window.PALiveReading.UNKNOWN });
  const paintSoundLink = (el, link) => {
    if (!el) return;
    el.textContent = link.word;
    el.dataset.state = SOUND_LINK_BADGE_STATES[link.state] || "disabled";
  };
  // Whether the Playback badge is showing the link's word rather than a play
  // state, so a link that comes back asks the module for its play state again.
  let badgeShowsLink = false;
  const soundDisabledCard = document.getElementById("sound-disabled-card");
  const mp3WireNote = document.getElementById("mp3-wire-note");
  const mp3MissingTrack = document.getElementById("mp3-missing-track");
  const mp3RangeWarning = document.getElementById("mp3-range-warning");
  // The MP3 Trigger's Component Registry display name, which is what its
  // driver reports as `driver` (include/audio_mp3trigger.h).
  const MP3_DRIVER_NAME = "MP3 Trigger";
  const MP3_WIRE_NOTE =
    "Power this board from the 3.3 V jumper: 5 V can kill the Body Controller's receive pin. It answers only with MP3TRIGR.INI on the card, holding #BAUD 9600.";
  const MP3_RANGE_WARNING =
    "This module stops at 255. 254 is Stop's silent file and 255 is the boot clip — they will not play as random chatter.";
  // The module checksums its own file names, so it notices a sound being added,
  // removed or renamed -- which is what shifts the numbers a saved assignment
  // points at. What it cannot notice is a file moved between pages under the
  // same name, so the check not having run is worth saying separately rather
  // than being folded into silence.
  const SOUND_LIST_CHANGED_WARNING =
    "The sound list has changed since these assignments were saved. Check the assigned sounds before using them.";
  const SOUND_LIST_UNCHECKED_NOTE =
    "The sound list could not be checked against these assignments.";
  const CATALOG_STALE_NOTE = "This listing is from an earlier refresh.";
  const CATALOG_PARTIAL_SUGGESTION_NOTE =
    "Suggestions need the whole listing, and part of it is missing. Refresh the catalog first.";
  // A full walk is 300 sounds and can wait 450 ms on each one, so a refresh
  // running for minutes is ordinary. The page keeps waiting while the
  // controller says the same refresh is still going, and stops here rather than
  // waiting forever on a controller that has stopped moving.
  const CATALOG_REFRESH_MAX_POLLS = 300;
  const CATALOG_REFRESH_OUTCOMES = {
    completed: { ok: true, text: "Catalog refreshed" },
    blocked: {
      ok: false,
      text: "Catalog refresh did not run — the module's link was busy. Try again in a moment.",
    },
    failed: {
      ok: false,
      text: "Catalog refresh failed — the module did not answer.",
    },
    interrupted: {
      ok: false,
      text: "Catalog refresh stopped early. Refresh again when the droid is idle.",
    },
  };
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

  let lastDriverIsMp3 = false;
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
  const modTotalTracksRow = document.getElementById("mod-total-tracks-row");
  const modStatusTable = document.getElementById("mod-status-table");
  const modPollSection = document.getElementById("mod-poll-section");
  const modQueryNote = document.getElementById("mod-query-note");
  const btnPoll = document.getElementById("btn-poll-status");
  const modStatusFb = document.getElementById("mod-status-feedback");
  const trackNumberNote = document.getElementById("track-number-note");
  const chirpCatalogCard = document.getElementById("chirp-catalog-card");
  const catalogRows = document.getElementById("catalog-rows");
  const catalogStatus = document.getElementById("catalog-status");
  const catalogLimits = document.getElementById("catalog-limits");
  const soundListChangedWarning = document.getElementById("sound-list-changed-warning");
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
  let catalogSupported = false;
  let catalogReady = false;
  let catalogBanks = [];
  let catalogEntries = [];
  // Which bank tab is selected, as "<bank>:<page>" -- empty means All banks. A
  // bank number alone is not an address: 2A and 2B are different pages of bank
  // 2 with different sounds in them, and filtering on the number showed both
  // under either tab.
  let catalogBankFilter = "";
  // What the controller said the last discovery could not see, and whether the
  // listing on screen came from the refresh the operator last asked for.
  let catalogManifestIncomplete = false;
  let catalogMissingNames = 0;
  let catalogEntryCapReached = false;
  let catalogStale = false;
  // The refresh the controller is reporting on. Queue acceptance and refresh
  // completion are different events, so the page watches its own request
  // number rather than reading "some catalog is ready" as "mine finished".
  let catalogRefreshStatus = { request: 0, active: 0, settled: 0, state: "none" };
  let soundListChanged = false;
  let soundListChecked = false;
  let catalogFetchPromise = null;
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

  // The two workspaces are two answers to one question, so they are one
  // segmented control and the chosen one takes .seg's own lit face. It used to
  // take .btn.accent, which is the anatomy's PRIMARY ACT - the one filled
  // control on a surface - and this page had two of them.
  const setModeButtonState = (button, active) => {
    if (!button) return;
    button.classList.toggle("active", active);
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

  // Owned by this surface: the shell stops it when the operator leaves Sound and
  // starts it again on the way back (ADR 0048, #360). Created here rather than
  // inside the reset below, because the surface a poll belongs to is decided
  // when it is made.
  //
  // The cadence is the module's to grant and the return-to-tab read is not:
  // this page has always re-read the module's state on coming back to the tab
  // whatever the backend can do, while only a backend that is safe to query
  // while playing gets asked every two seconds. skipWhen is what keeps those
  // two apart in one poll -- it gates the cadence tick and not the refresh.
  //
  // updateModuleStatus() rethrows after writing "Fetch error" onto the module
  // line, and that rejection is left to PASurface.poll(): it reports the
  // failure and leaves Sound showing what it last read. Catching it here would
  // say the screen is current when the module never answered (#360).
  //
  // Called through an arrow, not handed over by name: this poll is created
  // above the const that defines updateModuleStatus, so naming it here reads
  // it before it exists.
  let moduleStatusCadenceWanted = false;
  const moduleStatusPoll = window.PASurface.poll(
    () => updateModuleStatus(),
    { cadenceMs: 2000, skipWhen: () => !moduleStatusCadenceWanted, refreshOnReturn: true }
  );
  moduleStatusPoll.start();

  const resetModuleStatusAutoRefresh = (caps) => {
    moduleStatusCadenceWanted = (caps & AUDIO_CAP_QUERY_SAFE_PLAYING) !== 0;
  };

  const mp3RangeNeedsWarning = (lo, hi) => {
    if (!Number.isFinite(lo) || !Number.isFinite(hi)) return false;
    if (lo === 0 && hi === 0) return false;
    return lo > 255 || hi > 255 || (hi >= 254 && lo >= 1);
  };

  const refreshMp3RangeWarning = (isMp3) => {
    if (!mp3RangeWarning) return;
    if (!isMp3) {
      mp3RangeWarning.textContent = "";
      setElementVisible(mp3RangeWarning, false);
      return;
    }
    let warn = false;
    CATEGORY_SOUNDS.forEach((category) => {
      const minInput = document.getElementById(`cat-min-${category.loKey}`);
      const maxInput = document.getElementById(`cat-max-${category.hiKey}`);
      const lo = Number.parseInt(minInput?.value, 10);
      const hi = Number.parseInt(maxInput?.value, 10);
      if (mp3RangeNeedsWarning(lo, hi)) warn = true;
    });
    mp3RangeWarning.textContent = warn ? MP3_RANGE_WARNING : "";
    setElementVisible(mp3RangeWarning, warn);
  };

  const applyCapabilityUI = (caps) => {
    const supportsStatusQuery = (caps & AUDIO_CAP_STATUS_QUERY) !== 0;
    const supportsDeviceType = (caps & AUDIO_CAP_DEVICE_TYPE) !== 0;
    const supportsTrackCount = (caps & AUDIO_CAP_TRACK_COUNT) !== 0;
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
      catalogBankFilter = "";
      catalogManifestIncomplete = false;
      catalogMissingNames = 0;
      catalogEntryCapReached = false;
      catalogStale = false;
      soundListChanged = false;
      soundListChecked = false;
      catalogAutoLoadAttempted = false;
      catalogBulkMode = false;
      catalogSelectedKeys.clear();
      chirpCategoryBindings = {};
      catalogSuggestedCategoryMappings = [];
      if (catalogRows) catalogRows.innerHTML = "";
      if (catalogBankTabs) catalogBankTabs.innerHTML = "";
      if (catalogBulkTarget) catalogBulkTarget.value = "";
      if (catalogStatus) catalogStatus.textContent = "Catalog unavailable for this backend.";
      renderCatalogLimits();
    } else if (!catalogReady && catalogEntries.length === 0 && !catalogAutoLoadAttempted) {
      catalogAutoLoadAttempted = true;
      if (catalogStatus && !catalogStatus.textContent) {
        catalogStatus.textContent = "Catalog not loaded yet. Click Refresh Catalog.";
      }
      loadCatalog().catch(() => {});
    }
    syncCatalogBulkUi();
    applyChirpBindingBadges();
    renderSoundListWarning();

    setElementVisible(modDeviceRow, supportsStatusQuery && supportsDeviceType);
    setElementVisible(modTotalTracksRow, supportsStatusQuery && supportsTrackCount);
    setElementVisible(modCurrentTrackRow, supportsStatusQuery && supportsCurrentTrack);
    setElementVisible(modPollSection, showManualPoll);
    setElementVisible(btnPoll, showManualPoll);
    setElementVisible(modStatusTable, supportsStatusQuery);

    if (!modQueryNote) return;
    if (!supportsStatusQuery) {
      modQueryNote.textContent = "Status queries not supported by this module.";
      return;
    }
    if (supportsSafePlayingQuery) {
      modQueryNote.textContent = "";
      return;
    }
    modQueryNote.textContent = "Read at boot. Poll to refresh, but not while a track plays.";
  };

  const updateModuleStatus = async ({ handle = null } = {}) => {
    if (!window.PAApi) return;
    try {
      const api = handle || window.PAApi;
      const result = await api.get("/api/audio");
      const d = result.data;

      if (d.capabilities !== undefined && d.capabilities !== null) {
        const caps = Number(d.capabilities) & 0xFF;
        const capabilitiesChanged = lastCapabilities !== caps;
        lastCapabilities = caps;
        applyCapabilityUI(caps);
        if (capabilitiesChanged) resetModuleStatusAutoRefresh(caps);
      }

      if (modDriver) modDriver.textContent = d.driver ?? "—";
      const link = readSoundLink(d);
      paintSoundLink(modLink, link);
      if (modDevice) modDevice.textContent = d.device ?? "—";
      if (modPlayState) modPlayState.textContent = d.play_state ?? "—";
      if (modTotalTracks) modTotalTracks.textContent = d.total_tracks ?? "—";
      if (modCurrentTrack) modCurrentTrack.textContent = d.current_track ?? "—";

      const isMp3 = d.driver === MP3_DRIVER_NAME;
      lastDriverIsMp3 = isMp3;
      if (mp3WireNote) {
        mp3WireNote.textContent = isMp3 ? MP3_WIRE_NOTE : "";
        setElementVisible(mp3WireNote, isMp3);
      }
      const missing = Number(d.missing_track);
      if (mp3MissingTrack) {
        if (isMp3 && Number.isFinite(missing) && missing > 0) {
          mp3MissingTrack.textContent =
            `Track ${missing} is not on the card — that is the clip, not the wiring.`;
          setElementVisible(mp3MissingTrack, true);
        } else {
          mp3MissingTrack.textContent = "";
          setElementVisible(mp3MissingTrack, false);
        }
      }
      refreshMp3RangeWarning(isMp3);

      // The badge carries the module's play state while the link is up, and
      // the link's own word while it is not: a stale "Idle" beside a module
      // that did not answer would say it is fine.
      if (soundStateBadge && soundHardwareEnabled) {
        badgeShowsLink = link.state !== "ok";
        if (badgeShowsLink) {
          paintSoundLink(soundStateBadge, link);
        } else if (d.play_state === "playing") {
          soundStateBadge.textContent = "Playing";
          soundStateBadge.dataset.state = "playing";
        } else if (d.play_state === "paused") {
          soundStateBadge.textContent = "Paused";
          soundStateBadge.dataset.state = "idle";
        } else {
          soundStateBadge.textContent = "Idle";
          soundStateBadge.dataset.state = "idle";
        }
      }
    } catch (error) {
      if (modLink) {
        modLink.textContent = "Fetch error";
        modLink.dataset.state = "error";
      }
      throw error;
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

  // What a sound control says when Sound is switched off. Eight controls said
  // eight versions of this before #348, every one of them naming an S2 header
  // - the Artoo PCB's silkscreen, which is not a thing every board has. The
  // words match the Availability seam's own "off" reason and its route
  // (data/feature_availability.js); they are stated here rather than read from
  // it because this surface does not otherwise load that file, and a script
  // fetch for one sentence costs the page more than it is worth.
  const SOUND_OFF_LINE = "Sound is switched off. Switch it on in Configuration.";

  const postAudio = async (params, feedbackEl, label = 'Sound command') => {
    if (!window.PAApi) return false;
    if (!soundHardwareEnabled) {
      showFeedback(feedbackEl || globalFb, SOUND_OFF_LINE, false);
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
      showFeedback(feedbackEl || globalFb, SOUND_OFF_LINE, false);
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
        showFeedback(feedbackEl || globalFb, SOUND_OFF_LINE, false);
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
      showFeedback(feedbackEl || globalFb, SOUND_OFF_LINE, false);
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

      // Only what was actually listed. The old fallback built 1..count out of
      // the bank's declared size when no entry had been listed for it, which is
      // a range over sounds nobody has seen -- exactly the omitted entries a
      // suggestion must not claim.
      if (indexes.length === 0) return;
      const lo = Math.min(...indexes);
      const hi = Math.max(...indexes);
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

  // Missing banks and a listing cut off at the entry limit both leave sounds
  // out of the catalog with no row to say so. A suggested category range spans
  // lo..hi, so a range built over a listing with holes in it silently claims
  // sounds nobody listed.
  const catalogListingIsPartial = () => catalogManifestIncomplete || catalogEntryCapReached;

  // What the operator is NOT seeing. Every one of these used to live only in a
  // controller log line, which meant a catalog that was usable and a catalog
  // that was whole looked exactly the same on screen.
  const renderCatalogLimits = () => {
    if (!catalogLimits) return;
    const sentences = [];
    if (catalogSupported && catalogReady) {
      if (catalogManifestIncomplete) {
        sentences.push("Some banks did not arrive from the module, so this is not the whole card.");
      }
      if (catalogMissingNames === 1) {
        sentences.push("One sound came back without a name and is listed by its index.");
      } else if (catalogMissingNames > 1) {
        sentences.push(`${catalogMissingNames} sounds came back without a name and are listed by their index.`);
      }
      if (catalogEntryCapReached) {
        sentences.push("The listing stopped at the Body Controller's entry limit, so the end of the card is missing.");
      }
    }
    if (catalogStale) {
      sentences.push(CATALOG_STALE_NOTE);
    }
    catalogLimits.textContent = sentences.join(" ");
    setElementVisible(catalogLimits, sentences.length > 0);
  };

  const hasSavedChirpBindings = () =>
    Object.keys(chirpBindings || {}).length > 0 ||
    Object.keys(chirpCategoryBindings || {}).length > 0;

  // Shown beside the assignments themselves, because that is what a changed
  // sound list invalidates. Saving stays available throughout: the builder is
  // the one who decides what the new numbers should point at.
  const renderSoundListWarning = () => {
    if (!soundListChangedWarning) return;
    const sentences = [];
    if (catalogSupported && hasSavedChirpBindings()) {
      if (soundListChanged) {
        sentences.push(SOUND_LIST_CHANGED_WARNING);
      }
      if (!soundListChecked) {
        sentences.push(SOUND_LIST_UNCHECKED_NOTE);
      }
    }
    soundListChangedWarning.textContent = sentences.join(" ");
    setElementVisible(soundListChangedWarning, sentences.length > 0);
  };

  const syncCatalogSuggestionUi = () => {
    catalogSuggestedCategoryMappings = buildSuggestedCategoryMappings();
    const suggestionCount = catalogSuggestedCategoryMappings.length;

    if (catalogSuggestBtn) {
      catalogSuggestBtn.textContent = suggestionCount > 0
        ? `Apply suggestions (${suggestionCount})`
        : "Apply suggestions";
      const enabled = catalogSupported && catalogReady && soundHardwareEnabled &&
        !catalogRefreshInFlight && suggestionCount > 0 && !catalogListingIsPartial();
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
    if (catalogListingIsPartial()) {
      showFeedback(catalogFeedback, CATALOG_PARTIAL_SUGGESTION_NOTE, false);
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
      catalogBulkToggleBtn.textContent = bulkVisible ? "Done" : "Bulk";
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

  // A tab's identity is the bank AND the page it names. The label always said
  // both -- B2A and B2B -- while the filter kept only the number, so clicking
  // B2B listed every bank 2 entry, page A included.
  const catalogBankPageKey = (bank, page) => {
    const bankNumber = Number.parseInt(String(bank ?? "0"), 10);
    const pageLetter = String(page ?? "A").trim().toUpperCase() || "A";
    return `${Number.isFinite(bankNumber) ? bankNumber : 0}:${pageLetter}`;
  };

  const renderCatalogBankTabs = () => {
    if (!catalogBankTabs) return;
    catalogBankTabs.innerHTML = "";
    if (!catalogSupported) return;

    const tabs = [{ key: "", label: "All banks" }, ...catalogBanks.map((bank) => ({
      key: catalogBankPageKey(bank?.bank, bank?.page),
      label: `B${Number.parseInt(String(bank?.bank ?? "0"), 10) || 0}${String(bank?.page ?? "A").trim().toUpperCase() || "A"}`
    }))];

    tabs.forEach((tab) => {
      const selected = catalogBankFilter === tab.key;
      const button = document.createElement("button");
      button.className = `btn sound-btn-compact catalog-bank-tab${selected ? " accent" : ""}`;
      button.type = "button";
      button.textContent = tab.label;
      button.setAttribute("role", "tab");
      button.setAttribute("aria-selected", selected ? "true" : "false");
      button.disabled = catalogRefreshInFlight || !soundHardwareEnabled;
      button.setAttribute("aria-disabled", button.disabled ? "true" : "false");
      button.addEventListener("click", () => {
        if (catalogRefreshInFlight || !soundHardwareEnabled) return;
        catalogBankFilter = tab.key;
        renderCatalogBankTabs();
        renderCatalogRows();
      });
      catalogBankTabs.appendChild(button);
    });
  };

  // Selection and the bulk actions both run off this list, so filtering here is
  // what keeps a bulk map from reaching a row the operator cannot see.
  const getVisibleCatalogEntries = () => {
    const query = catalogFilterInput?.value?.trim().toLowerCase() ?? "";
    return catalogEntries.filter((entry) => {
      if (catalogBankFilter && catalogBankPageKey(entry?.bank, entry?.page) !== catalogBankFilter) {
        return false;
      }
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
        label: "Map",
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
        label: "Play",
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

  const loadCatalog = async ({ handle = null } = {}) => {
    if (!window.PAApi || !catalogSupported) return false;
    if (catalogFetchPromise) return catalogFetchPromise;
    catalogFetchPromise = (async () => {
      try {
        // GET /api/audio/catalog carries the Catalog deadline (12000ms) per ADR 0019,
        // regardless of context. When called as a section loader, handle carries it.
        // When called from pre-load or retry paths (non-section), use explicit timeout.
        let result;
        if (handle) {
          result = await handle.get("/api/audio/catalog");
        } else {
          result = await window.PAApi.get("/api/audio/catalog", { timeoutMs: window.PageBootstrap.CATALOG_DEADLINE_MS });
        }
        const data = result.data || {};

        // The refresh ledger and the saved-bindings check are answered whether
        // or not the controller could let this read at the catalog itself, so
        // they are taken from every reply.
        const refresh = (data && typeof data.refresh === "object" && data.refresh) || {};
        catalogRefreshStatus = {
          request: Number.parseInt(String(refresh.request ?? "0"), 10) || 0,
          active: Number.parseInt(String(refresh.active ?? "0"), 10) || 0,
          settled: Number.parseInt(String(refresh.settled ?? "0"), 10) || 0,
          state: String(refresh.state ?? "none"),
        };
        const bindings = (data && typeof data.bindings === "object" && data.bindings) || {};
        soundListChanged = Boolean(bindings.sound_list_changed);
        soundListChecked = Boolean(bindings.sound_list_checked);
        renderSoundListWarning();

        // A read the controller refused because a refresh holds the catalog has
        // learned nothing about the catalog. Overwriting the rows on this
        // answer would blank a listing that is still perfectly good, so what is
        // on screen stays exactly as it is (the same rule the shell applies to
        // a poll that did not come back).
        if (data.busy) {
          return catalogReady;
        }

        catalogReady = Boolean(data.ready);
        catalogBanks = Array.isArray(data.banks) ? data.banks : [];
        catalogEntries = Array.isArray(data.entries) ? data.entries : [];
        const limits = (data && typeof data.limits === "object" && data.limits) || {};
        catalogManifestIncomplete = Boolean(limits.manifest_incomplete);
        catalogMissingNames = Number.parseInt(String(limits.missing_names ?? "0"), 10) || 0;
        catalogEntryCapReached = Boolean(limits.entry_cap_reached);
        const validKeys = new Set(catalogEntries.map((entry) => catalogEntryKey(entry)));
        [...catalogSelectedKeys].forEach((key) => {
          if (!validKeys.has(key)) {
            catalogSelectedKeys.delete(key);
          }
        });
        // A bank tab whose page is no longer in the listing would filter every
        // row away with no way back except All banks.
        if (catalogBankFilter &&
            !catalogBanks.some((bankRow) =>
              catalogBankPageKey(bankRow?.bank, bankRow?.page) === catalogBankFilter)) {
          catalogBankFilter = "";
        }

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

        renderCatalogLimits();
        renderCatalogBankTabs();
        renderCatalogRows();
        return catalogReady;
      } catch (error) {
        if (catalogStatus) {
          delete catalogStatus.dataset.baseText;
          catalogStatus.textContent = "Catalog load failed.";
        }
        showFeedback(catalogFeedback, `Catalog load failed: ${getApiErrorMessage(error)}`, false);
        throw error;
      } finally {
        catalogFetchPromise = null;
      }
    })();
    return catalogFetchPromise;
  };
  // The controller answers which request settled and how. An outcome that is
  // not "completed" may still leave the earlier listing on screen -- it is
  // often the only one there is -- but it is labelled as the earlier one rather
  // than reported as this refresh succeeding.
  const reportRefreshOutcome = (state) => {
    const outcome = CATALOG_REFRESH_OUTCOMES[state];
    if (!outcome) {
      showFeedback(catalogFeedback, `Catalog refresh ended: ${state}`, false);
      catalogStale = catalogReady;
      renderCatalogLimits();
      return false;
    }
    catalogStale = !outcome.ok && catalogReady;
    renderCatalogLimits();
    showFeedback(catalogFeedback, outcome.text, outcome.ok);
    return outcome.ok;
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
      // Accepting the command onto the queue is all this answer reports. Which
      // request it accepted is what makes the difference between watching this
      // refresh and reading an older catalog that happens to still be ready.
      const requestId = Number.parseInt(String(result.data.request ?? "0"), 10);
      if (!Number.isFinite(requestId) || requestId < 1) {
        showFeedback(catalogFeedback, "The Body Controller did not say which refresh it accepted.", false);
        return false;
      }

      for (let attempt = 0; attempt < CATALOG_REFRESH_MAX_POLLS; attempt += 1) {
        await new Promise((resolve) => window.setTimeout(resolve, 1000));
        await loadCatalog();
        if (catalogRefreshStatus.settled >= requestId) {
          return reportRefreshOutcome(catalogRefreshStatus.state);
        }
        if (catalogRefreshStatus.request > requestId) {
          showFeedback(catalogFeedback,
                       "Another catalog refresh was started, so this one is no longer the current request.",
                       false);
          return false;
        }
        // Still queued or still running. The polling window ending is not the
        // operation ending: unlocking here would let a second refresh be
        // enqueued on top of the one still walking the card.
      }

      showFeedback(
        catalogFeedback,
        catalogRefreshStatus.active === requestId
          ? "Catalog refresh is still running. Open Sound again in a moment."
          : "Catalog refresh has not started yet. Open Sound again in a moment.",
        false
      );
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

  // A Setting's input carries no range of its own: the droid holds it, and a
  // value it will not take comes back as a refusal messageFor() words (ADR
  // 0068, amended 2026-09-26). min and max are for inputs that are not one.
  const createNumberInput = ({ id, min = null, max = null, className, ariaLabel, datasetKey = null, placeholder = null, value = null }) => {
    const input = document.createElement("input");
    input.type = "number";
    if (min !== null) input.min = String(min);
    if (max !== null) input.max = String(max);
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
        rowInput = createNumberInput({
          id: `track-input-${sound.key}`,
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
          label: "Save",
          title: "Save track number",
          ariaLabel: `Save ${sound.label} track number`,
          className: "btn sound-btn-compact",
          onClick: async () => {
            const ok = await postTrack(sound.key, rowInput.value.trim(), rowFeedback);
            if (ok) dirtyTracker?.markSaved();
          },
        });
        actionsWrap.appendChild(saveButton);
      } else {
        tdTrack.textContent = "—";
      }

      const playButton = createActionButton({
        label: "Play",
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
        value: 0,
        className: "sound-track-input-sm",
        ariaLabel: `${category.label} minimum track`,
      });
      tdMin.appendChild(minInput);

      const tdMax = document.createElement("td");
      const maxInput = createNumberInput({
        id: `cat-max-${category.hiKey}`,
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
        label: "Save",
        title: "Save category range",
        ariaLabel: `Save ${category.label} range`,
        className: "btn sound-btn-compact",
        onClick: async () => {
          // As typed: the droid judges each bound and the pair.
          const ok = await postCategoryRange(
            category.loKey,
            category.hiKey,
            minInput.value.trim(),
            maxInput.value.trim(),
            rowFeedback
          );
          if (ok) dirtyTracker.markSaved();
        },
      });

      const playButton = createActionButton({
        label: "Play",
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
        label: "Save",
        title: "Save track number",
        ariaLabel: `Save ${sound.label} track number`,
        className: "btn sound-btn-compact",
        onClick: async () => {
          const ok = await postTrack(sound.key, input.value.trim(), rowFeedback);
          if (ok) dirtyTracker.markSaved();
        },
      });

      const playButton = createActionButton({
        label: "Play",
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
      refreshMp3RangeWarning(lastDriverIsMp3);

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
      // Whether there are saved assignments at all decides whether a warning
      // about them has anything to warn about.
      renderSoundListWarning();
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

  const renderReading = (reading) => {
    const data = reading.status;
    // Nothing is known about the module until the droid has sent a frame, so
    // nothing is drawn from one: the badge keeps what the page last showed.
    if (data === null) return;
    const s2Enabled = Boolean(data.audio);
    setSoundHardwareEnabled(s2Enabled);
    if (!soundStateBadge) return;
    const link = readSoundLink(data.audio);
    if (link.state !== "ok") {
      paintSoundLink(modLink, link);
      paintSoundLink(soundStateBadge, link);
      badgeShowsLink = true;
    } else if (badgeShowsLink) {
      // The link is back: ask the module what it is doing rather than keep
      // the word it had while it could not be asked.
      badgeShowsLink = false;
      updateModuleStatus().catch(() => {});
    }
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

  // Whether a module is fitted, and whether it answers, ride the Live Reading,
  // which owns the stream or the one fallback poll for the whole shell
  // (data/live_reading.js). The module's own poll above is not a status read.
  window.PALiveReading.subscribe(renderReading);

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
      showFeedback(directFb, SOUND_OFF_LINE, false);
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
      showFeedback(randFb, SOUND_OFF_LINE, false);
      return;
    }
    const maxVal = Number.parseInt(document.getElementById("rand-max")?.value, 10);

    // The range of each is the droid's to hold; the order between the two is a
    // rule the droid does not keep, so the page still says it.
    if (minVal > maxVal) {
      showFeedback(randFb, MSG.minMustBeLeMax, false);
      return;
    }

    if (!window.PAApi) return;
    try {
      const [r1, r2] = await Promise.all([
        window.PAApi.postForm("/api/audio/tracks", { key: "rand_min", track: document.getElementById("rand-min")?.value ?? "" }, { timeoutMs: 3000 }),
        window.PAApi.postForm("/api/audio/tracks", { key: "rand_max", track: document.getElementById("rand-max")?.value ?? "" }, { timeoutMs: 3000 }),
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
      showFeedback(intFb, SOUND_OFF_LINE, false);
      return;
    }
    if (!window.PAApi) return;
    try {
      const results = await Promise.all(INT_FIELDS.map((field) =>
        window.PAApi.postForm("/api/audio/tracks",
          { key: field.key, track: document.getElementById(field.id)?.value ?? "" }, { timeoutMs: 3000 })));
      const ok = results.every((entry) => entry.data?.ok);
      showFeedback(intFb, ok ? "Intervals saved" : MSG.saveFailed, ok);
    } catch (error) {
      feedbackSaveFailure(intFb, error);
    }
  });

  moodMapSaveBtn?.addEventListener("click", async () => {
    if (!soundHardwareEnabled) {
      setMoodMapStatus(SOUND_OFF_LINE, false);
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

  // -------------------------------------------------------------------------
  // Boot — load audio module status and conditionally load catalog
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API loads as sections so the bootstrap
  // can show recovery state if any fetch fails.
  // The catalog load is conditional: it only runs if updateModuleStatus
  // detects catalog support via capability flags.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const loadAudioModuleStatus = async ({ handle = null } = {}) => {
    await updateModuleStatus({ handle });
    // Catalog load is triggered conditionally inside updateModuleStatus ->
    // applyCapabilityUI, so once module status is loaded, we know if catalog
    // is needed. If it was needed, loadCatalog already started in applyCapabilityUI.
  };

  const loadAudioCatalogIfSupported = async ({ handle = null } = {}) => {
    if (!catalogSupported) {
      // Catalog not supported; skip with success to keep the bootstrap
      // from retrying if the capability check indicated no support.
      return;
    }
    // CRITICAL: handle carries the Catalog deadline (12000ms). The bootstrap
    // always provides the handle for this section, ensuring the deadline is applied.
    await loadCatalog({ handle });
  };

  const SECTIONS = [
    ["audio-status", loadAudioModuleStatus, "audio module status"],
    ["audio-catalog", loadAudioCatalogIfSupported, "audio catalog", { deadlineMs: window.PageBootstrap.CATALOG_DEADLINE_MS }],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      updateModuleStatus().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/live_reading.js": "live updates",
      "/shell.js": "page layout",
      "/sound.js": "audio control",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label, opts]) =>
      window.PABootstrap.registerSection(name, load, { label, ...opts })
    );
  };

  startPageLoad();

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
