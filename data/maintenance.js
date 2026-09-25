// =============================================================================
// data/maintenance.js
//
// Maintenance: inspecting and repairing a controller that is already configured
// (CONTEXT.md "Maintenance", #288). The serial lanes, the diagnostics, the
// Memory Profiler, Backup & Restore, Restart - and the single
// deliberate way back into guided Setup, for a builder who skipped it or
// rebuilt the droid wholesale (#297).
//
// What the droid is made of is Configuration's (data/configuration.js); the two
// used to be one page (#404).
// =============================================================================

(() => {
  const rebootButton = document.getElementById("reboot-button");
  const rebootFeedback = document.getElementById("reboot-feedback");

  const setFeedbackState = (element, message, variant = "") => {
    if (!element) return;
    element.textContent = message;
    element.className = variant ? `feedback ${variant}` : "feedback";
  };

  // Reboot functionality

  const handleReboot = async () => {
    // A component change still on its way to the controller is lost if the
    // controller restarts under it. Restart shared a page with the component
    // toggles until #404 and was greyed out while one saved; now the two are
    // separate surfaces, so Configuration publishes the answer and this asks
    // it at the press.
    if (window.PAConfigurationSave?.isPending()) {
      setFeedbackState(rebootFeedback, "Still saving a component change. Press again in a moment.", "warning");
      return;
    }
    if (!confirm("Restart the Body Controller? This page drops for about 10 seconds.")) {
      return;
    }
    if (!window.PAApi) return;
    setFeedbackState(rebootFeedback, "Sending restart...");
    try {
      await window.PAApi.postForm("/api/reboot", {}, { timeoutMs: 5000 });
      setFeedbackState(rebootFeedback, "Restart sent. Back in about 10 seconds.", "success");
      // Start countdown
      let seconds = 12;
      const countdown = setInterval(() => {
        seconds--;
        if (rebootFeedback && seconds > 0) {
          rebootFeedback.textContent = `Restarting... ${seconds}s`;
        } else {
          clearInterval(countdown);
          if (rebootFeedback) {
            rebootFeedback.textContent = "The Body Controller should be back. Refresh the page.";
          }
        }
      }, 1000);
    } catch (error) {
      console.error("[maintenance] handleReboot failed:", error);
      setFeedbackState(rebootFeedback, window.PAApi.messageFor(error), "error");
    }
  };

  if (rebootButton) {
    rebootButton.addEventListener("click", handleReboot);
  }

  // --- Serial connection status ---
  const serialS1 = document.getElementById("serial-s1-state");
  const serialS2 = document.getElementById("serial-s2-state");
  const serialS3 = document.getElementById("serial-s3-state");
  const serialS1Light = document.getElementById("serial-s1-light");
  const serialS2Light = document.getElementById("serial-s2-light");
  const serialS3Light = document.getElementById("serial-s3-light");
  const serialStatusLine = document.getElementById("serial-status-line");
  const diagUptime = document.getElementById("diag-uptime");
  const diagHeapFree = document.getElementById("diag-heap-free");
  const diagHeapMin = document.getElementById("diag-heap-min");
  const diagHeapLargest = document.getElementById("diag-heap-largest");
  const diagHeapFreeLight = document.getElementById("diag-heap-free-light");
  const diagHeapMinLight = document.getElementById("diag-heap-min-light");
  const diagHeapLargestLight = document.getElementById("diag-heap-largest-light");

  // A health signal reads as a droid LED and the COLOR IS THE READING: the
  // light carries it and the value beside it stays ink (CONTEXT.md "Health
  // Signal", "Status Color"). Before this the state was painted onto the text
  // with element.style.color and spelled with an emoji beside it, which put a
  // color on a number and a picture in a readout.
  const setLight = (light, state) => {
    if (light) light.className = `indicator ${state}`;
  };

  const formatUptime = (uptimeMs) => {
    const totalSeconds = Math.floor(Number(uptimeMs || 0) / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    return `${hours}h ${minutes}m ${seconds}s`;
  };

  const renderSerialStatus = (d, receivedAt) => {
    // The same four readings as before, on the same four states: what changed
    // is that the light carries the color and the words carry the reading.
    // "off" for a lane that is switched off is the grey CONTEXT.md "Health
    // Signal" asks for - a thing never asked reads grey, never green.
    if (serialS1) {
      serialS1.textContent = !d.drive ? "Disabled"
        : d.drive.state === "commanding" ? "Active" : "Enabled / Idle";
      setLight(serialS1Light, d.drive ? "ok" : "off");
    }
    if (serialS2) {
      serialS2.textContent = !d.audio ? "Disabled"
        : d.audio.state === "playing" ? "Playing" : "Enabled / Idle";
      setLight(serialS2Light, d.audio ? "ok" : "off");
    }
    if (serialS3) {
      const dl = d.dome_link;
      const transport = typeof dl?.transport === "string" ? dl.transport.toUpperCase() : "N/A";
      if (!dl || dl.state === "disabled") {
        serialS3.textContent = "Disabled";
        setLight(serialS3Light, "off");
      } else if (dl.state === "connected") {
        serialS3.textContent = `Connected (${transport}, hb rx ${dl.hb_rx} / tx ${dl.hb_tx})`;
        setLight(serialS3Light, "ok");
      } else if (dl.state === "lost") {
        serialS3.textContent = `Lost (${transport}) — last seen ${dl.last_rx_ms} ms ago`;
        setLight(serialS3Light, "fail");
      } else {
        serialS3.textContent = `Waiting for dome heartbeat (${transport})`;
        setLight(serialS3Light, "warn");
      }
    }
    // Uptime is telemetry, not a health signal: a number that has never been a
    // state carried a green of its own here until this slice took it off.
    if (diagUptime) {
      diagUptime.textContent = formatUptime(d.uptimeMs);
    }

    const heapFreeKb = Math.round((d.heapFree || 0) / 1024);
    const heapMinKb = Math.round((d.heapMin || 0) / 1024);
    const hasLargest = d.heapLargestBlock !== undefined && d.heapLargestBlock !== null;
    const heapLargestKb = hasLargest ? Math.round(d.heapLargestBlock / 1024) : null;

    const t = window.PA_HEAP || {};
    const heapFreeState = heapFreeKb < Math.round((t.freeCritical || 40000) / 1024) ? "critical" : heapFreeKb < Math.round((t.freeWarn || 65000) / 1024) ? "watch" : "good";
    const heapMinState = heapMinKb < Math.round((t.minCritical || 36864) / 1024) ? "critical" : heapMinKb < Math.round((t.minWarn || 53248) / 1024) ? "watch" : "good";
    const heapLargestState = !hasLargest ? "na" : heapLargestKb < Math.round((t.largestCritical || 20480) / 1024) ? "critical" : heapLargestKb < Math.round((t.largestWarn || 36864) / 1024) ? "watch" : "good";

    // The same four states as before, on the same thresholds. "na" is the
    // firmware that reports no largest block at all - never asked, so grey.
    const lampForState = (state) =>
      state === "critical" ? "fail" : state === "watch" ? "warn" : state === "na" ? "off" : "ok";

    if (diagHeapFree) {
      const word = heapFreeState === "critical" ? "Critical" : heapFreeState === "watch" ? "Watch" : "Good";
      diagHeapFree.textContent = `${heapFreeKb} KB ${word}`;
      setLight(diagHeapFreeLight, lampForState(heapFreeState));
    }
    if (diagHeapMin) {
      const word = heapMinState === "critical" ? "Critical" : heapMinState === "watch" ? "Watch" : "Good";
      diagHeapMin.textContent = `${heapMinKb} KB ${word}`;
      setLight(diagHeapMinLight, lampForState(heapMinState));
    }
    if (diagHeapLargest) {
      if (!hasLargest) {
        diagHeapLargest.textContent = window.PALiveReading.UNKNOWN;
      } else {
        const word = heapLargestState === "critical" ? "Fragmented" : heapLargestState === "watch" ? "Watch" : "Good";
        diagHeapLargest.textContent = `${heapLargestKb} KB ${word}`;
      }
      setLight(diagHeapLargestLight, lampForState(heapLargestState));
    }
    // When the droid sent it, not when it was painted: a lost link repaints
    // the same frame, and "Updated" must not move with it.
    setFeedbackState(serialStatusLine, `Updated ${new Date(receivedAt).toLocaleTimeString()}`, "success");
  };

  // Every reading here rides the Live Reading, which owns the stream or the one
  // fallback poll for the whole shell (data/live_reading.js). Before the droid
  // has sent a frame each readout says so in its words; once contact is lost
  // the values stay, and the status line says they are not being refreshed.
  const FINDING_OUT_READOUTS = [serialS1, serialS2, serialS3, diagUptime, diagHeapFree, diagHeapMin, diagHeapLargest];
  const renderReading = (reading) => {
    if (reading.status === null) {
      FINDING_OUT_READOUTS.forEach((node) => {
        if (node) node.textContent = window.PALiveReading.FINDING_OUT;
      });
      return;
    }
    renderSerialStatus(reading.status, reading.receivedAt);
    if (reading.notHearing === "link") setFeedbackState(serialStatusLine, "Status unavailable", "error");
  };

  window.PALiveReading.subscribe(renderReading);
})();


// =============================================================================
// Backup & Restore
// =============================================================================
(() => {
  const downloadBtn = document.getElementById('backup-download-btn');
  const fileInput = document.getElementById('backup-file-input');
  const fileTrigger = document.getElementById('backup-file-trigger');
  const summary = document.getElementById('backup-summary');
  const restoreSections = document.getElementById('restore-sections');
  const restoreBtnRow = document.getElementById('restore-btn-row');
  const restoreBtn = document.getElementById('backup-restore-btn');
  const feedback = document.getElementById('backup-feedback');

  if (!downloadBtn || !fileInput || !feedback) return;

  let parsedBackup = null;

  const setFeedback = (msg, variant = '') => {
    feedback.textContent = msg;
    feedback.className = variant ? `feedback ${variant}` : 'feedback';
  };

  const showRestorePanel = (show) => {
    if (summary) summary.hidden = !show;
    if (restoreSections) restoreSections.hidden = !show;
    if (restoreBtnRow) restoreBtnRow.hidden = !show;
  };

  // ---- DOWNLOAD BACKUP ----
  const downloadBackup = async () => {
    if (!window.PAApi) return;
    downloadBtn.disabled = true;
    setFeedback('Downloading settings...');
    try {
      // The Outputs' centre, `calibrated` and Part map are on
      // /api/servo/outputs, not /api/config (ADR 0056 puts them in the
      // Configuration group all the same), so a backup carries both (#417).
      const [configRes, servoOutputsRes, rcMapRes, tracksRes, moodMapRes, fwRes] = await Promise.allSettled([
        window.PAApi.get('/api/config', { timeoutMs: 10000 }),
        window.PAApi.get('/api/servo/outputs', { timeoutMs: 10000 }),
        window.PAApi.get('/api/rc/map', { timeoutMs: 10000 }),
        window.PAApi.get('/api/audio/tracks', { timeoutMs: 10000 }),
        window.PAApi.get('/api/audio/mood-map', { timeoutMs: 10000 }),
        fetch('/fw-version.json').then((r) => r.json()).catch(() => ({})),
      ]);

      const failed = [];
      const extract = (res, label) => {
        if (res.status === 'fulfilled') return res.value?.data ?? null;
        failed.push(label);
        return null;
      };

      const config = extract(configRes, 'config');
      const servo_outputs = extract(servoOutputsRes, 'servo_outputs');
      const rc_map = extract(rcMapRes, 'rc_map');
      const audio_tracks = extract(tracksRes, 'audio_tracks');
      const audio_mood_map = extract(moodMapRes, 'audio_mood_map');

      if (failed.length > 0) {
        setFeedback(`No backup saved: the droid did not send ${failed.join(', ')}.`, 'error');
        return;
      }

      const fw_version =
        fwRes.status === 'fulfilled' ? (fwRes.value?.firmwareVersion || 'unknown') : 'unknown';

      const backup = {
        schema: 1,
        generated: new Date().toISOString(),
        fw_version,
        config,
        servo_outputs,
        rc_map,
        audio_tracks,
        audio_mood_map,
      };

      const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `artoo-backup-${new Date().toISOString().slice(0, 10)}.json`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);

      setFeedback(`Backup downloaded at ${new Date().toLocaleTimeString()}`, 'success');
    } catch (err) {
      setFeedback(`Backup failed: ${window.PAApi?.messageFor(err) || err.message}`, 'error');
    } finally {
      downloadBtn.disabled = false;
    }
  };

  // ---- RESTORE: the Configuration, in the shape it was read (ADR 0068) ----
  // A backup is GET /api/config and GET /api/servo/outputs as the droid answered
  // them, and POST /api/config takes the two back in one body: the config as it
  // was read, and each Output's row as `outputs`. One request and one Write
  // Window, so the Configuration lands whole or not at all, and a restore
  // replaces the part it writes (ADR 0056) - the Part map included.
  //
  // THE ONE PLACE THAT KNOWS AN OLDER BACKUP. Until an Output was read whole
  // from its row, /api/config carried its settings too: under components[<id>]
  // (enabled, type, ledCount, throwMs, accelMs, ease, boot) and as top-level
  // <id>OpenUs / <id>CloseUs. A backup made then is folded into rows here, once,
  // matched by the stored id the droid's own row carries; where the backup's
  // rows say something too, the row wins. Nothing else in the browser or the
  // firmware knows that shape.
  const ROW_SETTINGS = [
    'wired', 'component', 'ledCount', 'throwMs', 'accelMs', 'ease', 'boot',
    'openUs', 'centreUs', 'closeUs', 'calibrated', 'parts',
  ];
  const OLDER_SETTINGS = [
    ['enabled', 'wired'], ['type', 'component'], ['ledCount', 'ledCount'],
    ['throwMs', 'throwMs'], ['accelMs', 'accelMs'], ['ease', 'ease'], ['boot', 'boot'],
  ];

  const olderRow = (cfg, id) => {
    const row = {};
    const saved = cfg?.components?.[id];
    if (saved && typeof saved === 'object') {
      OLDER_SETTINGS.forEach(([from, to]) => {
        if (saved[from] !== undefined) row[to] = saved[from];
      });
    }
    if (cfg?.[`${id}OpenUs`] !== undefined) row.openUs = cfg[`${id}OpenUs`];
    if (cfg?.[`${id}CloseUs`] !== undefined) row.closeUs = cfg[`${id}CloseUs`];
    return row;
  };

  // The settings the backup holds for each Output this droid has, as rows. A
  // row's readings - its name, its band, where it was told to be - stay behind:
  // they are not settings, and the body stays inside what the droid buffers.
  // An Output the backup names and this droid does not have is not sent, and
  // the receipt says so.
  const rowsToRestore = (backup, outputs) => {
    const settings = new Map();
    const names = new Map();
    outputs.forEach((output) => {
      if (!output.id) return;
      const row = olderRow(backup.config, output.id);
      if (Object.keys(row).length > 0) settings.set(output.address, row);
    });
    const saved = backup.servo_outputs?.outputs;
    (Array.isArray(saved) ? saved : []).forEach((row) => {
      if (!row || typeof row.address !== 'string') return;
      const into = settings.get(row.address) || {};
      ROW_SETTINGS.forEach((key) => {
        if (row[key] !== undefined) into[key] = row[key];
      });
      settings.set(row.address, into);
      names.set(row.address, row.name || row.address);
    });
    const here = new Set(outputs.map((output) => output.address));
    const rows = [];
    const missing = [];
    settings.forEach((row, address) => {
      if (here.has(address)) rows.push({ address, ...row });
      else missing.push(names.get(address) || address);
    });
    return { rows, missing };
  };

  const restoreConfiguration = async (backup) => {
    const { outputs } = await window.PAOutputs.load();
    const { rows, missing } = rowsToRestore(backup, outputs);
    await window.PAApi.postJson('/api/config', { ...backup.config, outputs: rows }, { timeoutMs: 10000 });
    const gaps = missing.map((name) => `${name} not on this droid`);
    // A file from before backups carried the Outputs' rows has no centre,
    // calibration or Part map to give back.
    if (!Array.isArray(backup.servo_outputs?.outputs)) gaps.push('no centre, calibration or Part map in this file');
    return gaps;
  };

  // ---- RESTORE: audio tracks (one POST per key) ----
  const TRACKS_SKIP = new Set(['volume', 'chirp_bindings', 'chirp_category_bindings']);
  // A category's lo/hi pair, which only /api/audio/category-range writes
  // together with its CHIRP bank and page. A plain track post would leave the
  // bank and page behind (#417).
  const isCategoryKey = (key) => key.startsWith('snd_cat_');

  const restoreAudioTracks = async (tracks) => {
    const failed = [];
    for (const [key, value] of Object.entries(tracks)) {
      if (TRACKS_SKIP.has(key) || isCategoryKey(key) || typeof value !== 'number') continue;
      // A banked slot goes back banked or it has failed. Falling back to a
      // plain track would write 0 to its binding and still read as restored.
      const chirp = tracks.chirp_bindings?.[key];
      const fields = chirp
        ? { key, track: chirp.index, bank: chirp.bank, page: chirp.page }
        : { key, track: value };
      try {
        await window.PAApi.postForm('/api/audio/tracks', new URLSearchParams(fields), { timeoutMs: 5000 });
      } catch {
        failed.push(key);
      }
    }
    // Each category pair with its bank and page, the payload data/sound.js
    // sends. A file from a droid with a CHIRP catalog lists every bound pair,
    // so a pair it does not list was unbound there and is cleared here: a
    // restore replaces.
    const categoryBindings = tracks.chirp_category_bindings;
    for (const loKey of Object.keys(tracks).filter((key) => isCategoryKey(key) && key.endsWith('_lo'))) {
      const hiKey = loKey.replace(/_lo$/, '_hi');
      if (typeof tracks[loKey] !== 'number' || typeof tracks[hiKey] !== 'number') {
        failed.push(loKey);
        continue;
      }
      const payload = { lo_key: loKey, hi_key: hiKey, lo: tracks[loKey], hi: tracks[hiKey] };
      const binding = categoryBindings?.[loKey];
      if (binding?.bank && binding?.page) {
        payload.bank = binding.bank;
        payload.page = binding.page;
      } else if (categoryBindings && typeof categoryBindings === 'object') {
        payload.clear_binding = 1;
      }
      try {
        await window.PAApi.postForm('/api/audio/category-range', payload, { timeoutMs: 3000 });
      } catch {
        failed.push(loKey);
      }
    }
    if (typeof tracks.volume === 'number') {
      try {
        await window.PAApi.postForm('/api/audio',
          new URLSearchParams({ action: 'volume', level: tracks.volume }), { timeoutMs: 5000 });
      } catch {
        failed.push('volume');
      }
    }
    return failed;
  };

  // ---- RESTORE: apply all selected sections ----
  const performRestore = async () => {
    if (!parsedBackup || !window.PAApi) return;
    restoreBtn.disabled = true;
    setFeedback('Restoring...');

    const lines = [];

    const chkConfig = document.getElementById('restore-chk-config');
    const chkRcMap = document.getElementById('restore-chk-rc-map');
    const chkTracks = document.getElementById('restore-chk-audio-tracks');
    const chkMoodMap = document.getElementById('restore-chk-mood-map');

    if (chkConfig?.checked && parsedBackup.config) {
      try {
        // "restored" only when all of it landed.
        const gaps = await restoreConfiguration(parsedBackup);
        lines.push(gaps.length === 0 ? 'Core config: restored' : `Core config: partial — ${gaps.join(', ')}`);
      } catch (err) {
        // A refusal about an Output's row is worded by the module that knows
        // the Outputs; anything else is said as the droid said it.
        lines.push(`Core config: FAILED — ${window.PAApi.messageFor(window.PAOutputs.sayRefusal(err))}`);
      }
    }

    if (chkRcMap?.checked && parsedBackup.rc_map) {
      try {
        await window.PAApi.postForm('/api/rc/map',
          { plain: JSON.stringify(parsedBackup.rc_map) }, { timeoutMs: 10000 });
        lines.push('RC mappings: restored');
      } catch (err) {
        lines.push(`RC mappings: FAILED — ${window.PAApi.messageFor(err)}`);
      }
    }

    if (chkTracks?.checked && parsedBackup.audio_tracks) {
      const failed = await restoreAudioTracks(parsedBackup.audio_tracks);
      lines.push(
        failed.length === 0
          ? 'Audio tracks: restored'
          : `Audio tracks: partial — ${failed.length} failed (${failed.join(', ')})`,
      );
    }

    if (chkMoodMap?.checked && parsedBackup.audio_mood_map) {
      try {
        await window.PAApi.postForm('/api/audio/mood-map', parsedBackup.audio_mood_map,
          { timeoutMs: 5000 });
        lines.push('Audio mood map: restored');
      } catch (err) {
        lines.push(`Audio mood map: FAILED — ${window.PAApi.messageFor(err)}`);
      }
    }

    const anyRestored = lines.some((l) => l.includes(': restored'));
    const anyIssue = lines.some((l) => l.includes('FAILED') || l.includes('partial'));
    if (anyRestored) lines.push('Restart the Body Controller to apply everything restored.');
    setFeedback(lines.join('\n'), anyIssue ? 'error' : 'success');
    restoreBtn.disabled = false;
  };

  // ---- FILE PARSE ----
  const handleFile = (file) => {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (e) => {
      let backup;
      try {
        backup = JSON.parse(e.target.result);
      } catch {
        setFeedback('Not a backup file: it is not JSON. Nothing restored.', 'error');
        parsedBackup = null;
        showRestorePanel(false);
        return;
      }

      if (!backup.schema) {
        setFeedback('Not a backup file: it has no schema. Nothing restored.', 'error');
        parsedBackup = null;
        showRestorePanel(false);
        return;
      }

      parsedBackup = backup;

      const date = backup.generated ? backup.generated.slice(0, 10) : 'unknown';
      const fw = backup.fw_version || 'unknown';
      const sections = ['config', 'rc_map', 'audio_tracks', 'audio_mood_map'].filter(
        (k) => backup[k],
      );

      if (summary) {
        summary.textContent =
          `Backup from ${date}, firmware ${fw} — ${sections.length} section${sections.length !== 1 ? 's' : ''} found`;
      }

      [
        ['restore-chk-config', 'config'],
        ['restore-chk-rc-map', 'rc_map'],
        ['restore-chk-audio-tracks', 'audio_tracks'],
        ['restore-chk-mood-map', 'audio_mood_map'],
      ].forEach(([id, key]) => {
        const chk = document.getElementById(id);
        if (chk) { chk.checked = Boolean(backup[key]); chk.disabled = !backup[key]; }
      });

      showRestorePanel(true);

      if (backup.schema > 1) {
        setFeedback(
          `This backup is from newer firmware (schema ${backup.schema}). Some of it may not restore.`,
          'warning',
        );
      } else {
        setFeedback('');
      }
    };
    reader.readAsText(file);
  };

  downloadBtn.addEventListener('click', downloadBackup);
  if (fileTrigger) fileTrigger.addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', () => handleFile(fileInput.files?.[0] ?? null));
  if (restoreBtn) restoreBtn.addEventListener('click', performRestore);
})();

