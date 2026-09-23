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

  const renderSerialStatus = (d) => {
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
        diagHeapLargest.textContent = "Not reported by this firmware";
      } else {
        const word = heapLargestState === "critical" ? "Fragmented" : heapLargestState === "watch" ? "Watch" : "Good";
        diagHeapLargest.textContent = `${heapLargestKb} KB ${word}`;
      }
      setLight(diagHeapLargestLight, lampForState(heapLargestState));
    }
    setFeedbackState(serialStatusLine, `Updated ${new Date().toLocaleTimeString()}`, "success");
  };

  // Says so on the status line, then rethrows: the surface poll below has to be
  // able to tell a read that landed from one that did not, and swallowing here
  // would tell it every read landed (#360).
  const refreshSerialStatus = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
      renderSerialStatus(result.data);
    } catch (error) {
      setFeedbackState(serialStatusLine, "Status unavailable", "error");
      throw error;
    }
  };

  // SSE-first serial status updates with visibility-aware fallback polling.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderSerialStatus(payload);
    });
    // One-shot fetch if SSE hasn't delivered a status frame yet. The status
    // line already carries the failure; the stream is what this page reads
    // from after it.
    if (!window.PAStatusStream.getLastStatus()) {
      refreshSerialStatus().catch(() => {});
    }
  } else {
    // Fallback: poll every 5 s, suspended while the tab is hidden and while the
    // operator is reading another surface -- the shell stops it on the way out
    // and starts it again on the way back (ADR 0048, #360). The failed read is
    // PASurface.poll()'s to report, so that a refresh that never landed does
    // not take the "showing what this screen last read" note down (#360).
    window.PASurface.poll(refreshSerialStatus, {
      cadenceMs: 5000,
      runOnStart: true,
      refreshOnReturn: true,
    }).start();
  }
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
      const [configRes, rcMapRes, tracksRes, moodMapRes, fwRes] = await Promise.allSettled([
        window.PAApi.get('/api/config', { timeoutMs: 10000 }),
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

  // ---- RESTORE: the Outputs this droid has, as its firmware reports them ----
  // Which Outputs exist, and which config fields save each, is the running
  // firmware's answer (GET /api/config: every components{} entry carrying an
  // `address`, with its enabledField and typeField). A backup is matched to it
  // by the Output's stored id, so a backup made before the firmware reported
  // those fields restores the same way, and this page lists no Output of its
  // own (ADR 0033 Amendment 2026-09-19).
  const liveOutputs = async () => {
    const result = await window.PAApi.get('/api/config', { timeoutMs: 5000 });
    const components = result?.data?.components || {};
    return Object.keys(components)
      .filter((id) => typeof components[id]?.address === 'string'
        && typeof components[id].enabledField === 'string'
        && typeof components[id].typeField === 'string')
      .map((id) => ({
        id,
        enabledField: components[id].enabledField,
        typeField: components[id].typeField,
        // Absent on an Output that cannot carry a light, which is what stops a
        // restore inventing a field for one.
        ledCountField: typeof components[id].ledCountField === 'string'
          ? components[id].ledCountField : '',
      }));
  };

  // ---- RESTORE: flatten GET /api/config nested JSON to POST form params ----
  const configToFormParams = (cfg, outputs) => {
    const p = new URLSearchParams();
    const d = cfg?.drive || {};
    const rc = cfg?.rc || {};
    const components = cfg?.components || {};
    const domeEsc = cfg?.domeEsc || {};
    const protoR2link = cfg?.protoR2link || {};
    const sys = cfg?.system || {};

    if (d.speedLimitMax !== undefined) p.set('speedLimitMax', d.speedLimitMax);
    if (d.speedPresetSlow !== undefined) p.set('speedPresetSlow', d.speedPresetSlow);
    if (d.speedPresetNormal !== undefined) p.set('speedPresetNormal', d.speedPresetNormal);
    if (d.speedPresetTurbo !== undefined) p.set('speedPresetTurbo', d.speedPresetTurbo);
    if (d.stationary !== undefined) p.set('stationary', d.stationary ? 'true' : 'false');
    if (d.webDriveTimeoutMs !== undefined) p.set('webDriveTimeoutMs', d.webDriveTimeoutMs);

    if (rc.sbusTimeoutMs !== undefined) p.set('sbusTimeoutMs', rc.sbusTimeoutMs);
    if (rc.inputMode !== undefined) p.set('rcInputMode', rc.inputMode);
    // The RC Radio, the Radio Controller's Component Member (#369).
    if (rc.member !== undefined) p.set('rcMember', rc.member);
    if (rc?.sbus?.recvCh2 !== undefined) p.set('sbusRecvCh2', rc.sbus.recvCh2 ? 'true' : 'false');

    outputs.forEach(({ id, enabledField, typeField, ledCountField }) => {
      if (components[id]?.enabled !== undefined) p.set(enabledField, components[id].enabled ? 'true' : 'false');
      if (components[id]?.type !== undefined) p.set(typeField, components[id].type);
      // A light's settings, under the field that Output named for them. The
      // droid-wide aux_led_pin / aux_led_count this replaced could only carry
      // one answer, and a restore dropped every other lit wire (#413).
      if (ledCountField && components[id]?.ledCount !== undefined) {
        p.set(ledCountField, components[id].ledCount);
      }
      // The recorded ends, under the field names /api/config speaks for them.
      for (const end of ['OpenUs', 'CloseUs']) {
        if (cfg[`${id}${end}`] !== undefined) p.set(`${id}${end}`, cfg[`${id}${end}`]);
      }
    });

    [
      ['domeEsc', 'enableDomeEsc'],
      ['rcCh1', 'enableRcCh1'], ['rcCh2', 'enableRcCh2'], ['rcCh3', 'enableRcCh3'],
      ['rcCh4', 'enableRcCh4'], ['rcCh5', 'enableRcCh5'], ['rcCh6', 'enableRcCh6'],
      ['drive', 'enableDrive'],
      ['audio', 'enableAudio'],
      ['protoR2link', 'enableProtoR2link'],
    ].forEach(([key, param]) => {
      if (components[key]?.enabled !== undefined) {
        p.set(param, components[key].enabled ? 'true' : 'false');
      }
    });

    if (domeEsc.neutralUs !== undefined) p.set('domeEscNeutralUs', domeEsc.neutralUs);
    if (domeEsc.minPulseUs !== undefined) p.set('domeEscMinPulseUs', domeEsc.minPulseUs);
    if (domeEsc.maxPulseUs !== undefined) p.set('domeEscMaxPulseUs', domeEsc.maxPulseUs);
    if (domeEsc.speedLimitPct !== undefined) p.set('domeEscSpeedLimitPct', domeEsc.speedLimitPct);
    if (domeEsc.rndEnable !== undefined) p.set('domeEscRndEnable', domeEsc.rndEnable ? 'true' : 'false');
    if (domeEsc.rndSpeedPct !== undefined) p.set('domeEscRndSpeedPct', domeEsc.rndSpeedPct);
    if (domeEsc.rndPauseMin !== undefined) p.set('domeEscRndPauseMin', domeEsc.rndPauseMin);
    if (domeEsc.rndPauseMax !== undefined) p.set('domeEscRndPauseMax', domeEsc.rndPauseMax);
    if (domeEsc.rndMoveMs !== undefined) p.set('domeEscRndMoveMs', domeEsc.rndMoveMs);
    if (protoR2link.wifiPeerIp !== undefined) p.set('protoR2linkWifiPeerIp', protoR2link.wifiPeerIp);

    if (sys.logLevel !== undefined) p.set('logLevel', sys.logLevel);

    // The Sound Component Member: which module is actually fitted. The saved
    // choice, not `activeMember`, which is the one the droid booted with and is
    // not a setting anybody chose (src/web/api_config.cpp).
    if (components.audio?.member !== undefined) p.set('soundMember', components.audio.member);

    // The Droid Build (ADR 0047). Each half goes as a PAIR, because a variant
    // means nothing against another design and the controller refuses a request
    // that sends one without the other. The Fitted Parts go as the id list the
    // write side takes; an empty one is a real answer - a droid with nothing
    // fitted yet - and is sent as such.
    const build = cfg?.droidBuild || {};
    if (build.domeDesign !== undefined && build.domeVariant !== undefined) {
      p.set('domeDesign', build.domeDesign);
      p.set('domeVariant', build.domeVariant);
    }
    if (build.bodyDesign !== undefined && build.bodyVariant !== undefined) {
      p.set('bodyDesign', build.bodyDesign);
      p.set('bodyVariant', build.bodyVariant);
    }
    if (Array.isArray(build.fitted)) p.set('fittedParts', build.fitted.join(','));

    // Guided Setup's record travels with the backup like any other config key
    // (operator, 2026-09-17 on #371). A configured backup restored without it
    // would read as "never asked" for every category, which is the exact untruth
    // the record exists to prevent - and a pre-Setup backup restoring a droid
    // that guided Setup then offers itself to is honest rather than a surprise.
    // No exclusion list, and one rule decides it: Setup appears when the droid is
    // not set up.
    const guided = cfg?.guidedSetup || {};
    if (guided.run !== undefined) p.set('guidedSetupRun', guided.run);
    if (Array.isArray(guided.visited)) {
      // A run that showed nothing is a real answer and is written as the
      // sentinel; an empty form value would not survive the round trip as one.
      p.set('guidedSetupVisited', guided.visited.length > 0 ? guided.visited.join(',') : '-');
    }
    // Whether the ended run's summary was dismissed (#371) is the same record.
    if (typeof guided.summaryDone === 'boolean') p.set('guidedSetupSummaryDone', String(guided.summaryDone));

    return p;
  };

  // ---- RESTORE: audio tracks (one POST per key) ----
  const TRACKS_SKIP = new Set(['volume', 'chirp_bindings', 'chirp_category_bindings']);

  const restoreAudioTracks = async (tracks) => {
    const failed = [];
    for (const [key, value] of Object.entries(tracks)) {
      if (TRACKS_SKIP.has(key) || typeof value !== 'number') continue;
      const chirp = tracks.chirp_bindings?.[key];
      try {
        if (chirp) {
          let bankedOk = false;
          try {
            await window.PAApi.postForm('/api/audio/tracks',
              new URLSearchParams({ key, track: chirp.index, bank: chirp.bank, page: chirp.page }),
              { timeoutMs: 5000 });
            bankedOk = true;
          } catch { /* fall through to simple track */ }
          if (!bankedOk) {
            await window.PAApi.postForm('/api/audio/tracks',
              new URLSearchParams({ key, track: value }), { timeoutMs: 5000 });
          }
        } else {
          await window.PAApi.postForm('/api/audio/tracks',
            new URLSearchParams({ key, track: value }), { timeoutMs: 5000 });
        }
      } catch {
        failed.push(key);
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
        const outputs = await liveOutputs();
        await window.PAApi.postForm('/api/config', configToFormParams(parsedBackup.config, outputs),
          { timeoutMs: 10000 });
        lines.push('Core config: restored');
      } catch (err) {
        lines.push(`Core config: FAILED — ${window.PAApi.messageFor(err)}`);
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
