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

  const hasOwnKey = (obj, key) => Object.prototype.hasOwnProperty.call(obj, key);
  const healthSignal = (state, reason = "") => ({ state, reason });

  const evaluateSbus = (payload) => {
    // No rcCh1-rcCh6 key at all: the RC receiver is switched off.
    const anyRcEnabled = RC_CHANNEL_KEYS.some((key) => hasOwnKey(payload, key));
    if (!anyRcEnabled) return healthSignal("off", "No RC input");
    if (payload.sbusHwFailsafe === true) return healthSignal("fail", "HW failsafe");
    if (payload.sbusSignalLost === true) return healthSignal("fail", "Signal lost");
    return healthSignal("ok", "Frames ok");
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
    const t = (typeof window !== "undefined" && window.PA_HEAP) || {};

    // Judge memory health by the largest allocatable DRAM block — the value
    // the device's admission control keys on (requests are shed below its
    // floors: 14000 for new work, 12000 at accept). heapLargestBlock is NOT
    // used here: it reads a capability mask dominated by leftover IRAM that
    // malloc can never allocate, so it sits frozen regardless of pressure.
    const largest = Number(payload.heapLargest8bit);
    if (Number.isFinite(largest) && largest >= 0) {
      const warnAt = t.largestWarn ?? 16000;
      const failAt = t.largestCritical ?? 12000;
      if (largest > warnAt) return healthSignal("ok", "Normal");
      if (largest > failAt) return healthSignal("warn", "Low");
      return healthSignal("fail", "Critical");
    }

    // Older firmware without heapLargest8bit: fall back to total free heap.
    // Neither number present is a reading we do not have, not a low one.
    if (!Number.isFinite(heapBytes) || heapBytes < 0) return healthSignal("off", unknown);

    const warnAt = t.freeWarn ?? 65000;
    const failAt = t.freeCritical ?? 40000;
    if (heapBytes > warnAt) return healthSignal("ok", "Normal");
    if (heapBytes > failAt) return healthSignal("warn", "Low");
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
  // `unknown` for a link the frames never carry, and `findingOut` for a
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
    const { unknown, findingOut } = words || {};
    if (typeof unknown !== "string" || unknown === "") {
      throw new TypeError("a link reading needs the Live Reading's word for an unknown field");
    }
    if (status === null && (typeof findingOut !== "string" || findingOut === "")) {
      throw new TypeError("a link reading of no frame needs the Live Reading's Finding out word");
    }
    return { unknown, findingOut };
  };

  const isObject = (value) => value !== null && typeof value === "object";

  // protoR2link, read from the status frame's dome_link block. The firmware
  // emits that block on every frame (src/web/web_server.cpp), so a frame
  // without it is one that never carries it.
  const readProtoR2link = (status, words) => {
    const { unknown, findingOut } = requireLinkWords(words, status);
    if (status === null) return linkAnswer("off", findingOut);
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
    if (status === null) return linkAnswer("off", checked.findingOut);
    return readSoundBlock(isObject(status) ? status.audio : undefined, checked);
  };

  const evaluateDomeLink = (payload, unknown) => {
    const { state, word } = readProtoR2link(payload, { unknown });
    return healthSignal(state, word);
  };

  const evaluateSound = (payload, unknown) => {
    const { state, word } = readSoundLink(payload, { unknown });
    return healthSignal(state, word);
  };

  const evaluateDomeEsc = (payload, unknown) => {
    if (payload.domeEnabled !== true) return healthSignal("off", "Disabled");

    const domeData = payload.domeEsc && typeof payload.domeEsc === "object" ? payload.domeEsc : null;
    const domeState = domeData ? domeData.state : null;

    if (domeState === "spinning") return healthSignal("ok", "Spinning");
    if (domeState === "idle") return healthSignal("ok", "Idle");
    // A state we have no branch for is one we do not understand, which is not
    // reporting rather than degraded. The state string stays in the word so
    // the row still says what arrived.
    if (typeof domeState === "string" && domeState.length > 0) {
      return healthSignal("off", `${unknown} (${domeState})`);
    }
    return healthSignal("off", unknown);
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
    deriveHealthSignals,
    readProtoR2link,
    readSoundLink,
  });

  if (typeof window !== "undefined") {
    window.PAHealthSignals = api;
  }

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})();