// =============================================================================
// Memory Profiler UI
// Feature Availability comes from the identity manifest. The profiler endpoint
// is polled only after that manifest says this image contains the profiler.
// =============================================================================
(() => {
  const card = document.getElementById("profiler-card");
  if (!card) return;
  const content = document.getElementById("profiler-content");
  const availabilityStatus = document.getElementById("profiler-availability-status");
  const availabilityReason = document.getElementById("profiler-availability-reason");
  const availabilityLamp = document.getElementById("profiler-availability-lamp");
  const feedback = document.getElementById("profiler-feedback");

  function kb(bytes) {
    return (bytes / 1024).toFixed(1) + " KB";
  }

  // The same two thresholds as before, answering with a Status Color state
  // rather than with a hard-coded hex: a color literal outside :root is a
  // defect (CONTEXT.md "Status Color"), and these three were Material's own
  // green, amber and red rather than the droid's.
  function hwmState(hwm) {
    if (hwm > 2048) return "ok";
    if (hwm > 1024) return "warn";
    return "fail";
  }

  function fragState(ratio) {
    if (ratio < 0.30) return "ok";
    if (ratio < 0.50) return "warn";
    return "fail";
  }

  function setText(id, val) {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
  }

  function renderProfiler(d) {
    setText("prof-heap-free",    kb(d.heapFree));
    setText("prof-heap-min",     kb(d.heapMin));
    setText("prof-heap-largest", kb(d.heapLargest));
    setText("prof-frag-ratio",   (d.fragRatio * 100).toFixed(1) + "%");
    setText("prof-alloc-blocks", d.allocBlocks);
    setText("prof-free-blocks",  d.freeBlocks);
    setText("prof-failed-allocs", d.failedAllocs);

    // Fragmentation bar
    const pct = Math.min(d.fragRatio * 100, 100);
    const bar = document.getElementById("prof-frag-bar");
    const lbl = document.getElementById("prof-frag-label");
    if (bar) {
      bar.style.width = pct.toFixed(1) + "%";
      bar.className = `prof-bar is-${fragState(d.fragRatio)}`;
    }
    if (lbl) {
      const health = d.fragRatio < 0.30 ? "Healthy" : d.fragRatio < 0.50 ? "Watch" : "Critical";
      lbl.textContent = health + " — fragmentation " + pct.toFixed(1) + "% (1 - largest/free)";
    }

    // Task stack HWM table. The state is a signal light in its own cell, so the
    // word beside it stays ink: the table reads down the lights the way the
    // health grid does.
    const hwmTbody = document.getElementById("prof-hwm-tbody");
    if (hwmTbody && Array.isArray(d.taskStacks)) {
      hwmTbody.innerHTML = d.taskStacks.map(t => `<tr>
          <td>${t.name}</td>
          <td class="num">${kb(t.hwmBytes)}</td>
          <td class="num"><span class="indicator ${hwmState(t.hwmBytes)}"></span>${t.status.toUpperCase()}</td>
        </tr>`).join("");
    }

    // Task heap table (Tier 2 — only when taskHeap present)
    const heapSection = document.getElementById("prof-task-heap-section");
    const heapTbody = document.getElementById("prof-heap-tbody");
    if (heapSection && heapTbody && Array.isArray(d.taskHeap) && d.taskHeap.length > 0) {
      heapSection.hidden = false;
      heapTbody.innerHTML = d.taskHeap.map(t => `<tr>
        <td>${t.name}</td>
        <td class="num">${kb(t.current)}</td>
        <td class="num">${kb(t.peak)}</td>
        <td class="num">${t.heapCount}</td>
      </tr>`).join("");
    } else if (heapSection) {
      heapSection.hidden = true;
    }

    // Active window banner
    const currentEl = document.getElementById("prof-current-window");
    if (currentEl) {
      if (d.current) {
        currentEl.textContent = `Active: "${d.current.label}" — running min ${kb(d.current.heapFree)}`;
        currentEl.hidden = false;
      } else {
        currentEl.hidden = true;
      }
    }

    // Snapshot history table
    const snapTbody = document.getElementById("prof-snap-tbody");
    if (snapTbody && Array.isArray(d.snapshots)) {
      snapTbody.innerHTML = d.snapshots.map(s => `<tr>
        <td>${s.label}</td>
        <td class="num">${kb(s.heapFree)}</td>
        <td class="num">${kb(s.largestBlock)}</td>
        <td class="num">${s.ts}</td>
      </tr>`).join("");
    }
  }

  // Says so in the feedback line, then rethrows. The rethrow is what the poll
  // below reads: a profiler reading nobody could take must not come back from
  // the surface registry as a fresh one (#360).
  async function refreshProfiler() {
    try {
      const result = await window.PAApi.get("/api/profiler");
      renderProfiler(result.data);
      if (feedback) {
        feedback.textContent = `Memory readings updated at ${new Date().toLocaleTimeString()}`;
        feedback.className = "feedback success";
      }
    } catch (error) {
      if (feedback) {
        feedback.textContent = `Memory readings unavailable: ${window.PAApi.messageFor(error)}`;
        feedback.className = "feedback warning";
      }
      throw error;
    }
  }

  // The memory profiler is the other surface an operator opens when something
  // is already wrong, so it is owned by this surface: the shell stops it the
  // moment they read something else and starts it again on the way back
  // (ADR 0048, #360). Two answers gate it and they are asked in different
  // places -- whether this build HAS a profiler is the manifest's, below;
  // whether asking is wanted at all is the shell's.
  const poll = window.PASurface.poll(refreshProfiler, {
    cadenceMs: 5000,
    runOnStart: true,
    refreshOnReturn: true,
  });
  let polling = false;

  // Render the profiler's declared requirements and own its poll lifecycle;
  // the identity subscriber calls this after every availability transition.
  const renderAvailability = () => {
    const featureName = "Memory Profiler";
    const hasRequirementMetadata = Boolean(card.dataset.boardCapability || card.dataset.buildFlag);
    const result = hasRequirementMetadata
      ? window.PAFeatureAvailability.resolve({
          boardCapability: card.dataset.boardCapability || "",
          buildFlag: card.dataset.buildFlag || "",
          hasToggle: false,  // Profiler is compile-time only, no runtime toggle
        })
      : { phase: "checking", state: "checking" };
    // Derive available from phase and state: control is interactable when
    // the manifest is ready and the feature is not gated
    const available = window.PAFeatureAvailability.isFeatureAvailable(result);
    const stateLabel = window.PAFeatureAvailability.labelFor(result.state);
    // The reason's own copy, and where the builder goes about it. Both come
    // from the Availability seam (data/feature_availability.js) so the route is
    // painted as a link rather than named in a sentence nobody can click.
    const stateReasonOptions = {
      on: "Live memory readings refresh while this page is open.",
      notInThisBuild: "Memory Profiler is included only in troubleshooting firmware.", // PROVISIONAL: when a second Build Feature Flag needs a bespoke reason, promote this to a registry field + drift-checker coverage
    };
    card.hidden = false;
    card.classList.remove(
      "feature-state-on",
      "feature-state-not-in-this-build",
      "feature-state-not-on-this-board",
      "feature-state-checking",
      "feature-state-identity-unavailable",
    );
    card.classList.add("feature-availability-panel", `feature-state-${result.state}`);
    // Terminal and retryable identity failures are two families, which the
    // state class alone cannot say (data/feature_availability.js).
    card.classList.remove(...window.PAFeatureAvailability.FAMILY_CLASSES);
    const family = window.PAFeatureAvailability.familyClassFor(result.state);
    if (family) card.classList.add(family);
    card.dataset.featureState = result.state;

    if (availabilityStatus) {
      availabilityStatus.textContent = stateLabel;
      availabilityStatus.className = `feature-availability-status feature-state feature-state-${result.state}`;
    }
    if (availabilityReason) {
      // The sentence, then the route to the next move as a link where this
      // state's family has one (#348).
      availabilityReason.textContent =
        window.PAFeatureAvailability.reasonFor(result.state, featureName, stateReasonOptions);
      const route = window.PAFeatureAvailability.routeFor(result.state);
      if (route) {
        availabilityReason.textContent = `${availabilityReason.textContent} `;
        const link = document.createElement("a");
        link.className = "setup-link";
        link.setAttribute("href", route.href);
        link.textContent = `${route.label}.`;
        availabilityReason.appendChild(link);
      }
    }
    if (availabilityLamp) {
      availabilityLamp.className = `feature-availability-lamp-indicator feature-state-${result.state}`;
    }
    if (content) {
      content.inert = !available;
      content.setAttribute("aria-hidden", available ? "false" : "true");
    }

    if (available && !polling) {
      polling = true;
      poll.start();
    } else if (!available && polling) {
      // Stopping the poll on availability loss is part of inertness: when the
      // profiler is not available (compile-time gate or missing from this image),
      // cease endpoint polling to avoid false "update failed" messages.
      polling = false;
      poll.stop();
    }
  };

  window.PAFeatureAvailability.subscribe(renderAvailability);
})();


