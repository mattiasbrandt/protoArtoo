// =============================================================================
// data/sound.js
//
// Sound page controller — named sound commands, volume, direct play,
// random range configuration. All audio commands go through /api/audio.
// Named track assignments are loaded from and saved to /api/audio/tracks.
// =============================================================================
(() => {
  const NAMED_SOUNDS = [
    { label: "Scream", cmd: "$S", key: "scream", editable: true },
    { label: "Short Circuit", cmd: "$F", key: "faint", editable: true },
    { label: "Leia Message", cmd: "$L", key: "leia", editable: true },
    { label: "Short Cantina", cmd: "$c", key: "cantina_s", editable: true },
    { label: "Star Wars Theme", cmd: "$W", key: "sw_theme", editable: true },
    { label: "Imperial March", cmd: "$M", key: "imp_march", editable: true },
    { label: "Long Cantina", cmd: "$C", key: "cantina_l", editable: true },
    { label: "Boot Sound", cmd: "$B", key: "startup", editable: true },
    { label: "Random On", cmd: "$R", key: null, editable: false },
    { label: "Random Off", cmd: "$O", key: null, editable: false },
    { label: "Stop / Chatter Off", cmd: "$s", key: null, editable: false },
  ];

  // AudioDriver capability bits — must match audio_driver.h AUDIO_CAP_* constants
  const AUDIO_CAP_STATUS_QUERY = 0x01;
  const AUDIO_CAP_DEVICE_TYPE = 0x02;
  const AUDIO_CAP_TRACK_COUNT = 0x04;
  const AUDIO_CAP_CURRENT_TRACK = 0x08;
  const AUDIO_CAP_QUERY_SAFE_PLAYING = 0x10;

  const tbody = document.getElementById("named-sound-rows");
  const audioStateBadge = document.getElementById("audio-state-badge");
  const soundDisabledCard = document.getElementById("sound-disabled-card");
  const globalFb = document.getElementById("global-feedback");
  const volSlider = document.getElementById("vol-slider");
  const volDisplay = document.getElementById("vol-display");
  let soundHardwareEnabled = true;

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
      if (audioStateBadge && soundHardwareEnabled) {
        const linkOk = Boolean(d.link_ok);
        if (!linkOk) {
          audioStateBadge.textContent = "No module response";
          audioStateBadge.dataset.state = "error";
        } else if (d.play_state === "playing") {
          audioStateBadge.textContent = "🔊 Playing";
          audioStateBadge.dataset.state = "playing";
        } else if (d.play_state === "paused") {
          audioStateBadge.textContent = "⏸ Paused";
          audioStateBadge.dataset.state = "idle";
        } else {
          audioStateBadge.textContent = "✅ Idle";
          audioStateBadge.dataset.state = "idle";
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
  };

  const postAudio = async (params, feedbackEl) => {
    if (!window.PAApi) return;
    if (!soundHardwareEnabled) {
      showFeedback(feedbackEl || globalFb, "Sound controls unavailable: enable S2 — Sound in Setup.", false);
      return;
    }
    try {
      const result = await window.PAApi.postForm("/api/audio", params, { timeoutMs: 3000 });
      const ok = Boolean(result.data?.ok);
      showFeedback(feedbackEl, ok ? "Sent" : (result.data?.error || "Failed"), ok);
    } catch (error) {
      showFeedback(feedbackEl, `Command failed: ${window.PAApi.messageFor(error)}`, false);
    }
  };

  const postTrack = async (key, track, feedbackEl) => {
    if (!window.PAApi) return;
    if (!soundHardwareEnabled) {
      showFeedback(feedbackEl || globalFb, "Track updates unavailable: enable S2 — Sound in Setup.", false);
      return;
    }
    try {
      const result = await window.PAApi.postForm("/api/audio/tracks", { key, track }, { timeoutMs: 3000 });
      const ok = Boolean(result.data?.ok);
      showFeedback(feedbackEl, ok ? "Saved" : (result.data?.error || "Save failed"), ok);
    } catch (error) {
      showFeedback(feedbackEl, `Save failed: ${window.PAApi.messageFor(error)}`, false);
    }
  };

  const syncVolumeLabel = () => {
    if (!volSlider || !volDisplay) return;
    volDisplay.textContent = String(volSlider.value);
  };

  const buildNamedSoundRows = () => {
    if (!tbody) return;
    if (trackNumberNote) {
      trackNumberNote.textContent = "Track numbers are module-specific — set these to match your installed module's layout.";
    }

    NAMED_SOUNDS.forEach((sound) => {
      const tr = document.createElement("tr");
      tr.className = "sound-row-divider";

      const tdLabel = document.createElement("td");
      tdLabel.textContent = sound.label;

      const tdCmd = document.createElement("td");
      tdCmd.className = "sound-mono";
      tdCmd.textContent = sound.cmd;

      const tdTrack = document.createElement("td");
      const tdPlay = document.createElement("td");

      if (sound.editable && sound.key) {
        const input = document.createElement("input");
        input.type = "number";
        input.min = "1";
        input.max = "999";
        input.className = "sound-track-input-sm";
        input.dataset.key = sound.key;
        input.id = `track-input-${sound.key}`;
        input.setAttribute("aria-label", `${sound.label} track number`);
        tdTrack.appendChild(input);

        const saveButton = document.createElement("button");
        saveButton.className = "btn sound-btn-compact";
        saveButton.textContent = "💾";
        saveButton.title = "Save track number";
        saveButton.setAttribute("aria-label", `Save ${sound.label} track number`);

        const rowFeedback = document.createElement("span");
        rowFeedback.className = "sound-feedback-inline";
        rowFeedback.setAttribute("role", "status");
        rowFeedback.setAttribute("aria-live", "polite");
        rowFeedback.setAttribute("aria-atomic", "true");

        saveButton.addEventListener("click", () => {
          const value = Number.parseInt(input.value, 10);
          if (!value || value < 1 || value > 999) {
            showFeedback(rowFeedback, "1–999", false);
            return;
          }
          postTrack(sound.key, value, rowFeedback);
        });

        tdTrack.appendChild(saveButton);
        tdTrack.appendChild(rowFeedback);
      } else {
        tdTrack.textContent = "—";
      }

      const playButton = document.createElement("button");
      playButton.className = "btn sound-btn-play";
      playButton.textContent = "▶";
      playButton.title = `Play ${sound.cmd}`;
      playButton.setAttribute("aria-label", `Play ${sound.label}`);
      playButton.addEventListener("click", () => {
        postAudio({ action: "dollar", cmd: sound.cmd }, globalFb);
      });
      tdPlay.appendChild(playButton);

      tr.appendChild(tdLabel);
      tr.appendChild(tdCmd);
      tr.appendChild(tdTrack);
      tr.appendChild(tdPlay);
      tbody.appendChild(tr);
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

  const renderStatus = (data) => {
    const s2Enabled = Boolean(data.s2Sound);
    setSoundHardwareEnabled(s2Enabled);
    // Note: the audio-state-badge is intentionally NOT updated here from
    // data.s2Sound.state — that field reflects firmware's guess (audioActive),
    // not confirmed module state. Badge is updated by updateModuleStatus()
    // which uses the real module-reported play state from GET /api/audio.
    if (!audioStateBadge) return;
    if (!s2Enabled) {
      audioStateBadge.textContent = "Disabled";
      audioStateBadge.dataset.state = "disabled";
    }
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
  };

  buildNamedSoundRows();
  loadTracks();
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

  volSlider?.addEventListener("input", syncVolumeLabel);
  volSlider?.addEventListener("change", () => {
    postAudio({ action: "volume", level: volSlider.value }, globalFb);
  });

  document.getElementById("btn-stop")
    ?.addEventListener("click", () => postAudio({ action: "stop" }, globalFb));

  document.getElementById("btn-random-on")
    ?.addEventListener("click", () => postAudio({ action: "dollar", cmd: "$R" }, globalFb));

  document.getElementById("btn-random-off")
    ?.addEventListener("click", () => postAudio({ action: "dollar", cmd: "$O" }, globalFb));

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
    postAudio({ action: "play", track: value }, directFb);
  });

  const randFb = document.getElementById("rand-feedback");
  document.getElementById("btn-rand-save")?.addEventListener("click", async () => {
    const minVal = Number.parseInt(document.getElementById("rand-min")?.value, 10);
    if (!soundHardwareEnabled) {
      showFeedback(randFb, "Random range unavailable: enable S2 — Sound in Setup.", false);
      return;
    }
    const maxVal = Number.parseInt(document.getElementById("rand-max")?.value, 10);

    if (!minVal || minVal < 1 || minVal > 999 || !maxVal || maxVal < 1 || maxVal > 999) {
      showFeedback(randFb, "Values must be 1–999", false);
      return;
    }
    if (minVal > maxVal) {
      showFeedback(randFb, "Min must be ≤ Max", false);
      return;
    }

    if (!window.PAApi) return;
    try {
      const [r1, r2] = await Promise.all([
        window.PAApi.postForm("/api/audio/tracks", { key: "rand_min", track: minVal }, { timeoutMs: 3000 }),
        window.PAApi.postForm("/api/audio/tracks", { key: "rand_max", track: maxVal }, { timeoutMs: 3000 }),
      ]);
      const ok = Boolean(r1.data?.ok && r2.data?.ok);
      showFeedback(randFb, ok ? "Range saved" : "Save failed", ok);
    } catch (error) {
      showFeedback(randFb, `Save failed: ${window.PAApi.messageFor(error)}`, false);
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
      showFeedback(intFb, ok ? "Intervals saved" : "Save failed", ok);
    } catch (error) {
      showFeedback(intFb, `Save failed: ${window.PAApi.messageFor(error)}`, false);
    }
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
