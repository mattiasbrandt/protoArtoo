// =============================================================================
// data/parts_mapping.js
//
// The one mapping - which Part is on which Output - as both of its surfaces
// read and change it: Wiring from the part's end, Servos from the Output's end
// (operator, 2026-09-28 on #411: "the page we have 'parts' is where you do the
// 'wiring' ie mapping of parts to the board outputs ... is weird to have in a
// page called 'parts'"; Servos since 2026-09-19 on #412). Both read the same
// GET /api/servo/outputs answer and move a Part through the same request,
// asked in the same words, so the two ends cannot disagree about a Part or
// about what a move does.
//
// The part-first picker is picker() below, and Wiring is its one caller: it
// moved there from Parts with its question, rather than being copied. Parts
// keeps the Parts no Output claims (unclaimed()) and sends a builder to the
// picker with routeToOutput() - one route, taken from the droid picture's
// act and from each Unused row alike.
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
// 2026-09-19): servoWord() hands it over exactly as the droid gave it. What an
// Output is - its rows, its name, which Part it carries - is data/outputs.js's
// answer (#415); this file asks it and works none of it out again.
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

  // The word POST /api/servo moves an Output by: the board's own label for it,
  // exactly as the droid gave it (data/outputs.js `label`) - ARM3 on the Artoo
  // PCB, GPIO 49 on the FireBeetle 2, the space included (ADR 0033 Amendment
  // 2026-09-19).
  // Never folded or rewritten: the label is not an id to be derived from, and a
  // board whose label is not the old protoArtoo word would be sent a word it
  // refuses. An Output no board labels - an expander's row - has none, and the
  // route cannot move it.
  const servoWord = (output) => output.label;
  const hasServoWord = (output) => servoWord(output) !== "";

  const optionText = (output) =>
    `${output.name} · ${output.parts.length ? listParts(output.parts) : "nothing on it"}`;

  // A row whose every Part is a light carries no travel and no release: a light
  // has neither, and a zero or an empty bar would still read as a promise about
  // movement (data/droid_part_kind.js).
  const isLightRow = (output) =>
    output.parts.length > 0 && output.parts.every((id) => Boolean(kinds?.isLight(partById.get(id))));

  // What putting a Part on `to` would do, read off the rows as the droid last
  // reported them. `from` is what the firmware needs told; the rest is what a
  // builder needs told first. Only taking a Part off one Output and putting it
  // on another is announced: choosing "not wired" on a Part's own row takes
  // nothing from anywhere else, and putting an unwired Part on an Output takes
  // nothing from any Part already there.
  const moveFor = (outputs, partId, to) => {
    const leaves = window.PAOutputs.forPart(partId, outputs);
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
    const from = move.leaves.name;
    const to = move.arrives.name;
    const lines = [`${part} is on ${from}. Move it to ${to} and unwire it from ${from}?`];
    lines.push(move.keeps.length ? `${from} keeps ${listParts(move.keeps)}.` : `${from} will have nothing on it.`);
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

    // The question covers the surface it belongs to and never the chrome
    // around it (ADR 0048, #359). A native showModal() makes everything
    // outside the dialog inert, the shell's STOP included, so the dialog is
    // opened non-modally and what goes inert is the surface: at each level
    // from the dialog up to its .surface, every sibling of the path. Inert is
    // inherited, so the path itself stays live and the dialog can be answered.
    // The Sequences dialogs make the same move (data/seq.js showModal()).
    let heldInert = [];
    const holdSurface = () => {
      // Never above the surface: with no .surface around it, only the dialog's
      // own siblings go inert, so the climb can never reach the chrome.
      const surface = dialog.closest?.(".surface") || dialog.parentElement;
      let node = dialog;
      while (node && node !== surface && node.parentElement) {
        const parent = node.parentElement;
        [...parent.children].forEach((sibling) => {
          if (sibling !== node && !sibling.inert) {
            sibling.inert = true;
            heldInert.push(sibling);
          }
        });
        node = parent;
      }
    };
    const releaseSurface = () => {
      heldInert.forEach((node) => {
        node.inert = false;
      });
      heldInert = [];
    };

    const send = async (move) => {
      const label = partLabel(move.part);
      pendingPart = move.part;
      onSending(move.part);
      say(`Moving ${label}...`);
      const form = { movePart: move.part, movePartFrom: move.from, movePartTo: move.to };
      try {
        await window.PAApi.postForm("/api/config", form, { timeoutMs: 4000 });
        say(move.arrives ? `${label} is on ${move.arrives.name}.` : `${label} is not on any output now.`, "success");
      } catch (error) {
        // Worded from the refusal's field and reason, naming the Output from
        // what this move sent (data/web_api.js sayRefusal()).
        say(`${label} did not move: ${window.PAApi.messageFor(error, form)}`, "error");
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
      releaseSurface();
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
      dialog.show();
      holdSurface();
      // Focus on the safe answer. Not a trap: Tab leaves the dialog for the
      // chrome, which is where STOP is.
      dialog.querySelector(".move-cancel")?.focus?.();
    };

    dialog.querySelector(".move-confirm")?.addEventListener("click", () => answer(true));
    dialog.querySelector(".move-cancel")?.addEventListener("click", () => answer(false));
    // Escape is a cancel, not a dialog left open with no question on it. A
    // non-modal dialog fires no "cancel" event, so the key is read here.
    dialog.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      event.preventDefault?.();
      answer(false);
    });

    return { request, pending: () => pendingPart };
  };

  // ---------------------------------------------------------------------------
  // The Parts no Output claims
  //
  // What this image could move at all bounds the list. A Part whose control
  // path is the dome link is the Dome Controller's to move, not this image's,
  // and a Part that declares no control path (the two dome orientation
  // fixtures) is not moved by anything. Neither is ever "unused": Parts names
  // and counts them beside the list instead of dropping them, because a bound
  // a builder cannot see is indistinguishable from a row that went missing.
  //
  // This reads the catalog's `control` field for the question it answers: what
  // path a Part CAN be moved through on the design. Which Output a Part is on
  // for THIS droid is still the Servo Output rows and never this field (#375).
  //
  // UNCLAIMED is the Availability Reason for exactly this Part (CONTEXT.md
  // "Availability Reason"): the droid reports it when a sequence names one.
  // ---------------------------------------------------------------------------
  const UNCLAIMED = "part-not-assigned";

  const thisImageMoves = (part) =>
    part.control !== null && part.control !== undefined && part.control !== "dome-link";

  const unclaimed = (parts, outputs) =>
    parts.filter((part) => thisImageMoves(part) && !window.PAOutputs.forPart(part.id, outputs));

  // What moves a dome-link Part: the Dome Controller, told by a command over
  // the dome link, never a wire from this board (operator, 2026-09-29 on #411:
  // "what would make sense is to list the actual action/command instead of the
  // wire mapping"). The command is the one the dome is sent for that panel,
  // from the body's own map (data/dome_command_map.js, ADR 0009), which is
  // keyed by the catalog's shorthand (data/dome_layout.js). A panel the map has
  // no command for says so rather than guessing one: its number on the dome is
  // a declared unknown (docs/droid-parts.yaml `dome_link_panel: TBD`).
  const DOME_ACTS = [
    { capability: "open", word: "Open" },
    { capability: "close", word: "Close" },
  ];
  const domeCommandText = (part) => {
    const resolve = window.DomeCommandMap?.resolvePanelCommand;
    const said = DOME_ACTS.map(({ capability, word }) => {
      const command = resolve && part.shorthand ? resolve(part.shorthand, capability) : null;
      return command ? `${word} ${command}` : null;
    }).filter(Boolean);
    return said.length ? said.join(" · ") : "No command yet";
  };

  // ---------------------------------------------------------------------------
  // The route to the picker
  //
  // Giving a Part an Output, or changing it, is done in the picker on Wiring
  // and nowhere else (operator, 2026-09-28 on #411). A surface that offers it -
  // the droid picture's "Give it an output", an Unused row on Parts - hands
  // over the Part and goes there. The shell's address is a bare surface name
  // (data/shell.js surfaceFromHash()), so the Part rides here instead, in the
  // one module both surfaces load, and the picker takes it once it is on
  // screen with the droid's answer painted. `off`: the Part is off the droid
  // but still on an Output, so the builder is asked about the wire, not told
  // to choose one.
  // ---------------------------------------------------------------------------
  const PICKER_SURFACE = "wiring";
  let wanted = null;

  const routeToOutput = (partId, { off = false } = {}) => {
    wanted = { part: partId, off };
    window.location.hash = PICKER_SURFACE;
  };

  /**
   * The part-first picker: one row per Part this image moves, grouped the way
   * a builder thinks about them, each choosing the Output the Part is on.
   * Moved here from Parts with its question (#347) when the mapping moved to
   * Wiring.
   *
   * No such row is ever hidden. A fresh droid shows every one reading
   * "- not wired -", which is the honest state of a build in progress, and
   * hiding a row is how an operator loses an output (#296).
   *
   * A Part this image does not move gets no Output to choose, by the same rule
   * that keeps it out of Unused (thisImageMoves()). The Dome Controller's
   * Parts are one group after the rest, each showing the command that moves
   * it (domeCommandText()); a dome fixture that nothing moves has no row.
   *
   * The table is built once and repainted in place. A repaint writes
   * textContent, value, disabled and classList on nodes that already exist,
   * and leaves the control the builder is holding alone until they let go of
   * it (r2d2-astromech-simulator v1.79.0, src/js/maestro/hw-table.js:171-173).
   *
   * @param {object} hosts
   * @param {Element} hosts.table - where the table goes
   * @param {Element} hosts.summary - the counts, beside the section's heading
   * @param {Element} hosts.feedback - the line a move and a route answer on
   * @param {HTMLDialogElement} hosts.dialog - the move question, as mover()
   *   takes it
   * @returns {boolean} whether it mounted
   */
  const picker = ({ table, summary, feedback, dialog } = {}) => {
    const OUTPUTS = window.PAOutputs;
    if (!table || !summary || !feedback || !dialog || !OUTPUTS) return false;
    const esc = (value) => window.PAUtils.escapeHtml(String(value));
    const say = (text, level) => window.PAUtils.showFeedback(feedback, text, level);
    const answered = () => OUTPUTS.known().table;
    const outputOf = (partId) => OUTPUTS.forPart(partId);

    if (!partById.size) {
      // A table with no rows reads as a droid with no parts. Say what broke.
      summary.textContent = "The parts list did not load, so there is nothing to show. Reload the page to try again.";
      console.error("[parts] window.DroidParts is missing; /droid_parts.js did not load");
      return false;
    }

    // Until the table answers, the summary says so in the one word for it
    // (data/outputs.js live()), which a slot shows as the waiting dots.
    summary.classList.add("waiting");
    summary.textContent = window.PALiveReading.slotText(OUTPUTS.live(null).word);

    const groupHeading = (group) => {
      const [one, many] = group.unit || ["part", "parts"];
      const count = group.parts.length;
      return `${group.label} — ${count} ${count === 1 ? one : many}`;
    };

    const moved = catalog.parts.filter(thisImageMoves);
    const domeLink = catalog.parts.filter((part) => part.control === "dome-link");

    const rowHtml = (part) => {
      const kind = kinds ? kinds.treatmentClass(part) : "";
      const shorthand = part.shorthand ? `<span class="parts-shorthand">${esc(part.shorthand)}</span>` : "";
      const light = kinds?.isLight(part) ? `<span class="parts-kind">light</span>` : "";
      return (
        `<tr class="parts-row${kind ? ` ${kind}` : ""}" data-part="${esc(part.id)}">` +
        `<th scope="row"><span class="parts-name">${esc(part.name)}</span>${shorthand}${light}` +
        `<span class="parts-gang"></span></th>` +
        `<td><select class="parts-output" aria-label="${esc(`Output for ${part.name}`)}" disabled>` +
        `<option value="${NO_OUTPUT}">${esc(OUTPUTS.live(null).word)}</option></select></td></tr>`
      );
    };

    // A dome-link Part's row: its command where an Output select would be,
    // and no control at all. Its `.parts-gang` names a body Output it sits on
    // anyway, which a builder can record, so a Part on a wire is never
    // invisible here.
    const domeRowHtml = (part) => {
      const shorthand = part.shorthand ? `<span class="parts-shorthand">${esc(part.shorthand)}</span>` : "";
      return (
        `<tr class="parts-row parts-dome-row" data-dome-part="${esc(part.id)}">` +
        `<th scope="row"><span class="parts-name">${esc(part.name)}</span>${shorthand}` +
        `<span class="parts-gang"></span></th>` +
        `<td class="parts-command">${esc(domeCommandText(part))}</td></tr>`
      );
    };

    const groupBody = (id, heading, rowsHtml) =>
      `<tbody data-group="${id}"><tr class="parts-group"><th colspan="2" scope="colgroup">` +
      `${esc(heading)}</th></tr>${rowsHtml}</tbody>`;

    table.innerHTML =
      `<table class="parts-table"><thead><tr><th scope="col">Part</th><th scope="col">Output</th></tr></thead>` +
      groupParts(moved)
        .map((group) => groupBody(group.id, groupHeading(group), group.parts.map(rowHtml).join("")))
        .join("") +
      (domeLink.length
        ? groupBody("dome-controller", groupHeading({ label: "Dome Controller", parts: domeLink }), domeLink.map(domeRowHtml).join(""))
        : "") +
      `</table>`;

    const rows = new Map();
    table.querySelectorAll("[data-part]").forEach((node) => {
      rows.set(node.dataset.part, {
        node,
        select: node.querySelector("select"),
        gang: node.querySelector(".parts-gang"),
        addresses: null,
      });
    });
    const domeRows = new Map();
    table.querySelectorAll("[data-dome-part]").forEach((node) => {
      domeRows.set(node.dataset.domePart, node.querySelector(".parts-gang"));
    });

    // Declared before the mover, which is handed a way to repaint.
    let paint = () => {};
    const move = mover({
      dialog,
      say,
      reload: () => OUTPUTS.refresh(),
      repaint: () => paint(),
      onSending: (partId) => {
        const row = rows.get(partId);
        if (row) row.select.disabled = true;
      },
    });

    // The control the builder has hold of: the one focused, or the one whose
    // move is being asked about or is on its way. Its value and its options
    // are theirs until they let go.
    const held = (id, select) => id === move.pending() || document.activeElement === select;

    const paintRow = (id, row, outputs, addresses) => {
      const output = outputOf(id);
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

    // A Part handed over by routeToOutput(), taken once the picker is on
    // screen with the droid's answer in it: before then there is no Output to
    // choose, and a focus on a control that is not showing lands nowhere.
    const claimWanted = () => {
      if (wanted === null || !answered() || document.body?.dataset?.page !== PICKER_SURFACE) return;
      const { part, off } = wanted;
      wanted = null;
      const row = rows.get(part);
      if (!row) {
        if (domeRows.has(part)) say(`The Dome Controller moves ${partLabel(part)}, not an output.`);
        return;
      }
      row.node.scrollIntoView?.({ block: "center" });
      row.select.focus();
      say(
        off
          ? `Pick ${NOT_WIRED} in ${partLabel(part)}'s row if the wire came off too.`
          : `Choose the output that moves ${partLabel(part)} in its row.`
      );
    };

    paint = () => {
      if (!answered()) return;
      const outputs = OUTPUTS.list();
      const addresses = outputs.map((output) => output.address).join(",");
      rows.forEach((row, id) => paintRow(id, row, outputs, addresses));
      domeRows.forEach((gang, id) => {
        const output = outputOf(id);
        gang.textContent = output ? ` on ${output.name} too` : "";
      });

      const on = moved.filter((part) => outputOf(part.id) !== null).length;
      const empty = outputs.filter((output) => output.parts.length === 0).length;
      let text = `${on} of ${moved.length} parts on an output · ${empty} of ${outputs.length} outputs with nothing on them`;
      // A Part on an Output that this page has no row for would otherwise be
      // invisible, which is the one thing this table must never be.
      const unknown = outputs.flatMap((output) => output.parts).filter((id) => !partById.has(id));
      if (unknown.length) {
        text += ` · ${unknown.join(", ")} on an output too, unknown to this page - upload the matching web UI`;
      }
      summary.textContent = text;
      claimWanted();
    };

    table.addEventListener("change", (event) => {
      const select = event.target;
      const id = select?.closest?.("[data-part]")?.dataset.part;
      if (!id || !answered()) return;
      move.request(moveFor(OUTPUTS.list(), id, select.value), select);
    });

    // A control that was held catches up with whatever arrived while it was.
    table.addEventListener("focusout", () => paint());

    // Every read of the Outputs - this surface's, a move's, a save made on
    // another surface - publishes once, and this is the one place the picker
    // paints from it.
    OUTPUTS.onChange(() => paint());
    paint();
    return true;
  };

  window.PAParts = Object.freeze({
    NOT_WIRED,
    NO_OUTPUT,
    groupParts,
    partById,
    partLabel,
    listParts,
    servoWord,
    hasServoWord,
    optionText,
    isLightRow,
    moveFor,
    announcement,
    mover,
    UNCLAIMED,
    thisImageMoves,
    unclaimed,
    routeToOutput,
    picker,
  });
})();
