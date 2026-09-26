// =============================================================================
// data/web_api.js
//
// Shared HTTP helper for protoArtoo web pages.
// - Normalizes timeout/network/http/json errors
// - Provides GET / form POST / JSON POST helpers
// - Keeps API error handling consistent across pages
// =============================================================================
(() => {
  const DEFAULT_TIMEOUT_MS = 6000;

  class ApiError extends Error {
    constructor(message, {
      kind = "unknown", status = 0, cause = null, retryAfterMs = null,
      field = null, reason = null, accepts = null,
    } = {}) {
      super(message);
      this.name = "ApiError";
      this.kind = kind;
      this.status = status;
      this.cause = cause;
      // Populated from the response's Retry-After when the device refuses a
      // request as busy (ADR 0016), so callers honor the server's own interval
      // instead of guessing one.
      this.retryAfterMs = retryAfterMs;
      // A settings write the droid refused says which field, why and what it
      // would have taken as keys beside its sentence (docs/api.md "Refusals
      // from a settings write"). A caller words the refusal from these and
      // never reads them out of the message. null when the answer had none.
      this.field = field;
      this.reason = reason;
      this.accepts = accepts;
    }
  }

  // Retry-After is seconds-or-HTTP-date per RFC 9110. The device sends a small
  // integer, but parse defensively and reject anything non-positive so a bad
  // header degrades to the caller's default rather than a zero-delay hot loop.
  const parseRetryAfterMs = (headerValue) => {
    if (!headerValue) return null;
    const seconds = Number(headerValue);
    if (Number.isFinite(seconds)) return seconds > 0 ? seconds * 1000 : null;
    const dateMs = Date.parse(headerValue);
    if (Number.isNaN(dateMs)) return null;
    const delta = dateMs - Date.now();
    return delta > 0 ? delta : null;
  };

  // An AbortError has two distinct causes that callers must be able to tell
  // apart: the request's own timeout fired (kind "timeout", surfaced to the
  // operator), or the caller cancelled it through the signal it owns (kind
  // "cancelled", normally swallowed because the caller asked for it).
  const normalizeError = (error, callerSignal = null) => {
    if (error instanceof ApiError) return error;
    if (error?.name === "AbortError") {
      if (callerSignal?.aborted) {
        return new ApiError("Request cancelled", { kind: "cancelled", cause: error });
      }
      return new ApiError("Request timeout", { kind: "timeout" });
    }
    return new ApiError("Network request failed", { kind: "network", cause: error });
  };

  const parseResponse = async (response) => {
    const contentType = response.headers.get("content-type") || "";
    const isJson = contentType.includes("application/json");

    if (isJson) {
      return response.json().catch(() => {
        throw new ApiError("Malformed JSON response", { kind: "bad-json", status: response.status });
      });
    }

    return response.text();
  };

  // ESP32 has few concurrent AsyncTCP socket slots; a GET racing the page's
  // persistent SSE connection can occasionally receive a truncated response
  // body. That's a transient transport fault, not a real client/server
  // error, so it's worth one quiet retry before surfacing it.
  const BAD_JSON_RETRY_DELAY_MS = 150;

  // The device serves without HTTP keep-alive, so every request is its own
  // TCP connection, and its accept-time admission control rejects connection
  // bursts outright. Page code that fires many calls at once
  // (Promise.all/allSettled) would open them all as parallel sockets; this
  // FIFO caps in-flight requests so a page load presents as a short paced
  // trickle instead of a burst. Queue wait does not consume the request
  // timeout — the timeout timer starts when the request actually goes out.
  //
  // Narrowed from 2 to 1 per ADR 0019: page recovery assumes a single active
  // request slot, so the transport must not run two requests behind the
  // bootstrap's back and defeat its ordering.
  const MAX_CONCURRENT_REQUESTS = 1;
  let inFlightCount = 0;
  const requestWaiters = [];

  const acquireRequestSlot = () => {
    if (inFlightCount < MAX_CONCURRENT_REQUESTS) {
      inFlightCount += 1;
      return Promise.resolve();
    }
    return new Promise((resolve) => requestWaiters.push(resolve));
  };

  const releaseRequestSlot = () => {
    const next = requestWaiters.shift();
    if (next) {
      next(); // hand the slot to the next queued request
    } else {
      inFlightCount -= 1;
    }
  };

  // Cancellation is caller-owned: a caller that needs to cancel its request
  // creates its own AbortController and passes controller.signal as
  // opts.signal (standard platform pattern). Ownership travels with the
  // request, so cancelling one caller's request can never touch another
  // caller's request - there is no shared abort state in this module at all.
  // The bootstrap owns one controller per section run (see page_bootstrap.js);
  // requests issued without a signal (footer, status widgets) are not
  // cancellable by anyone.
  const throwIfCancelled = (signal) => {
    if (signal?.aborted) {
      throw new ApiError("Request cancelled", { kind: "cancelled" });
    }
  };

  const request = async (path, opts = {}) => {
    const { signal = null } = opts;
    throwIfCancelled(signal);
    await acquireRequestSlot();
    try {
      // Cancelled while queued for the slot: skip the dispatch entirely so a
      // dead request does not spend slot time, and release via the same
      // finally as every other outcome.
      throwIfCancelled(signal);
      return await performRequest(path, opts);
    } finally {
      // Sole release point for the slot this request acquired, on every
      // outcome (success, error, timeout, cancellation). Nothing else may
      // release a slot, so accounting cannot drift (ADR 0019).
      releaseRequestSlot();
    }
  };

  // Estop bypass: skips slot acquisition entirely so estop is never queued,
  // must never be auto-retried, and strips any caller signal so the E-Stop
  // POST can never be cancelled once dispatched.
  const estopRequest = async (path, opts = {}) => {
    return performRequest(path, { ...opts, noRetry: true, signal: null });
  };

  const performRequest = async (path, {
    method = "GET",
    timeoutMs = DEFAULT_TIMEOUT_MS,
    cache = "no-store",
    headers = {},
    form = null,
    json = null,
    noRetry = false,
    signal = null,
  } = {}) => {
    const attempt = async (isRetry) => {
      // The internal controller drives the timeout; the caller's signal (if
      // any) is bridged onto it so either source can abort the fetch. The
      // caller's controller is never touched from here - ownership stays with
      // the caller.
      const controller = new AbortController();
      const onCallerAbort = () => controller.abort();
      if (signal) {
        signal.addEventListener("abort", onCallerAbort, { once: true });
      }
      const timeoutId = window.setTimeout(() => controller.abort(), timeoutMs);

      try {
        const requestHeaders = { ...headers };
        let body;
        if (form) {
          body = form instanceof URLSearchParams ? form : new URLSearchParams(form);
          requestHeaders["Content-Type"] = "application/x-www-form-urlencoded;charset=UTF-8";
        } else if (json !== null) {
          body = JSON.stringify(json);
          requestHeaders["Content-Type"] = "application/json";
        }

        const response = await fetch(path, {
          method,
          cache,
          headers: requestHeaders,
          body,
          signal: controller.signal,
        });

        const payload = await parseResponse(response);

        if (!response.ok) {
          const body = payload && typeof payload === "object" ? payload : {};
          const key = (name) => (typeof body[name] === "string" ? body[name] : null);
          throw new ApiError(key("error") || `HTTP ${response.status}`, {
            kind: "http",
            status: response.status,
            retryAfterMs: parseRetryAfterMs(response.headers.get("retry-after")),
            field: key("field"),
            reason: key("reason"),
            accepts: key("accepts"),
          });
        }

        return {
          ok: true,
          status: response.status,
          // The response's declared media type, surfaced so a caller can check
          // that what came back is what its endpoint contracts. parseResponse()
          // already reads this header to choose JSON-vs-text parsing but kept
          // it to itself, so the Live Logs panel had no way to tell log text
          // from an intercepted HTML page and rendered the page (#261).
          // Strictly additive: every existing caller destructures ok/status/data.
          contentType: response.headers.get("content-type") || "",
          data: payload,
        };
      } catch (error) {
        const apiError = normalizeError(error, signal);
        // Network errors on GETs are usually the device shedding a
        // connection under load (admission control closes the socket);
        // like the truncated-JSON case, one quiet retry beats surfacing a
        // transient. POSTs are never retried — they may not be idempotent.
        // Also skip retries if noRetry is set (estop cannot be retried).
        if (!noRetry && !isRetry && method === "GET"
            && (apiError.kind === "bad-json" || apiError.kind === "network")) {
          await new Promise((resolve) => window.setTimeout(resolve, BAD_JSON_RETRY_DELAY_MS));
          // The caller may have cancelled during the retry delay; a cancelled
          // request must not go back out on the wire.
          throwIfCancelled(signal);
          return attempt(true);
        }
        throw apiError;
      } finally {
        window.clearTimeout(timeoutId);
        if (signal) {
          signal.removeEventListener("abort", onCallerAbort);
        }
      }
    };

    return attempt(false);
  };

  const get = (path, opts = {}) => request(path, { ...opts, method: "GET" });
  const postForm = (path, form, opts = {}) => request(path, { ...opts, method: "POST", form });
  const postJson = (path, json, opts = {}) => request(path, { ...opts, method: "POST", json });
  const estopPostForm = (path, form, opts = {}) => estopRequest(path, { ...opts, method: "POST", form });

  const HTTP_STATUS_MESSAGES = {
    400: "Device rejected the request",
    403: "Access denied by device",
    404: "Not found on device",
    500: "Device error",
    501: "Not supported by device",
    503: "Device unavailable",
  };

  // ---------------------------------------------------------------------------
  // The words a refusal is shown in (#348)
  //
  // The droid names a refusal in its own vocabulary - `web_control_disabled`,
  // and three more it emits from src/web/api_actions.cpp. Those tokens are the
  // wire format and stay exactly as they are; what may never happen is one of
  // them reaching a screen, which is what every caller of messageFor() did
  // until this map existed, because an HTTP error carries the droid's `error`
  // field as its message.
  //
  // So this is the door: one map from token to sentence, read rather than
  // typed, so a surface CANNOT say the internal word. That is the shape
  // r2d2-astromech-simulator v1.79.0 uses for the same problem
  // (src/js/maestro/setup-hw.js:999, `PW_END_WORD`): storage keeps `min`/`max`,
  // every message reads the word out of the map.
  //
  // `route` is the builder's next move where there is one, and null where there
  // is not. A refusal by a safety rule is a settled no: nothing to do about it,
  // so no destination, no link, and no suggestion to buy or fit anything
  // (CONTEXT.md "Availability Family").
  const DEVICE_REFUSALS = Object.freeze({
    invalid_action_token: Object.freeze({
      text: "Unknown action. Reload the page.",
      route: null,
    }),
    // Only the estop reaches this guard (evaluateActionTestGuard,
    // include/api_actions.h). Stopping the droid is not something a test press
    // does, and there is nothing for the builder to change about that.
    safety_critical_blocked: Object.freeze({
      text: "The estop is never sent as a test.",
      route: null,
    }),
    web_control_disabled: Object.freeze({
      text: "Web control is off.",
      route: Object.freeze({ href: "#drive", label: "Turn it on in Foot Drive" }),
    }),
    // An analog action, or one that needs a payload: the test button sends
    // neither, so this control cannot drive it however the droid is set up.
    action_not_testable: Object.freeze({
      text: "Needs a value the test button cannot send.",
      route: null,
    }),
  });

  /**
   * The refusal behind an error, where the droid named one this map knows.
   *
   * @returns {{text: string, route: ({href: string, label: string}|null)}|null}
   *   null when the error is not one of the droid's own refusal tokens, which
   *   is every transport failure and every message already written for people.
   */
  const refusalFor = (error) => {
    if (!(error instanceof ApiError) || error.kind !== "http") return null;
    return DEVICE_REFUSALS[error.message] || null;
  };

  // ---------------------------------------------------------------------------
  // The builder's words for every Setting (ADR 0068, amended 2026-09-26, twice)
  //
  // The droid refuses a value it will not take with its field, its reason and
  // what it accepts as keys beside its sentence (docs/api.md "Refusals from a
  // settings write"). The field is the droid's own name for the Setting -
  // `speedLimitMax`, or `ledc:0.throwMs` for an Output's - and that name is
  // wire vocabulary that must never reach a screen (ADR 0059). So a refusal is
  // worded here, from the keys and never from the sentence, and this is the
  // one table a field is turned into words by.
  //
  // One entry per field holds everything the screen says about it: pages take
  // a Setting's label and when it takes effect from here and keep none of
  // their own. What a Setting accepts is the droid's to say, on every refusal,
  // so no range is copied into the browser. Every field the firmware declares -
  // a Setting (src/config_settings.cpp), a Record's field (include/config_records.h)
  // or an act's (src/web/api_config_apply.cpp) - must have an entry, and
  // tools/check_setting_words.py fails the build when one has none, or when an
  // entry's timing is not the one its declaration states.
  //
  //   label  - what a page shows beside the control, where a page reads it from
  //            here (the Component Toggles, the Sound page's tracks)
  //   applies - when a saved value takes effect, in data/apply_timing.js's
  //            vocabulary: the token the firmware declares, never decided here
  //   word   - what the Setting is called in a sentence, as a refusal says it
  //   unit   - the unit its number is in, where it has one (R15)
  //   path   - where GET /api/config has it: a restore's refusal can be named
  //            by either
  //   values - the words its accepted tokens are said in, where they are wire
  //            vocabulary themselves
  //   must   - an Output's Setting said as a verb on the Output instead of
  //            "<Output>'s <word> must be": "GPIO 49 must carry ..."
  //   clash  - what a conflict with the Settings beside it says
  //   valueOnly - worded only when its value is refused; any other refusal
  //            naming it keeps the droid's own sentence
  //   refused - what a refusal with nothing to accept says, where "was not
  //            saved" says too little: a part the droid does not list, an
  //            address that is not one
  //   malformed - what a refusal of a request the droid could not read says
  //
  // An act's field names an Output by the address the page sent, never by a
  // word of the droid's (R16):
  //   subject - the refused value IS an Output, and the Output is what the
  //            sentence is about: "ARM3 already drives as many parts as it can"
  //   on     - the field whose sent value is the Output this one is about:
  //            "GPIO 49's captured width must be 500 to 2500 µs"
  const MS = " ms";
  const US = " µs";
  const PCT = "%";
  const PRESET_CLASH = "must differ from the other presets";
  const PULSE_CLASH = "must sit between the minimum and maximum pulses";
  const NOT_LISTED = "is not one this droid takes";
  const OFF_THE_DROID = "is not on this droid";
  const CATEGORY_CLASH = "must be at most the last track, or both 0";
  const SETTING_WORDS = Object.freeze({
    speedLimitMax: { applies: "immediate", word: "maximum speed limit", path: "drive.speedLimitMax" },
    speedPresetSlow: { applies: "immediate", word: "slow preset", path: "drive.speedPresetSlow", clash: PRESET_CLASH },
    speedPresetNormal: { applies: "immediate", word: "normal preset", path: "drive.speedPresetNormal", clash: PRESET_CLASH },
    speedPresetTurbo: { applies: "immediate", word: "turbo preset", path: "drive.speedPresetTurbo", clash: PRESET_CLASH },
    webDriveTimeoutMs: { applies: "immediate", word: "web control timeout", unit: MS, path: "drive.webDriveTimeoutMs" },
    stationary: { applies: "immediate", word: "stationary mode", path: "drive.stationary" },
    rcInputMode: {
      applies: "restart-required",
      word: "receiver type",
      path: "rc.inputMode",
      values: { standard_pwm: "PWM", single_sbus: "one SBUS", dual_sbus: "two SBUS", elrs: "ELRS" },
    },
    sbusTimeoutMs: { applies: "immediate", word: "signal-lost timeout", unit: MS, path: "rc.sbusTimeoutMs" },
    rcMember: { applies: "immediate", word: "radio", path: "rc.member", refused: NOT_LISTED },
    sbusRecvCh2: { applies: "restart-required", word: "second SBUS input", path: "rc.sbus.recvCh2" },
    enableDomeEsc: { label: "Dome ESC", applies: "at-reboot", word: "Dome ESC", path: "components.domeEsc.enabled" },
    enableRcCh1: { label: "RC channel 1", applies: "restart-required", word: "RC channel 1", path: "components.rcCh1.enabled" },
    enableRcCh2: { label: "RC channel 2", applies: "restart-required", word: "RC channel 2", path: "components.rcCh2.enabled" },
    enableRcCh3: { label: "RC channel 3", applies: "restart-required", word: "RC channel 3", path: "components.rcCh3.enabled" },
    enableRcCh4: { label: "RC channel 4", applies: "restart-required", word: "RC channel 4", path: "components.rcCh4.enabled" },
    enableRcCh5: { label: "RC channel 5", applies: "restart-required", word: "RC channel 5", path: "components.rcCh5.enabled" },
    enableRcCh6: { label: "RC channel 6", applies: "restart-required", word: "RC channel 6", path: "components.rcCh6.enabled" },
    enableDrive: { label: "Foot Drive", applies: "at-reboot", word: "Foot Drive", path: "components.drive.enabled" },
    enableAudio: { label: "Sound", applies: "at-reboot", word: "Sound", path: "components.audio.enabled" },
    soundMember: { applies: "at-reboot", word: "sound module", path: "components.audio.member", refused: NOT_LISTED },
    enableProtoR2link: { label: "Dome link", applies: "at-reboot", word: "dome link", path: "components.protoR2link.enabled" },
    domeEscNeutralUs: { applies: "immediate", word: "neutral pulse", unit: US, path: "domeEsc.neutralUs", clash: PULSE_CLASH },
    domeEscMinPulseUs: {
      applies: "immediate",
      word: "minimum pulse", unit: US, path: "domeEsc.minPulseUs", clash: "must be at most the neutral pulse",
    },
    domeEscMaxPulseUs: {
      applies: "immediate",
      word: "maximum pulse", unit: US, path: "domeEsc.maxPulseUs", clash: "must be at least the neutral pulse",
    },
    domeEscSpeedLimitPct: { applies: "immediate", word: "dome speed limit", unit: PCT, path: "domeEsc.speedLimitPct" },
    domeEscRndEnable: { applies: "immediate", word: "dome turning on its own", path: "domeEsc.rndEnable" },
    domeEscRndSpeedPct: { applies: "immediate", word: "turn speed", unit: PCT, path: "domeEsc.rndSpeedPct" },
    domeEscRndPauseMin: {
      applies: "immediate",
      word: "shortest pause", unit: " s", path: "domeEsc.rndPauseMin", clash: "must be at most the longest pause",
    },
    domeEscRndPauseMax: {
      applies: "immediate",
      word: "longest pause", unit: " s", path: "domeEsc.rndPauseMax", clash: "must be at least the shortest pause",
    },
    domeEscRndMoveMs: { applies: "immediate", word: "move duration", unit: MS, path: "domeEsc.rndMoveMs" },
    protoR2linkWifiPeerIp: {
      applies: "immediate",
      word: "dome's IP address", path: "protoR2link.wifiPeerIp",
      refused: "must be empty or an address like 192.168.4.2",
    },
    logLevel: { applies: "immediate", word: "log level", path: "system.logLevel" },
    // An act's width, not a stored Setting: POST /api/servo words its
    // refusal the same way.
    positionUs: { word: "width", unit: US },

    // The Records (include/config_records.h), by the form name each field is
    // posted under. Guided Setup and the Droid Build picker state them.
    domeDesign: { applies: "immediate", word: "dome design", path: "droidBuild.domeDesign", refused: NOT_LISTED },
    domeVariant: { applies: "immediate", word: "dome variant", path: "droidBuild.domeVariant", refused: NOT_LISTED },
    bodyDesign: { applies: "immediate", word: "body design", path: "droidBuild.bodyDesign", refused: NOT_LISTED },
    bodyVariant: { applies: "immediate", word: "body variant", path: "droidBuild.bodyVariant", refused: NOT_LISTED },
    fittedParts: {
      applies: "immediate",
      word: "the list of fitted parts", path: "droidBuild.fitted", refused: "names a part this droid does not model",
    },
    guidedSetupRun: {
      applies: "immediate",
      word: "setup's progress",
      path: "guidedSetup.run",
      values: { "not-run": "not started", skipped: "skipped", completed: "finished" },
    },
    guidedSetupVisited: {
      applies: "immediate",
      word: "the list of questions shown", path: "guidedSetup.visited", refused: "names a question the droid cannot keep",
    },
    guidedSetupSummaryDone: {
      applies: "immediate",
      word: "setup summary", path: "guidedSetup.summaryDone", values: { true: "done", false: "not done" },
    },

    // The acts' fields (src/web/api_config_apply.cpp kActFields): move a Part,
    // capture an end, reverse an Output.
    movePart: { word: "this part", refused: "is not one this droid models" },
    movePartFrom: {
      word: "the output it was on", subject: true, clash: "no longer drives it", refused: OFF_THE_DROID,
    },
    movePartTo: {
      word: "the output it goes to", subject: true,
      clash: "already drives as many parts as it can", refused: OFF_THE_DROID,
    },
    captureOutput: { word: "this output", subject: true, refused: OFF_THE_DROID },
    captureEnd: { word: "end to set", on: "captureOutput" },
    captureUs: { word: "captured width", unit: US, on: "captureOutput" },
    reverseOutput: { word: "this output", subject: true, refused: OFF_THE_DROID },
    // A body the droid could not read at all: no one field is to blame.
    plain: { word: "the request", malformed: "could not be read" },

    // The audio Settings (#431 addendum), by the name their door takes them
    // under. The Sound page's own labels.
    volume: { applies: "immediate", word: "volume" },
    scream: { label: "Scream", applies: "immediate", word: "Scream track" },
    faint: { label: "Short Circuit", applies: "immediate", word: "Short Circuit track" },
    leia: { label: "Leia Message", applies: "immediate", word: "Leia Message track" },
    cantina_s: { label: "Short Cantina", applies: "immediate", word: "Short Cantina track" },
    sw_theme: { label: "Star Wars Theme", applies: "immediate", word: "Star Wars Theme track" },
    imp_march: { label: "Imperial March", applies: "immediate", word: "Imperial March track" },
    cantina_l: { label: "Long Cantina", applies: "immediate", word: "Long Cantina track" },
    startup: { label: "Boot Sound", applies: "immediate", word: "boot sound track" },
    doodoo: { label: "Doo-doo", applies: "immediate", word: "Doo-doo track" },
    failure: { label: "Failure", applies: "immediate", word: "Failure track" },
    disco: { label: "Disco", applies: "immediate", word: "Disco track" },
    mahna: { label: "Mahna Mahna", applies: "immediate", word: "Mahna Mahna track" },
    inlove: { label: "In Love", applies: "immediate", word: "In Love track" },
    macho: { label: "Macho Man", applies: "immediate", word: "Macho Man track" },
    gangnam: { label: "Gangnam Style", applies: "immediate", word: "Gangnam Style track" },
    uptown: { label: "Uptown Funk", applies: "immediate", word: "Uptown Funk track" },
    celebr: { label: "Celebration", applies: "immediate", word: "Celebration track" },
    stayin: { label: "Stayin' Alive", applies: "immediate", word: "Stayin' Alive track" },
    harlem: { label: "Harlem Shake", applies: "immediate", word: "Harlem Shake track" },
    pbjtime: { label: "PBJ Time", applies: "immediate", word: "PBJ Time track" },
    sys_boot: { label: "Boot Complete (auto)", applies: "immediate", word: "boot complete track" },
    sys_mode_n: { label: "Mode → Normal", applies: "immediate", word: "Normal mode track" },
    sys_mode_s: { label: "Mode → Slow", applies: "immediate", word: "Slow mode track" },
    sys_mode_t: { label: "Mode → Turbo", applies: "immediate", word: "Turbo mode track" },
    sys_drv_on: { label: "Drives engaged", applies: "immediate", word: "drives engaged track" },
    sys_dome_on: { label: "Dome enabled", applies: "immediate", word: "dome enabled track" },
    sys_net_down: { label: "Network Link Lost (auto)", applies: "immediate", word: "link lost track" },
    rand_min: { applies: "immediate", word: "random range's first track" },
    rand_max: { applies: "immediate", word: "random range's last track" },
    snd_int_quiet: { applies: "immediate", word: "Quiet chatter interval", unit: " s" },
    snd_int_mid: { applies: "immediate", word: "Mid-Awake chatter interval", unit: " s" },
    snd_int_full: { applies: "immediate", word: "Full-Awake chatter interval", unit: " s" },
    snd_int_awake: { applies: "immediate", word: "Awake+ chatter interval", unit: " s" },
    snd_cat_gen_lo: { label: "General", applies: "immediate", word: "General first track", clash: CATEGORY_CLASH },
    snd_cat_gen_hi: { label: "General", applies: "immediate", word: "General last track" },
    snd_cat_chat_lo: { label: "Chatty", applies: "immediate", word: "Chatty first track", clash: CATEGORY_CLASH },
    snd_cat_chat_hi: { label: "Chatty", applies: "immediate", word: "Chatty last track" },
    snd_cat_hap_lo: { label: "Happy", applies: "immediate", word: "Happy first track", clash: CATEGORY_CLASH },
    snd_cat_hap_hi: { label: "Happy", applies: "immediate", word: "Happy last track" },
    snd_cat_proc_lo: { label: "Processing", applies: "immediate", word: "Processing first track", clash: CATEGORY_CLASH },
    snd_cat_proc_hi: { label: "Processing", applies: "immediate", word: "Processing last track" },
    snd_cat_sad_lo: { label: "Sad", applies: "immediate", word: "Sad first track", clash: CATEGORY_CLASH },
    snd_cat_sad_hi: { label: "Sad", applies: "immediate", word: "Sad last track" },
    snd_cat_sent_lo: { label: "Sentimental", applies: "immediate", word: "Sentimental first track", clash: CATEGORY_CLASH },
    snd_cat_sent_hi: { label: "Sentimental", applies: "immediate", word: "Sentimental last track" },
    snd_cat_hum_lo: { label: "Humming", applies: "immediate", word: "Humming first track", clash: CATEGORY_CLASH },
    snd_cat_hum_hi: { label: "Humming", applies: "immediate", word: "Humming last track" },
    snd_cat_scrm_lo: { label: "Scream", applies: "immediate", word: "Scream first track", clash: CATEGORY_CLASH },
    snd_cat_scrm_hi: { label: "Scream", applies: "immediate", word: "Scream last track" },
    snd_cat_ooh_lo: { label: "Surprised", applies: "immediate", word: "Surprised first track", clash: CATEGORY_CLASH },
    snd_cat_ooh_hi: { label: "Surprised", applies: "immediate", word: "Surprised last track" },
    snd_cat_alrm_lo: { label: "Alert", applies: "immediate", word: "Alert first track", clash: CATEGORY_CLASH },
    snd_cat_alrm_hi: { label: "Alert", applies: "immediate", word: "Alert last track" },
    snd_cat_snrk_lo: { label: "Snarky", applies: "immediate", word: "Snarky first track", clash: CATEGORY_CLASH },
    snd_cat_snrk_hi: { label: "Snarky", applies: "immediate", word: "Snarky last track" },
    snd_cat_whis_lo: { label: "Whistle", applies: "immediate", word: "Whistle first track", clash: CATEGORY_CLASH },
    snd_cat_whis_hi: { label: "Whistle", applies: "immediate", word: "Whistle last track" },
    quiet: { label: "Quiet", applies: "immediate", word: "Quiet mood's sound set" },
    mid: { label: "Mid-Awake", applies: "immediate", word: "Mid-Awake mood's sound set" },
    full: { label: "Full-Awake", applies: "immediate", word: "Full-Awake mood's sound set" },
    awakeplus: { label: "Awake+", applies: "immediate", word: "Awake+ mood's sound set" },
    // A catalog binding's bank and page, beside a track.
    bank: { word: "catalog bank", valueOnly: true },
    page: { word: "catalog page", valueOnly: true },
    index: { word: "catalog index", valueOnly: true },
  });

  // An Output's Settings, by the row key the droid refuses them under
  // (`ledc:1.throwMs`). The Output is named by the page (nameOutputsWith()).
  const ROW_SETTING_WORDS = Object.freeze({
    // An expander's Output has no tick to switch off: it takes only `true`.
    wired: { applies: "at-reboot", word: "wired tick", values: { true: "on" } },
    // Said with the verb Output Settings uses ("GPIO 49 carries Servo"), not
    // as a possessive: "GPIO 49 must carry nothing, an MG996R or an LED strip".
    component: {
      applies: "immediate",
      word: "wire",
      must: "must carry",
      values: { none: "nothing", mg996r: "an MG996R", mg90s: "an MG90S", rgb: "an LED strip" },
    },
    ledCount: { applies: "at-reboot", word: "LED count" },
    throwMs: { applies: "immediate", word: "time to full throw", unit: MS },
    accelMs: { applies: "immediate", word: "time to get up to speed", unit: MS },
    ease: { applies: "immediate", word: "ease" },
    boot: {
      applies: "at-reboot",
      word: "power-up setting",
      values: { limp: "limp", "home-hold": "home and hold", "home-release": "home then release" },
    },
    openUs: { applies: "immediate", word: "open end", unit: US },
    centreUs: { applies: "immediate", word: "centre", unit: US },
    closeUs: { applies: "immediate", word: "close end", unit: US },
    calibrated: { applies: "immediate", word: "calibration" },
    parts: { applies: "immediate", word: "list of parts" },
    address: { word: "address", clash: "is on two rows" },
  });

  // An entry, by the name a page knows the field by: a form name, or an
  // Output row Setting's key. A name with no entry is a page asking about a
  // field nobody declared, which is a defect to say rather than a default to
  // guess, so this throws.
  const entryFor = (table, name, what) => {
    const entry = Object.hasOwn(table, name) ? table[name] : null;
    if (!entry || entry[what] === undefined) {
      throw new TypeError(`${name} has no ${what} in the words table (data/web_api.js)`);
    }
    return entry[what];
  };
  const labelOf = (name) => entryFor(SETTING_WORDS, name, "label");
  const timingOf = (name) => entryFor(SETTING_WORDS, name, "applies");
  const rowTimingOf = (key) => entryFor(ROW_SETTING_WORDS, key, "applies");

  // The Setting a refusal's field names, and the Output it is on where it is
  // one of an Output's. null for a field this table has no words for.
  const settingFor = (field) => {
    if (typeof field !== "string" || !field) return null;
    if (Object.hasOwn(SETTING_WORDS, field)) return { words: SETTING_WORDS[field], address: null, key: field };
    const byPath = Object.keys(SETTING_WORDS).find((name) => SETTING_WORDS[name].path === field);
    if (byPath) return { words: SETTING_WORDS[byPath], address: null, key: byPath };
    const dot = field.lastIndexOf(".");
    const key = field.slice(dot + 1);
    if (dot > 0 && field.slice(0, dot).includes(":") && Object.hasOwn(ROW_SETTING_WORDS, key)) {
      return { words: ROW_SETTING_WORDS[key], address: field.slice(0, dot), key };
    }
    return null;
  };

  // What a Setting takes, from the refusal's `accepts`: `20..10000` is a
  // range, `true,false,1,0` is on or off, anything else the words it takes.
  const sayAccepts = (accepts, words) => {
    const range = /^(\d+|[A-Z])\.\.(\d+|[A-Z])$/.exec(accepts);
    if (range) return `${range[1]} to ${range[2]}${words.unit || ""}`;
    if (accepts === "true,false,1,0") return "on or off";
    const said = accepts.split(",").map((token) => (words.values && words.values[token]) || token);
    return said.length > 1 ? `${said.slice(0, -1).join(", ")} or ${said[said.length - 1]}` : said[0];
  };

  // How the page names an Output by its address. data/outputs.js tells this
  // module once it has read the table; until then an Output is "this output".
  let outputName = () => null;
  const nameOutputsWith = (nameOf) => {
    if (typeof nameOf === "function") outputName = nameOf;
  };

  const capitalise = (text) => text.charAt(0).toUpperCase() + text.slice(1);

  // What the refused request sent under `name`, from the form or the plain
  // object the page posted; null when the page did not say.
  const sentValue = (sent, name) => {
    if (!sent || !name) return null;
    const value = sent instanceof URLSearchParams ? sent.get(name) : sent[name];
    return typeof value === "string" ? value : null;
  };

  /**
   * A settings refusal in the builder's words: what they changed, and what it
   * takes - `Maximum speed limit must be 0 to 600`, `GPIO 49's time to full throw must be
   * 20 to 10000 ms`. Read from the refusal's field, reason and accepts, never
   * from its sentence.
   *
   * @param {Error} error - what PAApi threw
   * @param {object|URLSearchParams} [sent] - what the refused request posted,
   *   so a refusal of an act names the Output the page sent it for
   * @returns {string|null} null when the error is not a refusal of a Setting
   *   this table has words for.
   */
  const sayRefusal = (error, sent = null) => {
    if (!(error instanceof Error)) return null;
    // "The fitted module has no catalog" is not about any value, and its
    // sentence carries no wire name: it is left as the droid said it.
    if (error.reason === "not-in-this-build") return null;
    const setting = settingFor(error.field);
    if (!setting) return null;
    const { words, address, key } = setting;
    // A part named only for its value: "bank and page must be provided
    // together" is not a refusal of the bank the builder chose.
    if (words.valueOnly && error.reason !== "out-of-range") return null;
    const about = address || (words.on ? sentValue(sent, words.on) : null);
    const owner = about ? outputName(about) || "This output" : null;
    const subject = words.subject ? outputName(sentValue(sent, key)) : null;
    const name = subject || (owner ? `${owner}'s ${words.word}` : capitalise(words.word));
    const accepts = typeof error.accepts === "string" ? error.accepts : "";
    if (error.reason === "malformed-argument" && words.malformed) return `${name} ${words.malformed}`;
    if (error.reason === "conflict") return `${name} ${words.clash || "clashes with another setting"}`;
    if (error.reason === "out-of-range") {
      if (accepts && words.must && owner) return `${owner} ${words.must} ${sayAccepts(accepts, words)}`;
      if (accepts) return `${name} must be ${sayAccepts(accepts, words)}`;
      if (words.refused) return `${name} ${words.refused}`;
    }
    return `${name} was not saved`;
  };

  // `sent`: what the refused request posted (sayRefusal()).
  const messageFor = (error, sent = null) => {
    if (!(error instanceof ApiError)) return "Request failed";
    if (error.kind === "timeout") return "Request timed out";
    if (error.kind === "cancelled") return "Request cancelled";
    if (error.kind === "network") return "Network error";
    if (error.kind === "http") {
      // Before the raw message: a token the droid refused in its own
      // vocabulary is translated here or it is shown verbatim.
      const refusal = refusalFor(error);
      if (refusal) return refusal.route ? `${refusal.text} ${refusal.route.label}.` : refusal.text;
      // A Setting the droid would not take, in the builder's words.
      const said = sayRefusal(error, sent);
      if (said) return said;
      if (error.message && !error.message.startsWith("HTTP ")) return error.message;
      return HTTP_STATUS_MESSAGES[error.status]
        || (error.status >= 500 ? "Device error" : "Device rejected the request");
    }
    return error.message || "Request failed";
  };

  // Disable or enable a list of controls, keeping aria-disabled in sync.
  const gateControls = (elements, enabled) => {
    elements.forEach((el) => {
      if (!el) return;
      el.disabled = !enabled;
      el.setAttribute("aria-disabled", enabled ? "false" : "true");
    });
  };

  // Shared UI utilities
  const showFeedback = (el, text, level = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = level ? `feedback ${level}` : "feedback";
  };

  // Escape HTML special characters for text context (null/undefined → empty string).
  const escapeHtml = (value) => {
    return String(value ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  };

  // Escape HTML special characters for attribute context (null/undefined → empty string).
  const escapeAttr = (value) => {
    return String(value ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  };

  // Debounce with per-call-site state management.
  const debounce = (fn, ms) => {
    let timeoutId = null;
    return (...args) => {
      if (timeoutId !== null) window.clearTimeout(timeoutId);
      timeoutId = window.setTimeout(() => {
        fn(...args);
        timeoutId = null;
      }, ms);
    };
  };

  window.PAApi = {
    ApiError,
    request,
    get,
    postForm,
    postJson,
    estopPostForm,
    messageFor,
    refusalFor,
    sayRefusal,
    nameOutputsWith,
    labelOf,
    timingOf,
    rowTimingOf,
    gateControls,
  };

  window.PAUtils = {
    showFeedback,
    escapeHtml,
    escapeAttr,
    debounce,
  };
})();
