// =============================================================================
// data/droid_picture.js
//
// What a Part on the droid picture MEANS, for every surface that draws one and
// lets a builder open or close a Part from it: Parts (data/parts.js), and the
// Dashboard's Moving parts card (data/dome_control.js). Moved here out of
// data/parts.js when the Dashboard card became its second caller (#372), so
// the two surfaces decide from one ladder and send one request, and cannot
// come to disagree about whether a door may open or what refusing it says.
//
// data/body_view.js draws the picture and never writes; this file knows the
// Output rows, the Droid Build and the estop, and is where a pick is turned
// into a mark, a decision and - only when the decision allows it - a request.
// What each page does with a pick (Parts selects and offers named buttons; the
// Dashboard card opens or closes on the click) stays with that page.
//
// Every refusal names the builder's next move (#298), in one sentence, and a
// refused act sends nothing.
// =============================================================================
(() => {
  "use strict";

  // The estop holds every servo move a picture of the droid can start
  // (operator, 2026-09-19, #372): refused while the estop is latched, and while
  // the droid has not said whether it is - before its first frame, and again
  // once contact with it is lost. `estop` is the Live Reading's three-valued
  // answer (data/live_reading.js). The dome's panel press on the Dashboard is
  // held by this same function (data/dome_control.js).
  // Returns the sentence to say instead, or null when a move may go.
  const estopRefusal = (estop) => {
    if (estop === "clear") return null;
    if (estop === "latched") return "Estop latched. Nothing moves until it is cleared.";
    return "Waiting to hear if the droid is stopped. Open waits for the answer.";
  };

  // One caller per drawing. `drawing` is the handle BodyView.mountDrawing()
  // returned. The page's own state that the decision needs - what it has told
  // each dome piece - lives in here, one copy per drawing.
  const caller = (drawing) => {
    const catalog = window.DroidParts;
    const kinds = window.DroidPartKind;
    const P = window.PAParts;
    const OUTPUTS = window.PAOutputs;
    const view = window.BodyView;
    const { partById, hasServoWord, isLightRow, servoWord, listParts } = P;

    // The Output a Part is on, as the droid last answered.
    const outputOf = (partId) => OUTPUTS.forPart(partId);
    // Whether the droid has answered with its Outputs yet.
    const answered = () => OUTPUTS.known().table;

    // What this page has told each dome piece, by the Panel Intent target the
    // vendored map names it with. The dome reports nothing back, so this is the
    // whole of what is known: a piece this page has told draws what it was told,
    // and one it has not told draws no state at all. Closed is a position, and
    // nobody said it (#417).
    const domeTold = new Map();

    // What one Part looks like on the picture, from the Output rows the droid
    // last answered with. Commanded, all of it: `at` is the width the controller
    // has put on the pin as a fraction of the travel the BUILDER recorded, so a
    // reversed Endpoint Pair reads the same way round with no invert flag
    // anywhere (ADR 0041), and an Output nobody has measured has no travel for a
    // fraction to be of, which is its own mark rather than a made-up number.
    //
    // Whether there is a position at all, and the word when there is not, are
    // data/outputs.js's answer, so a limp Part says why in the words Servos
    // uses for the same Output.
    const markFor = (partId) => {
      const part = partById.get(partId);
      if (!answered()) return { mark: view.MARKS.UNKNOWN, said: OUTPUTS.live(null).word };
      const output = outputOf(partId);
      if (!output) return { mark: view.MARKS.UNASSIGNED };
      const live = OUTPUTS.live(output);
      if (live.state === "unknown") return { mark: view.MARKS.UNKNOWN, said: live.word };
      if (live.state === "limp") return { mark: view.MARKS.LIMP, said: live.word };
      // A light has no travel, so it gets no position and no Open: the treatment
      // removes what its Kind cannot promise (data/droid_part_kind.js).
      if (kinds?.isLight(part)) return { mark: view.MARKS.UNKNOWN, said: `lit by ${output.name}` };
      if (!output.calibrated) return { mark: view.MARKS.UNMEASURED };
      const span = output.openUs - output.closeUs;
      return { mark: view.MARKS.OPENABLE, at: span === 0 ? 0 : (output.commandedUs - output.closeUs) / span };
    };

    // A dome piece can stand for several Parts - a panel and the light on it -
    // and it is one shape, so it draws the state of the Part on it that
    // something drives, the panel before the light. A piece with no Output of
    // ours but a Panel Intent target is the dome's to move, and draws what this
    // page last told it, or nothing until it has told it anything.
    const markerMark = (markerId) => {
      const marker = drawing.markerOf(markerId);
      if (marker.panTilt) return {};
      const own = marker.parts.filter((id) => !kinds?.isLight(partById.get(id)));
      const wired = own.concat(marker.parts).find((id) => answered() && outputOf(id) !== null);
      if (wired) return markFor(wired);
      if (marker.target) {
        return domeTold.has(marker.target)
          ? { mark: view.MARKS.OPENABLE, at: domeTold.get(marker.target) ? 1 : 0 }
          : { mark: view.MARKS.UNKNOWN, said: "Not told yet" };
      }
      return markFor(own[0] || marker.parts[0]);
    };

    const marks = () => {
      const all = {};
      drawing.markerIds().forEach((markerId) => {
        all[markerId] = markerMark(markerId);
      });
      return all;
    };

    const fittedNow = () => {
      const build = window.DroidBuild?.current();
      return build ? build.fitted : null;
    };

    const designLabel = (half) => {
      const build = window.DroidBuild?.current();
      const design = build ? (catalog.designs || []).find((row) => row.id === build[half].design) : null;
      if (!design) return "";
      const variant = (design.variants || []).find((row) => row.id === build[half].variant);
      return variant ? `${design.label} · ${variant.label}` : design.label;
    };

    // Which Parts are on this droid's picture: what the stated design seeds,
    // plus whatever the builder has fitted beyond it - a Common Addition is on
    // the picture once it is on the droid, and not before. Before the Droid
    // Build has been read, the renderer's own default holds (everything but the
    // additions), because an empty picture would read as "fitted nothing".
    const pictureFor = () => {
      const build = window.DroidBuild?.current();
      const markerIds = drawing.markerIds();
      if (!build) return { shown: null, domePending: false, domeNote: "" };
      const onDroid = new Set(build.fitted);
      ["body", "dome"].forEach((half) => {
        const complement = window.DroidBuild.complementFor(build[half].design, build[half].variant, half);
        complement.ids.forEach((id) => onDroid.add(id));
      });
      const shown = markerIds.filter((id) => drawing.markerOf(id).parts.some((partId) => onDroid.has(partId)));

      // The dome drawing is the vendored MK4 Complex dome, and it is shown only
      // where it IS the dome the builder says they built - the one rule the
      // Dashboard's dome follows too (DroidBuild.showsBuiltInDome). A drawing of
      // somebody else's dome presented as theirs is worse than none.
      const dome = build.dome;
      const domeComplement = window.DroidBuild.complementFor(dome.design, dome.variant, "dome");
      const domePending = dome.design !== "" && !domeComplement.known;
      const drawingIsTheirs = window.DroidBuild.showsBuiltInDome(dome.design, dome.variant);
      let domeNote = "";
      if (domePending) domeNote = "Dome parts pending. This build does not record which panels that dome carries.";
      else if (!drawingIsTheirs) domeNote = "No drawing of that dome design yet.";
      else if (!markerIds.some((id) => drawing.markerOf(id).half === "dome")) {
        domeNote = "No dome drawing in this firmware.";
      }
      return { shown, domePending, domeNote };
    };

    // The Output a body Part's Open it would go through. POST /api/servo
    // addresses an Output by the name a builder already knows it by, so an
    // expander's unnamed row cannot be reached from here at all - the same bound
    // the calibration dial keeps.
    const servoOutputFor = (partId) => {
      const output = answered() ? outputOf(partId) : null;
      if (!output || !hasServoWord(output) || isLightRow(output) || !output.calibrated) return null;
      return output;
    };

    // Whether a marker may be opened or closed right now, and when it may not,
    // the one sentence that says why. `estop` is the Live Reading's.
    //
    // THE LADDER, in the order a builder has to deal with it: is the Part on
    // the droid at all, does the droid know what drives it, is the droid
    // stopped, and can what drives it actually be sent to.
    const decide = (markerId, estop) => {
      const marker = drawing.markerOf(markerId);
      const parts = marker.parts;
      const fitted = fittedNow();
      const unfitted = fitted === null ? [] : parts.filter((id) => fitted.indexOf(id) === -1);
      const onDroid = fitted === null ? [] : parts.filter((id) => fitted.indexOf(id) !== -1);
      const isFitted = unfitted.length < parts.length;
      const own = parts.filter((id) => !kinds?.isLight(partById.get(id)));
      const wiredPart = own.concat(parts).find((id) => answered() && outputOf(id) !== null) || null;
      const output = wiredPart === null ? null : outputOf(wiredPart);
      const servoOutput = wiredPart === null ? null : servoOutputFor(wiredPart);
      const unwired = parts.filter((id) => answered() && outputOf(id) === null);
      // A Part off the droid with an Output still mapped: the two facts disagree,
      // and neither is wrong, so the panel says both and changes neither.
      const offButMapped = fitted !== null && !isFitted && output !== null;
      const mark = markerMark(markerId);
      const cls = marker.panTilt ? null : view.markClass(mark, isFitted);
      const open = cls === "open";

      // The dome is moved over the dome link, so its Open it waits on nothing of
      // ours but the estop; a body Part's waits on an Output with measured ends.
      const canToggle = marker.panTilt
        ? false
        : output
          ? servoOutput !== null
          : Boolean(marker.target);

      // A holoprojector is offered no act at all, so there is nothing refused
      // to explain.
      let why = "";
      if (marker.panTilt) {
        why = "";
      } else if (offButMapped) {
        why = `Not on your droid, but still mapped to ${output.name}. Add it back, or change its output.`;
      } else if (!isFitted) {
        why = "Not on your droid. Add it to your build first.";
      } else if (!answered() && !marker.target) {
        why = "Waiting for the droid to say what drives it.";
      } else if (estopRefusal(estop)) {
        why = estopRefusal(estop);
      } else if (!output && !marker.target) {
        why = "No output mapped. Give it one first.";
      } else if (output && isLightRow(output)) {
        why = "A light has no travel. Nothing to open.";
      } else if (output && !output.calibrated) {
        why = "Ends not measured yet. Calibrate its output on Servos.";
      } else if (output && !hasServoWord(output)) {
        why = "Its output has no name this page can send to. Drive it from its row on Servos.";
      }

      return {
        marker,
        mark,
        cls,
        open,
        output,
        servoOutput,
        wiredPart,
        isFitted,
        onDroid,
        unfitted,
        unwired,
        offButMapped,
        why,
        toggle: {
          shown: !marker.panTilt,
          label: open ? "Close it" : "Open it",
          enabled: canToggle && isFitted && estopRefusal(estop) === null,
        },
      };
    };

    // Open or close what a decision allows, with the one request each half
    // moves through: a body Part goes to POST /api/servo on its Output, a dome
    // piece to POST /api/dome/cmd as the Panel Intent the vendored map names it
    // by. A refused decision sends nothing and hands back its reason: this is
    // the guard, so a caller may press straight through it. Resolves to the
    // sentence to show and whether it went; it never rejects, so no press ends
    // in silence.
    const openClose = (decision) => {
      const label = decision.marker.label;
      if (!decision.toggle.enabled) {
        return Promise.resolve({ sent: false, text: decision.why || `${label} has nothing to open.`, level: "error" });
      }
      const verb = decision.open ? "close" : "open";
      const failed = (error) => ({
        sent: false,
        text: `${label} did not ${verb}: ${window.PAApi.messageFor(error)}`,
        level: "error",
      });
      if (decision.servoOutput) {
        const gang = decision.servoOutput.parts.filter((id) => decision.marker.parts.indexOf(id) === -1);
        return window.PAApi.postForm(
          "/api/servo",
          { arm: servoWord(decision.servoOutput), action: verb },
          { timeoutMs: 4000 }
        ).then(() => {
          // The table says where the Output went; the page repaints from it.
          OUTPUTS.refresh().catch((error) => console.warn("[droid-picture] reading the outputs failed:", error));
          return {
            sent: true,
            text:
              `${label} told to ${verb}.` +
              (gang.length ? ` ${listParts(gang)} ${gang.length === 1 ? "moves" : "move"} with it.` : ""),
            level: "success",
          };
        }, failed);
      }
      const target = decision.marker.target;
      return window.PAApi.postForm("/api/dome/cmd", { cmd: `${verb === "open" ? ":OP" : ":CL"}${target}` }).then(
        () => {
          domeTold.set(target, verb === "open");
          return { sent: true, text: `${label} told to ${verb}.`, level: "success" };
        },
        failed
      );
    };

    return Object.freeze({ answered, outputOf, markFor, marks, fittedNow, designLabel, pictureFor, decide, openClose });
  };

  window.PADroidPicture = Object.freeze({ estopRefusal, caller });
})();
