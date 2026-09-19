// =============================================================================
// data/parts_mapping.js
//
// The one mapping - which Output drives which Part - as both of its surfaces
// read and change it: Parts from the part's end, Servos from the Output's end
// (CONTEXT.md "Parts", "Servos"; operator, 2026-09-19 on #412). Both read the
// same GET /api/servo/outputs answer and move a Part through the same request,
// asked in the same words, so the two ends cannot disagree about a Part or
// about what a move does.
//
// A move is announced before it happens, on every surface that can make one
// (#347). Taking a Part off the Output it is on names the Part, the Output it
// leaves, what that Output keeps and what the Part will move with, and asks
// with the verb. The firmware refuses a move that does not name the Output the
// Part is leaving, so a surface cannot skip the question by accident.
// moveFor() and announcement() are exported so guided Setup and an import ask
// it in the same words.
//
// An Output is called by what its board prints beside its pin, and that label
// is also the word POST /api/servo moves it by (ADR 0033 Amendment
// 2026-09-19): servoWord() hands it over exactly as the droid gave it.
// =============================================================================
(() => {
  const catalog = window.DroidParts;
  const kinds = window.DroidPartKind;

  const NOT_WIRED = "– not wired –";
  // The Output Address token a move sends for "no Output" (docs/api.md).
  const NO_OUTPUT = "none";

  // In the order a builder walks the droid: the dome top down, then the body.
  // A Common Addition is a Part the base design does not carry (`cadName:
  // null` in the catalog), and it gets its own group so a builder can tell the
  // arms they added from the ones the design came with (CONTEXT.md "Parts").
  const GROUPS = [
    { id: "dome-pies", label: "Dome pie panels", sections: ["dome_pies"] },
    { id: "dome-panels", label: "Dome side panels", sections: ["dome_panels"] },
    { id: "dome-lights", label: "Dome lights", sections: ["dome_lights"] },
    { id: "holos", label: "Holoprojectors", sections: ["holoprojectors"] },
    { id: "dome-buttons", label: "Dome buttons", sections: ["dome_fixtures"] },
    { id: "body", label: "Body doors & arms", sections: ["body_doors", "body_arms"] },
    { id: "additions", label: "Common additions", sections: [] },
    { id: "other", label: "Other (not on the model)", sections: ["other_slots"], unit: ["placeholder", "placeholders"] },
    { id: "unsorted", label: "More parts", sections: [] },
  ];

  const groupFor = (part) => {
    if (part.cadName === null) return GROUPS.find((group) => group.id === "additions");
    return GROUPS.find((group) => group.sections.includes(part.section)) || GROUPS[GROUPS.length - 1];
  };

  // Every Part in exactly one group, in catalog order, and a group with no
  // Parts is not drawn at all -- an empty heading is not a row.
  const groupParts = (parts) =>
    GROUPS.map((group) => ({ ...group, parts: parts.filter((part) => groupFor(part) === group) })).filter(
      (group) => group.parts.length > 0
    );

  const partById = new Map((catalog?.parts || []).map((part) => [part.id, part]));
  const partLabel = (id) => {
    const part = partById.get(id);
    if (!part) return id;
    return part.shorthand ? `${part.name} (${part.shorthand})` : part.name;
  };
  const listParts = (ids) => ids.map(partLabel).join(", ");
  const outputLabel = (output) => output.name || output.address;

  // The word POST /api/servo moves an Output by: the board's own label for it,
  // exactly as GET /api/servo/outputs gave it - ARM3 on the Artoo PCB, GPIO 49
  // on the FireBeetle 2, the space included (ADR 0033 Amendment 2026-09-19).
  // Never folded or rewritten: the label is not an id to be derived from, and a
  // board whose label is not the old protoArtoo word would be sent a word it
  // refuses. An Output no board labels - an expander's row - has none, and the
  // route cannot move it.
  const servoWord = (output) => output.name;
  const hasServoWord = (output) => servoWord(output) !== "";

  const optionText = (output) =>
    `${outputLabel(output)} · ${output.parts.length ? listParts(output.parts) : "drives nothing"}`;

  // The Output a Part is on. The firmware keeps a Part on at most one, so the
  // first answer is the only answer.
  const outputOf = (outputs, partId) => outputs.find((output) => output.parts.includes(partId)) || null;

  // A row whose every Part is a light carries no travel and no release: a light
  // has neither, and a zero or an empty bar would still read as a promise about
  // movement (data/droid_part_kind.js).
  const isLightRow = (output) =>
    output.parts.length > 0 && output.parts.every((id) => Boolean(kinds?.isLight(partById.get(id))));

  // GET /api/servo/outputs, read into the shape both surfaces paint from.
  const readOutputs = (answer) =>
    answer.map((output) => ({
      address: String(output.address),
      name: typeof output.name === "string" ? output.name : "",
      parts: Array.isArray(output.parts) ? output.parts.map(String) : [],
      reported: "commandedUs" in output,
      bandLoUs: Number(output.bandLoUs) || 0,
      bandHiUs: Number(output.bandHiUs) || 0,
      commandedUs: typeof output.commandedUs === "number" ? output.commandedUs : null,
      targetUs: typeof output.targetUs === "number" ? output.targetUs : null,
      // How many nudges have ended on this Output (#363); null from a firmware
      // that does not say, which a run must refuse rather than wait on.
      nudgesDone: typeof output.nudgesDone === "number" ? output.nudgesDone : null,
      // What the calibration dial reads (#364). The three widths are the
      // recorded positions, directional: openUs is whichever end the builder
      // recorded as open, so nothing here sorts the pair.
      component: typeof output.component === "string" ? output.component : "",
      openUs: typeof output.openUs === "number" ? output.openUs : null,
      centreUs: typeof output.centreUs === "number" ? output.centreUs : null,
      closeUs: typeof output.closeUs === "number" ? output.closeUs : null,
      calibrated: output.calibrated === true,
      held: output.held === true,
      // Why there is no pulse, meaningful only while commandedUs is null.
      limp: typeof output.limp === "string" ? output.limp : "off",
    }));

  // What putting a Part on `to` would do, read off the rows as the droid last
  // reported them. `from` is what the firmware needs told; the rest is what a
  // builder needs told first. Only taking a Part off one Output and putting it
  // on another is announced: choosing "not wired" on a Part's own row takes
  // nothing from anywhere else, and putting an unwired Part on an Output takes
  // nothing from any Part already there.
  const moveFor = (outputs, partId, to) => {
    const leaves = outputOf(outputs, partId);
    const arrives = outputs.find((output) => output.address === to) || null;
    return {
      part: partId,
      from: leaves ? leaves.address : NO_OUTPUT,
      to: arrives ? arrives.address : NO_OUTPUT,
      leaves,
      arrives,
      announce: leaves !== null && arrives !== null && leaves !== arrives,
      keeps: leaves ? leaves.parts.filter((id) => id !== partId) : [],
      joins: arrives && arrives !== leaves ? arrives.parts.slice() : [],
    };
  };

  // The question, after the reference's displacement dialog, taken nearly
  // verbatim: it names both Parts, says what the loser is left with, and the
  // button that agrees is the verb (r2d2-astromech-simulator v1.79.0; #347).
  const announcement = (move) => {
    const part = partLabel(move.part);
    const from = outputLabel(move.leaves);
    const to = outputLabel(move.arrives);
    const lines = [`${part} is on ${from}. Move it to ${to} and unwire it from ${from}?`];
    lines.push(move.keeps.length ? `${from} keeps driving ${listParts(move.keeps)}.` : `${from} will drive nothing.`);
    if (move.joins.length) {
      const verb = move.joins.length === 1 ? "is" : "are";
      lines.push(`${listParts(move.joins)} ${verb} on ${to} too — they will move together.`);
    }
    return { title: "Part already wired", body: lines.join(" "), confirm: "Move it", cancel: "Cancel" };
  };

  /**
   * The one rule a surface moves a Part by: one move at a time, a move that
   * takes a Part off another Output is asked first, and anything else goes
   * straight to the droid (#347, #362).
   *
   * @param {object} hosts
   * @param {HTMLDialogElement} hosts.dialog - the question, carrying
   *   .move-title, .move-body, .move-confirm and .move-cancel
   * @param {function} hosts.say - (text, level) the surface's feedback line
   * @param {function} hosts.reload - reads the outputs again after a move
   * @param {function} hosts.repaint - draws the controls back to the truth
   * @param {function} [hosts.onSending] - (partId) a move is on its way
   */
  const mover = ({ dialog, say, reload, repaint, onSending = () => {} }) => {
    const title = dialog.querySelector(".move-title");
    const body = dialog.querySelector(".move-body");
    let pendingPart = null;
    let asking = null;

    const send = async (move) => {
      const label = partLabel(move.part);
      pendingPart = move.part;
      onSending(move.part);
      say(`Moving ${label}...`);
      try {
        await window.PAApi.postForm(
          "/api/config",
          { movePart: move.part, movePartFrom: move.from, movePartTo: move.to },
          { timeoutMs: 4000 }
        );
        say(move.arrives ? `${label} is on ${outputLabel(move.arrives)}.` : `${label} is not on any output now.`, "success");
      } catch (error) {
        say(`${label} did not move: ${window.PAApi.messageFor(error)}`, "error");
      } finally {
        pendingPart = null;
      }
      try {
        await reload();
      } catch (error) {
        // The table still shows what the droid said before the move; the next
        // poll catches it up.
        console.warn("[parts] reading the outputs after a move failed:", error);
        repaint();
      }
    };

    const answer = (confirmed) => {
      if (!asking) return;
      const { move, control } = asking;
      asking = null;
      pendingPart = null;
      if (dialog.open) dialog.close();
      if (confirmed) {
        send(move);
        return;
      }
      // Cancelled: the control goes back to the truth, and focus to the control.
      repaint();
      control?.focus?.();
    };

    // `control` is the one the builder chose with, and it is where focus goes
    // back to if they cancel.
    const request = (move, control) => {
      if (pendingPart !== null) {
        say(`One move at a time: wait for ${partLabel(pendingPart)} to land.`, "warning");
        return;
      }
      if (move.from === move.to) return;
      if (!move.announce) {
        send(move);
        return;
      }
      const words = announcement(move);
      title.textContent = words.title;
      body.textContent = words.body;
      asking = { move, control };
      pendingPart = move.part;
      dialog.showModal();
    };

    dialog.querySelector(".move-confirm")?.addEventListener("click", () => answer(true));
    dialog.querySelector(".move-cancel")?.addEventListener("click", () => answer(false));
    // Escape is a cancel, not a dialog left open with no question on it.
    dialog.addEventListener("cancel", (event) => {
      event.preventDefault?.();
      answer(false);
    });

    return { request, pending: () => pendingPart };
  };

  window.PAParts = Object.freeze({
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
    readOutputs,
    moveFor,
    announcement,
    mover,
  });
})();
