// =============================================================================
// data/maintenance.js
//
// Maintenance: inspecting and repairing a controller that is already configured
// (GLOSSARY.md "Maintenance", #288). The serial lanes, the diagnostics, the
// Memory Profiler, the crash dump, Backup & Restore, Restart - and the single
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
    // it at the press - and again at the answer: the question does not block
    // the page the way confirm() did, and the chrome stays live under it, so a
    // save can start while it is open.
    const savePending = () => {
      if (!window.PAConfigurationSave?.isPending()) return false;
      setFeedbackState(rebootFeedback, "Still saving a component change. Press again in a moment.", "warning");
      return true;
    };
    if (savePending()) return;
    const restart = await window.PAOverlay.ask({
      title: "Restart the Body Controller?",
      body: "Every output cuts out, and this page drops for about 10 seconds.",
      yes: "Restart it",
      yesIcon: "restart",
      no: "Not now",
      danger: true,
      near: rebootButton,
    });
    if (!restart || savePending()) return;
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
  const diagHeapLargestLight = document.getElementById("diag-heap-largest-light");

  // A health signal reads as a droid LED and the COLOR IS THE READING: the
  // light carries it and the value beside it stays ink (GLOSSARY.md "Health
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
    // The Foot Drive, Sound and protoR2link are the health-signal model's word
    // and light, the same ones every other page shows (data/health_signals.js,
    // #422, #399). The Foot Drive lights green only on its wheel controller's
    // readings, never on the saved toggle: the toggle is a command, not a
    // report. The heartbeat counts and the last-seen time beside protoR2link's
    // word are numbers, not a verdict, and stay this row's own.
    if (serialS1) {
      const drive = window.PAHealthSignals.readFootDrive(d);
      serialS1.textContent = drive.word;
      setLight(serialS1Light, drive.state);
    }
    const words = { unknown: window.PALiveReading.UNKNOWN };
    if (serialS2) {
      const sound = window.PAHealthSignals.readSoundLink(d, words);
      serialS2.textContent = sound.word;
      setLight(serialS2Light, sound.state);
    }
    if (serialS3) {
      const link = window.PAHealthSignals.readProtoR2link(d, words);
      // Which numbers follow the word is the model's answer too: the counts
      // while it is linked, and how long ago it was last heard once lost.
      const dl = d.dome_link;
      let counts = "";
      if (link.state === "ok") counts = ` \u00b7 hb rx ${dl.hb_rx} / tx ${dl.hb_tx}`;
      else if (link.state === "fail") counts = ` \u00b7 last seen ${dl.last_rx_ms} ms ago`;
      serialS3.textContent = `${link.word}${counts}`;
      setLight(serialS3Light, link.state);
    }
    // Uptime is telemetry, not a health signal: a number that has never been a
    // state carried a green of its own here until this slice took it off.
    if (diagUptime) {
      diagUptime.textContent = formatUptime(d.uptimeMs);
    }

    const kb = (bytes) => `${Math.round(bytes / 1024)} KB`;
    // A number the frame did not carry reads the Live Reading's Unknown,
    // never "0 KB".
    const reading = (value) => {
      const bytes = Number(value);
      return value !== undefined && value !== null && Number.isFinite(bytes) && bytes >= 0 ? bytes : null;
    };
    const heapFree = reading(d.heapFree);
    const heapMin = reading(d.heapMin);
    const heapLargest = reading(d.heapLargestBlock);

    // Free and the lowest since the last restart are readings, not states:
    // neither separates a healthy droid from a failing one, so they carry no
    // light and no word, as Uptime carries none (#355 grilling Q2, Q2b; the
    // evidence is at HEAP_FLOORS, data/health_signals.js).
    if (diagHeapFree) {
      diagHeapFree.textContent = heapFree === null ? window.PALiveReading.UNKNOWN : kb(heapFree);
    }
    if (diagHeapMin) {
      diagHeapMin.textContent = heapMin === null ? window.PALiveReading.UNKNOWN : kb(heapMin);
    }
    // The largest block is judged by the health grid's one judge and table
    // (data/health_signals.js largestBlockState, HEAP_FLOORS), in bytes, so a
    // reading on a floor reads the same here as on the Dashboard. Firmware
    // that reports no largest block was never asked, so grey.
    if (diagHeapLargest) {
      if (heapLargest === null) {
        diagHeapLargest.textContent = window.PALiveReading.UNKNOWN;
        setLight(diagHeapLargestLight, "off");
      } else {
        const state = window.PAHealthSignals.largestBlockState(heapLargest);
        const word = state === "fail" ? "Fragmented" : state === "warn" ? "Watch" : "Good";
        diagHeapLargest.textContent = `${kb(heapLargest)} ${word}`;
        setLight(diagHeapLargestLight, state);
      }
    }
    // When the droid sent it, not when it was painted: a lost link repaints
    // the same frame, and "Updated" must not move with it.
    setFeedbackState(serialStatusLine, `Updated ${new Date(receivedAt).toLocaleTimeString()}`, "success");
  };

  // Every reading here rides the Live Reading, which owns the stream or the one
  // fallback poll for the whole shell (data/live_reading.js). Before the droid
  // has sent a frame each readout says so in its words; once contact is lost
  // the values stay, and the status line says they are not being refreshed.
  const WAITING_READOUTS = [serialS1, serialS2, serialS3, diagUptime, diagHeapFree, diagHeapMin, diagHeapLargest];
  const renderReading = (reading) => {
    if (reading.status === null) {
      WAITING_READOUTS.forEach((node) => {
        if (node) node.textContent = window.PALiveReading.slotText(window.PALiveReading.WAITING);
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
//
// A backup holds what the builder made on the controller in three parts, and a
// restore writes the parts ticked, each replacing what the droid holds and
// never merging into it (GLOSSARY.md "Backup", ADR 0056 and its 2026-09-25
// amendment, #448):
//   Sequences      every Learned Sequence, as GET /api/seq?name= answers it,
//                  and the take files they name (#442)
//   Configuration  the config and the Outputs' rows, and the sound setup
//   RC Map         which action each RC Channel fires
// Every answer is taken before the first write: the parts, then whether to save
// a copy of what is about to go. A copy that cannot be built stops the restore.
// =============================================================================
(() => {
  const downloadBtn = document.getElementById('backup-download-btn');
  const fileInput = document.getElementById('backup-file-input');
  const fileTrigger = document.getElementById('backup-file-trigger');
  const summary = document.getElementById('backup-summary');
  const restoreSections = document.getElementById('restore-sections');
  const restoreBtnRow = document.getElementById('restore-btn-row');
  const restoreBtn = document.getElementById('backup-restore-btn');
  const question = document.getElementById('restore-question');
  const questionText = document.getElementById('restore-question-text');
  const cancelBtn = document.getElementById('restore-cancel-btn');
  const copyBtn = document.getElementById('restore-copy-btn');
  const replaceBtn = document.getElementById('restore-replace-btn');
  const feedback = document.getElementById('backup-feedback');

  if (!downloadBtn || !fileInput || !feedback) return;

  // The file this page writes. Schema 1 carried no Sequences and did not say
  // which board wrote it; a file above this number is from newer firmware.
  const BACKUP_SCHEMA = 2;

  // The chosen file, and what the droid said about itself when it was chosen:
  // its board, its Sequence cap, the Learned Sequences it holds and the Factory
  // ones it ships. `facts` is null while the droid is being asked.
  let parsedBackup = null;
  let facts = null;
  let factsAsked = 0;
  // True from the answer to the question until the restore's feedback is in.
  let restoring = false;

  const setFeedback = (msg, variant = '') => {
    feedback.textContent = msg;
    feedback.className = variant ? `feedback ${variant}` : 'feedback';
  };

  const listOf = (names) => names.join(', ');
  const seqPath = (name) => `/api/seq?name=${encodeURIComponent(name)}`;
  const isObject = (value) => Boolean(value) && typeof value === 'object' && !Array.isArray(value);

  // Two JSON values with the same content, whatever order their keys came in:
  // the order a Sequence's keys were written in is not part of the Sequence.
  const sameJson = (a, b) => {
    if (a === b) return true;
    if (!a || !b || typeof a !== 'object' || typeof b !== 'object') return false;
    if (Array.isArray(a) !== Array.isArray(b)) return false;
    const keys = Object.keys(a);
    return keys.length === Object.keys(b).length
      && keys.every((key) => Object.prototype.hasOwnProperty.call(b, key) && sameJson(a[key], b[key]));
  };

  // The feedback line for a write the droid did not take. POST /api/config, the
  // RC Map and the mood map answer 4xx and 503 before anything changes. Their
  // 500 comes later: the config and the RC Map apply to the live settings
  // before they persist (src/web/api_config.cpp), and the mood map's NVS save
  // writes key by key (configSaveAudio()). So a 500, or no answer at all, can
  // leave that part changed in part.
  const failedLine = (label, error) => {
    const refusedWhole = error?.kind === 'http' && ((error.status >= 400 && error.status < 500) || error.status === 503);
    const said = `${label}: FAILED — ${window.PAApi.messageFor(error)}`;
    return refusedWhole ? said : `${said}; it may have partly changed`;
  };

  // ---- READ THE DROID: the reads a backup is made of, by part ----
  // Download reads every part; the copy offered before a restore reads the
  // parts about to be replaced. One function, so the copy is a backup like any
  // other and restores the same way. The Outputs' centre, `calibrated` and Part
  // map are on /api/servo/outputs, not /api/config, and ADR 0056 puts them in
  // the Configuration all the same (#417).
  const PART_READS = {
    configuration: [
      ['config', '/api/config'],
      ['servo_outputs', '/api/servo/outputs'],
      ['audio_tracks', '/api/audio/tracks'],
      ['audio_mood_map', '/api/audio/mood-map'],
    ],
    rc_map: [['rc_map', '/api/rc/map']],
  };

  // The Learned Sequences, in the droid's list order, each as it is stored.
  // One at a time: a sequence runs to the per-file cap and the controller
  // answers one of those at a time anyway. Every name that did not come back
  // is in `failed`, so the message can say which.
  const readSequences = async (failed) => {
    let list;
    try {
      list = (await window.PAApi.get('/api/seq/list', { timeoutMs: 10000 })).data;
    } catch {
      list = null;
    }
    if (!Array.isArray(list)) {
      failed.push('the Sequence list');
      return [];
    }
    const sequences = [];
    for (const { name } of list) {
      const seq = await window.PAApi.get(seqPath(name), { timeoutMs: 10000 }).then((res) => res.data, () => null);
      if (isObject(seq) && typeof seq.name === 'string') sequences.push(seq);
      else failed.push(name);
    }
    return sequences;
  };

  // A take file's bytes as text the backup's JSON can carry, and back. In
  // pieces, so a 24 KB file never becomes one huge argument list.
  const bytesToBase64 = (buffer) => {
    const bytes = new Uint8Array(buffer);
    let text = '';
    for (let at = 0; at < bytes.length; at += 0x2000) {
      text += String.fromCharCode(...bytes.subarray(at, at + 0x2000));
    }
    return btoa(text);
  };
  const base64ToBlob = (data) => {
    const text = atob(data);
    const bytes = new Uint8Array(text.length);
    for (let at = 0; at < text.length; at += 1) bytes[at] = text.charCodeAt(at);
    return new Blob([bytes], { type: 'application/octet-stream' });
  };

  // The take files the Learned Sequences name (#442, ADR 0061), each as the
  // droid stores it, carried in the Sequences part beside them as
  // `takes: [{seq, owner, id, data}]` - `owner` the sequence's stable id the
  // droid files it under, `data` the file in base64. A take a sequence names
  // that the droid does not hold (404) has nothing to carry and is left out;
  // one that does not come back otherwise is in `failed`, as a sequence is.
  const readTakes = async (sequences, failed) => {
    const takes = [];
    for (const seq of sequences) {
      if (!Array.isArray(seq.takes) || typeof seq.id !== 'string') continue;
      for (const ref of seq.takes) {
        if (typeof ref?.id !== 'string') continue;
        const path = `/api/take/file?owner=${encodeURIComponent(seq.id)}&take=${encodeURIComponent(ref.id)}`;
        try {
          const answer = await window.PAApi.getBytes(path, { timeoutMs: 10000 });
          takes.push({ seq: seq.name, owner: seq.id, id: ref.id, data: bytesToBase64(answer.data) });
        } catch (error) {
          if (error?.kind === 'http' && error.status === 404) continue;
          failed.push(`a take of ${seq.name}`);
        }
      }
    }
    return takes;
  };

  // A backup of `parts` ('sequences', 'configuration', 'rc_map') read off the
  // droid, with the board that wrote it, or the reads that did not answer.
  // A read counts only when it returned data: an answer with no JSON body is
  // a part missing from the copy, and an identity without its board makes a
  // file this page would refuse to restore.
  const readDroid = async (parts) => {
    const reads = [['identity', '/api/identity'], ...parts.flatMap((part) => PART_READS[part] || [])];
    const [fwRes, ...answers] = await Promise.allSettled([
      fetch('/fw-version.json').then((r) => r.json()),
      ...reads.map(([, path]) => window.PAApi.get(path, { timeoutMs: 10000 })),
    ]);
    const failed = [];
    const read = {};
    answers.forEach((res, index) => {
      const [key] = reads[index];
      const data = res.status === 'fulfilled' ? res.value?.data : null;
      if (isObject(data) && (key !== 'identity' || typeof data.board === 'string')) read[key] = data;
      else failed.push(key);
    });
    const sequences = parts.includes('sequences') ? await readSequences(failed) : undefined;
    const takes = sequences ? await readTakes(sequences, failed) : undefined;
    if (failed.length > 0) return { failed };
    const { identity, ...held } = read;
    return {
      backup: {
        schema: BACKUP_SCHEMA,
        generated: new Date().toISOString(),
        fw_version: fwRes.status === 'fulfilled' ? (fwRes.value?.firmwareVersion || 'unknown') : 'unknown',
        board: identity?.board,
        ...(sequences ? { sequences, takes } : {}),
        ...held,
      },
    };
  };

  const saveFile = (backup, suffix = '') => {
    const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `artoo-backup-${new Date().toISOString().slice(0, 10)}${suffix}.json`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  // ---- DOWNLOAD BACKUP ----
  const downloadBackup = async () => {
    if (!window.PAApi) return;
    downloadBtn.disabled = true;
    setFeedback('Downloading settings...');
    try {
      const { backup, failed } = await readDroid(['sequences', 'configuration', 'rc_map']);
      if (failed) {
        setFeedback(`No backup saved: the droid did not send ${listOf(failed)}.`, 'error');
        return;
      }
      saveFile(backup);
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
  //
  // An older backup can also name a Part this build no longer models: `drawer`
  // in droidBuild.fitted, retired by #409. POST /api/config refuses the whole
  // body over one unknown id, and must - a live request naming a Part that does
  // not exist is a real error - so the restore drops it here, against the Part
  // catalog this page carries (data/droid_parts.js, generated from the same
  // list as the firmware's), and names it in the feedback, as it does an Output
  // this droid lacks. The NVS load drops such an id the same way
  // (droidFittedPartsParse).
  const ROW_SETTINGS = [
    'wired', 'component', 'ledCount', 'throwMs', 'accelMs', 'ease', 'release', 'boot',
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
  // the feedback says so.
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

  // The backup's config with every fitted Part this build does not model taken
  // out, and those ids. Without the catalog nothing is dropped: the droid's
  // refusal then says what is wrong, rather than a guess made here.
  const configToRestore = (config) => {
    const fitted = config?.droidBuild?.fitted;
    const catalog = window.DroidParts?.parts;
    if (!Array.isArray(fitted) || !Array.isArray(catalog)) return { config, retired: [] };
    const known = new Set(catalog.map((part) => part.id));
    const retired = fitted.filter((id) => !known.has(id));
    if (retired.length === 0) return { config, retired };
    const droidBuild = { ...config.droidBuild, fitted: fitted.filter((id) => known.has(id)) };
    return { config: { ...config, droidBuild }, retired };
  };

  // The Configuration's feedback line: "restored" only when all of it landed.
  // A read of the droid's Outputs that fails throws, before anything is sent.
  const restoreConfiguration = async (backup) => {
    const { outputs } = await window.PAOutputs.load();
    const { rows, missing } = rowsToRestore(backup, outputs);
    const { config, retired } = configToRestore(backup.config);
    try {
      await window.PAApi.postJson('/api/config', { ...config, outputs: rows }, { timeoutMs: 10000 });
    } catch (error) {
      // A refusal about an Output's row is worded by the module that knows the
      // Outputs, from the rows this restore sent; anything else is said as the
      // droid said it.
      return failedLine('Configuration', window.PAOutputs.sayRefusal(error, rows));
    }
    const gaps = missing.map((name) => `${name} not on this droid`);
    retired.forEach((id) => gaps.push(`${id} is no longer a Part`));
    // A file from before backups carried the Outputs' rows has no centre,
    // calibration or Part map to give back.
    if (!Array.isArray(backup.servo_outputs?.outputs)) gaps.push('no centre, calibration or Part map in this file');
    return gaps.length === 0 ? 'Configuration: restored' : `Configuration: partial — ${gaps.join(', ')}`;
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
    // With Sound off this boot nothing drives a module, and POST /api/audio
    // refuses the volume (409, #370). The restore leaves it out and says so
    // rather than counting it failed (operator, 2026-09-28). If the droid
    // cannot say whether Sound is on, the volume is sent and its own answer
    // decides.
    const skipped = [];
    if (typeof tracks.volume === 'number') {
      const audio = await window.PAApi.get('/api/audio', { timeoutMs: 5000 }).catch(() => null);
      if (audio?.data?.output === 'off') {
        skipped.push('volume skipped, sound is off');
      } else {
        try {
          await window.PAApi.postForm('/api/audio',
            new URLSearchParams({ action: 'volume', level: tracks.volume }), { timeoutMs: 5000 });
        } catch {
          failed.push('volume');
        }
      }
    }
    return { failed, skipped };
  };

  // ---- RESTORE: the Sequences ----
  // Replacing the Learned Sequences is deleting what the file does not hold and
  // posting what it does, through the droid's own routes. The droid refuses a
  // NEW save while it holds its cap, so the deletes go first.
  //
  // The droid keeps the first Sequences in file order up to its cap, and the
  // rest are named (ADR 0056, amended 2026-09-25). Nothing in a Sequence is
  // changed on the way: the droid's Protocol Check is the only gate, and a
  // routine it refuses - or one over this board's per-file cap - is refused by
  // the droid, not guessed at here.
  //
  // THE INVARIANT: if the droid refuses anything after the library has
  // changed, the library is put back from `prior`, the Sequences read off the
  // droid just before, and the feedback says so. `held` and `changed` track what
  // the droid holds now, so the put-back deletes only what this restore added
  // and re-posts only what it removed or overwrote.
  //
  // Both are written BEFORE each request, never after its answer. The droid
  // changes its store before it answers (src/web/api_seq.cpp), and a save that
  // fails at the rename has already removed the old file (src/seq_store.cpp,
  // LittleFS.remove before rename). So a refusal, a lost reply or a timeout can
  // each leave a name changed, and only a name recorded up front is put back.
  const restoreSequences = async (fileSequences, prior, cap) => {
    const keep = cap ? fileSequences.slice(0, cap) : fileSequences.slice();
    const leftOut = fileSequences.slice(keep.length).map((seq) => seq?.name);
    const keepNames = new Set(keep.map((seq) => seq?.name));
    const priorNames = new Set(prior.map((seq) => seq.name));
    const priorByName = new Map(prior.map((seq) => [seq.name, seq]));
    const held = new Set(priorNames);
    const changed = new Set();
    const dangling = [];
    let refused = null;

    const remove = async (name) => {
      held.delete(name);
      changed.add(name);
      const answer = await window.PAApi.request(seqPath(name), { method: 'DELETE', timeoutMs: 10000 });
      return answer?.data?.danglingBindings || [];
    };
    const save = async (seq) => {
      held.add(seq.name);
      changed.add(seq.name);
      await window.PAApi.postJson('/api/seq', seq, { timeoutMs: 15000 });
    };

    let step = null;
    try {
      for (const seq of prior) {
        if (keepNames.has(seq.name)) continue;
        step = seq.name;
        (await remove(seq.name)).forEach((binding) => dangling.push({ ...binding, name: seq.name }));
      }
      for (const seq of keep) {
        // A Sequence the droid already holds exactly is left alone: on a full
        // store every save can be refused for room (src/seq_store_util.cpp),
        // and re-posting an identical copy would fail a restore that changes
        // nothing about it.
        if (sameJson(seq, priorByName.get(seq?.name))) continue;
        step = seq?.name;
        await save(seq);
      }
    } catch (error) {
      refused = { name: step, reason: window.PAApi.messageFor(error) };
    }
    if (!refused) return { restored: keep, leftOut, dangling, held };

    // Put back. What this restore added goes first, so a full store has room
    // for what it removed. A name recorded before a save whose reply was lost
    // may never have reached the droid: its "not found" means it is gone,
    // which is what the put-back wants.
    const notBack = [];
    for (const name of [...held].filter((each) => !priorNames.has(each))) {
      try {
        await remove(name);
      } catch (error) {
        if (error?.kind === 'http' && error.status === 404) continue;
        held.add(name);
        notBack.push(name);
      }
    }
    for (const seq of prior.filter((each) => changed.has(each.name))) {
      try {
        await save(seq);
      } catch {
        held.delete(seq.name);
        notBack.push(seq.name);
      }
    }
    return { refused, notBack, held };
  };

  // ---- RESTORE: the takes ----
  // After the Sequences, as part of them: a take goes back only to a sequence
  // the droid now holds, under the stable id it was kept with - its owner on
  // the droid, which files it by that id (include/take_store_util.h). Each is
  // an upload the droid checks as it arrives; one it refuses is named and the
  // rest still go.
  const restoreTakes = async (takes, sequences) => {
    const owners = new Set(sequences
      .filter((seq) => typeof seq?.id === 'string')
      .map((seq) => `${seq.name}\n${seq.id}`));
    let restored = 0;
    const failed = [];
    for (const take of Array.isArray(takes) ? takes : []) {
      if (!owners.has(`${take.seq}\n${take.owner}`)) continue;
      try {
        await window.PAApi.postFile('/api/take/file', 'take', base64ToBlob(take.data),
          `${take.owner}.${take.id}.take`, { timeoutMs: 15000 });
        restored += 1;
      } catch (error) {
        failed.push(`${take.seq} (${window.PAApi.messageFor(error)})`);
      }
    }
    return { restored, failed };
  };

  const takesLine = ({ restored, failed }) => {
    if (failed.length > 0) return `Takes: partial — ${restored} restored; refused: ${listOf(failed)}`;
    return restored > 0 ? `Takes: restored ${restored}` : null;
  };

  // ---- RESTORE: the RC Map ----
  // POST /api/rc/map refuses the WHOLE map over one dome_seq binding whose
  // Sequence the droid does not hold (isValidDomeSeqPayload(),
  // src/web/api_rc_map_apply.cpp). So a binding whose Sequence will not be on
  // the droid is left out of what is sent and named, and the rest of the map
  // still replaces the droid's (operator, 2026-09-30, #448) - the "not sent,
  // named in the feedback" shape of an Output this droid lacks, above.
  //
  // `holds(name)` answers for a Learned Sequence the droid holds or a Factory
  // one it ships (GET /api/seq/builtins). The droid's own accepted Factory list
  // is narrower than the catalog; a binding to a catalog name outside it that
  // only a Learned copy made valid, when that copy is left out, is kept here
  // and refused there - and the feedback then says the RC Map did not land.
  const rcMapToRestore = (rcMap, holds) => {
    const map = Array.isArray(rcMap?.map) ? rcMap.map : [];
    const send = [];
    const leftOut = [];
    map.forEach((entry) => {
      if (entry?.action === 'dome_seq' && typeof entry.payload === 'string' && !holds(entry.payload)) {
        leftOut.push(`RC Channel ${entry.channel}: ${entry.payload} is not on this droid`);
      } else {
        send.push(entry);
      }
    });
    return { body: { ...rcMap, map: send }, leftOut };
  };

  // The map is filtered against the library the droid holds NOW, read after the
  // Sequences have landed or been put back, never against the plan.
  const restoreRcMap = async (rcMap, factory) => {
    let list;
    try {
      list = (await window.PAApi.get('/api/seq/list', { timeoutMs: 10000 })).data;
    } catch {
      list = null;
    }
    if (!Array.isArray(list)) {
      return ['RC Map: FAILED — the droid did not say which Sequences it holds. Nothing was written.'];
    }
    const learned = new Set(list.map((row) => row.name));
    const { body, leftOut } = rcMapToRestore(rcMap, (name) => learned.has(name) || factory.has(name));
    try {
      await window.PAApi.postForm('/api/rc/map', { plain: JSON.stringify(body) }, { timeoutMs: 10000 });
    } catch (err) {
      return [failedLine('RC Map', err)];
    }
    if (leftOut.length === 0) return ['RC Map: restored'];
    return [`RC Map: partial — ${leftOut.length} left out`, ...leftOut];
  };

  // ---- THE THREE PARTS ----
  // Each part carries the two lines it is shown with, so no part can be drawn
  // without both: what it replaces and what it leaves (r2d2-astromech-simulator
  // v1.79.0, wizard-import.js:715). The lines are authored; what is computed
  // from the file is whether a part is offered and its count.
  const PARTS = [
    {
      id: 'sequences',
      label: 'Sequences',
      touches: 'Replaces every Learned Sequence on the droid.',
      leaves: 'Leaves the Configuration and the RC Map.',
      inFile: (backup) => Array.isArray(backup.sequences),
    },
    {
      id: 'configuration',
      label: 'Configuration',
      touches: 'Replaces the droid build, the Parts on its Outputs, their calibration and the sound setup.',
      leaves: 'Leaves the Sequences, the RC Map and the WiFi.',
      inFile: (backup) => Boolean(backup.config),
    },
    {
      id: 'rc-map',
      label: 'RC Map',
      touches: 'Replaces what the droid will do this evening.',
      leaves: 'Leaves the Sequences and the Configuration.',
      inFile: (backup) => Array.isArray(backup.rc_map?.map),
    },
  ];
  const partEl = (kind, id) => document.getElementById(`restore-${kind}-${id}`);
  const tick = (id) => partEl('chk', id);
  const isTicked = (id) => Boolean(tick(id)?.checked) && !tick(id)?.disabled;

  // A file from the other Board Variant writes only what names no pin: the
  // Sequences, the RC Map and the sound setup (ADR 0056). A file that does not
  // say which board wrote it (schema 1) is this droid's own.
  const fromOtherBoard = (file = parsedBackup, known = facts) =>
    typeof file?.board === 'string' && typeof known?.board === 'string' && file.board !== known.board;

  // The Learned Sequences the droid will hold once this restore is done, as far
  // as it can be known before it runs: the file's first `cap` when Sequences is
  // ticked, what the droid holds now when it is not.
  const learnedAfter = () => {
    if (isTicked('sequences')) {
      const names = parsedBackup.sequences.map((seq) => seq?.name);
      return new Set(facts.cap ? names.slice(0, facts.cap) : names);
    }
    return new Set(facts.library);
  };

  const countFor = (part) => {
    const backup = parsedBackup;
    if (part.id === 'sequences') {
      const n = backup.sequences.length;
      const said = `${n} in this file`;
      return facts?.cap && n > facts.cap ? `${said} · keeps the first ${facts.cap}` : said;
    }
    if (part.id === 'configuration') return fromOtherBoard() ? 'from another board' : '';
    const n = backup.rc_map.map.length;
    const said = `${n} binding${n === 1 ? '' : 's'}`;
    if (!facts?.library) return said;
    const after = learnedAfter();
    const { leftOut } = rcMapToRestore(backup.rc_map, (name) => after.has(name) || facts.factory.has(name));
    return leftOut.length === 0 ? said : `${said} · ${leftOut.length} left out, its Sequence is not on this droid`;
  };

  const touchesFor = (part) =>
    part.id === 'configuration' && fromOtherBoard()
      ? 'Replaces only the sound setup. The rest names another board\'s pins and stays.'
      : part.touches;

  const tickedParts = () => PARTS.filter((part) => isTicked(part.id));

  // Draws the three parts from the file and the droid's facts. The Restore
  // press waits for the facts: without them the cap, the board and the RC Map's
  // count are not known, and a restore would be a guess.
  const renderParts = () => {
    if (!parsedBackup) return;
    PARTS.forEach((part) => {
      const carried = part.inFile(parsedBackup);
      const box = tick(part.id);
      if (box) box.disabled = !carried;
      const why = partEl('why', part.id);
      if (why) {
        why.hidden = carried;
        why.textContent = carried ? '' : `This file has no ${part.label}. Download backup makes one that does.`;
      }
      const touches = partEl('touches', part.id);
      const leaves = partEl('leaves', part.id);
      if (touches) {
        touches.hidden = !carried;
        touches.textContent = carried ? touchesFor(part) : '';
      }
      if (leaves) {
        leaves.hidden = !carried;
        leaves.textContent = carried ? part.leaves : '';
      }
      const count = partEl('count', part.id);
      if (count) count.textContent = carried ? countFor(part) : '';
    });
    const n = tickedParts().length;
    if (restoreBtn) {
      window.PAUi.setAct(restoreBtn, `Restore ${n} ticked part${n === 1 ? '' : 's'}`);
      restoreBtn.disabled = restoring || n === 0 || !facts?.library;
    }
    if (question) question.hidden = true;
  };

  // What the droid says about itself, asked when a file is chosen. A later
  // file supersedes an answer still on its way.
  const askDroid = async () => {
    const asked = ++factsAsked;
    facts = null;
    const [identity, list, builtins] = await Promise.allSettled([
      window.PAApi.get('/api/identity', { timeoutMs: 10000 }),
      window.PAApi.get('/api/seq/list', { timeoutMs: 10000 }),
      window.PAApi.get('/api/seq/builtins', { timeoutMs: 10000 }),
    ]);
    if (asked !== factsAsked) return;
    const rows = (res) => (res.status === 'fulfilled' && Array.isArray(res.value?.data) ? res.value.data : null);
    const failed = [];
    if (identity.status !== 'fulfilled' || typeof identity.value?.data?.board !== 'string') failed.push('which board it is');
    if (!rows(list)) failed.push('which Sequences it holds');
    if (!rows(builtins)) failed.push('its Factory Sequences');
    if (failed.length > 0) {
      facts = { failed };
      renderParts();
      setFeedback(`The droid did not say ${listOf(failed)}. Choose the file again to retry.`, 'error');
      return;
    }
    const id = identity.value?.data || {};
    const whole = (value) => (Number.isInteger(value) && value > 0 ? value : null);
    facts = {
      board: id.board,
      cap: whole(id.learned_sequence_cap),
      maxBytes: whole(id.learned_sequence_max_bytes),
      library: rows(list).map((row) => row.name),
      factory: new Set(rows(builtins).map((row) => row.name)),
    };
    renderParts();
  };

  // ---- ASK BEFORE ANYTHING IS WRITTEN ----
  // Both answers - the parts and the copy - are in before the first request
  // that changes the droid, so Keep what I have means nothing was touched
  // (wizard-import.js :1014).
  const askToReplace = () => {
    const parts = tickedParts();
    if (parts.length === 0 || !facts?.library || !question) return;
    const names = parts.map((part) => part.label);
    const said = names.length === 1 ? names[0] : `${names.slice(0, -1).join(', ')} and ${names[names.length - 1]}`;
    if (questionText) questionText.textContent = `What is on the droid now goes. Replace its ${said}?`;
    question.hidden = false;
    if (restoreBtn) restoreBtn.disabled = true;
  };

  const cancelRestore = () => {
    if (question) question.hidden = true;
    renderParts();
    setFeedback('Nothing was touched.');
  };

  // ---- RESTORE: apply the ticked parts ----
  // `file` and `known` are the chosen file and what the droid said about
  // itself, as they stood when the question was answered. Nothing below reads
  // the live ones: the copy's reads take seconds, and a file chosen meanwhile
  // must not become the one written.
  const applyRestore = async (file, known, parts, withCopy) => {
    // The copy is built from the droid's own answers for every part about to
    // be replaced, and a download the browser blocked still reads as saved -
    // so what can be checked is the reads: if any fails, there is no copy and
    // nothing is replaced (wizard-import.js:1041, :1096). Sequences read their
    // current library either way, since that is what a refusal puts back.
    const reads = withCopy ? parts.map((id) => (id === 'rc-map' ? 'rc_map' : id)) : [];
    if (parts.includes('sequences') && !reads.includes('sequences')) reads.push('sequences');
    let before = null;
    if (reads.length > 0) {
      const { backup, failed } = await readDroid(reads);
      if (failed) {
        setFeedback(withCopy
          ? `No copy saved: the droid did not send ${listOf(failed)}. Nothing was replaced.`
          : `The droid did not send its Sequences (${listOf(failed)}). Nothing was replaced.`, 'error');
        return;
      }
      before = backup;
      if (withCopy) {
        const kept = { ...backup };
        if (!parts.includes('sequences')) {
          delete kept.sequences;
          delete kept.takes;
        }
        saveFile(kept, '-before-restore');
      }
    }

    const lines = [];
    let sequencesDone = null;

    if (parts.includes('sequences')) {
      const result = await restoreSequences(file.sequences, before.sequences, known.cap);
      sequencesDone = result;
      if (result.refused) {
        const back = result.notBack.length === 0
          ? 'The Sequences the droid had are put back.'
          : `Not put back: ${listOf(result.notBack)}.${withCopy ? ' They are in the copy you saved.' : ''}`;
        lines.push(`Sequences: FAILED — the droid refused ${result.refused.name}: ${result.refused.reason}. ${back}`);
      } else if (result.leftOut.length === 0) {
        lines.push(`Sequences: restored ${result.restored.length}`);
      } else {
        lines.push(`Sequences: partial — ${result.restored.length} restored; left out, this droid holds ${known.cap}: ${listOf(result.leftOut)}`);
      }
      // The takes of what landed, or - where the droid refused and the
      // library was put back - of what was put back, whose takes went with
      // the sequences the restore deleted.
      const takes = result.refused
        ? await restoreTakes(before.takes, before.sequences)
        : await restoreTakes(file.takes, result.restored);
      const line = takesLine(takes);
      if (line) lines.push(line);
    }

    if (parts.includes('configuration')) {
      if (fromOtherBoard(file, known)) {
        lines.push('Configuration: partial — the sound setup only; the rest names another board\'s pins and stays');
      } else {
        try {
          lines.push(await restoreConfiguration(file));
        } catch (err) {
          lines.push(`Configuration: FAILED — ${window.PAApi.messageFor(err)}`);
        }
      }
      // The sound setup follows on its own routes, with its own lines.
      if (file.audio_tracks) {
        const { failed, skipped } = await restoreAudioTracks(file.audio_tracks);
        const line = failed.length === 0
          ? 'Audio tracks: restored'
          : `Audio tracks: partial — ${failed.length} failed (${failed.join(', ')})`;
        lines.push(skipped.length === 0 ? line : `${line}; ${skipped.join(', ')}`);
      }
      if (file.audio_mood_map) {
        try {
          await window.PAApi.postForm('/api/audio/mood-map', file.audio_mood_map, { timeoutMs: 5000 });
          lines.push('Audio mood map: restored');
        } catch (err) {
          lines.push(failedLine('Audio mood map', err));
        }
      }
    }

    let rcMapLanded = false;
    if (parts.includes('rc-map')) {
      const rcLines = await restoreRcMap(file.rc_map, known.factory);
      rcMapLanded = !rcLines[0].includes('FAILED');
      lines.push(...rcLines);
    }

    // A binding whose Sequence this restore removed fires nothing now, unless
    // the RC Map written after it replaced that binding (DELETE /api/seq
    // answers danglingBindings for each Sequence it removes).
    if (sequencesDone && !rcMapLanded) {
      sequencesDone.dangling
        ?.filter((binding) => !sequencesDone.held.has(binding.name))
        .forEach((binding) => lines.push(`RC Channel ${binding.channel} fires ${binding.name}, which is not on this droid`));
    }

    // What the Rehearsal finds in each Sequence written, against the droid as
    // it now stands. It never refuses (GLOSSARY.md "Rehearsal"): nothing here
    // stops or changes the restore. A read that fails leaves the rules that
    // need the droid's rows silent, exactly as in the editor.
    if (sequencesDone?.restored?.length > 0 && window.SeqRehearsal) {
      const droid = await window.PAOutputs?.load().catch(() => null);
      const context = { outputs: droid?.outputs || null, config: droid?.config || null, maxBytes: known.maxBytes };
      sequencesDone.restored.forEach((seq) => {
        lines.push(`${seq.name} — ${window.SeqRehearsal.summaryText(window.SeqRehearsal.rehearse(seq, context))}`);
      });
    }

    const anyRestored = lines.some((l) => l.includes(': restored') || l.includes(': partial'));
    const anyIssue = lines.some((l) => l.includes('FAILED') || l.includes('partial') || l.startsWith('RC Channel'));
    if (anyRestored) lines.push('Restart the Body Controller to apply everything restored.');
    setFeedback(lines.join('\n'), anyIssue ? 'error' : 'success');
    // The library the parts are counted against is the one the droid holds now.
    if (sequencesDone) known.library = [...sequencesDone.held];
  };

  // The chooser is locked from the answer until the feedback is in, and a file
  // whose read lands meanwhile is dropped (handleFile()).
  const lockChooser = (locked) => {
    restoring = locked;
    fileInput.disabled = locked;
    if (fileTrigger) fileTrigger.disabled = locked;
  };

  const performRestore = async ({ withCopy }) => {
    if (restoring || !parsedBackup || !facts?.library || !window.PAApi) return;
    const parts = tickedParts().map((part) => part.id);
    if (parts.length === 0) return;
    if (question) question.hidden = true;
    if (restoreBtn) restoreBtn.disabled = true;
    setFeedback('Restoring...');
    lockChooser(true);
    try {
      await applyRestore(parsedBackup, facts, parts, withCopy);
    } finally {
      lockChooser(false);
      renderParts();
    }
  };

  // ---- FILE PARSE ----
  // A protoArtoo backup is a JSON object with a schema number, in the shape
  // Download backup writes it. Anything else is refused with the reason, never
  // half-read, and as not supported rather than impossible: opening sharing
  // later is a policy change, not a format change (ADR 0056).
  //
  // The shape is checked part by part, because each part a file carries
  // replaces the droid's: `{"schema":1,"rc_map":{"map":[]}}` would otherwise be
  // offered and empty the RC Map. A schema 2 file names its board, and one that
  // does not is refused rather than taken as this droid's own, which would
  // write another board's pins. The copy saved before a restore carries only
  // the parts it replaces, so a part may be absent; a part present is whole.
  const whyNotABackup = (backup) => {
    if (!isObject(backup)) return 'it is not a backup object';
    if (!Number.isInteger(backup.schema) || backup.schema < 1) return 'it has no backup schema';
    const current = backup.schema >= BACKUP_SCHEMA;
    if (current && (typeof backup.board !== 'string' || backup.board === '')) {
      return 'it does not say which board wrote it';
    }
    const has = (key) => backup[key] !== undefined;
    if (has('sequences') && !(Array.isArray(backup.sequences)
        && backup.sequences.every((seq) => isObject(seq) && typeof seq.name === 'string'))) {
      return 'its Sequences are incomplete';
    }
    if (has('takes') && !(Array.isArray(backup.takes) && backup.takes.every((take) => isObject(take)
        && ['seq', 'owner', 'id', 'data'].every((key) => typeof take[key] === 'string')))) {
      return 'its takes are incomplete';
    }
    if (has('rc_map') && !(isObject(backup.rc_map) && Array.isArray(backup.rc_map.map))) {
      return 'its RC Map is incomplete';
    }
    // The Configuration is the config, the Outputs' rows and the sound setup,
    // read together. Schema 1 wrote the rows only from #417 on.
    const configuration = current
      ? ['config', 'servo_outputs', 'audio_tracks', 'audio_mood_map']
      : ['config', 'audio_tracks', 'audio_mood_map'];
    if (configuration.some(has) && !(configuration.every((key) => isObject(backup[key]))
        && (!has('servo_outputs') || Array.isArray(backup.servo_outputs?.outputs)))) {
      return 'its Configuration is incomplete';
    }
    // Download wrote schema 1 whole: the Configuration and the RC Map, always.
    if (!current && !has('config')) return 'it has no Configuration';
    if (!current && !has('rc_map')) return 'it has no RC Map';
    if (!has('sequences') && !has('config') && !has('rc_map')) return 'it holds no Sequences, Configuration or RC Map';
    return null;
  };

  const refuseFile = (why) => {
    setFeedback(`Not a protoArtoo backup: ${why}. Restoring it is not supported; choose a file Download backup saved.`, 'error');
    parsedBackup = null;
    showRestorePanel(false);
  };

  const showRestorePanel = (show) => {
    if (summary) summary.hidden = !show;
    if (restoreSections) restoreSections.hidden = !show;
    if (restoreBtnRow) restoreBtnRow.hidden = !show;
    if (!show && question) question.hidden = true;
  };

  const handleFile = (file) => {
    if (!file) return;
    if (restoring) {
      fileInput.value = '';
      return;
    }
    const reader = new FileReader();
    reader.onload = (e) => {
      if (restoring) return;
      let backup;
      try {
        backup = JSON.parse(e.target.result);
      } catch {
        refuseFile('it is not JSON');
        return;
      }
      const why = whyNotABackup(backup);
      if (why) {
        refuseFile(why);
        return;
      }

      parsedBackup = backup;
      const date = backup.generated ? String(backup.generated).slice(0, 10) : 'unknown';
      if (summary) summary.textContent = `Backup from ${date}, firmware ${backup.fw_version || 'unknown'}`;
      PARTS.forEach((part) => {
        const box = tick(part.id);
        if (box) box.checked = part.inFile(backup);
      });
      facts = null;
      renderParts();
      showRestorePanel(true);
      if (backup.schema > BACKUP_SCHEMA) {
        setFeedback(`This backup is from newer firmware (schema ${backup.schema}). Some of it may not restore.`, 'warning');
      } else {
        setFeedback('');
      }
      askDroid();
    };
    reader.readAsText(file);
  };

  downloadBtn.addEventListener('click', downloadBackup);
  if (fileTrigger) fileTrigger.addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', () => handleFile(fileInput.files?.[0] ?? null));
  PARTS.forEach((part) => tick(part.id)?.addEventListener('change', renderParts));
  if (restoreBtn) restoreBtn.addEventListener('click', askToReplace);
  if (cancelBtn) cancelBtn.addEventListener('click', cancelRestore);
  if (copyBtn) copyBtn.addEventListener('click', () => performRestore({ withCopy: true }));
  if (replaceBtn) replaceBtn.addEventListener('click', () => performRestore({ withCopy: false }));
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
  // defect (GLOSSARY.md "Status Color"), and these three were Material's own
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
// The crash dump (#474)
//
// The controller keeps one crash dump, from its last crash, until it is erased.
// This says whether one is stored and, when it is, offers it as a file - the
// one place a builder can fetch it, since the Console never moves a file
// (system.api.get-coredump names this page). The download is a plain link to
// GET /api/coredump: the controller streams it from flash a chunk at a time,
// and the browser saves it as it arrives rather than holding it whole here.
// Asked when the page opens and on every return to it, never on a timer.
// =============================================================================
(() => {
  const state = document.getElementById("crash-dump-state");
  const row = document.getElementById("crash-dump-row");
  const feedback = document.getElementById("crash-dump-feedback");
  if (!state || !row || !window.PAApi || !window.PASurface) return;

  const setFeedback = (message, variant = "") => {
    if (!feedback) return;
    feedback.textContent = message;
    feedback.className = variant ? `feedback ${variant}` : "feedback";
  };

  const readCrashDump = async () => {
    let answer;
    try {
      answer = (await window.PAApi.get("/api/coredump/status", { timeoutMs: 5000 })).data;
    } catch (error) {
      state.textContent = "not checked";
      row.hidden = true;
      setFeedback(`The droid did not say: ${window.PAApi.messageFor(error)}`, "warning");
      // Rethrown so the surface's poll records the failure (#360).
      throw error;
    }
    const stored = Boolean(answer && answer.present);
    state.textContent = stored ? `one stored, ${(answer.size / 1024).toFixed(1)} KB` : "none stored";
    row.hidden = !stored;
    setFeedback("");
    return true;
  };

  window.PASurface.poll(readCrashDump, { runOnStart: true, refreshOnReturn: true }).start();
})();


// =============================================================================
// The way back into guided Setup (#297, GLOSSARY.md "Maintenance")
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
