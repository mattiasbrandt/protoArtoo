// =============================================================================
// data/parts.js
//
// Parts (ADR 0050, #347, #362): every Part on the droid, grouped the way a
// builder thinks about them, and the Output that drives each one - the
// part-first projection of the one mapping GET /api/servo/outputs answers -
// with the droid picture at its head. The output-first projection, *centre
// all*, Find by Moving and the calibration dial live on Servos (CONTEXT.md
// "Parts", "Servos"; operator, 2026-09-19 on #412), which reads the same answer
// and moves a Part through the same request (data/parts_mapping.js), so the two
// ends cannot disagree.
//
// Three rules shape this file.
//
// No row is ever hidden. A fresh droid shows every catalog Part reading
// "- not wired -", which is the honest state of a build in progress, and hiding
// a row is how an operator loses an output (#296). The groups are disjoint and
// together cover the catalog: a Part no group names lands in a last group
// rather than off the page.
//
// The table is built once and repainted in place. A repaint writes
// textContent, value, disabled and classList on nodes that already exist, and
// leaves the control the builder is holding alone until they let go of it
// (r2d2-astromech-simulator v1.79.0, src/js/maestro/hw-table.js:171-173).
//
// A move is announced before it happens (#347): data/parts_mapping.js holds the
// question and the one request both surfaces move a Part through.
// =============================================================================
(() => {
  const catalog = window.DroidParts;
  const kinds = window.DroidPartKind;
  const P = window.PAParts;

  const {
    NOT_WIRED,
    NO_OUTPUT,
    groupParts,
    partById,
    partLabel,
    listParts,
    outputLabel,
    servoWord,
    hasServoWord,
    optionText,
    outputOf,
    isLightRow,
  } = P;

  // The bench feed (#318). One read of the outputs answer a second repaints the
  // table and the picture - any Part another client or the Console moved, and
  // what every part was last told - and only while Parts is on screen: the
  // shell stops it when the operator leaves (#360).
  const POLL_MS = 1000;

  const groupHeading = (group) => {
    const [one, many] = group.unit || ["part", "parts"];
    const count = group.parts.length;
    return `${group.label} — ${count} ${count === 1 ? one : many}`;
  };

  const tableRegion = document.getElementById("parts-table");
  const summary = document.getElementById("parts-summary");
  const feedback = document.getElementById("parts-feedback");
  const dialog = document.getElementById("parts-move-dialog");
  if (!tableRegion || !summary || !dialog) return;

  if (!partById.size) {
    // A table with no rows reads as a droid with no parts. Say what broke.
    summary.textContent = "The parts list did not load, so there is nothing to show. Reload the page to try again.";
    console.error("[parts] window.DroidParts is missing; /droid_parts.js did not load");
    return;
  }

  const esc = (value) => window.PAUtils.escapeHtml(String(value));
  const showFeedback = (text, level) => window.PAUtils.showFeedback(feedback, text, level);

  // ---------------------------------------------------------------------------
  // Built once
  // ---------------------------------------------------------------------------
  const rowHtml = (part) => {
    const kind = kinds ? kinds.treatmentClass(part) : "";
    const shorthand = part.shorthand ? `<span class="parts-shorthand">${esc(part.shorthand)}</span>` : "";
    const light = kinds?.isLight(part) ? `<span class="parts-kind">light</span>` : "";
    return (
      `<tr class="parts-row${kind ? ` ${kind}` : ""}" data-part="${esc(part.id)}">` +
      `<th scope="row"><span class="parts-name">${esc(part.name)}</span>${shorthand}${light}` +
      `<span class="parts-gang"></span></th>` +
      `<td><select class="parts-output" aria-label="${esc(`Output that drives ${part.name}`)}" disabled>` +
      `<option value="${NO_OUTPUT}">Finding out...</option></select></td></tr>`
    );
  };

  tableRegion.innerHTML =
    `<table class="parts-table"><thead><tr><th scope="col">Part</th><th scope="col">Driven by</th></tr></thead>` +
    groupParts(catalog.parts)
      .map(
        (group) =>
          `<tbody data-group="${group.id}"><tr class="parts-group"><th colspan="2" scope="colgroup">` +
          `${esc(groupHeading(group))}</th></tr>${group.parts.map(rowHtml).join("")}</tbody>`
      )
      .join("") +
    `</table>`;

  const rows = new Map();
  tableRegion.querySelectorAll("[data-part]").forEach((node) => {
    rows.set(node.dataset.part, {
      node,
      select: node.querySelector("select"),
      gang: node.querySelector(".parts-gang"),
      addresses: null,
    });
  });

  // ---------------------------------------------------------------------------
  // Repainted in place
  // ---------------------------------------------------------------------------
  let outputs = null; // null until the droid has answered
  let estopLatched = null; // null until the droid has said

  const mover = P.mover({
    dialog,
    say: showFeedback,
    reload: () => loadOutputs(),
    repaint: () => paint(),
    onSending: (partId) => {
      const row = rows.get(partId);
      if (row) row.select.disabled = true;
    },
  });

  // The control the builder has hold of: the one focused, or the one whose move
  // is being asked about or is on its way. Its value and its options are theirs
  // until they let go.
  const held = (id, select) => id === mover.pending() || document.activeElement === select;

  const paintRow = (id, row, addresses) => {
    const output = outputOf(outputs, id);
    row.node.classList.toggle("is-wired", output !== null);
    const gang = output ? output.parts.filter((other) => other !== id) : [];
    row.gang.textContent = gang.length ? ` moves with ${listParts(gang)}` : "";
    if (held(id, row.select)) return;
    // The only rebuild, and only of a control nobody is holding: the set of
    // Outputs is fixed from boot, so this runs once per page in practice.
    if (row.addresses !== addresses) {
      row.select.innerHTML =
        `<option value="${NO_OUTPUT}">${NOT_WIRED}</option>` +
        outputs.map((each) => `<option value="${esc(each.address)}"></option>`).join("");
      row.addresses = addresses;
    }
    const options = row.select.querySelectorAll("option");
    outputs.forEach((each, index) => {
      const text = optionText(each);
      if (options[index + 1].textContent !== text) options[index + 1].textContent = text;
    });
    row.select.value = output ? output.address : NO_OUTPUT;
    row.select.disabled = false;
  };

  const paint = () => {
    if (outputs === null) return;
    const addresses = outputs.map((output) => output.address).join(",");
    rows.forEach((row, id) => paintRow(id, row, addresses));

    const wired = catalog.parts.filter((part) => outputOf(outputs, part.id) !== null).length;
    const idle = outputs.filter((output) => output.parts.length === 0).length;
    let text = `${wired} of ${catalog.parts.length} parts on an output · ${idle} of ${outputs.length} outputs driving nothing`;
    // A Part the droid drives and this page has no row for would otherwise be
    // invisible, which is the one thing this table must never be.
    const unknown = outputs.flatMap((output) => output.parts).filter((id) => !partById.has(id));
    if (unknown.length) {
      text += ` · also drives ${unknown.join(", ")}, unknown to this page - upload the matching web UI`;
    }
    summary.textContent = text;
    // And the picture at the head of the surface, from the same one answer the
    // table was just painted from: a body view that read the droid on its own
    // clock could show a part open while the row below it said closed.
    paintBody();
  };

  const loadOutputs = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    const result = await api.get("/api/servo/outputs");
    const answer = result?.data?.outputs;
    if (!Array.isArray(answer)) throw new Error("the droid's outputs answer carries no table");
    outputs = P.readOutputs(answer);
    paint();
  };

  const refresh = () =>
    loadOutputs().catch((error) => {
      console.warn("[parts] reading the outputs failed:", error);
    });

  // Every act here is started from a click handler and finishes later, so
  // nothing awaits it; a rejection nobody handles is a control that did nothing
  // and said nothing, so this refuses to be silent about it.
  const started = (promise) =>
    promise?.catch?.((error) => {
      console.error("[parts] an act failed:", error);
      showFeedback(`Something went wrong on this page: ${error && error.message ? error.message : error}`, "error");
    });

  tableRegion.addEventListener("change", (event) => {
    const select = event.target;
    const id = select?.closest?.("[data-part]")?.dataset.part;
    if (!id || outputs === null) return;
    mover.request(P.moveFor(outputs, id, select.value), select);
  });

  // A control that was held catches up with whatever arrived while it was.
  tableRegion.addEventListener("focusout", () => paint());

  // The estop gates the picture's Open it; the answer rides the status stream.
  window.PAStatusStream?.subscribe((eventType, payload) => {
    if (eventType !== "status" || !payload || typeof payload !== "object") return;
    estopLatched = payload.estop === true;
  });

  // ---------------------------------------------------------------------------
  // The droid picture (#352, #372, ADR 0063 as amended 2026-09-19)
  //
  // A picture of the droid at the head of this surface, showing many parts at
  // once - the one thing the two tables below cannot do, however honest each
  // row is. One card, three faces: Front, Rear and Dome (Top).
  // data/body_view.js draws it and this file is its caller, and the seam
  // between the two is the whole design: the renderer reports "this marker was
  // picked" and knows nothing else, while everything about what a pick MEANS
  // lives here, where the Output rows are.
  //
  // THE VIEW NEVER WRITES. Every request below is made from this file, by an
  // act the builder pressed by name in the panel, or by an Add in the Parts
  // list beside the picture. A click on the picture selects and does nothing
  // else, which is what makes one gesture safe on a surface where most parts
  // mid-build are unfitted, unassigned or both.
  //
  // THE ACTS, and each one either runs or says what to do instead - #298's rule
  // that every no names the builder's next move, kept by a refused button with
  // a reason beside it rather than by a button that disappears:
  //
  //   Open it / Close it  one press, one command. A body Part's goes to
  //                       POST /api/servo action=open or action=close on the
  //                       Output it is on, which drives the Output to the end
  //                       the builder RECORDED for that side; so it waits for
  //                       measured ends, like every move this page makes. A
  //                       dome piece's goes to POST /api/dome/cmd as the
  //                       Panel Intent the vendored map names it by. A
  //                       holoprojector is never offered it: it pans and tilts
  //                       and never opens.
  //   Drop from build /   one button, worded by where the Part is: Drop on a
  //   Add to build        Part the droid carries, Add on one it does not. The
  //                       Part leaves or joins the Fitted Parts (ADR 0047)
  //                       through applyDroidBuild() and nothing else. Dropping
  //                       leaves the Part's Output alone: the builder took the
  //                       Part off, and whether the wire came off too is theirs
  //                       to say, so the panel says the Output is still mapped
  //                       and routes to its picker rather than unmapping it.
  //                       A Common Addition comes off as the group the Parts
  //                       list fits it as: an arm takes its claw or tool.
  //   Give it an output   routes to this Part's row in the part-first table
  //                       and puts the cursor in its picker. Deliberately NOT a
  //                       third picker of its own: the mapping has two
  //                       projections of one table on this page and a third
  //                       would be a surface that can disagree with them
  //                       (CONTEXT.md "Parts"). Offered where nothing is mapped
  //                       yet, and as Change its output on a Part off the droid
  //                       that still has an Output mapped.
  //
  // No Non-RC Control consent is asked for any of them - that flag has never
  // reached POST /api/servo (ADR 0064).
  // ---------------------------------------------------------------------------
  const view = window.BodyView;
  const drawingHost = document.getElementById("bodyview-drawing");
  const railHost = document.getElementById("bodyview-panel");

  const ACTS = [
    { id: "toggle", label: "Open it" },
    { id: "fit", label: "Drop from build" },
    { id: "wire", label: "Give it an output" },
  ];

  let drawing = null;
  let panel = null;

  // What this page has told each dome piece, by the Panel Intent target the
  // vendored map names it with. The dome reports nothing back, so this is the
  // whole of what is known: a piece this page has not opened draws Closed, the
  // way the dome picker on the Dashboard has always drawn it.
  const domeTold = new Map();

  // What one Part looks like on the picture, from the Output rows the droid
  // last answered with. Commanded, all of it: `at` is the width the controller
  // has put on the pin as a fraction of the travel the BUILDER recorded, so a
  // reversed Endpoint Pair reads the same way round with no invert flag
  // anywhere (ADR 0041), and an Output nobody has measured has no travel for a
  // fraction to be of, which is its own mark rather than a made-up number.
  const markFor = (partId) => {
    const part = partById.get(partId);
    const output = outputs === null ? null : outputOf(outputs, partId);
    if (outputs === null) return { mark: view.MARKS.UNKNOWN, said: "finding out" };
    if (!output) return { mark: view.MARKS.UNASSIGNED };
    if (!output.reported) return { mark: view.MARKS.UNKNOWN, said: "this firmware does not say where it is" };
    if (output.commandedUs === null) return { mark: view.MARKS.LIMP };
    // A light has no travel, so it gets no position and no Open: the treatment
    // removes what its Kind cannot promise (data/droid_part_kind.js).
    if (kinds?.isLight(part)) return { mark: view.MARKS.UNKNOWN, said: `lit by ${outputLabel(output)}` };
    if (!output.calibrated) return { mark: view.MARKS.UNMEASURED };
    const span = output.openUs - output.closeUs;
    return { mark: view.MARKS.OPENABLE, at: span === 0 ? 0 : (output.commandedUs - output.closeUs) / span };
  };

  // A dome piece can stand for several Parts - a panel and the light on it -
  // and it is one shape, so it draws the state of the Part on it that
  // something drives, the panel before the light. A piece with no Output of
  // ours but a Panel Intent target is the dome's to move, and draws what this
  // page last told it.
  const markerMark = (markerId) => {
    const marker = drawing.markerOf(markerId);
    if (marker.panTilt) return {};
    const own = marker.parts.filter((id) => !kinds?.isLight(partById.get(id)));
    const wired = own.concat(marker.parts).find((id) => outputs !== null && outputOf(outputs, id) !== null);
    if (wired) return markFor(wired);
    if (marker.target) return { mark: view.MARKS.OPENABLE, at: domeTold.get(marker.target) ? 1 : 0 };
    return markFor(own[0] || marker.parts[0]);
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
    // where it IS the dome the builder says they built - the same rule the
    // Dashboard's dome follows (data/dome_layout.js, tier 3). A drawing of
    // somebody else's dome presented as theirs is worse than none.
    const dome = build.dome;
    const domeComplement = window.DroidBuild.complementFor(dome.design, dome.variant, "dome");
    const domePending = dome.design !== "" && !domeComplement.known;
    const drawingIsTheirs =
      dome.design === "" ||
      (dome.design === window.DOME_PANEL_MAP_DESIGN && dome.variant === window.DOME_PANEL_MAP_VARIANT);
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
    const output = outputs === null ? null : outputOf(outputs, partId);
    if (!output || !hasServoWord(output) || isLightRow(output) || !output.calibrated) return null;
    return output;
  };

  // Everything the panel says about a pick, and the one sentence that says why
  // an act cannot run. Every refusal names the next move.
  const describePick = (markerId) => {
    const marker = drawing.markerOf(markerId);
    const parts = marker.parts;
    const fitted = fittedNow();
    const unfitted = fitted === null ? [] : parts.filter((id) => fitted.indexOf(id) === -1);
    const onDroid = fitted === null ? [] : parts.filter((id) => fitted.indexOf(id) !== -1);
    const isFitted = unfitted.length < parts.length;
    const own = parts.filter((id) => !kinds?.isLight(partById.get(id)));
    const wiredPart = own.concat(parts).find((id) => outputs !== null && outputOf(outputs, id) !== null) || null;
    const output = wiredPart === null ? null : outputOf(outputs, wiredPart);
    const servoOutput = wiredPart === null ? null : servoOutputFor(wiredPart);
    const unwired = parts.filter((id) => outputs !== null && outputOf(outputs, id) === null);
    // A Part off the droid with an Output still mapped: the two facts disagree,
    // and neither is wrong, so the panel says both and changes neither.
    const offButMapped = fitted !== null && !isFitted && output !== null;
    const mark = markerMark(markerId);
    const cls = marker.panTilt ? null : view.markClass(mark, isFitted);
    const open = cls === "open";

    let servo;
    if (output) servo = `On a servo (${outputLabel(output)})`;
    else if (marker.target) servo = "On a servo (dome-link)";
    else if (outputs === null) servo = "Finding out";
    else servo = "No output mapped";

    const facts = [{ term: "Servo", value: servo }];
    if (cls !== null) {
      facts.push({ term: "State", value: cls === "unknown" ? mark.said || "Finding out" : view.LEGEND_TEXT[cls] });
    }
    if (fitted !== null) facts.push({ term: "Fitted", value: isFitted ? "Yes" : "No" });

    const part = partById.get(parts[0]);
    const subtitle =
      marker.half === "dome"
        ? parts.map((id) => partById.get(id)?.shorthand || partLabel(id)).join(" · ")
        : [part?.position, part?.cadName === null ? "Common Addition" : ""].filter(Boolean).join(" · ");

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
      why = `Not on your droid, but still mapped to ${outputLabel(output)}. Add it back, or change its output.`;
    } else if (!isFitted) {
      why = "Not on your droid. Add it to your build first.";
    } else if (outputs === null && !marker.target) {
      why = "Finding out what drives it.";
    } else if (estopLatched === null) {
      why = "Finding out if the droid is stopped. Open it waits for the answer.";
    } else if (estopLatched) {
      why = "Estop latched. Nothing moves until it is cleared.";
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
      title: marker.label,
      subtitle,
      facts,
      acts: {
        toggle: {
          shown: !marker.panTilt,
          label: open ? "Close it" : "Open it",
          enabled: canToggle && isFitted && estopLatched === false,
        },
        fit: {
          shown: fitted !== null,
          label: isFitted ? "Drop from build" : "Add to build",
          enabled: fitted !== null,
        },
        wire: {
          shown: offButMapped || (!marker.panTilt && !marker.target && unwired.length > 0),
          label: offButMapped ? "Change its output" : "Give it an output",
          enabled: parts.some((id) => rows.has(id)),
        },
      },
      why,
      marker,
      open,
      output: servoOutput,
      isFitted,
      onDroid,
      unfitted,
      unwired,
      offButMapped,
      wiredPart,
    };
  };

  const paintPanel = () => {
    const markerId = drawing.selected();
    if (markerId === null) panel.clear();
    else panel.show(describePick(markerId));
  };

  const paintBody = () => {
    if (drawing === null) return;
    const marks = {};
    drawing.markerIds().forEach((markerId) => {
      marks[markerId] = markerMark(markerId);
    });
    const picture = pictureFor();
    // One kind of state at a time. This surface shows the live kind - what the
    // droid was last told. A routine's moment is the same shape through the
    // same renderer and is never mixed into this one.
    drawing.update({
      kind: view.STATE_KINDS.LIVE,
      marks,
      shown: picture.shown,
      fitted: fittedNow(),
      stamp: designLabel("body"),
      domePending: picture.domePending,
      domeNote: picture.domeNote,
    });
    paintPanel();
  };

  // The one way this surface changes the Fitted Parts: the Droid Build seam,
  // for Add and Drop alike. applyDroidBuild() publishes before it persists, so
  // a write the droid did not confirm is already on the picture by the time it
  // answers. Ask the droid what it holds instead - the seam's own re-read - and
  // only if it cannot say either, put the picture back to the build it last
  // confirmed. The caller then reads fittedNow() for what actually happened,
  // which also covers a write that timed out after the droid took it.
  const writeFitted = (nextFitted) => {
    const before = fittedNow();
    return window.DroidBuild.applyDroidBuild({ fitted: nextFitted }).then((result) => {
      if (result.persisted) return;
      return window.DroidBuild.load({ refresh: true }).then((held) => {
        if (held === null) window.DroidBuild.applyDroidBuild({ fitted: before }, { persist: false });
      });
    });
  };

  const addToBuild = (ids, names) => {
    const fitted = fittedNow();
    if (fitted === null) return;
    const missing = ids.filter((id) => fitted.indexOf(id) === -1);
    if (missing.length === 0) return;
    writeFitted(fitted.concat(missing))
      .then(() => {
        const now = fittedNow() || [];
        const took = missing.every((id) => now.indexOf(id) !== -1);
        showFeedback(
          took
            ? `${names} ${missing.length === 1 ? "is" : "are"} on your droid now.`
            : `${names} did not reach the droid, so nothing was added.`,
          took ? "success" : "error"
        );
        paintBody();
      })
      .catch((error) => {
        showFeedback(`${names} was not added: ${window.PAApi.messageFor(error)}`, "error");
      });
  };

  // Drop takes the Part off the droid and leaves its Output exactly as it was:
  // the wire may still be plugged in, and only the builder knows. So a dropped
  // Part that still has an Output mapped is said, with where to change it, and
  // never unmapped here.
  const dropFromBuild = (ids, names) => {
    const fitted = fittedNow();
    if (fitted === null) return;
    const leaving = ids.filter((id) => fitted.indexOf(id) !== -1);
    if (leaving.length === 0) return;
    writeFitted(fitted.filter((id) => leaving.indexOf(id) === -1))
      .then(() => {
        const now = fittedNow() || [];
        if (!leaving.every((id) => now.indexOf(id) === -1)) {
          showFeedback(`${names} did not reach the droid, so nothing was dropped.`, "error");
          paintBody();
          return;
        }
        const mapped = [];
        leaving.forEach((id) => {
          const output = outputs === null ? null : outputOf(outputs, id);
          if (output && mapped.indexOf(outputLabel(output)) === -1) mapped.push(outputLabel(output));
        });
        const off = `${names} ${leaving.length === 1 ? "is" : "are"} off your droid now.`;
        showFeedback(
          mapped.length
            ? `${off} Still mapped to ${mapped.join(", ")}. Change its output below if the wire came off too.`
            : off,
          mapped.length ? "warning" : "success"
        );
        paintBody();
      })
      .catch((error) => {
        showFeedback(`${names} was not dropped: ${window.PAApi.messageFor(error)}`, "error");
      });
  };

  const runAct = (actId) => {
    const markerId = drawing.selected();
    if (markerId === null) return;
    const pick = describePick(markerId);
    if (actId === "fit") {
      if (pick.isFitted) {
        // A Common Addition comes off the way the Parts list fits it, as one
        // group: an arm without its claw is not a thing a builder has on the
        // bench (operator, 2026-09-19 on #373).
        const group = view.ADDITION_GROUPS.find((each) => each.ids.some((id) => pick.onDroid.indexOf(id) !== -1));
        const fitted = fittedNow() || [];
        const leaving = group ? group.ids.filter((id) => fitted.indexOf(id) !== -1) : pick.onDroid;
        const whole = group && leaving.length === group.ids.length;
        dropFromBuild(leaving, whole ? group.label : leaving.map(partLabel).join(", "));
      } else {
        addToBuild(pick.unfitted, pick.unfitted.map(partLabel).join(", "));
      }
      return;
    }
    if (actId === "wire") {
      // A route, not a write: the picker on this Part's own row is where an
      // Output is chosen or taken off, and it is the same control either
      // table uses.
      const target = pick.offButMapped ? pick.wiredPart : pick.unwired[0] || pick.marker.parts[0];
      const row = rows.get(target);
      if (!row) return;
      row.node.scrollIntoView?.({ block: "center" });
      row.select.focus();
      showFeedback(
        pick.offButMapped
          ? `Pick ${NOT_WIRED} in ${partLabel(target)}'s row below if the wire came off too.`
          : `Choose the output that moves ${partLabel(target)} in its row below.`
      );
      return;
    }
    if (actId !== "toggle" || !pick.acts.toggle.enabled) return;
    const verb = pick.open ? "close" : "open";
    const label = pick.marker.label;
    if (pick.output) {
      const gang = pick.output.parts.filter((id) => pick.marker.parts.indexOf(id) === -1);
      started(
        window.PAApi.postForm(
          "/api/servo",
          { arm: servoWord(pick.output), action: verb },
          { timeoutMs: 4000 }
        ).then(
          () => {
            showFeedback(
              `${label} told to ${verb}.` +
                (gang.length ? ` ${listParts(gang)} ${gang.length === 1 ? "moves" : "move"} with it.` : ""),
              "success"
            );
            refresh();
          },
          (error) => {
            showFeedback(`${label} did not ${verb}: ${window.PAApi.messageFor(error)}`, "error");
          }
        )
      );
      return;
    }
    const target = pick.marker.target;
    started(
      window.PAApi.postForm("/api/dome/cmd", { cmd: `${verb === "open" ? ":OP" : ":CL"}${target}` }).then(
        () => {
          domeTold.set(target, verb === "open");
          showFeedback(`${label} told to ${verb}.`, "success");
          paintBody();
        },
        (error) => {
          showFeedback(`${label} did not ${verb}: ${window.PAApi.messageFor(error)}`, "error");
        }
      )
    );
  };

  if (view && drawingHost && railHost) {
    panel = view.mountPanel(railHost, { acts: ACTS, onAct: runAct });
    drawing = view.mountDrawing(drawingHost, {
      parts: catalog.parts,
      art: window.BodyArt,
      domeSvg: window.DOME_PANEL_MAP_SVG,
      railHost,
      // Picking the Part already picked lets it go, so there is a way back to
      // the empty panel.
      onPick: (markerId) => {
        drawing.select(drawing.selected() === markerId ? null : markerId);
        paintPanel();
      },
      onFace: () => paintPanel(),
      onAdd: (group) => addToBuild(group.ids, group.label),
    });
    // The Fitted Parts, so the picture knows what is on this droid. One read,
    // shared with every other surface that wants the Droid Build
    // (data/droid_build.js holds it single-flight), and the picture draws from
    // whatever the droid has answered so far rather than waiting on it.
    window.DroidBuild?.load()?.then(() => paintBody());
    window.DroidBuild?.onChange?.(() => paintBody());
    paintBody();
    // The estop gates every act on this page, and it arrives on the status
    // stream rather than on the bench feed. Subscribed here, below the
    // definitions it calls, because the stream replays its last frame to a new
    // subscriber synchronously.
    window.PAStatusStream?.subscribe((eventType) => {
      if (eventType !== "status") return;
      paintBody();
    });
  } else if (!view) {
    console.error("[parts] window.BodyView is missing; /body_view.js did not load");
  }

  // ---------------------------------------------------------------------------
  // Loading
  // ---------------------------------------------------------------------------
  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/droid_parts.js": "parts list",
      "/droid_part_kind.js": "parts list",
      "/parts.js": "parts table",
    });
    window.PABootstrap.registerSection("parts-outputs", loadOutputs, { label: "what drives each part and output" });
  } else {
    loadOutputs().catch((error) => console.warn("[parts] outputs unavailable:", error));
  }

  // Owned by this surface, so the shell stops it when the operator leaves
  // Parts and starts it on the way back (#360). A failed read is
  // PASurface.poll()'s to report: catching it here handed the registry a
  // fulfilled promise and marked Parts current on a read that never landed.
  window.PASurface?.poll(() => loadOutputs(), { cadenceMs: POLL_MS, refreshOnReturn: true }).start();
})();
