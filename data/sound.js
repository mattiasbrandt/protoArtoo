// =============================================================================
// data/sound.js
//
// Sound page controller — named sound commands, volume, direct play,
// random range configuration. All audio commands go through /api/audio.
// Named track assignments are loaded from and saved to /api/audio/tracks.
// =============================================================================
(() => {
  // ---------------------------------------------------------------------------
  // Named sound rows definition
  // key: RobotState / NVS field name; cmd: $ command to trigger play;
  // editable: whether the track # can be changed by the operator.
  // ---------------------------------------------------------------------------
  const NAMED_SOUNDS = [
    { label: "Scream",          cmd: "$S", key: "scream",    editable: true },
    { label: "Short Circuit",   cmd: "$F", key: "faint",     editable: true },
    { label: "Leia Message",    cmd: "$L", key: "leia",      editable: true },
    { label: "Short Cantina",   cmd: "$c", key: "cantina_s", editable: true },
    { label: "Star Wars Theme", cmd: "$W", key: "sw_theme",  editable: true },
    { label: "Imperial March",  cmd: "$M", key: "imp_march", editable: true },
    { label: "Long Cantina",    cmd: "$C", key: "cantina_l", editable: true },
    { label: "Boot Sound",      cmd: "$B", key: "startup",   editable: true },
    { label: "Random On",       cmd: "$R", key: null,        editable: false },
    { label: "Random Off",      cmd: "$O", key: null,        editable: false },
    { label: "Stop / Chatter Off", cmd: "$s", key: null,     editable: false },
  ];

  // ---------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------
  const postAudio = async (params, feedbackEl) => {
    const body = new URLSearchParams(params);
    try {
      const res  = await fetch("/api/audio", { method: "POST", body });
      const json = await res.json();
      showFeedback(feedbackEl, json.ok ? "✅ Sent" : `❌ ${json.error}`, json.ok);
    } catch (_e) {
      showFeedback(feedbackEl, "❌ Request failed", false);
    }
  };

  const postTrack = async (key, track, feedbackEl) => {
    const body = new URLSearchParams({ key, track });
    try {
      const res  = await fetch("/api/audio/tracks", { method: "POST", body });
      const json = await res.json();
      showFeedback(feedbackEl, json.ok ? "✅ Saved" : `❌ ${json.error}`, json.ok);
    } catch (_e) {
      showFeedback(feedbackEl, "❌ Request failed", false);
    }
  };

  const showFeedback = (el, msg, ok) => {
    if (!el) return;
    el.textContent  = msg;
    el.className    = `feedback ${ok ? "success" : "error"}`;
    setTimeout(() => { if (el) { el.textContent = ""; el.className = "feedback"; } }, 2500);
  };

  // ---------------------------------------------------------------------------
  // Build named sound rows table
  // ---------------------------------------------------------------------------
  const tbody = document.getElementById("named-sound-rows");
  const rowFeedback = {};

  if (tbody) {
    NAMED_SOUNDS.forEach((s) => {
      const tr = document.createElement("tr");
      tr.style.borderBottom = "1px solid var(--border)";

      const tdLabel = document.createElement("td");
      tdLabel.style.padding = "8px";
      tdLabel.textContent   = s.label;

      const tdCmd = document.createElement("td");
      tdCmd.style.padding      = "8px";
      tdCmd.style.fontFamily   = "monospace";
      tdCmd.style.color        = "var(--accent)";
      tdCmd.textContent        = s.cmd;

      const tdTrack = document.createElement("td");
      tdTrack.style.padding = "8px";

      const fbEl = document.createElement("span");
      fbEl.style.marginLeft = "8px";
      fbEl.style.fontSize   = "0.85rem";
      rowFeedback[s.key || s.cmd] = fbEl;

      if (s.editable) {
        const inp = document.createElement("input");
        inp.type          = "number";
        inp.min           = "1";
        inp.max           = "999";
        inp.style.width   = "70px";
        inp.dataset.key   = s.key;
        inp.id            = `track-input-${s.key}`;
        tdTrack.appendChild(inp);

        const saveBtn = document.createElement("button");
        saveBtn.className   = "btn";
        saveBtn.textContent = "💾";
        saveBtn.title       = "Save track number";
        saveBtn.style.marginLeft = "6px";
        saveBtn.style.padding    = "4px 8px";
        saveBtn.addEventListener("click", () => {
          const val = parseInt(inp.value, 10);
          if (!val || val < 1 || val > 999) {
            showFeedback(fbEl, "❌ 1–999", false);
            return;
          }
          postTrack(s.key, val, fbEl);
        });
        tdTrack.appendChild(saveBtn);
        tdTrack.appendChild(fbEl);
      } else {
        tdTrack.textContent = "—";
        tdTrack.style.color = "var(--text-dim)";
      }

      const tdPlay = document.createElement("td");
      tdPlay.style.padding = "8px";

      const playBtn = document.createElement("button");
      playBtn.className   = "btn";
      playBtn.textContent = "▶";
      playBtn.title       = `Play ${s.cmd}`;
      playBtn.style.padding = "4px 10px";
      playBtn.addEventListener("click", () => {
        const globalFb = document.getElementById("global-feedback");
        postAudio({ action: "dollar", cmd: s.cmd }, globalFb);
      });
      tdPlay.appendChild(playBtn);

      tr.appendChild(tdLabel);
      tr.appendChild(tdCmd);
      tr.appendChild(tdTrack);
      tr.appendChild(tdPlay);
      tbody.appendChild(tr);
    });
  }

  // ---------------------------------------------------------------------------
  // Load current track assignments from /api/audio/tracks
  // ---------------------------------------------------------------------------
  const loadTracks = async () => {
    try {
      const res  = await fetch("/api/audio/tracks", { cache: "no-store" });
      if (!res.ok) return;
      const data = await res.json();

      NAMED_SOUNDS.forEach((s) => {
        if (!s.editable || !s.key) return;
        const inp = document.getElementById(`track-input-${s.key}`);
        if (inp && data[s.key] !== undefined) {
          inp.value = data[s.key];
        }
      });

      // Random range
      const randMin = document.getElementById("rand-min");
      const randMax = document.getElementById("rand-max");
      if (randMin && data.rand_min !== undefined) randMin.value = data.rand_min;
      if (randMax && data.rand_max !== undefined) randMax.value = data.rand_max;
    } catch (_e) {}
  };

  loadTracks();

  // ---------------------------------------------------------------------------
  // Global audio state polling
  // ---------------------------------------------------------------------------
  const audioStateBadge  = document.getElementById("audio-state-badge");
  const soundDisabledCard = document.getElementById("sound-disabled-card");

  const pollState = async () => {
    try {
      const res  = await fetch("/api/status", { cache: "no-store" });
      if (!res.ok) return;
      const data = await res.json();

      const s2enabled = Boolean(data.s2Sound);
      if (soundDisabledCard) {
        soundDisabledCard.classList.toggle("hidden", s2enabled);
      }

      if (!audioStateBadge) return;
      if (!s2enabled) {
        audioStateBadge.textContent = "Disabled";
        audioStateBadge.style.color = "var(--text-dim)";
      } else {
        const s2 = data.s2Sound;
        audioStateBadge.textContent = s2.state === "playing" ? "🔊 Playing" : "✅ Idle";
        audioStateBadge.style.color = s2.state === "playing"
          ? "var(--success)" : "var(--text)";
      }
    } catch (_e) {}
  };

  pollState();
  window.setInterval(pollState, 2000);

  // ---------------------------------------------------------------------------
  // Volume slider
  // ---------------------------------------------------------------------------
  const volSlider = document.getElementById("vol-slider");
  const globalFb  = document.getElementById("global-feedback");

  volSlider?.addEventListener("change", () => {
    postAudio({ action: "volume", level: volSlider.value }, globalFb);
  });

  // ---------------------------------------------------------------------------
  // Global control buttons
  // ---------------------------------------------------------------------------
  document.getElementById("btn-stop")
    ?.addEventListener("click", () => postAudio({ action: "stop" }, globalFb));

  document.getElementById("btn-random-on")
    ?.addEventListener("click", () => postAudio({ action: "dollar", cmd: "$R" }, globalFb));

  document.getElementById("btn-random-off")
    ?.addEventListener("click", () => postAudio({ action: "dollar", cmd: "$O" }, globalFb));

  // ---------------------------------------------------------------------------
  // Direct track play
  // ---------------------------------------------------------------------------
  const directFb = document.getElementById("direct-feedback");

  document.getElementById("btn-direct-play")?.addEventListener("click", () => {
    const val = parseInt(document.getElementById("direct-track")?.value, 10);
    if (!val || val < 1 || val > 65535) {
      showFeedback(directFb, "❌ Track must be 1–65535", false);
      return;
    }
    postAudio({ action: "play", track: val }, directFb);
  });

  // ---------------------------------------------------------------------------
  // Random range save
  // ---------------------------------------------------------------------------
  const randFb = document.getElementById("rand-feedback");

  document.getElementById("btn-rand-save")?.addEventListener("click", async () => {
    const minVal = parseInt(document.getElementById("rand-min")?.value, 10);
    const maxVal = parseInt(document.getElementById("rand-max")?.value, 10);

    if (!minVal || minVal < 1 || minVal > 999 || !maxVal || maxVal < 1 || maxVal > 999) {
      showFeedback(randFb, "❌ Values must be 1–999", false);
      return;
    }
    if (minVal > maxVal) {
      showFeedback(randFb, "❌ Min must be ≤ Max", false);
      return;
    }

    const body1 = new URLSearchParams({ key: "rand_min", track: minVal });
    const body2 = new URLSearchParams({ key: "rand_max", track: maxVal });
    try {
      const [r1, r2] = await Promise.all([
        fetch("/api/audio/tracks", { method: "POST", body: body1 }),
        fetch("/api/audio/tracks", { method: "POST", body: body2 }),
      ]);
      const [j1, j2] = await Promise.all([r1.json(), r2.json()]);
      const ok = j1.ok && j2.ok;
      showFeedback(randFb, ok ? "✅ Range saved" : "❌ Save failed", ok);
    } catch (_e) {
      showFeedback(randFb, "❌ Request failed", false);
    }
  });
})();
