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
  // `dome: true` marks the groups that sit on the dome (isDomePart()).
  const GROUPS = [
    { id: "dome-pies", label: "Dome pie panels", sections: ["dome_pies"], dome: true },
    { id: "dome-panels", label: "Dome side panels", sections: ["dome_panels"], dome: true },
    { id: "dome-lights", label: "Dome lights", sections: ["dome_lights"], dome: true },
    { id: "holos", label: "Holoprojectors", sections: ["holoprojectors"], dome: true },
    { id: "dome-buttons", label: "Dome buttons", sections: ["dome_fixtures"], dome: true },
    { id: "body", label: "Body doors & arms", sections: ["body_doors", "body_arms"] },
    { id: "additions", label: "Common additions", sections: [] },
    { id: "other", label: "Other (not on the model)", sections: ["other_slots"], unit: ["placeholder", "placeholders"] },
    { id: "unsorted", label: "More parts", sections: [] },
  ];

  const groupFor = (part) => {
    if (part.cadName === null) return GROUPS.find((group) => group.id === "additions");
    return GROUPS.find((group) => group.sections.includes(part.section)) || GROUPS[GROUPS.length - 1];
  };

  // A dome Part is one that sits on the dome: its group says so, whatever its
  // catalog `control` path. Dome wiring is the Dome Controller's, so the
  // part-first picker offers no body Output for any of them (operator,
  // 2026-09-29 on #411: "Dome wiring is all handled and managed by the dome
  // controller"). This is where a Part sits, not what this image can move:
  // Unused on Parts asks the second question, with thisImageMoves().
  const isDomePart = (part) => groupFor(part).dome === true;

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

  // An Output with a Part on it is wired, and one with none is free (CONTEXT.md
  // "Wiring"): the word a builder reads for an empty Output.
  const FREE = "free";

  const optionText = (output) =>
    `${output.name} · ${output.parts.length ? listParts(output.parts) : FREE}`;

  // The Outputs a Part may be put on: every one for a servo Part, and only the
  // ones a light can go on for a light Part (operator, 2026-09-29 on #411:
  // "either we limit what you can define in the wiring page or give
  // recommendations" - both). Whether an Output can carry a light is its
  // board's fact, on its row (data/outputs.js `canLight`); nothing here knows
  // which Outputs those are. The Output the Part is on now is always offered,
  // so the select shows the truth even where an older droid put a light on a
  // wire that cannot carry one.
  const outputsFor = (part, outputs, current) => {
    const light = Boolean(kinds?.isLight(part));
    return outputs.filter((output) => !light || output.canLight || output === current);
  };

  // The recommend half: the one Output whose board says it usually carries
  // this Part, marked in the fewest words.
  const SUGGESTED = "suggested";

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

  // What moves a dome Part: the Dome Controller, told by a command over the
  // dome link, never a wire from this board (operator, 2026-09-29 on #411:
  // "what would make sense is to list the actual action/command instead of the
  // wire mapping"). The command is the one the dome is sent for that panel,
  // from the body's own map (data/dome_command_map.js, ADR 0009), which is
  // keyed by the catalog's shorthand (data/dome_layout.js). A Part the map has
  // no command for says so rather than guessing one: a panel's number on the
  // dome is a declared unknown (docs/droid-parts.yaml `dome_link_panel: TBD`),
  // and the map carries no light, holoprojector or button at all.
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
   * The part-first picker: one row per body Part, grouped the way a builder
   * thinks about them, each choosing the Output the Part is on and, on the
   * same row once it is on one, what is on that wire - its servo, or its
   * Light Type for a light Part (operator, 2026-09-29 on #411: "define and
   * wire a body part/panel to a output (GPIO) and then for each you then
   * define what servo type, same way for light on a part"). This table is the
   * one place an Output is wired: an Output with a Part on it is wired, and
   * the droid writes its wired tick with the move (docs/api.md, `movePart`).
   * Moved here from Parts with its question (#347) when the mapping moved to
   * Wiring.
   *
   * No such row is ever hidden. A fresh droid shows every one reading
   * "- not wired -", which is the honest state of a build in progress, and
   * hiding a row is how an operator loses an output (#296).
   *
   * A dome Part gets no Output to choose (isDomePart()). Every one of them is
   * a row of one group after the rest, the Dome Controller's, showing the
   * command that moves it or "No command yet" (domeCommandText()), so no dome
   * Part vanishes from the page.
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
   * @param {Element} hosts.timing - the line saying a move waits for the
   *   droid's next start, while one does
   * @param {Element} [hosts.find] - where a Find by Moving run's line goes
   *   (data/find_by_moving.js): a Part on no Output offers a run on its row
   * @returns {boolean} whether it mounted
   */
  const picker = ({ table, summary, feedback, dialog, timing, find } = {}) => {
    const OUTPUTS = window.PAOutputs;
    const TIMING = window.PAApplyTiming;
    if (!table || !summary || !feedback || !dialog || !timing || !OUTPUTS || !TIMING) return false;
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

    const body = catalog.parts.filter((part) => !isDomePart(part));
    const dome = catalog.parts.filter(isDomePart);

    const rowHtml = (part) => {
      const kind = kinds ? kinds.treatmentClass(part) : "";
      const shorthand = part.shorthand ? `<span class="parts-shorthand">${esc(part.shorthand)}</span>` : "";
      const light = kinds?.isLight(part) ? `<span class="parts-kind">light</span>` : "";
      return (
        `<tr class="parts-row${kind ? ` ${kind}` : ""}" data-part="${esc(part.id)}">` +
        `<th scope="row"><span class="parts-name">${esc(part.name)}</span>${shorthand}${light}` +
        `<span class="parts-gang"></span></th>` +
        `<td><select class="parts-output" aria-label="${esc(`Output for ${part.name}`)}" disabled>` +
        `<option value="${NO_OUTPUT}">${esc(OUTPUTS.live(null).word)}</option></select></td>` +
        `<td class="parts-carries"></td></tr>`
      );
    };

    // A dome Part's row: its command where an Output select would be, and no
    // control at all. It keeps its Part Kind's treatment, a light's included.
    // Its `.parts-gang` names a body Output it sits on anyway, which a builder
    // can record, so a Part on a wire is never invisible here.
    const domeRowHtml = (part) => {
      const kind = kinds ? kinds.treatmentClass(part) : "";
      const shorthand = part.shorthand ? `<span class="parts-shorthand">${esc(part.shorthand)}</span>` : "";
      const light = kinds?.isLight(part) ? `<span class="parts-kind">light</span>` : "";
      return (
        `<tr class="parts-row parts-dome-row${kind ? ` ${kind}` : ""}" data-dome-part="${esc(part.id)}">` +
        `<th scope="row"><span class="parts-name">${esc(part.name)}</span>${shorthand}${light}` +
        `<span class="parts-gang"></span></th>` +
        `<td class="parts-command" colspan="2">${esc(domeCommandText(part))}</td></tr>`
      );
    };

    const groupBody = (id, heading, rowsHtml) =>
      `<tbody data-group="${id}"><tr class="parts-group"><th colspan="3" scope="colgroup">` +
      `${esc(heading)}</th></tr>${rowsHtml}</tbody>`;

    table.innerHTML =
      `<table class="parts-table"><thead><tr><th scope="col">Part</th><th scope="col">Output</th>` +
      `<th scope="col">Type</th></tr></thead>` +
      groupParts(body)
        .map((group) => groupBody(group.id, groupHeading(group), group.parts.map(rowHtml).join("")))
        .join("") +
      (dome.length
        ? groupBody("dome-controller", groupHeading({ label: "Dome Controller", parts: dome }), dome.map(domeRowHtml).join(""))
        : "") +
      `</table>`;

    const rows = new Map();
    table.querySelectorAll("[data-part]").forEach((node) => {
      rows.set(node.dataset.part, {
        node,
        part: partById.get(node.dataset.part),
        select: node.querySelector("select"),
        gang: node.querySelector(".parts-gang"),
        carries: node.querySelector(".parts-carries"),
        addresses: null,
        drawn: null,
      });
    });
    const domeRows = new Map();
    table.querySelectorAll("[data-dome-part]").forEach((node) => {
      domeRows.set(node.dataset.domePart, node.querySelector(".parts-gang"));
    });

    // Declared before the mover and the finder, which are handed a way to
    // repaint.
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

    // Find by Moving, run from a Part's row (data/find_by_moving.js). Its That
    // one is the same move this row's select makes, question and all. Absent
    // where the page carries no run, and then no row offers one.
    const finder = find && window.PAFindByMoving
      ? window.PAFindByMoving.runner({
        panel: find,
        say,
        move: (partId, address) => move.request(moveFor(OUTPUTS.list(), partId, address), null),
        pending: () => move.pending(),
        changed: () => paint(),
        surface: "Wiring",
      })
      : null;

    table.addEventListener("click", (event) => {
      const act = event.target?.closest?.("[data-find]");
      if (!act || act.disabled || finder === null) return;
      finder.start(act.dataset.find);
    });

    // The control the builder has hold of: the one focused, or the one whose
    // move is being asked about or is on its way. Its value and its options
    // are theirs until they let go.
    const held = (id, select) => id === move.pending() || document.activeElement === select;

    // A Part on no Output has no wire yet, and its row offers to find the one
    // it is on by moving the free Outputs (data/find_by_moving.js; operator,
    // 2026-09-30 on #411: "Pulse free Outputs in a run"). Refused while the
    // estop is latched or the droid is out of reach, and while a run is going.
    const drawFind = (row, id) => {
      const live = finder !== null && finder.live();
      const running = finder !== null && finder.running() !== null;
      const drawn = finder === null ? "none" : `find|${live}|${running}`;
      if (row.drawn === drawn) return;
      row.drawn = drawn;
      if (finder === null) {
        row.carries.replaceChildren();
        return;
      }
      const act = document.createElement("button");
      act.type = "button";
      act.className = "btn btn-sm btn-quiet parts-find-act";
      act.dataset.find = id;
      // The magnifying glass beside the words (operator, 2026-09-30 on #411),
      // in the markup data/shell.js icon() writes: the sprite is the shell's,
      // and a module names a symbol by its literal <use> so
      // tools/check_surface_anatomy.py can resolve it.
      act.innerHTML =
        `<svg class="i" aria-hidden="true" focusable="false"><use href="#i-magnify"/></svg>find by moving`;
      act.setAttribute("aria-label", `Find the output ${row.part.name} is on by moving each free one`);
      window.PAApi.gateControls([act], live && !running);
      row.carries.replaceChildren(act);
    };

    // What is on the wire a Part is on, on the Part's own row: which servo
    // for a servo Part, which Light Type for a light Part, and the find act
    // until the Part is on an Output. It is the Output's answer (CONTEXT.md
    // "Output"), so two Parts ganged on one wire show the same pick, and a
    // pick saves the Output's row. Redrawn only when what it shows changes.
    const drawCarries = (row, output, id) => {
      if (!output) {
        drawFind(row, id);
        return;
      }
      const light = Boolean(kinds?.isLight(row.part));
      const options = light ? OUTPUTS.LIGHT_TYPES : OUTPUTS.SERVO_MODELS;
      const current = light ? output.light?.id ?? null : output.servo?.id ?? null;
      const drawn = `${output.address}|${current}`;
      if (row.drawn === drawn) return;
      row.drawn = drawn;
      row.carries.replaceChildren(
        window.PAOutputSettings.segmented(`${row.part.name} ${light ? "light type" : "servo"}`, options, current,
          (value) => carry(output, value))
      );
    };

    const paintRow = (id, row, outputs) => {
      const output = outputOf(id);
      row.node.classList.toggle("is-wired", output !== null);
      const gang = output ? output.parts.filter((other) => other !== id) : [];
      row.gang.textContent = gang.length ? ` moves with ${listParts(gang)}` : "";
      drawCarries(row, output, id);
      if (held(id, row.select)) return;
      // The only rebuild, and only of a control nobody is holding: the set of
      // Outputs a Part may go on is fixed from boot, so this runs once per
      // row in practice.
      const offered = outputsFor(row.part, outputs, output);
      const addresses = offered.map((each) => each.address).join(",");
      if (row.addresses !== addresses) {
        row.select.innerHTML =
          `<option value="${NO_OUTPUT}">${NOT_WIRED}</option>` +
          offered.map((each) => `<option value="${esc(each.address)}"></option>`).join("");
        row.addresses = addresses;
      }
      const options = row.select.querySelectorAll("option");
      offered.forEach((each, index) => {
        const text = each.suggestedPart === id ? `${optionText(each)} · ${SUGGESTED}` : optionText(each);
        if (options[index + 1].textContent !== text) options[index + 1].textContent = text;
      });
      row.select.value = output ? output.address : NO_OUTPUT;
      row.select.disabled = false;
    };

    // A pick of what is on a wire saves that Output's row, and the droid's
    // answer is what is drawn after it, taken or refused (data/outputs.js).
    const carry = async (output, type) => {
      say("Saving…");
      try {
        await OUTPUTS.save(output.address, { type });
        say(`Saved on ${output.name}.`, "success");
      } catch (error) {
        say(window.PAApi.messageFor(error), "error");
      }
    };

    // An Output with a Part on it is wired, and the droid reads that at its
    // next start (the `wired` row Setting's timing): a Part put on a free
    // Output, or the last one taken off, waits for it. The line says so only
    // while something waits, in the timing's own words, compared against
    // what the droid reports it started with, never this page's first read.
    const WIRED_TIMING = window.PAApi.rowTimingOf("wired");
    const paintTiming = (outputs) => {
      const waiting = outputs.some((output) => output.started && (
        output.wired !== output.started.wired || (output.light ? output.light.id : null) !== output.started.light));
      if (waiting) {
        TIMING.paint(timing, WIRED_TIMING, { pending: true });
        return;
      }
      timing.textContent = "";
      timing.classList.add("hidden");
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
      rows.forEach((row, id) => paintRow(id, row, outputs));
      paintTiming(outputs);
      domeRows.forEach((gang, id) => {
        const output = outputOf(id);
        gang.textContent = output ? ` on ${output.name} too` : "";
      });

      const on = body.filter((part) => outputOf(part.id) !== null).length;
      const empty = outputs.filter((output) => output.parts.length === 0).length;
      let text = `${on} of ${body.length} parts on an output · ${empty} of ${outputs.length} outputs ${FREE}`;
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
    isDomePart,
    unclaimed,
    routeToOutput,
    picker,
  });
})();