// =============================================================================
// The way back into guided Setup (#297, CONTEXT.md "Maintenance")
//
// ONE way in. Guided Setup is drawn over Configuration whenever the droid is
// not set up (data/setup.js), and this is the only control that makes a droid
// that is set up read as not set up again: it writes the run back to not-run,
// as the ordinary config key Backup & Restore already carries, and takes the
// builder to Configuration, where the run then opens. Nothing here draws a
// step and nothing here decides whether the run is open - that rule is
// data/setup.js's, and a second copy of it would be a second answer.
//
// It clears nothing the builder answered. Every component stays as it is, and
// so does the record of which questions were shown: those questions WERE
// asked, and reporting them as never asked is the untruth that record exists
// to prevent (#351).
// =============================================================================
(() => {
  const button = document.getElementById("setup-again-button");
  const feedback = document.getElementById("setup-again-feedback");
  if (!button) return;

  const setFeedback = (message, variant = "") => {
    if (!feedback) return;
    feedback.textContent = message;
    feedback.className = variant ? `feedback ${variant}` : "feedback";
  };

  const runAgain = async () => {
    if (!window.PAApi) return;
    button.disabled = true;
    setFeedback("Opening guided Setup…");
    try {
      // Read at the press rather than when this surface was mounted: the run
      // may have been written since, on Configuration or in another tab.
      const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
      const guided = result.data?.guidedSetup || {};
      const body = { guidedSetupRun: "not-run" };
      // A droid configured before the record existed carries none, and
      // data/setup.js reads such a droid as set up whatever its run says - so
      // without a record, not-run would reopen nothing. Writing one, empty
      // because nothing has been shown on it, is what makes not-run mean not
      // set up. A droid that has a record keeps it exactly as it stands: the
      // controller merges the two halves separately (src/web/api_config.cpp),
      // so a request that does not carry the list leaves it alone.
      if (guided.recorded === false) body.guidedSetupVisited = "-";
      await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
    } catch (error) {
      console.error("[maintenance] reopening guided Setup failed:", error);
      setFeedback(`The droid did not reopen guided Setup: ${window.PAApi.messageFor(error)}`, "error");
      button.disabled = false;
      return;
    }
    button.disabled = false;
    setFeedback("");
    // Configuration may already be mounted this session, holding a run it read
    // as ended, and it reads the run once; the event is what asks it to read
    // again. Mounted for the first time, it reads the run anyway.
    //
    // ORDER IS THE POINT. The event goes out from a hashchange listener, which
    // runs after the shell's own - shell.js loads before this file - so by
    // then the shell has put Configuration back in the document. data/setup.js
    // lays the run out against whatever surface is in the document, and told
    // any earlier it would hide this surface's cards instead of its own.
    window.addEventListener(
      "hashchange",
      () => {
        if (typeof window.dispatchEvent === "function") {
          window.dispatchEvent(new CustomEvent("pa:guided-setup-reopened"));
        }
      },
      { once: true },
    );
    window.location.hash = "configuration";
  };

  button.addEventListener("click", runAgain);
})();
