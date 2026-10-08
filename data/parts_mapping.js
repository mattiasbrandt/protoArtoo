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
// The parts wiring table is picker() below, and Wiring is its one caller: it
// moved there from Parts with its question, rather than being copied. It is
// the one place on Wiring that says which Output is wired and which is free
// (operator, 2026-10-01 on #463: "it makes most sense to also define this
// also in the wiring table"). A Part's Output is picked on Parts too, with
// the same bar (operator, 2026-10-04 on #463: "Also pick it on Parts"):
// outputChooser() is that bar, its one-press suggestion, take off and the one
// request behind them, and Wiring's table and Parts' panel and Unused rows
// each make one, so the two surfaces cannot come to pick differently.
//
// A move is announced before it happens, on every surface that can make one
// (#347). Taking a Part off the Output it is on names the Part, the Output it
// leaves, what that Output keeps and what the Part will move with, and asks
// with the verb. The firmware refuses a move that does not name the Output the
// Part is leaving, so a surface cannot skip the question by accident.
// moveFor() and announcement() are exported; announcement() answers in the
// shape window.PAOverlay.ask() takes (data/overlay.js), so any other surface
// can ask the same question in the same words.
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

  // The Output Address token a move sends for "no Output" (docs/api.md).
  const NO_OUTPUT = "none";

  // In the order a builder walks the droid: the dome top down, then the body.
  // A Common Addition is a Part the base design does not carry (`cadName:
  // null` in the catalog), and it gets its own group so a builder can tell the
  // arms they added from the ones the design came with (GLOSSARY.md "Parts").
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
  // board whose label is not the old protoR2 word would be sent a word it
  // refuses. An Output no board labels - an expander's row - has none, and the
  // route cannot move it.
  const servoWord = (output) => output.label;
  const hasServoWord = (output) => servoWord(output) !== "";

  // An Output with a Part on it is wired, and one with none is free (GLOSSARY.md
  // "Wiring"): the word a builder reads for an empty Output.
  const FREE = "free";

  const countOf = (count, [one, many]) => `${count} ${count === 1 ? one : many}`;

  // The words the parts wiring table is headed with, on the screen and on
  // Wiring's printed copy (data/wiring.js), which is the same table as text.
  const TABLE = Object.freeze({
    columns: Object.freeze(["Part", "Output", "On the wire"]),
    board: "Board outputs",
    // The serial links, and the fitted products no serial link carries: the
    // dome's ESC, the radio and its receiver (operator, 2026-10-01 on #463).
    links: "Links",
    dome: "Dome Controller",
  });

  // How many Outputs are wired and how many are free, and how many Parts are
  // on them: the one count Wiring states, over its table and on its printed
  // copy alike (data/wiring.js), so the two cannot come to count differently.
  const wiredCounts = (outputs) => {
    const wired = outputs.filter((output) => output.parts.length > 0);
    return {
      wired: wired.length,
      free: outputs.length - wired.length,
      parts: wired.reduce((sum, output) => sum + output.parts.length, 0),
    };
  };
  const wiredSummary = (outputs) => {
    const counts = wiredCounts(outputs);
    return (
      `${countOf(counts.parts, ["part", "parts"])} on ${countOf(counts.wired, ["output", "outputs"])}` +
      ` · ${counts.free} ${FREE}`
    );
  };

  // The Outputs a Part may not be put on: the ones a light cannot go on, for a
  // light Part (operator, 2026-09-29 on #411: "either we limit what you can
  // define in the wiring page or give recommendations" - both). Whether an
  // Output can carry a light is its board's fact, on its row (data/outputs.js
  // `canLight`); nothing here knows which Outputs those are. The Output the
  // Part is on now is never refused, so the row shows the truth even where an
  // older droid put a light on a wire that cannot carry one.
  const refusedFor = (part, outputs, current) => {
    const light = Boolean(kinds?.isLight(part));
    return outputs.filter((output) => light && !output.canLight && output !== current);
  };

  // The recommend half: the one Output whose board says it usually carries
  // this Part, marked in the fewest words.
  const SUGGESTED = "suggested";

  // Why an Output is refused a light Part, in the fewest words.
  const NO_LIGHT = "no light";

  // A row whose every Part is a light carries no travel and no release: a light
  // has neither, and a zero or an empty bar would still read as a promise about
  // movement (data/droid_part_kind.js).
  const isLightRow = (output) =>
    output.parts.length > 0 && output.parts.every((id) => Boolean(kinds?.isLight(partById.get(id))));

  // What putting a Part on `to` would do, read off the rows as the droid last
  // reported them. `from` is what the firmware needs told; the rest is what a
  // builder needs told first. Only taking a Part off one Output and putting it
  // on another is announced: taking a Part off on its own row takes
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

  // The question, after the reference's displacement dialog: it names both
  // Parts and says what the Output it leaves is left with
  // (r2d2-astromech-simulator v1.79.0; #347). The title is the question, the
  // body the consequence, and both buttons are verbs - the one that keeps
  // things says what is kept, never "Cancel" (#456). Shaped as
  // window.PAOverlay.ask() takes a question.
  const announcement = (move) => {
    const part = partLabel(move.part);
    const from = move.leaves.name;
    const to = move.arrives.name;
    const lines = [
      move.keeps.length
        ? `It comes off ${from}, which keeps ${listParts(move.keeps)}.`
        : `It comes off ${from}, which is left with nothing on it.`,
    ];
    if (move.joins.length) {
      const verb = move.joins.length === 1 ? "is" : "are";
      lines.push(`${listParts(move.joins)} ${verb} on ${to} too, and will move with it.`);
    }
    return { title: `Move ${part} to ${to}?`, body: lines.join(" "), yes: "Move it", yesIcon: "transfer", no: "Leave it where it is" };
  };

  /**
   * The one rule a surface moves a Part by: one move at a time, a move that
   * takes a Part off another Output is asked first, and anything else goes
   * straight to the droid (#347, #362).
   *
   * @param {object} hosts
   * @param {HTMLDialogElement} hosts.dialog - the question, carrying
   *   .move-title, .move-body, .move-confirm and .move-cancel; the words on
   *   all four are written here, from announcement(). The two answers are
   *   acts (#460): each must carry its .act-label span and icon, or
   *   PAUi.setAct() throws when the question is asked
   * @param {function} hosts.say - (text, level) the surface's feedback line
   * @param {function} hosts.reload - reads the outputs again after a move
   * @param {function} hosts.repaint - draws the controls back to the truth
   * @param {function} [hosts.onSending] - (partId) a move is on its way
   */
  const mover = ({ dialog, say, reload, repaint, onSending = () => {} }) => {
    const title = dialog.querySelector(".move-title");
    const body = dialog.querySelector(".move-body");
    const confirmButton = dialog.querySelector(".move-confirm");
    const cancelButton = dialog.querySelector(".move-cancel");
    let pendingPart = null;
    let asking = null;

    // The question covers the surface it belongs to and never the chrome
    // around it (ADR 0048, #359): opened non-modally, with the surface held
    // inert by the shared climb (data/overlay.js holdSurface()).
    let releaseSurface = () => {};

    // Escape is "leave it where it is", through the one shared guard, so a
    // question opened over this one takes the key first and this one never
    // hears it (data/overlay.js escGuard()). A question on a surface that was
    // navigated away from is not on top of anything.
    const escape = window.PAOverlay.escGuard(
      () => asking !== null && dialog.open && dialog.isConnected,
      () => answer(false)
    );

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
      escape.unbind();
      if (dialog.open) dialog.close();
      releaseSurface();
      releaseSurface = () => {};
      if (confirmed) {
        send(move);
        return;
      }
      // Left where it is: the control goes back to the truth, and focus to it.
      repaint();
      control?.focus?.();
    };

    // `control` is the one the builder chose with, and it is where focus goes
    // back to if they leave the Part where it is.
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
      if (confirmButton) window.PAUi.setAct(confirmButton, words.yes, words.yesIcon);
      if (cancelButton) window.PAUi.setAct(cancelButton, words.no);
      asking = { move, control };
      pendingPart = move.part;
      dialog.show();
      releaseSurface = window.PAOverlay.holdSurface(dialog);
      escape.bind();
      // Focus on the answer that keeps things. Not a trap: Tab leaves the
      // dialog for the chrome, which is where STOP is.
      cancelButton?.focus?.();
    };

    confirmButton?.addEventListener("click", () => answer(true));
    cancelButton?.addEventListener("click", () => answer(false));

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
  // UNCLAIMED is the Availability Reason for exactly this Part (GLOSSARY.md
  // "Availability Reason"): the droid reports it when a sequence names one.
  // ---------------------------------------------------------------------------
  const UNCLAIMED = "part-not-assigned";

  const thisImageMoves = (part) =>
    part.control !== null && part.control !== undefined && part.control !== "dome-link";

  const unclaimed = (parts, outputs) =>
    parts.filter((part) => thisImageMoves(part) && !window.PAOutputs.forPart(part.id, outputs));

  // ---------------------------------------------------------------------------
  // A Part off the droid that an Output still claims
  //
  // Two of the builder's answers that cannot both be true: the Fitted Parts
  // say the Part is not on the droid, and the Output rows say its wire is
  // still on an Output (#328, #373). Neither is wrong on its own - the wire
  // may still be plugged in, and only the builder knows - so every surface
  // that meets it says both and changes neither (never an automatic unmap).
  // The rule lives here once: the droid picture's panel (data/droid_picture.js),
  // a Drop on Parts (data/parts.js) and Wiring's list of what does not line up
  // (data/wiring.js) all ask it.
  //
  // `fitted` is the Droid Build's Fitted Parts as the droid answered them
  // (window.DroidBuild.current().fitted), or null before it has: until then
  // nobody has said the Part is off, so there is nothing to disagree with.
  // Returns the Output that still claims the Part, or null.
  // ---------------------------------------------------------------------------
  const offButMapped = (partId, fitted, outputs) => {
    if (!Array.isArray(fitted) || partId === null || partId === undefined) return null;
    if (fitted.indexOf(partId) !== -1) return null;
    return window.PAOutputs.forPart(partId, outputs);
  };

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

  // What a surface says instead of an Output bar for a dome Part: the Dome
  // Controller moves it, and no body Output does (isDomePart()).
  const domeMovesText = (name) => `The Dome Controller moves ${name}, not an output.`;

  // ---------------------------------------------------------------------------
  // The route to the table
  //
  // A Part's Output is picked where the builder is, on Wiring's table or on
  // Parts with the same bar (outputChooser() below; operator, 2026-10-04 on
  // #463, overturning 2026-09-28 on #411's Wiring-only rule). What still sends
  // a builder to Wiring is a question only its table answers in full - a Part
  // off the droid whose wire may still be on an Output, where the answer is
  // take off on its row. The shell's address is a bare surface name
  // (data/shell.js surfaceFromHash()), so the Part rides here instead, in the
  // one module both surfaces load, and the table takes it once it is on
  // screen with the droid's answer painted: its row scrolled into view and
  // marked, with the question in the row.
  // ---------------------------------------------------------------------------
  const PICKER_SURFACE = "wiring";
  let wanted = null;

  const routeToOutput = (partId) => {
    wanted = partId;
    window.location.hash = PICKER_SURFACE;
  };

  // ---------------------------------------------------------------------------
  // The segmented control a small set of peers is picked with on a Part's row
  // of the table below: which Output the Part is on, and which servo, or which
  // Light Type, is on its wire. Both bars are this one control, so the two
  // cannot come to draw a choice differently. (It once drew Wiring's Output
  // plates and Servos' servo pick too, as data/output_settings.js; both went
  // when Wiring became part-first, #411.)
  // ---------------------------------------------------------------------------
  /**
   * A joined bar of radio buttons, one per option, the current one lit. A
   * press on another option is handed to `onPick`; a press on the one already
   * picked asks nothing, so it never sends a save that changes nothing.
   *
   * An option the builder cannot pick is `disabled`: it stays on the bar, so
   * the bar still shows every peer, and a press on it asks nothing. The
   * refusal is here as well as on the button, because the request it would
   * send is one the droid refuses.
   *
   * @param {string} label - what the group picks, for a screen reader
   * @param {object[]} options
   * @param {string} options[].id
   * @param {string} options[].label
   * @param {boolean} [options[].disabled] - shown, and cannot be picked
   * @param {string} [options[].className] - a mark the surface styles
   * @param {string} [options[].name] - what a screen reader calls the option,
   *   where the label alone does not say enough
   * @param {string|null} current - the id picked now, or null for none
   * @param {function} onPick - (id) a different option was pressed
   * @returns {Element}
   */
  const segmented = (label, options, current, onPick) => {
    const row = document.createElement("div");
    row.className = "seg output-seg";
    row.setAttribute("role", "radiogroup");
    row.setAttribute("aria-label", label);
    options.forEach((option) => {
      const on = option.id === current;
      const off = option.disabled === true;
      const button = document.createElement("button");
      const classes = [on ? "active" : "", option.className || ""].filter(Boolean).join(" ");
      if (classes) button.className = classes;
      button.textContent = option.label;
      button.type = "button";
      button.dataset.value = option.id;
      button.setAttribute("role", "radio");
      button.setAttribute("aria-checked", on ? "true" : "false");
      if (option.name) button.setAttribute("aria-label", option.name);
      if (off) {
        button.disabled = true;
        button.setAttribute("aria-disabled", "true");
      }
      button.addEventListener("click", () => {
        if (!on && !off) onPick(option.id);
      });
      row.appendChild(button);
    });
    return row;
  };

  /**
   * The one way a surface puts a Part on an Output, moves it or takes it off:
   * the Output bar, the board's suggestion as one press, and the request
   * behind both, asked first where it takes the Part off another Output
   * (mover()). Wiring's table and Parts' panel and Unused rows each make one,
   * so a Part is picked with the same control, refused the same Outputs and
   * saved by the same request on either surface (operator, 2026-10-04 on
   * #463: "Also pick it on Parts").
   *
   * @param {object} hosts - as mover() takes them, less `reload`: a move is
   *   followed by a read of the Outputs (data/outputs.js refresh()), which
   *   publishes to every surface that paints from it
   * @returns {{bar: function, suggestion: function, put: function, pending: function}}
   */
  const outputChooser = ({ dialog, say, repaint, onSending = repaint }) => {
    const OUTPUTS = window.PAOutputs;
    const move = mover({ dialog, say, reload: () => OUTPUTS.refresh(), repaint, onSending });

    // `control` is the one the builder chose with: focus goes back to it if
    // they leave the Part where it is. The surface may have rebuilt its bar
    // while the question was open (a read of the Outputs lands every second
    // on Parts), so the focus goes to the same choice in the bar now on
    // screen, found by the Part's bar and the Output, in the card the press
    // was made in; the node pressed only where it is still there.
    const put = (partId, address, control = null) => {
      const scope = control?.closest?.(".card") || document;
      const name = control?.closest?.('[role="radiogroup"]')?.getAttribute?.("aria-label") ?? null;
      const again = control && {
        focus: () => {
          if (control.isConnected) return control.focus?.();
          const bar = name === null ? null : Array.from(scope.querySelectorAll('[role="radiogroup"]'))
            .find((each) => each.getAttribute("aria-label") === name);
          const same = bar ? Array.from(bar.querySelectorAll("button")).find((each) => each.dataset.value === address) : null;
          same?.focus?.();
        },
      };
      move.request(moveFor(OUTPUTS.list(), partId, address), again || null);
    };

    // Why an Output on a Part's bar is refused, said under the bar and never
    // as a title (a bench tablet has no hover, docs/ui-copy-voice.md rule 12),
    // or "" where none is. One sentence for every bar, on Wiring's row and on
    // Parts alike.
    const reason = (part, outputs) => {
      const refused = refusedFor(part, outputs, OUTPUTS.forPart(part.id, outputs));
      return refused.length ? `${refused.map((each) => each.name).join(", ")}: ${NO_LIGHT}` : "";
    };

    // The Output a Part is on, as a bar of every Output by what its board
    // prints, the one it is on lit. An Output another Part is on carries a
    // mark, in ink and never a Status Color: two Parts on one wire move
    // together, which is a choice and not a fault. An Output a light cannot go
    // on stays on a light Part's bar, refused and marked so, with the reason
    // under the bar. A bar whose move is on its way takes no press either, and
    // is not marked: it is waiting, and nothing on it is refused. A bar with
    // nothing lit is a choice still to make, and says so in its class
    // (`is-unpicked`), which the surface draws as one.
    const bar = (part, outputs) => {
      const here = OUTPUTS.forPart(part.id, outputs);
      const refused = refusedFor(part, outputs, here);
      const sending = move.pending() === part.id;
      const control = segmented(
        `Output for ${part.name}`,
        outputs.map((output) => {
          const others = output.parts.filter((id) => id !== part.id);
          const usual = output.suggestedPart === part.id;
          const no = refused.includes(output);
          const state = no ? `, ${NO_LIGHT}` : others.length ? `, wired: ${listParts(others)}` : output === here ? "" : `, ${FREE}`;
          return {
            id: output.address,
            label: output.name,
            disabled: sending || no,
            className: [
              others.length ? "is-taken" : "",
              usual ? "is-suggested" : "",
              no ? "is-refused" : "",
            ].filter(Boolean).join(" "),
            name: `${output.name}${state}${usual ? `, ${SUGGESTED}` : ""}`,
          };
        }),
        here ? here.address : null,
        (address) => {
          const pressed = Array.from(control.querySelectorAll("button")).find((each) => each.dataset.value === address);
          put(part.id, address, pressed || null);
        }
      );
      if (!here) control.classList.add("is-unpicked");
      return control;
    };

    // The Output the board usually carries this Part on, while the Part is on
    // none: offered only where it is free and a light may go on it. A Part on
    // no Output takes another one with no question asked (moveFor()
    // `announce`), so a suggestion onto an Output another Part is on would
    // gang the two in one press; the bar still marks it, and putting two
    // Parts on one wire stays a choice made on the bar.
    const suggested = (part, outputs) => {
      if (OUTPUTS.forPart(part.id, outputs)) return null;
      const refused = refusedFor(part, outputs, null);
      return outputs.find((output) =>
        output.suggestedPart === part.id && output.parts.length === 0 && !refused.includes(output)) || null;
    };

    // The suggestion as one press, "Use GPIO 49", or null where there is
    // none. Nothing is picked without it. At the press the droid is read
    // again and the suggestion asked again: an Output another client put a
    // Part on since this was drawn is said and redrawn, never ganged in
    // silence. (The page's own last read is no check: every read repaints
    // the bar, so it always agrees with what was drawn.)
    const suggestion = (part, outputs) => {
      const output = suggested(part, outputs);
      if (!output) return null;
      const act = document.createElement("button");
      act.type = "button";
      act.className = "btn btn-sm btn-quiet parts-use";
      act.dataset.use = output.address;
      act.textContent = `Use ${output.name}`;
      act.setAttribute("aria-label", `Use ${output.name} for ${part.name}`);
      act.disabled = move.pending() === part.id;
      // Busy while the droid is read, and marked so rather than disabled: a
      // disabled button drops the focus, and the builder is on this one.
      let reading = false;
      act.addEventListener("click", async () => {
        if (reading) return;
        reading = true;
        act.setAttribute("aria-busy", "true");
        try {
          await OUTPUTS.refresh();
        } catch (error) {
          say(`${partLabel(part.id)} did not move: ${window.PAApi.messageFor(error)}`, "error");
          return;
        } finally {
          reading = false;
          act.removeAttribute("aria-busy");
        }
        if (suggested(part, OUTPUTS.list())?.address !== output.address) {
          say(`${output.name} is not free now. Pick an output for ${partLabel(part.id)}.`, "warning");
          repaint();
          return;
        }
        put(part.id, output.address, act);
      });
      return act;
    };

    return Object.freeze({ bar, reason, suggestion, put, pending: () => move.pending() });
  };

  /**
   * The parts wiring table: Wiring's one table of what is on which wire
   * (operator, 2026-10-01 on #463, the pick of three drawn options). Each row
   * is a Part, the Output it is on, and on the same row what is on that wire -
   * its servo, or its Light Type for a light Part (operator, 2026-09-29 on
   * #411: "define and wire a body part/panel to a output (GPIO) and then for
   * each you then define what servo type, same way for light on a part"). An
   * Output with a Part on it is wired, and the droid writes its wired tick
   * with the move (docs/api.md, `movePart`).
   *
   * EVERY WIRE IS IN IT, AND NOTHING ELSE ON THE PAGE SAYS USED OR FREE. Its
   * groups, in one table:
   *   Board outputs   a row per Part on an Output, then the free Outputs by
   *                   what the board prints, then the Parts on no Output yet
   *                   as pills to add
   *   Links           the Board Lanes, then each fitted product no lane
   *                   carries, read only, as the caller hands them over
   *                   (`links`)
   *   Dome Controller the dome's Parts, collapsed, each with the command the
   *                   dome is sent for it (domeCommandText())
   *
   * NO PART IS EVER MISSING FROM IT. A body Part is a row while it is on an
   * Output and a pill while it is not, so a fresh droid shows every one as a
   * pill; hiding one is how an operator loses an output (#296). A pill pressed
   * becomes a row with no Output lit, which is kept on this page only: nothing
   * is sent until an Output is picked. Such a row says it waits for a pick, in
   * the row, and offers the board's suggestion as one press.
   *
   * A dome Part gets no Output to choose (isDomePart()). One a builder recorded
   * on a body Output anyway is a row of the board's group all the same, naming
   * the Output and offering only to take it off, so a wire with a Part on it is
   * never a wired count with no row under it.
   *
   * A product's wiring card opens on a row under the row it belongs to, where
   * the caller has one (`cards`, #458).
   *
   * The table is rebuilt only when what it shows has changed, and the control
   * the builder has hold of is found again in the new one. A read that changed
   * nothing - a Find by Moving run reads once a second - touches no node.
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
   * @param {function} [hosts.links] - the links' rows, each
   *   { key, name, output, note, wire, fitted, product, route }: `product` is
   *   { id, name } where a wiring card exists for it, `route` the { href,
   *   label } of where the link is switched
   * @param {object} [hosts.cards] - the product wiring cards, where the image
   *   carries them: `board()` the { id, word } acts of the board's own group,
   *   `html(id)` one product's card
   * @returns {{repaint: function}|null} the mounted table, or null
   */
  const picker = ({ table, summary, feedback, dialog, timing, find, links = () => [], cards = null } = {}) => {
    const OUTPUTS = window.PAOutputs;
    const TIMING = window.PAApplyTiming;
    if (!table || !summary || !feedback || !dialog || !timing || !OUTPUTS || !TIMING) return null;
    const esc = (value) => window.PAUtils.escapeHtml(String(value));
    const say = (text, level) => window.PAUtils.showFeedback(feedback, text, level);
    const answered = () => OUTPUTS.known().table;
    const outputOf = (partId) => OUTPUTS.forPart(partId);

    if (!partById.size) {
      // A table with no rows reads as a droid with no parts. Say what broke.
      summary.textContent = "The parts list did not load, so there is nothing to show. Reload the page to try again.";
      console.error("[parts] window.DroidParts is missing; /droid_parts.js did not load");
      return null;
    }

    // Until the table answers, the summary says so in the one word for it
    // (data/outputs.js live()), which a slot shows as the waiting dots.
    summary.classList.add("waiting");
    summary.textContent = window.PALiveReading.slotText(OUTPUTS.live(null).word);

    const COLUMNS = 4;
    const body = catalog.parts.filter((part) => !isDomePart(part));
    const dome = catalog.parts.filter(isDomePart);

    // What this page holds and the droid does not: the Parts a builder added
    // to the table and has not yet put on an Output, which cards are open,
    // whether the dome's group is, and the row a builder arrived for from
    // another surface (claimWanted()). All four are gone on a reload.
    const added = [];
    const cardsOpen = new Set();
    let domeOpen = false;
    // { part, at }: `at` is the Output the Part was on when it was marked,
    // and the mark goes once that changes - the Part taken off, or moved.
    let marked = null;

    // A row's act is a quiet word at row scale, never a box on every row.
    const actHtml = (act, word, attrs = "") =>
      `<button class="btn btn-sm btn-quiet parts-act" type="button" data-act="${act}"${attrs}>${esc(word)}</button>`;
    // An act that changes the droid shows its icon alone (#460); the card and
    // the dome's group above are disclosures and keep their words.
    const iconActHtml = (icon, act, word) =>
      `<button class="btn btn-sm btn-quiet parts-act icon-act" type="button" data-act="${act}">${window.PAUi.actFace(icon, word)}</button>`;

    // A card opens as a row of the table it belongs to, under the row whose
    // act opened it, and the act says whether it is open.
    const cardAct = (product, word) =>
      cards && product
        ? actHtml("card", word, ` data-product="${esc(product.id)}" aria-expanded="${cardsOpen.has(product.id)}"`)
        : "";
    const cardRow = (product) =>
      cards && product && cardsOpen.has(product.id)
        ? `<tr class="parts-card-row" data-card="${esc(product.id)}"><td colspan="${COLUMNS}">${cards.html(product.id)}</td></tr>`
        : "";

    const groupHead = (label, count, acts = "") =>
      `<tr class="parts-group"><th colspan="${COLUMNS}" scope="colgroup"><span class="parts-group-line">` +
      `<span>${esc(label)}</span><span class="parts-group-count">${esc(count)}${acts}</span></span></th></tr>`;

    const nameHtml = (part) => {
      const shorthand = part.shorthand ? `<span class="parts-shorthand">${esc(part.shorthand)}</span>` : "";
      const light = kinds?.isLight(part) ? `<span class="parts-kind">light</span>` : "";
      return `<span class="parts-name">${esc(part.name)}</span>${shorthand}${light}`;
    };
    const kindClass = (part) => {
      const kind = kinds ? kinds.treatmentClass(part) : "";
      return kind ? ` ${kind}` : "";
    };

    // The Parts with a row: every one on an Output, in catalog order, then the
    // ones added and not yet on one, in the order they were added.
    const rowParts = () => [
      ...catalog.parts.filter((part) => outputOf(part.id) !== null),
      ...added.map((id) => partById.get(id)).filter((part) => part && outputOf(part.id) === null),
    ];

    // Why a choice is off, under the bar: the chooser's one sentence for it.
    const whyHtml = (part, outputs) => {
      const why = chooser.reason(part, outputs);
      return why ? `<span class="why">${esc(why)}</span>` : "";
    };

    // The row's own prompt: a Part on no Output waits for a pick, and the
    // board's suggestion is one press beside the words (filled by
    // fillRows()); a Part a builder arrived for because it is off the droid
    // asks about its wire. In the row, where the builder is looking.
    const PICK = "Pick an output";
    const OFF_TOO = "Take it off if the wire came off too";
    const promptHtml = (part, output) => {
      const asked = marked !== null && marked.part === part.id && output;
      if (!asked && output) return "";
      return `<span class="parts-pick" data-pick><span class="parts-pick-word">${asked ? OFF_TOO : PICK}</span></span>`;
    };

    const partRowHtml = (part, outputs) => {
      const output = outputOf(part.id);
      const gang = output ? output.parts.filter((other) => other !== part.id) : [];
      // The Dome Controller's Part, recorded on this board anyway: it is named
      // with its Output and can be taken off, and is offered no other Output.
      const chosen = isDomePart(part) && output
        ? `<span class="parts-out">${esc(output.name)}</span>`
        : `<span class="parts-bar" data-bar="output"></span>${promptHtml(part, output)}${whyHtml(part, outputs)}`;
      const mark = marked !== null && marked.part === part.id ? " is-marked" : "";
      return (
        `<tr class="parts-row${output ? " is-wired" : ""}${mark}${kindClass(part)}" data-part="${esc(part.id)}">` +
        `<th scope="row">${nameHtml(part)}` +
        (gang.length ? `<span class="parts-gang">moves with ${esc(listParts(gang))}</span>` : "") +
        `</th>` +
        `<td class="parts-on">${chosen}</td>` +
        `<td class="parts-carries"></td>` +
        `<td class="parts-acts">${output ? iconActHtml("link-variant-off", "off", "take off") : iconActHtml("delete-outline", "off", "remove")}</td></tr>`
      );
    };

    // The free Outputs, by what the board prints, and which of them a light
    // can go on as well as a servo.
    const freeRowHtml = (outputs) => {
      const free = outputs.filter((output) => output.parts.length === 0);
      return (
        `<tr class="parts-foot" data-foot="free"><th scope="row">${FREE}</th><td colspan="${COLUMNS - 1}">` +
        `<span class="parts-free-list">` +
        (free.length
          ? free.map((output) =>
            `<span data-free="${esc(output.address)}"><span class="parts-out">${esc(output.name)}</span>` +
            (output.canLight ? ` <span class="parts-word">servo or light</span>` : "") + `</span>`).join("")
          : `<span class="parts-word">none</span>`) +
        `</span></td></tr>`
      );
    };

    // The body Parts on no Output and not yet added, as wrapping pills in the
    // groups a builder thinks of them in. A pill pressed is a row.
    const addRowHtml = () => {
      const waiting = body.filter((part) => outputOf(part.id) === null && !added.includes(part.id));
      if (!waiting.length) return "";
      return (
        `<tr class="parts-foot" data-foot="add"><th scope="row">add a part</th><td colspan="${COLUMNS - 1}">` +
        groupParts(waiting).map((group) =>
          `<div class="part-pills-group"><span class="part-pills-name">${esc(group.label)}</span><span class="part-pills">` +
          group.parts.map((part) =>
            `<button class="part-pill" type="button" data-act="add" data-part="${esc(part.id)}">${esc(part.name)}</button>`).join("") +
          `</span></div>`).join("") +
        `</td></tr>`
      );
    };

    const boardHtml = (outputs) => {
      const counts = wiredCounts(outputs);
      const heads = cards ? cards.board() : [];
      const parts = rowParts();
      return (
        `<tbody data-group="board-outputs">` +
        groupHead(TABLE.board, `${counts.wired} wired · ${counts.free} ${FREE}`,
          heads.map((product) => cardAct(product, product.word)).join("")) +
        heads.map(cardRow).join("") +
        (parts.length
          ? parts.map((part) => partRowHtml(part, outputs)).join("")
          : `<tr class="parts-row parts-empty"><td colspan="${COLUMNS}"><span class="parts-word">no part on an output yet</span></td></tr>`) +
        freeRowHtml(outputs) +
        addRowHtml() +
        `</tbody>`
      );
    };

    // A link: read only here, because it is switched in Configuration. Every
    // word on it is the caller's, which lists the same link on the sheet.
    const linkRowHtml = (link) =>
      `<tr class="parts-row parts-link-row${link.fitted ? " is-wired" : ""}" data-link="${esc(link.key)}">` +
      `<th scope="row"><span class="parts-name">${esc(link.name)}</span>` +
      (link.product ? `<span class="parts-detail">${esc(link.product.name)}</span>` : "") + `</th>` +
      `<td><span class="parts-out">${esc(link.output)}</span>` +
      (link.note ? `<span class="parts-detail">${esc(link.note)}</span>` : "") + `</td>` +
      `<td><span class="${link.fitted ? "parts-detail" : "parts-word"}">${esc(link.wire)}</span></td>` +
      `<td class="parts-acts">${cardAct(link.product, "wiring card")}` +
      `<a class="btn btn-sm btn-quiet link-btn parts-act" href="${esc(link.route.href)}">${esc(link.route.label)}</a></td></tr>` +
      cardRow(link.product);

    const linksHtml = (rows) => {
      if (!rows.length) return "";
      const fitted = rows.filter((link) => link.fitted).length;
      // "fitted", not "wired": a radio is one of them, and it is on no wire.
      const count = `${fitted} fitted${rows.length - fitted ? ` · ${rows.length - fitted} not fitted` : ""}`;
      return `<tbody data-group="links">${groupHead(TABLE.links, count)}${rows.map(linkRowHtml).join("")}</tbody>`;
    };

    // A dome Part's row: its command across the columns an Output and its wire
    // would take, and no control at all. It keeps its Part Kind's treatment, a
    // light's included.
    const domeRowHtml = (part) =>
      `<tr class="parts-row parts-dome-row${kindClass(part)}" data-dome-part="${esc(part.id)}">` +
      `<th scope="row">${nameHtml(part)}</th>` +
      `<td class="parts-command" colspan="${COLUMNS - 1}">${esc(domeCommandText(part))}</td></tr>`;

    const domeHtml = () =>
      dome.length
        ? `<tbody data-group="dome-controller">` +
          groupHead(TABLE.dome, countOf(dome.length, ["part", "parts"]),
            actHtml("dome", domeOpen ? "hide" : "show", ` aria-expanded="${domeOpen}"`)) +
          (domeOpen ? dome.map(domeRowHtml).join("") : "") +
          `</tbody>`
        : "";

    const tableHtml = (outputs) =>
      `<table class="parts-table parts-wiring"><thead><tr>` +
      TABLE.columns.map((column) => `<th scope="col">${esc(column)}</th>`).join("") +
      `<th scope="col"></th></tr></thead>` +
      boardHtml(outputs) + linksHtml(links()) + domeHtml() + `</table>`;

    // Declared before the chooser and the finder, which are handed a way to
    // repaint.
    let paint = () => {};
    // The bar, the suggestion and the request, the same ones Parts makes
    // (outputChooser()). A row's bar takes no second press while its move is
    // on its way, so a move on its way repaints.
    const chooser = outputChooser({ dialog, say, repaint: () => paint() });

    // Find by Moving, run from a Part's row (data/find_by_moving.js). Its That
    // one is the same move this row's bar makes, question and all. Absent
    // where the page carries no run, and then no row offers one.
    const finder = find && window.PAFindByMoving
      ? window.PAFindByMoving.runner({
        panel: find,
        say,
        move: (partId, address) => chooser.put(partId, address),
        pending: () => chooser.pending(),
        changed: () => paint(),
        surface: "Wiring",
      })
      : null;

    // A Part on no Output has no wire yet, and its row offers to find the one
    // it is on by moving the free Outputs (data/find_by_moving.js; operator,
    // 2026-09-30 on #411: "Pulse free Outputs in a run"). Refused while the
    // estop is latched or the droid is out of reach, and while a run is going.
    const findAct = (part) => {
      const act = document.createElement("button");
      act.type = "button";
      act.className = "btn btn-sm btn-quiet parts-find-act icon-act act-keeps-words";
      act.dataset.find = part.id;
      // The magnifying glass (operator, 2026-09-30 on #411), an act that shows
      // its icon alone (#460): its words are its name and its tooltip.
      act.innerHTML = window.PAUi.actFace("magnify", "find by moving");
      window.PAApi.gateControls([act], finder.live() && finder.running() === null);
      return act;
    };

    // What is on the wire a Part is on, on the Part's own row: which servo
    // for a servo Part, which Light Type for a light Part. It is the Output's
    // answer (GLOSSARY.md "Output"), so two Parts ganged on one wire show the
    // same pick, and a pick saves the Output's row.
    const typeBar = (part, output) => {
      const light = Boolean(kinds?.isLight(part));
      const options = light ? OUTPUTS.LIGHT_TYPES : OUTPUTS.SERVO_MODELS;
      const current = light ? output.light?.id ?? null : output.servo?.id ?? null;
      return segmented(`${part.name} ${light ? "light type" : "servo"}`, options, current,
        (value) => carry(output, value));
    };

    // The controls of the rows just written: each Part's Output bar, and what
    // is on its wire or the act that finds it.
    const fillRows = (outputs) => {
      Array.from(table.querySelectorAll("[data-part]")).forEach((node) => {
        // The add pills carry data-part too; only a row is filled.
        if (!node.classList.contains("parts-row")) return;
        const part = partById.get(node.dataset.part);
        const output = outputOf(part.id);
        node.querySelector("[data-bar]")?.replaceChildren(chooser.bar(part, outputs));
        const use = output ? null : chooser.suggestion(part, outputs);
        if (use) node.querySelector("[data-pick]")?.appendChild(use);
        const carries = node.querySelector(".parts-carries");
        // A dome Part on a body Output is offered take off and nothing else:
        // what is on that wire is not this board's to say either.
        if (output && !isDomePart(part)) carries.replaceChildren(typeBar(part, output));
        else if (!output && finder !== null) carries.replaceChildren(findAct(part));
      });
    };

    // Which control a node is, in words that survive a rebuild: its row or
    // its group, what kind of control, and the choice it stands for.
    const controlKey = (node) => {
      if (!node || !node.closest?.(".parts-table")) return null;
      const row = node.closest("tr");
      const where = row?.dataset.part || row?.dataset.link || node.closest("tbody")?.dataset.group || "";
      const kind = node.dataset.act || (node.dataset.find ? "find" : node.dataset.use ? "use" : node.closest("[data-bar]") ? "output" : "wire");
      return `${where}|${kind}|${node.dataset.value || node.dataset.product || ""}`;
    };
    // Hands the focus back to the control a rebuild took it from. `true` only
    // where that control is still there and cannot take it yet. A control
    // that is gone either went with its row - its Part was taken off, or
    // removed, and the Part is a pill again - or went from a row that stays:
    // a suggestion pressed, whose Part is now on that Output. The focus goes
    // to the pill, else to the Output the row has lit, so it never falls off
    // the table.
    const focusControl = (key) => {
      const buttons = Array.from(table.querySelectorAll("button"));
      const control = buttons.find((each) => controlKey(each) === key);
      if (control && control.disabled) return true;
      const partId = key.split("|")[0];
      (control ||
        buttons.find((each) => each.dataset.act === "add" && each.dataset.part === partId) ||
        rowOf(partId)?.querySelector("[data-bar] button.active"))?.focus?.();
      return false;
    };

    // Everything the filled controls are drawn from that the markup does not
    // spell out: each Output's row as a bar shows it, the move on its way, and
    // whether a run may start.
    const stateOf = (outputs) =>
      outputs.map((output) =>
        [output.address, output.name, output.parts.join("+"), output.canLight, output.suggestedPart, output.type].join(":")).join(";") +
      `|${chooser.pending()}|${finder === null ? "" : `${finder.live()}:${finder.running() !== null}`}`;

    let drawn = null;
    // The control a rebuild took the focus from and could not hand it back
    // to: a bar takes no press while its move is on its way, and a disabled
    // control cannot hold the focus. It is owed the focus at the next
    // rebuild, unless the builder has put the focus somewhere since, and
    // nothing is owed to a control that is no longer in the table.
    let owed = null;
    const draw = (outputs) => {
      // A Part that has landed on an Output is the droid's now; taken off
      // again, it goes back among the pills.
      added.splice(0, added.length, ...added.filter((id) => outputOf(id) === null));
      if (marked !== null && (outputOf(marked.part)?.address ?? null) !== marked.at) marked = null;
      const html = tableHtml(outputs);
      const state = `${html}|${stateOf(outputs)}`;
      if (state === drawn) return;
      const active = document.activeElement;
      const nowhere = !active || active === document.body;
      const held = controlKey(active) ?? (nowhere ? owed : null);
      drawn = state;
      table.innerHTML = html;
      fillRows(outputs);
      owed = held !== null && focusControl(held) ? held : null;
    };

    const rowOf = (partId) =>
      Array.from(table.querySelectorAll("[data-part]")).find((node) =>
        node.dataset.part === partId && node.classList.contains("parts-row")) || null;

    // The first control of a Part's row a builder can press: the Output it is
    // on, else the first it may go on.
    const focusRow = (partId) => {
      const row = rowOf(partId);
      if (!row) return;
      row.scrollIntoView?.({ block: "center" });
      const buttons = Array.from(row.querySelectorAll("button")).filter((each) => !each.disabled);
      (buttons.find((each) => each.classList.contains("active")) || buttons[0])?.focus?.();
    };

    table.addEventListener("click", (event) => {
      const starter = event.target?.closest?.("[data-find]");
      if (starter && !starter.disabled && finder !== null) {
        finder.start(starter.dataset.find);
        return;
      }
      const act = event.target?.closest?.("[data-act]");
      if (!act || act.disabled || !answered()) return;
      const partId = act.dataset.part || act.closest("[data-part]")?.dataset.part || null;
      if (act.dataset.act === "add") {
        added.push(partId);
        paint();
        focusRow(partId);
      } else if (act.dataset.act === "off") {
        // Off an Output is a move to none, sent as one; off this page's own
        // list sends nothing, since the droid never held it.
        if (outputOf(partId)) {
          chooser.put(partId, NO_OUTPUT, act);
        } else if (added.includes(partId)) {
          added.splice(added.indexOf(partId), 1);
          paint();
        }
      } else if (act.dataset.act === "card") {
        const id = act.dataset.product;
        if (!cardsOpen.delete(id)) cardsOpen.add(id);
        paint();
      } else if (act.dataset.act === "dome") {
        domeOpen = !domeOpen;
        paint();
      }
    });

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

    // A Part handed over by routeToOutput(), taken once the table is on
    // screen with the droid's answer in it: before then a focus on a control
    // that is not showing lands nowhere. The route is taken for a Part off
    // the droid that is still on an Output, so its row is there: it is marked
    // and scrolled into view, and its question is in the row (promptHtml()),
    // not on the feedback line under the table. A Part taken off since the
    // route was taken has nothing left to ask, and nothing is marked.
    const claimWanted = (outputs) => {
      if (wanted === null || !answered() || document.body?.dataset?.page !== PICKER_SURFACE) return;
      const part = wanted;
      wanted = null;
      const output = outputOf(part);
      if (!partById.has(part) || output === null) return;
      marked = { part, at: output.address };
      draw(outputs);
      focusRow(part);
    };

    paint = () => {
      if (!answered()) return;
      const outputs = OUTPUTS.list();
      draw(outputs);
      paintTiming(outputs);

      let text = wiredSummary(outputs);
      // A Part on an Output that this page has no row for would otherwise be
      // invisible, which is the one thing this table must never be.
      const unknown = outputs.flatMap((output) => output.parts).filter((id) => !partById.has(id));
      if (unknown.length) {
        text += ` · ${unknown.join(", ")} on an output too, unknown to this page - upload the matching web UI`;
      }
      summary.textContent = text;
      claimWanted(outputs);
    };

    // Every read of the Outputs - this surface's, a move's, a save made on
    // another surface - publishes once, and this is the one place the table
    // paints from it.
    OUTPUTS.onChange(() => paint());
    paint();
    // What the caller hands over - the links, the cards - changes
    // without the Outputs changing, and the caller says when.
    return Object.freeze({ repaint: () => paint() });
  };

  window.PAParts = Object.freeze({
    NO_OUTPUT,
    groupParts,
    partById,
    partLabel,
    listParts,
    servoWord,
    hasServoWord,
    isLightRow,
    moveFor,
    announcement,
    mover,
    UNCLAIMED,
    thisImageMoves,
    isDomePart,
    unclaimed,
    offButMapped,
    routeToOutput,
    domeMovesText,
    TABLE,
    FREE,
    wiredCounts,
    wiredSummary,
    segmented,
    outputChooser,
    picker,
  });
})();
