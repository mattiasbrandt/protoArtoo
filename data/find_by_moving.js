// =============================================================================
// data/find_by_moving.js
//
// Find by Moving (ADR 0050, #363): a builder who cannot remember which wire the
// rear-left door is on starts a run from that door's row on Wiring, and watches
// the droid. The page steps through the FREE servo Outputs - no Part on them,
// no light on their wire - and asks the droid to nudge each one a little, one
// at a time; the builder presses "That one" when the Part twitches, and that
// is the same move the row's own picker makes (data/parts_mapping.js mover()).
//
// A FREE OUTPUT HAS NO PULSE, AND THE RUN IS WHAT GIVES IT ONE. An Output with
// no Part on it is free and never wired, so nothing drives it (CONTEXT.md
// "Wiring"). A nudge on one is the firmware's cue to take it for the run: it
// puts the Output's recorded centre on the pin - a jump, on a servo nobody has
// driven - and nudges about it (POST /api/servo action=nudge; operator,
// 2026-09-30 on #411: "Pulse free Outputs in a run"). The firmware bounds that
// hold the way it bounds the calibration dial's (ADR 0064): it lets the Output
// go a few seconds after nudges for it stop arriving, and within ten minutes in
// any case, and this page can extend neither. So a run needs no keepalive, and
// a browser that dies mid-run leaves nothing driven for long. The page still
// lets go of the Output under the nudge the moment the run ends by its own hand
// - Stop, That one, leaving Wiring - because a builder who stopped expects
// stillness now, not in three seconds.
//
// One Output at a time, and the droid says when. The next nudge goes only
// when the previous one has ended, and the page knows that from the answer's
// nudgesDone count going up - never from watching the Output move, because a
// whole nudge can fall between two of the run's once-a-second reads. The run
// owns those reads: they start with it and stop with it (data/outputs.js
// follow()), because Wiring has no live reading of its own.
//
// The estop ends a run. The firmware refuses and lets go of every Output on its
// own; this side stops asking. While the estop is latched, or contact with the
// droid is lost, every act that starts a run is refused - disabled plus
// aria-disabled - and the shell's own notice names why. No Non-RC Control
// consent is asked: that flag has never reached POST /api/servo (ADR 0064).
// =============================================================================
(() => {
  "use strict";

  /**
   * One run's worth of Find by Moving, mounted by the surface that lists the
   * Parts it can find.
   *
   * @param {object} hosts
   * @param {Element} hosts.panel - where the run's line, That one and Stop go
   * @param {function} hosts.say - (text, level) the surface's feedback line
   * @param {function} hosts.move - (partId, address) put the Part on the Output
   *   through the surface's own mover, question and all
   * @param {function} hosts.pending - the Part a move is on its way for, or null
   * @param {function} [hosts.changed] - the run started or ended, or the acts
   *   were gated: the surface redraws its find acts
   * @param {string} hosts.surface - the surface's name, as a builder reads it
   * @returns {{start: function, running: function, live: function}}
   */
  const runner = ({ panel, say, move, pending, changed = () => {}, surface }) => {
    const OUTPUTS = window.PAOutputs;
    const P = window.PAParts;
    const { NOT_WIRED, partLabel, servoWord, hasServoWord } = P;

    let run = null;
    // Whether anything may be asked to move: the Live Reading's answer
    // (data/live_reading.js), false until the droid has said its estop is
    // clear and whenever contact with it is lost.
    let moveActsLive = false;
    // The run's reads, one a second, only while a run is going.
    const feed = OUTPUTS.follow();

    const line = document.createElement("span");
    line.className = "parts-find-run";
    line.innerHTML =
      `<span class="parts-find-text" role="status" aria-live="polite"></span>` +
      `<button class="btn btn-sm accent parts-find-that" type="button">That one</button>` +
      `<button class="btn btn-sm parts-find-stop" type="button">Stop</button>`;
    const text = line.querySelector(".parts-find-text");

    // The Outputs a run steps through: nothing on them, no light on the wire,
    // and a name the servo route takes. No pulse is needed: the nudge is what
    // gives a free Output one.
    const freeOutputs = () =>
      OUTPUTS.list().filter((output) => output.parts.length === 0 && !output.light && hasServoWord(output));

    // Let go of the Output under the nudge, now rather than at the firmware's
    // bound. The bound still ends it if this does not arrive, so a failure is
    // said in the log and nothing more is owed.
    const letGo = (address) => {
      const output = address === null ? null : OUTPUTS.at(address);
      if (!output || output.parts.length > 0) return;
      window.PAApi.postForm("/api/servo", { arm: servoWord(output), action: "release" }, { timeoutMs: 4000 })
        .catch((error) => console.warn(`[find] letting go of ${output.name} failed; its bound lets go:`, error));
    };

    const end = (said, level, { release = true } = {}) => {
      if (run === null) return;
      const { address } = run;
      run = null;
      feed.stop();
      line.remove();
      if (release) letGo(address);
      if (said) say(said, level);
      changed();
    };

    // Ask the droid to nudge the next free Output, or end the run when there
    // is none left. `before` is the count from the droid's latest answer, and
    // a later answer with a higher one is the only thing that moves the run on.
    const nudgeNext = async () => {
      const current = run;
      current.at += 1;
      const label = partLabel(current.partId);
      const count = current.candidates.length;
      if (current.at >= count) {
        end(
          `None of the ${count} free ${count === 1 ? "output" : "outputs"} moved ${label} in one pass, so it stays ${NOT_WIRED}. ` +
            `Check the wire, or run it again.`,
          "warning"
        );
        return;
      }
      const address = current.candidates[current.at];
      const output = OUTPUTS.at(address);
      if (!output || output.nudgesDone === null) {
        // The droid's answer changed shape under the run: a reboot, or a
        // different firmware. Nothing is asked of an Output the page cannot
        // tell has finished.
        end(`${address} is not in the droid's answer any more, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning");
        return;
      }
      current.address = address;
      current.before = output.nudgesDone;
      current.pulsed = false;
      current.sending = true;
      text.textContent =
        `Nudging ${output.name} (${current.at + 1} of ${count}). Watch the droid, and press That one when ${label} moves.`;
      try {
        await window.PAApi.postForm("/api/servo", { arm: servoWord(output), action: "nudge" }, { timeoutMs: 4000 });
      } catch (error) {
        if (run === current) {
          end(`The nudge did not reach the droid: ${window.PAApi.messageFor(error)}. ${label} stays ${NOT_WIRED}.`, "error");
        }
        return;
      }
      if (run === current) current.sending = false;
    };

    // Called with every answer from the droid. Steps on when the Output the
    // run asked about says its nudge has ended.
    const step = () => {
      if (run === null || run.sending || run.address === null) return;
      const label = partLabel(run.partId);
      const wiredTo = OUTPUTS.forPart(run.partId);
      if (wiredTo) {
        end(`${label} is on ${wiredTo.name} now, so the run stopped.`);
        return;
      }
      const output = OUTPUTS.at(run.address);
      if (!output || output.nudgesDone === null) {
        end(`${run.address} is not in the droid's answer any more, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning");
        return;
      }
      // Ended - returned, cut short or refused. The run's hold on a free
      // Output outlasts its nudge by the firmware's expiry, so an Output read
      // limp AFTER its count went up is the run letting go, not a fault.
      if (output.nudgesDone !== run.before) {
        nudgeNext();
        return;
      }
      // Still going. A free Output reads limp until the firmware has taken it;
      // one that goes limp after it was pulsing was let go under the nudge -
      // pulses off, the dial's bounds, anything that takes a pulse off a pin
      // (#364) - and cannot twitch, so the run ENDS rather than waiting.
      const state = OUTPUTS.live(output).state;
      if (state === "pulsing") {
        run.pulsed = true;
      } else if (state === "limp" && run.pulsed) {
        end(`${output.name} is limp, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning", { release: false });
      }
    };

    const start = (partId) => {
      if (!OUTPUTS.known().table || !partId) return;
      if (run !== null) {
        say(`One run at a time: ${partLabel(run.partId)} is being found. Stop that run first.`, "warning");
        return;
      }
      if (pending() !== null) {
        say(`One move at a time: wait for ${partLabel(pending())} to land.`, "warning");
        return;
      }
      if (!moveActsLive) return;
      const candidates = freeOutputs();
      if (!candidates.length) {
        say("Nothing to nudge. There is no free servo output: every one has a part or carries a light.", "warning");
        return;
      }
      if (candidates.some((output) => output.nudgesDone === null)) {
        say(
          "This firmware does not say when a nudge has ended, so Find by moving cannot step through the outputs. Update the firmware.",
          "warning"
        );
        return;
      }
      run = {
        partId,
        candidates: candidates.map((output) => output.address),
        at: -1,
        address: null,
        before: null,
        pulsed: false,
        sending: false,
      };
      panel.appendChild(line);
      feed.start();
      changed();
      nudgeNext();
    };

    // "That one": the same request the Part's row makes. The run lets go of
    // the Output first: once the Part is on it, it is the Part's, and waits
    // for the droid's next start like any Output a Part is put on.
    line.querySelector(".parts-find-that").addEventListener("click", () => {
      if (run === null) return;
      const { partId, address } = run;
      end();
      move(partId, address);
    });

    line.querySelector(".parts-find-stop").addEventListener("click", () => {
      if (run === null) return;
      end(`Stopped. ${partLabel(run.partId)} stays ${NOT_WIRED}.`);
    });

    // Every read of the Outputs - the run's own, a move's, a save's - is the
    // only thing a run steps on.
    OUTPUTS.onChange(() => step());

    // Leaving the surface ends a run: its reads stop with the surface (#360),
    // so nothing could step it on, and a nudge sent on the way back would be
    // motion the builder did not press for. Never a hold - leaving is always
    // allowed; this only hears it happening.
    window.PASurface?.holdUnmount(() => {
      if (run !== null) end(`The run stopped when you left ${surface}. ${partLabel(run.partId)} stays ${NOT_WIRED}.`);
      return false;
    });

    // The estop, from the Live Reading. The firmware has already let go of
    // every Output a run drove (ADR 0043), so nothing is sent.
    window.PALiveReading.subscribe((reading) => {
      const was = moveActsLive;
      moveActsLive = reading.moveActsLive;
      if (reading.estopLatched && run !== null) {
        end(`The estop stopped the run. ${partLabel(run.partId)} stays ${NOT_WIRED}.`, "error", { release: false });
        return;
      }
      if (was !== moveActsLive) changed();
    });

    return Object.freeze({
      start,
      running: () => run,
      live: () => moveActsLive,
    });
  };

  window.PAFindByMoving = Object.freeze({ runner });
})();
