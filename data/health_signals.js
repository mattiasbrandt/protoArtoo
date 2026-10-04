// =============================================================================
// data/health_signals.js
//
// Shared health indicator derivation for the dashboard traffic-light grid.
// - Explicit state semantics (CONTEXT.md "Status Color"): ok=nominal,
//   warn=degraded and the builder can do something about it, fail=hard fault,
//   off=not reporting, never asked, not fitted
// - A field the status frame does not carry reads the Live Reading's Unknown
//   (CONTEXT.md "Live Reading"). The word is handed in by the caller rather
//   than written here, so this model and every surface say the same one
// - A reading we do not have is off, never warn: amber promises a next move,
//   and "we have not heard" offers none (#402)
// - Staleness is not a health state. A stale row keeps the state the
//   controller last reported; the Status Plate carries the one freshness
//   statement for the whole surface (CONTEXT.md "Health Signal", _Avoid_)
// - A signal is a state and one word. It carries no key=value detail: a raw
//   field name is not something a builder reads (#298, #422)
// - protoR2link and the sound link are answered from one word table, which
//   every page that shows either link reads (readProtoR2link, readSoundLink)
// - The Dome ESC and the Foot Drive are answered the same way (readDomeEsc,
//   readFootDrive): green only for something heard back, and what the droid
//   commands is the detail, never the light (#399)
// - Memory is judged against one table of heap floors (HEAP_FLOORS), which
//   Maintenance's memory rows read too
// =============================================================================
(() => {
  const INDICATOR_STATE_LABELS = Object.freeze({
    ok: "OK",
    warn: "WARN",
    fail: "FAIL",
    off: "OFF",
  });

  const RC_CHANNEL_KEYS = Object.freeze([
    "rcCh1",
    "rcCh2",
    "rcCh3",
    "rcCh4",
    "rcCh5",
    "rcCh6",
  ]);

  // The heap floors, in bytes: the one table the health grid here and
  // Maintenance's memory rows both judge by. On the grid a reading at or below
  // a warn floor is Low, at or below a fail floor Critical.
  //
  // largest* judges the Internal Data Heap's largest free block. Its floors are
  // the admission ones until the bench day (#355) measures this reading's own.
  // free* and min* are the earlier runtime floors (heapMin held >= 40 KB with
  // the stream open), kept as they were.
  const HEAP_FLOORS = Object.freeze({
    freeCritical: 40000,
    freeWarn: 65000,
    minCritical: 36864,
    minWarn: 53248,
    largestCritical: 12000,
    largestWarn: 16000,
  });

  const hasOwnKey = (obj, key) => Object.prototype.hasOwnProperty.call(obj, key);
  const healthSignal = (state, reason = "") => ({ state, reason });

  const evaluateSbus = (payload) => {
    const { state, word } = readRcLink(payload);
    return healthSignal(state, word);
  };

  // An AP-only droid is a normal droid, so "not joined" is not "degraded" -
  // and a payload that never carried these keys is one we have not heard from.
  // Both read off. There is no warn branch here on purpose: the payload
  // carries no measure of a join that exists and is unhealthy (wifiRssi is 0
  // whenever the station is not connected - deriveWiFiConnectivityFields,
  // src/web/api_status_serializers.cpp), and no threshold is defined for it.
  const evaluateWifi = (payload, unknown) => {
    const reported = hasOwnKey(payload, "wifiConnected") || hasOwnKey(payload, "wifiClientConnected");
    const connected = payload.wifiConnected === true || payload.wifiClientConnected === true;
    if (connected) return healthSignal("ok", "Connected");
    if (reported) return healthSignal("off", "Not joined");
    return healthSignal("off", unknown);
  };

  // A payload that never carried littleFsReady has not told us the mount
  // failed; it has told us nothing. Red is "stopped or refused", and claiming
  // it for a key we were never sent is the same defect as claiming amber.
  const evaluateFilesystem = (payload, unknown) => {
    if (!hasOwnKey(payload, "littleFsReady")) return healthSignal("off", unknown);
    return payload.littleFsReady === true
      ? healthSignal("ok", "Mounted")
      : healthSignal("fail", "Not ready");
  };

  const evaluateHeap = (payload, unknown) => {
    const heapBytes = Number(payload.heapFree);

    // Judge memory health by the Internal Data Heap's largest free block
    // (heapLargestBlock, include/heap_reading.h): the droid's own RAM, which
    // counts no IRAM on the artoo-esp32 and no PSRAM on the ESP32-P4. NOT
    // heapLargest8bit: that is the Buffer Reading admission sheds requests by,
    // and on the P4 it counts megabytes of PSRAM, so it stays high while the
    // internal heap runs out.
    const largest = Number(payload.heapLargestBlock);
    if (Number.isFinite(largest) && largest >= 0) {
      if (largest > HEAP_FLOORS.largestWarn) return healthSignal("ok", "Normal");
      if (largest > HEAP_FLOORS.largestCritical) return healthSignal("warn", "Low");
      return healthSignal("fail", "Critical");
    }

    // A payload without heapLargestBlock: fall back to total free heap.
    // Neither number present is a reading we do not have, not a low one.
    if (!Number.isFinite(heapBytes) || heapBytes < 0) return healthSignal("off", unknown);

    if (heapBytes > HEAP_FLOORS.freeWarn) return healthSignal("ok", "Normal");
    if (heapBytes > HEAP_FLOORS.freeCritical) return healthSignal("warn", "Low");
    return healthSignal("fail", "Critical");
  };

  // ---------------------------------------------------------------------------
  // protoR2link and the sound link: one word table for both
  //
  // The two share one serial line. protoR2link hands it to sound only while it
  // runs on WiFi fallback and takes it back at will (src/tasks/dome_link.cpp,
  // releaseUartToAudioRx and domeUartAcquire), so sound can be held by
  // protoR2link and protoR2link is never held by sound: a protoR2link "lost"
  // is always a real loss. The droid already says which state each link is in
  // - dome_link.state, and the sound block's rx_status - and this is the one
  // place a page turns that into a word and a light. No page reads the line
  // owner or combines it with a state to reach a verdict of its own (#422,
  // CONTEXT.md "Health Signal").
  //
  // Each answer is { state, word, short }: `state` is the light (ok green,
  // fail red, off grey - neither table has an amber row), `word` is what a
  // page prints, and `short` is the Status Plate's form of it, set there in
  // capitals. `short` is the word itself wherever the table gives no short
  // form.
  //
  // `words` carries the Live Reading's two words (window.PALiveReading):
  // `unknown` for a link the frames never carry, and `waiting` for a
  // status of null, before the droid has sent a good frame.
  // ---------------------------------------------------------------------------
  const linkAnswer = (state, word, short = word) => ({ state, word, short });

  // While linked, the transport IS the value: the glossary's operator labels,
  // and the chip's short form of each (CONTEXT.md "protoR2link Transport
  // Visibility").
  const PROTO_R2LINK_TRANSPORT_WORDS = Object.freeze({
    uart: linkAnswer("ok", "UART (slip ring)", "UART"),
    wifi: linkAnswer("ok", "WiFi (fallback)", "WIFI"),
  });

  const PROTO_R2LINK_WORDS = Object.freeze({
    disabled: linkAnswer("off", "Off"),
    // Enabled and never answered: a droid with no dome board fitted reads
    // exactly this, so it is not reporting rather than degraded.
    not_seen: linkAnswer("off", "Not seen"),
    // Heard, then stopped.
    lost: linkAnswer("fail", "Lost"),
  });

  const SOUND_LINK_WORDS = Object.freeze({
    off: linkAnswer("off", "Off"),
    // protoR2link holds the shared line, so nobody can ask the module. Not
    // reporting, and never the module's fault.
    held: linkAnswer("off", "Held by protoR2link"),
    noAnswer: linkAnswer("fail", "No answer"),
  });

  const requireLinkWords = (words, status) => {
    const { unknown, waiting } = words || {};
    if (typeof unknown !== "string" || unknown === "") {
      throw new TypeError("a link reading needs the Live Reading's word for an unknown field");
    }
    if (status === null && (typeof waiting !== "string" || waiting === "")) {
      throw new TypeError("a link reading of no frame needs the Live Reading's Waiting word");
    }
    return { unknown, waiting };
  };

  const isObject = (value) => value !== null && typeof value === "object";

  // protoR2link, read from the status frame's dome_link block. The firmware
  // emits that block on every frame (src/web/web_server.cpp), so a frame
  // without it is one that never carries it.
  const readProtoR2link = (status, words) => {
    const { unknown, waiting } = requireLinkWords(words, status);
    if (status === null) return linkAnswer("off", waiting);
    const link = isObject(status) ? status.dome_link : undefined;
    if (!isObject(link)) return linkAnswer("off", unknown);
    if (link.state === "connected") {
      return PROTO_R2LINK_TRANSPORT_WORDS[link.transport] || linkAnswer("ok", unknown);
    }
    return PROTO_R2LINK_WORDS[link.state] || linkAnswer("off", unknown);
  };

  // The sound link, read from a sound block: the status frame's `audio`, or
  // the same fields as GET /api/audio answers them (the Sound page reads
  // both). The frame has no `audio` key when the sound component is switched
  // off in config, so an absent block is Off rather than Unknown.
  const readSoundBlock = (audio, { unknown }) => {
    if (audio === undefined) return SOUND_LINK_WORDS.off;
    if (!isObject(audio)) return linkAnswer("off", unknown);
    // Saved on but off this boot: no module is behind it (#370).
    if (audio.output === "off") return SOUND_LINK_WORDS.off;
    // Ahead of link_ok on purpose: a held line also reports link_ok false,
    // and only rx_status tells it from a module that did not answer.
    if (audio.rx_status === "blocked_by_dome_uart") return SOUND_LINK_WORDS.held;
    if (audio.link_ok === true) {
      // The fitted module's registry display name, as its driver reports it.
      const name = typeof audio.driver === "string" && audio.driver !== "" ? audio.driver : unknown;
      return linkAnswer("ok", name);
    }
    if (audio.link_ok === false) return SOUND_LINK_WORDS.noAnswer;
    return linkAnswer("off", unknown);
  };

  const readSoundLink = (status, words) => {
    const checked = requireLinkWords(words, status);
    if (status === null) return linkAnswer("off", checked.waiting);
    return readSoundBlock(isObject(status) ? status.audio : undefined, checked);
  };

  // ---------------------------------------------------------------------------
  // The Dome ESC and the Foot Drive: one word table each, the shape Sound's is
  //
  // Both rows used to light green on what the droid COMMANDS - a target speed,
  // a drive command - which is not a report (CONTEXT.md "Status Color": green
  // is nominal and reporting). Each answer is linkAnswer()'s { state, word,
  // short } plus `detail`, the firmware's own line for what is commanded
  // (src/web/status_json.cpp: "Target 0%", "Command 120/0"), which a page may
  // print under the word and never lights.
  // ---------------------------------------------------------------------------
  const commanded = (answer, entry) =>
    ({ ...answer, detail: isObject(entry) && typeof entry.detail === "string" ? entry.detail : "" });

  // A PWM ESC has no return wire, so nothing is ever heard from it: fitted, it
  // is grey and its word says what is commanded. Never green, never red.
  const DOME_ESC_DISABLED = linkAnswer("off", "Disabled");
  const DOME_ESC_WORDS = Object.freeze({
    idle: linkAnswer("off", "Idle"),
    spinning: linkAnswer("off", "Spinning"),
  });

  const readDomeEsc = (status, { unknown }) => {
    if (!isObject(status) || status.domeEnabled !== true) return { ...DOME_ESC_DISABLED, detail: "" };
    const entry = isObject(status.domeEsc) ? status.domeEsc : null;
    const domeState = entry ? entry.state : null;
    if (Object.hasOwn(DOME_ESC_WORDS, domeState)) return commanded(DOME_ESC_WORDS[domeState], entry);
    // A state we have no branch for is one we do not understand, which is not
    // reporting rather than degraded. The state string stays in the word so
    // the row still says what arrived.
    if (typeof domeState === "string" && domeState.length > 0) {
      return commanded(linkAnswer("off", `${unknown} (${domeState})`), entry);
    }
    return { ...linkAnswer("off", unknown), detail: "" };
  };

  // The Foot Drive is heard only through its backend's feedback: the frame
  // carries the `hoverboard` block while those readings are valid, and drops it
  // once they go stale (src/web/status_json.cpp, src/tasks/drive.cpp).
  //
  // The `drive` key follows the SAVED Foot Drive toggle, not what this boot
  // started: the frame reads it from the live config cache
  // (src/web/web_server.cpp captureStatusJsonInputs), while DriveTask reads it
  // once at boot (enableDrive applies at reboot). So no key is "switched off
  // in Configuration", and a toggle saved on and not yet restarted carries the
  // key with no drive running behind it - which reads "No answer" here until
  // the restart. Telling those apart needs a boot-state field in the frame.
  //
  // The word for a drive heard is keyed off that block's own name: the frame
  // carries no name for the backend, and the hoverboard is the only one the
  // firmware builds (include/drive_backend.h). A second backend needs a name
  // field in the frame before this word can be its. "No answer" is red for the
  // same reason: the hoverboard declares that it reports back
  // (DRIVE_CAP_REPORTS_FEEDBACK), so its silence is a fault. A backend that
  // declares no feedback must read grey instead, as Wiring's Foot Drive row
  // does from GET /api/identity/components; this reader does not ask, so
  // such a backend needs that question added here.
  const FOOT_DRIVE_WORDS = Object.freeze({
    off: linkAnswer("off", "Off"),
    hoverboard: linkAnswer("ok", "Hoverboard"),
    noAnswer: linkAnswer("fail", "No answer"),
  });

  const readFootDrive = (status) => {
    const entry = isObject(status) ? status.drive : undefined;
    if (entry === undefined) return { ...FOOT_DRIVE_WORDS.off, detail: "" };
    if (isObject(status.hoverboard)) return commanded(FOOT_DRIVE_WORDS.hoverboard, entry);
    return commanded(FOOT_DRIVE_WORDS.noAnswer, entry);
  };

  // ---------------------------------------------------------------------------
  // The RC receiver's link: one word table, read by Health, the Status Plate's
  // RC LINK chip (`short`) and Wiring's receiver row (#399)
  //
  // Every receiver input is read, rcCh1..rcCh6. rcCh1 is the drive receiver
  // except in single_sbus + useCh2, where the firmware routes it to rcCh2 and
  // omits rcCh1 entirely (src/web/web_server.cpp, the enableRcCh1 guard), so
  // reading rcCh1 alone would say "no RC" on a working droid. rcCh3..rcCh6
  // only ever report `ready` or `standby`, so they never outrank a link state;
  // with no rcCh1/rcCh2 on they say a spare wire is on, not that nothing is.
  //
  // The worst state across every receiver input that reports one, plus the
  // hardware failsafe bit - the half that would otherwise be missed: a radio
  // switched off makes the receiver assert failsafe while it keeps sending
  // frames, so the channel still reads `active` and only `sbusHwFailsafe` says
  // the link is dead. The channel states are the firmware's
  // (src/web/status_json.cpp). `sbusSignalLost` is not read: the boot arms
  // the SBUS watchdog before any frame (src/main.cpp), so it is true while a
  // receiver has simply not been heard yet, which `not_seen` already says.
  //
  // Standard PWM inputs say `ready`: the firmware publishes that they are
  // enabled and nothing whatever about whether pulses arrive (PWM loss submits
  // a zero frame and raises no failsafe, src/tasks/rc_input.cpp
  // dispatchStandardPwmInputs). So they read Unmeasured, grey - nothing is
  // wrong, nothing was measured. The plate said "PWM" until the operator
  // settled that word on 2026-09-17: a mode reads like a thing that is fine.
  // ---------------------------------------------------------------------------
  const RC_LINK_WORDS = Object.freeze({
    failsafe: linkAnswer("fail", "HW failsafe", "Failsafe"),
    lost: linkAnswer("fail", "Signal lost", "Lost"),
    noFrames: linkAnswer("off", "No frames"),
    framesOk: linkAnswer("ok", "Frames ok", "OK"),
    unmeasured: linkAnswer("off", "Unmeasured"),
    standby: linkAnswer("off", "Standby"),
    // No receiver input switched on at all.
    noInput: linkAnswer("off", "No RC input", "Off"),
  });

  const readRcLink = (status) => {
    if (isObject(status) && status.sbusHwFailsafe === true) return RC_LINK_WORDS.failsafe;
    const states = RC_CHANNEL_KEYS.filter((key) => isObject(status) && hasOwnKey(status, key))
      .map((key) => (isObject(status[key]) ? status[key].state : undefined));
    if (states.length === 0) return RC_LINK_WORDS.noInput;
    if (states.includes("signal_lost")) return RC_LINK_WORDS.lost;
    if (states.includes("not_seen")) return RC_LINK_WORDS.noFrames;
    if (states.includes("active")) return RC_LINK_WORDS.framesOk;
    if (states.includes("ready")) return RC_LINK_WORDS.unmeasured;
    return RC_LINK_WORDS.standby;
  };

  const evaluateDomeLink = (payload, unknown) => {
    const { state, word } = readProtoR2link(payload, { unknown });
    return healthSignal(state, word);
  };

  const evaluateSound = (payload, unknown) => {
    const { state, word } = readSoundLink(payload, { unknown });
    return healthSignal(state, word);
  };

  // Health's row is one line, so the commanded detail rides after the word
  // ("Idle, Target 0%"): it says what the grey is about.
  const evaluateDomeEsc = (payload, unknown) => {
    const { state, word, detail } = readDomeEsc(payload, { unknown });
    return healthSignal(state, detail ? `${word}, ${detail}` : word);
  };

  const HEALTH_EVALUATORS = Object.freeze({
    "h-sbus": evaluateSbus,
    "h-wifi": evaluateWifi,
    "h-fs": evaluateFilesystem,
    "h-heap": evaluateHeap,
    "h-dome-link": evaluateDomeLink,
    "h-sound": evaluateSound,
    "h-dome-esc": evaluateDomeEsc,
  });

  // `unknown` is the Live Reading's word for a field the frame does not carry
  // (window.PALiveReading.UNKNOWN). Required: a model that fell back to a word
  // of its own is exactly the drift the Live Reading exists to stop.
  const deriveHealthSignals = (payload, { unknown } = {}) => {
    if (typeof unknown !== "string" || unknown === "") {
      throw new TypeError("deriveHealthSignals needs the Live Reading's word for an unknown field");
    }
    const safePayload = payload && typeof payload === "object" ? payload : {};

    return Object.entries(HEALTH_EVALUATORS).map(([id, evaluate]) => {
      const signal = evaluate(safePayload, unknown);
      const resolved = signal && typeof signal === "object"
        ? signal
        : healthSignal("off", "Invalid state");
      return {
        id,
        state: resolved.state,
        reason: resolved.reason || "",
      };
    });
  };

  const api = Object.freeze({
    INDICATOR_STATE_LABELS,
    HEAP_FLOORS,
    deriveHealthSignals,
    readProtoR2link,
    readSoundLink,
    readDomeEsc,
    readFootDrive,
    readRcLink,
    RC_LINK_WORDS,
  });

  if (typeof window !== "undefined") {
    window.PAHealthSignals = api;
  }

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})();
