// =============================================================================
// data/live_reading.js
//
// The Live Reading (CONTEXT.md): what every surface is handed about the droid's
// live state, and the one place that decides it (#419).
//
// - The last status frame that carried all six core fields. A frame missing
//   one is ignored and the last reading stands -- the estop included.
// - For each field, one of three answers: heard, Finding out (no good frame
//   yet) or Unknown (frames arrive but do not carry it). Both words live here,
//   and a surface writes neither of its own.
// - The estop, three-valued: latched, clear, or Finding out. A move act is live
//   only on a heard, clear estop, and losing contact with the droid turns the
//   estop back to Finding out while every other field keeps its last value.
// - The stream-or-poll choice: the status stream when the browser has one,
//   otherwise ONE /api/status poll every 5 s for the whole shell, running while
//   anybody listens and paused on a hidden tab.
//
// The Operator Shell loads it and is the only thing that starts it
// (data/shell.js). It covers the status frame only: an Output row's `reported`
// belongs to data/outputs.js, and what a page has told the dome is that page's
// own record.
// =============================================================================
(() => {
  const FINDING_OUT = "Finding out";
  const UNKNOWN = "Unknown";

  // One cadence for the whole shell when there is no stream. It replaced one
  // poll per surface, at 2, 3 and 5 s, each spending one of the controller's
  // three client slots to ask what the others had just asked (#419).
  const POLL_MS = 5000;

  const hasKey = (payload, key) =>
    payload !== null && typeof payload === "object" && Object.prototype.hasOwnProperty.call(payload, key);

  // The fields a reader tests with no unknown branch of its own. Each is tested
  // for `=== true`, so a frame that does not carry one does not read as
  // "unknown" -- it reads as the OTHER value, and for the four failsafe
  // mirrors that value is "nothing is holding the feet". A Codex review fed
  // the controller's own error shape through the shipped stream and got
  // LATCHED -> CLEAR, DRIVE -> OFF, CONTROL -> OFF, over "just now" (#346).
  //
  // They are read here rather than defended one reader at a time because that
  // is the honest shape of it: a reading is made of ONE frame, so the frame is
  // either one a reading may be made from or it is not. A field-level guard
  // would also be unreachable, and an unreachable guard is a comfort rather
  // than a check.
  //
  // All six come from the first unconditional chunk of buildStatusJson()
  // (src/web/web_server.cpp), so a real frame from any firmware that has ever
  // shipped carries all six or none of them.
  const VERIFIABLE_STATUS_FIELDS = [
    "estop",
    "sbusHwFailsafe",
    "sbusSignalLost",
    "webDriveExpired",
    "webControlEnabled",
    "sleepMode",
  ];

  // A frame the droid built, complete enough to read. The envelope half of the
  // question is the transport's (data/status_stream.js, isStatusFrame): an
  // {"ok":false} payload never reaches a subscriber as a status at all, so it
  // is not re-tested here.
  const isVerifiedStatus = (payload) =>
    payload !== null &&
    typeof payload === "object" &&
    VERIFIABLE_STATUS_FIELDS.every((field) => hasKey(payload, field));

  // What a frame says about the latch. `=== true` and never truthiness: `1`,
  // `"true"` and `null` are not a latch the firmware reports. Only ever asked
  // of a verified frame, so a field that did not arrive never answers it.
  const latchedIn = (status) => status.estop === true;

  // The last verified frame, and when it ARRIVED -- which the transport
  // supplies, so a frame handed out again on a reconnect keeps the age it was
  // measured at rather than claiming a reading nobody took.
  let frame = null;
  let frameAt = 0;

  // Why the droid is not being heard, or null while it is. "link": nothing is
  // arriving -- the stream dropped, the fallback poll was refused, or a resync
  // read failed. "frame": something arrived and was not a reading -- the
  // controller could not build a status, or the frame did not carry the core
  // fields. The two are told apart because the operator's next move differs:
  // one is waiting, the other is a droid that answered.
  let notHearing = null;

  // Set by a "link" failure, and cleared only by a frame the droid has just
  // sent. Kept apart from notHearing because a bad frame after a lost link
  // does not bring the estop back: the droid answered, but not with a reading,
  // and the last reading since contact was lost has no estop in it.
  let contactLost = false;

  const answerFor = (snapshot, field) => {
    if (snapshot === null) return "finding-out";
    return hasKey(snapshot, field) ? "heard" : "unknown";
  };

  const WORDS = { "finding-out": FINDING_OUT, unknown: UNKNOWN };

  const buildReading = () => {
    const snapshot = frame;
    let estop = "finding-out";
    if (snapshot !== null && !contactLost) estop = latchedIn(snapshot) ? "latched" : "clear";
    return Object.freeze({
      // The last verified frame, or null before one has arrived. A surface
      // reads its fields from here and from nowhere else.
      status: snapshot,
      receivedAt: frameAt,
      notHearing,
      estop,
      // The two questions a reader asks of it, answered here so no reader
      // compares the estop itself: is a latch reported (a release is
      // offered), and may anything be asked to move.
      estopLatched: estop === "latched",
      moveActsLive: estop === "clear",
      answer: (field) => answerFor(snapshot, field),
      // The word to show for a field there is no reading of, or null when
      // there is one.
      word: (field) => WORDS[answerFor(snapshot, field)] || null,
    });
  };

  let reading = buildReading();
  const listeners = new Set();

  // A listener that throws is reported and does not stop the rest being told:
  // the estop on the chrome is one of them, and a surface's bug must not be
  // able to leave it showing an old answer.
  const tell = (listener) => {
    try {
      listener(reading);
    } catch (error) {
      console.error("[live-reading] a listener failed:", error);
    }
  };

  const publish = () => {
    reading = buildReading();
    listeners.forEach(tell);
  };

  const loseContact = () => {
    notHearing = "link";
    contactLost = true;
    publish();
  };

  // The session's one status read. It hands its answer to the stream rather
  // than keeping it, so the answer reaches every listener by the one path a
  // pushed frame takes, and an {"ok":false} envelope is refused there exactly
  // as one arriving on the stream would be (data/status_stream.js, seed).
  // Rejects when nothing came back, so its caller can say the link is down.
  const read = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    if (!api) throw new Error("the Body Controller connection (/web_api.js) is not loaded");
    const result = await api.get("/api/status", { cache: "no-store" });
    window.PAStatusStream.seed(result.data);
  };

  const onStreamEvent = (eventType, payload, meta) => {
    if (eventType === "status") {
      // Not a reading. What stands is the last one that WAS, and keeping it
      // beside "the droid could not report its status" is the whole of
      // "values, not exceptions" under a failure (#324).
      if (!isVerifiedStatus(payload)) {
        notHearing = "frame";
        publish();
        return;
      }
      frame = payload;
      frameAt = typeof meta?.receivedAt === "number" ? meta.receivedAt : Date.now();
      // Only a frame the droid has just sent is evidence that we are hearing
      // it. A replay is the session's own cache handed back -- it proves the
      // browser still has a copy, which is not the same claim.
      if (!meta?.cached) {
        notHearing = null;
        contactLost = false;
      }
      publish();
      return;
    }
    // The controller answered and could not build a status, or sent one this
    // browser could not parse. A refresh was attempted and produced no reading.
    if (eventType === "status_error") {
      notHearing = "frame";
      publish();
      return;
    }
    if (eventType === "stream_error") {
      loseContact();
      return;
    }
    if (eventType === "stream_resync") {
      // The stream came back. What it replays is the frame from before it went
      // away, so the only way to learn what happened meanwhile is to have the
      // state re-sent. The controller does that itself on admission
      // (src/web/api_events.cpp); this asks as well, because it is here that a
      // resync is known to be outstanding and the two answers are one frame.
      read().catch((error) => {
        console.warn("[live-reading] status resync after reconnect failed:", error);
        loseContact();
      });
    }
  };

  let started = false;
  let poll = null;

  // The fallback poll runs while the shell is started, the browser has no
  // stream, and somebody is listening. createBackgroundPoll skips a tick while
  // the tab is hidden and asks at once when it comes back (Hidden Tab Pause).
  // It is chrome, not a surface's: navigating never stops it, because what it
  // feeds -- the estop among it -- outlives every surface (#360).
  const syncPoll = () => {
    const wanted = started && !window.PAStatusStream.isSupported() && listeners.size > 0;
    if (wanted && poll === null) {
      poll = window.PageBootstrap.createBackgroundPoll(
        () =>
          read().then(
            () => true,
            (error) => {
              console.warn("[live-reading] status poll failed:", error);
              // On this path there is no stream to drop, so a refused poll is
              // the only way to learn the droid has stopped answering.
              loseContact();
              return false;
            },
          ),
        { cadenceMs: POLL_MS, refreshOnReturn: true },
      );
      poll.start();
    } else if (!wanted && poll !== null) {
      poll.stop();
      poll = null;
    }
  };

  window.PALiveReading = {
    FINDING_OUT,
    UNKNOWN,
    // The Operator Shell's call, and nobody else's. Listening on the stream is
    // what opens it, so a page that never starts this opens nothing.
    start() {
      if (started) return;
      started = true;
      window.PAStatusStream.subscribe(onStreamEvent);
      syncPoll();
    },
    // listener(reading) is told the current reading at once, and again on
    // every change. Returns the call that stops it.
    subscribe(listener) {
      listeners.add(listener);
      tell(listener);
      syncPoll();
      return () => {
        listeners.delete(listener);
        syncPoll();
      };
    },
    current() {
      return reading;
    },
    read,
    // What a frame says about the latch, for a reader deriving something else
    // from one frame's facts -- the Status Plate's DRIVE chip. A move act is
    // never gated on it: that is reading.estop, which knows contact was lost.
    latchedIn,
  };
})();
