// =============================================================================
// data/parts.js
//
// Parts (ADR 0050, #347, #362): the droid's parts - the droid picture at the
// head of the surface, and the Parts no Output claims under it. Which Output a
// Part is on is picked here too, with the bar Wiring's parts table picks it
// with (operator, 2026-10-04 on #463: "Also pick it on Parts", overturning
// 2026-09-28 on #411's Wiring-only rule): the picture's panel and each Unused
// row carry data/parts_mapping.js outputChooser(), the same bar, the same
// refusals, the same move question and the same request, so Parts and Wiring
// cannot disagree. Wiring stays the full view, with what is on each wire,
// the links and Find by Moving.
//
// No row is ever hidden. The Unused list is every Part this image could move
// that no Output claims, and the Parts it could never move are counted beside
// it rather than dropped (#296).
//
// What each Output is, and which Part is on it, is data/outputs.js's answer
// (#415), and which Parts no Output claims is data/parts_mapping.js's
// (unclaimed()), in the one place that rule lives.
// =============================================================================
(() => {
  const catalog = window.DroidParts;
  const P = window.PAParts;
  const OUTPUTS = window.PAOutputs;

  const { partById, partLabel } = P;

  // Whether the droid has answered with its Outputs yet.
  const answered = () => OUTPUTS.known().table;

  const unusedRegion = document.getElementById("parts-unused");
  const unusedSummary = document.getElementById("parts-unused-summary");
  const feedback = document.getElementById("parts-feedback");
  const unusedFeedback = document.getElementById("parts-unused-feedback");
  const moveDialog = document.getElementById("parts-move-dialog");
  if (!unusedRegion || !unusedSummary) return;

  if (!partById.size) {
    // A list with no rows reads as a droid with every part on an output. Say
    // what broke.
    unusedSummary.textContent = "The parts list did not load, so there is nothing to show. Reload the page to try again.";
    console.error("[parts] window.DroidParts is missing; /droid_parts.js did not load");
    return;
  }

  const esc = (value) => window.PAUtils.escapeHtml(String(value));
  const escAttr = (value) => window.PAUtils.escapeAttr?.(value) ?? esc(value);
  const showFeedback = (text, level) => window.PAUtils.showFeedback(feedback, text, level);
  const plural = (count, [one, many]) => `${count} ${count === 1 ? one : many}`;

  // Until the Outputs answer, the summary says so in the one word for it
  // (data/outputs.js live()), which a slot shows as the waiting dots.
  unusedSummary.classList.add("waiting");
  unusedSummary.textContent = window.PALiveReading.slotText(OUTPUTS.live(null).word);

  // ---------------------------------------------------------------------------
  // The Output bar
  //
  // One chooser for the page (data/parts_mapping.js outputChooser()), so one
  // move is on its way at a time whichever card it was pressed in. It answers
  // on the feedback line of the card the builder last pressed in, which is
  // where they are looking.
  //
  // Every bar here is rebuilt only when what it shows changes - the Outputs'
  // rows as a bar draws them, and the move on its way - and never on a frame
  // of the once-a-second bench feed: a rebuilt button moves out from under a
  // finger. The focus goes back to the same choice in the new bar, or waits
  // for it while it takes no press (a move on its way).
  // ---------------------------------------------------------------------------
  let sayOn = feedback;
  const chooser = moveDialog
    ? P.outputChooser({
      dialog: moveDialog,
      say: (text, level) => window.PAUtils.showFeedback(sayOn || feedback, text, level),
      repaint: () => paint(),
    })
    : null;
  if (!chooser) console.error("[parts] #parts-move-dialog is missing; no Output bar is offered on Parts");

  const barState = (outputs) =>
    outputs.map((output) => [output.address, output.name, output.parts.join("+"), output.canLight, output.suggestedPart].join(":")).join(";") +
    `|${chooser ? chooser.pending() : ""}`;

  // A choice in words that survive a rebuild of its bar.
  const choiceKey = (node) => (node?.dataset?.value ? `output:${node.dataset.value}` : node?.dataset?.use ? "use" : null);
  const owed = new Map();
  const rebuildHolding = (host, where, build) => {
    const active = document.activeElement;
    const inside = Boolean(active && host.contains(active));
    const key = inside ? `${where}|${choiceKey(active)}` : (!active || active === document.body) ? owed.get(host) ?? null : null;
    build();
    owed.delete(host);
    if (key === null) return;
    const [at, choice] = [key.slice(0, key.lastIndexOf("|")), key.slice(key.lastIndexOf("|") + 1)];
    if (at !== where) return;
    const buttons = Array.from(host.querySelectorAll("button"));
    const again = buttons.find((each) => choiceKey(each) === choice);
    if (again && again.disabled) {
      owed.set(host, key);
      return;
    }
    (again || buttons.find((each) => each.classList.contains("active")))?.focus?.();
  };

  // A Part's bar, and under it, while the Part is on no Output, the words
  // that say a pick is waiting and the board's suggestion as one press.
  const barFor = (part, outputs, { prompt = true } = {}) => {
    const holder = document.createDocumentFragment();
    holder.appendChild(chooser.bar(part, outputs));
    if (window.PAOutputs.forPart(part.id, outputs)) return holder;
    const use = chooser.suggestion(part, outputs);
    if (!prompt && !use) return holder;
    const line = document.createElement("span");
    line.className = "parts-pick";
    if (prompt) {
      const word = document.createElement("span");
      word.className = "parts-pick-word";
      word.textContent = "Pick an output";
      line.appendChild(word);
    }
    if (use) line.appendChild(use);
    holder.appendChild(line);
    return holder;
  };

  // ---------------------------------------------------------------------------
  // Unused: the Parts no Output claims
  //
  // Moved here from Wiring (operator, 2026-09-28 on #411: "the 'unused'
  // section there makes more sense to have in the parts page"), with its
  // Availability Reason on each row so a reader does not re-derive it. Each
  // row acts rather than pointing: its Output is picked on the row, with the
  // bar above.
  // ---------------------------------------------------------------------------
  // A part's design name, and the three things that field can say. `cadName`
  // absent is a name nobody has read out of the design files yet; `cadName`
  // null is a part the design does not carry at all, which is what marks a
  // Common Addition (data/droid_parts.js). This list bridges the two naming
  // systems (CONTEXT.md "Part"), so neither case prints as a blank cell a
  // builder would read as a missing row.
  const designNameHtml = (part) => {
    if (typeof part.cadName === "string" && part.cadName !== "") {
      return `<span class="parts-unused-cad">${esc(part.cadName)}</span>`;
    }
    if (part.cadName === null) {
      return '<span class="parts-unused-dim">a common addition, no design name</span>';
    }
    return '<span class="parts-unused-dim">not read out of the design files yet</span>';
  };

  const whereHtml = (part) => {
    const position = part.position ? esc(part.position) : "";
    const bearing = typeof part.bearingDeg === "number" ? `${part.bearingDeg}&deg;` : "";
    if (position && bearing) return `${position} · ${bearing}`;
    return position || bearing || '<span class="parts-unused-dim">wherever you wired it</span>';
  };

  const partNameHtml = (part) => {
    const shorthand = part.shorthand
      ? ` <span class="parts-unused-shorthand">${esc(part.shorthand)}</span>`
      : "";
    const kind = part.kind ? ` <span class="parts-unused-kind">${esc(part.kind)}</span>` : "";
    return `${esc(part.name)}${shorthand}${kind}`;
  };

  // A part, its design name, where it sits on the droid, and the Output bar
  // it is given one with (filled by fillUnusedBars()).
  const unusedTableHtml = (parts) =>
    '<table class="parts-unused-table"><thead><tr>' +
    '<th scope="col">Part</th><th scope="col">Design name</th><th scope="col">Where</th><th scope="col">Output</th>' +
    "</tr></thead><tbody>" +
    parts
      .map(
        (part) =>
          `<tr class="parts-unused-row" data-tier="${P.UNCLAIMED}" data-part="${escAttr(part.id)}">` +
          `<th scope="row">${partNameHtml(part)}</th>` +
          `<td>${designNameHtml(part)}</td>` +
          `<td>${whereHtml(part)}</td>` +
          `<td class="parts-unused-output"><span class="parts-bar" data-bar-for="${escAttr(part.id)}"></span></td></tr>`
      )
      .join("") +
    "</tbody></table>";

  // The bound, said out loud: the Parts this image never moves are named and
  // counted rather than filtered away in silence. It carries no act, because
  // there is nothing for a builder to do about it - a settled no (CONTEXT.md
  // "Availability Family").
  const boundHtml = () => {
    const outside = catalog.parts.filter((part) => !P.thisImageMoves(part));
    if (outside.length === 0) return "";
    const domeLink = outside.filter((part) => part.control === "dome-link").length;
    const noPath = outside.length - domeLink;
    const clauses = [];
    if (domeLink > 0) clauses.push(`${plural(domeLink, ["part", "parts"])} the Dome Controller moves over the dome link`);
    if (noPath > 0) clauses.push(`${plural(noPath, ["part", "parts"])} nothing on this droid moves at all`);
    return `<p class="hint parts-unused-bound">Not in this list: ${clauses.join(" and ")}. This image sends them no signal.</p>`;
  };

  // What the list means, and - when any row carries one - what a design name
  // is. A line for a column the list does not show is never written.
  const footnoteHtml = (parts) => {
    if (parts.length === 0) return "";
    const lines = ["<li><b>Unused</b>: no output claims the part. Moves you author for it wait until one does.</li>"];
    if (parts.some((part) => typeof part.cadName === "string" && part.cadName !== "")) {
      lines.push(
        "<li><b>Design name</b> is the part's name in the files you printed it from, the " +
          "same one your slicer shows. Label the wire with it.</li>"
      );
    }
    return `<ul class="parts-unused-footnote">${lines.join("")}</ul>`;
  };

  // Each row's bar, written into the cell the row left for it. No pick
  // prompt on these rows: every one of them is waiting for an Output, and
  // the column says so once. A dome Part is offered no body Output
  // (PAParts.isDomePart()): its cell says what moves it instead.
  const fillUnusedBars = (outputs) => {
    if (!chooser) return;
    unusedRegion.querySelectorAll("[data-bar-for]").forEach((cell) => {
      const part = partById.get(cell.dataset.barFor);
      if (!part) return;
      if (P.isDomePart(part)) {
        if (!cell.childNodes.length) cell.innerHTML = '<span class="parts-word">the Dome Controller moves it</span>';
        return;
      }
      rebuildHolding(cell, part.id, () => cell.replaceChildren(barFor(part, outputs, { prompt: false })));
    });
  };

  // Rebuilt only when the list itself changes, and its bars only when what
  // they show does. The Outputs are read once a second here (the bench feed
  // below), and a list rebuilt on every read would take the bar out from
  // under a builder's pointer and focus.
  let unusedKey = null;
  let unusedBars = null;
  const paintUnused = () => {
    const outputs = OUTPUTS.list();
    const unused = P.unclaimed(catalog.parts, outputs);
    unusedSummary.textContent = plural(unused.length, ["part", "parts"]);
    const key = unused.map((part) => part.id).join(",");
    if (key !== unusedKey) {
      // The row the builder was on - focused, or owed the focus while its
      // move was on its way - by its place in the list. A Part given an
      // Output leaves the list, and the focus goes to the row that takes
      // its place rather than falling off the page.
      const rows = Array.from(unusedRegion.querySelectorAll("tr[data-part]"));
      const at = rows.findIndex((row) =>
        row.contains(document.activeElement) || Array.from(owed.keys()).some((host) => row.contains(host)));
      unusedKey = key;
      unusedBars = null;
      unusedRegion.innerHTML =
        (unused.length ? unusedTableHtml(unused) : '<p class="hint">Every part is on an output.</p>') +
        boundHtml() +
        footnoteHtml(unused);
      Array.from(owed.keys()).forEach((host) => {
        if (!host.isConnected) owed.delete(host);
      });
      if (at !== -1) {
        fillUnusedBars(outputs);
        unusedBars = barState(outputs);
        const next = Array.from(unusedRegion.querySelectorAll("tr[data-part]"));
        const row = next[Math.min(at, next.length - 1)];
        const target = row ? Array.from(row.querySelectorAll("button")).find((each) => !each.disabled) : null;
        if (target) target.focus();
        else {
          unusedSummary.tabIndex = -1;
          unusedSummary.focus();
        }
      }
    }
    const bars = barState(outputs);
    if (bars === unusedBars) return;
    unusedBars = bars;
    fillUnusedBars(outputs);
  };

  // Captured, so the line is chosen before the press itself answers on it.
  unusedRegion.addEventListener("click", () => {
    sayOn = unusedFeedback || feedback;
  }, true);

  const paint = () => {
    if (!answered()) return;
    paintUnused();
    // And the picture at the head of the surface, from the same one answer the
    // list was just painted from: a body view that read the droid on its own
    // clock could show a part open while the list below it said otherwise.
    paintBody();
  };

  // The servo table alone: this page shows nothing the config answers. The
  // read publishes, and the page paints from that (below), once.
  const loadOutputs = async ({ handle = null } = {}) => {
    await OUTPUTS.refresh({ handle });
  };

  // Every act here is started from a click handler and finishes later, so
  // nothing awaits it; a rejection nobody handles is a control that did nothing
  // and said nothing, so this refuses to be silent about it.
  const started = (promise) =>
    promise?.catch?.((error) => {
      console.error("[parts] an act failed:", error);
      showFeedback(`Something went wrong on this page: ${error && error.message ? error.message : error}`, "error");
    });

  // The Live Reading's three-valued estop (data/live_reading.js): "latched",
  // "clear", or "waiting" until the droid has said and whenever contact
  // with it is lost. It gates the picture's Open it.
  let estop = "waiting";
  window.PALiveReading.subscribe((reading) => {
    estop = reading.estop;
  });

  // ---------------------------------------------------------------------------
  // The droid picture (#352, #372, ADR 0063 as amended 2026-09-19)
  //
  // A picture of the droid at the head of this surface, showing many parts at
  // once - the one thing a list cannot do, however honest each row is. One card, three faces: Front, Rear and Dome (Top).
  // data/body_view.js draws it and this file is its caller, and the seam
  // between the two is the whole design: the renderer reports "this marker was
  // picked" and knows nothing else, while everything about what a pick MEANS
  // lives here, with the Outputs the droid answered.
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
  //   The Output bar      not an act but the panel's slot: a body Part's
  //                       Output, picked, moved or given the board's
  //                       suggestion in one press, with the bar Wiring's
  //                       table carries (outputChooser(); operator,
  //                       2026-10-04 on #463: "Also pick it on Parts"). One
  //                       bar, one question and one request on both surfaces,
  //                       so neither can disagree with the other. A dome
  //                       Part's slot says the Dome Controller moves it.
  //   Take it off on      a Part off the droid that still has an Output
  //   Wiring              mapped: whether its wire came off too is asked on
  //                       its row on Wiring, which the route marks
  //                       (PAParts.routeToOutput()).
  //
  // No Non-RC Control consent is asked for any of them - that flag has never
  // reached POST /api/servo (ADR 0064).
  // ---------------------------------------------------------------------------
  const view = window.BodyView;
  const drawingHost = document.getElementById("bodyview-drawing");
  const railHost = document.getElementById("bodyview-panel");

  const ACTS = [
    // Open it moves the Part, and the panel says what it is but not what the
    // press does, so its words stay in view (#460).
    { id: "toggle", label: "Open it", icon: "arrow-expand-horizontal", keepsWords: true },
    { id: "fit", label: "Drop from build", icon: "delete-outline" },
    { id: "wire", label: "Take it off on Wiring", icon: "link-variant-off" },
  ];

  let drawing = null;
  let panel = null;
  // What a pick means - its mark, whether it may open, and the one request
  // that opens it - is data/droid_picture.js's, shared with the Dashboard's
  // Moving parts card so the two cannot disagree. Made once the drawing is.
  let picture = null;

  // Everything the panel says about a pick, and the one sentence that says why
  // an act cannot run. Every refusal names the next move.
  const describePick = (markerId) => {
    const decision = picture.decide(markerId, estop);
    const { marker, mark, cls, output, isFitted, unwired, offButMapped } = decision;
    const parts = marker.parts;
    const fitted = picture.fittedNow();

    let servo;
    if (output) servo = `On a servo (${output.name})`;
    else if (marker.target) servo = "On a servo (protoR2link)";
    else if (!picture.answered()) servo = OUTPUTS.live(null).word;
    else servo = "No output mapped";

    const facts = [{ term: "Servo", value: servo }];
    if (cls !== null) {
      // The mark's own words where it has them - a limp Part says why, and a
      // Part with no reading says which kind - and the legend's word otherwise.
      facts.push({ term: "State", value: cls === mark.mark && mark.said ? mark.said : view.LEGEND_TEXT[cls] });
    }
    if (fitted !== null) facts.push({ term: "Fitted", value: isFitted ? "Yes" : "No" });

    const part = partById.get(parts[0]);
    const subtitle =
      marker.half === "dome"
        ? parts.map((id) => partById.get(id)?.shorthand || partLabel(id)).join(" · ")
        : [part?.position, part?.cadName === null ? "Common Addition" : ""].filter(Boolean).join(" · ");

    return {
      ...decision,
      title: marker.label,
      subtitle,
      facts,
      acts: {
        toggle: decision.toggle,
        fit: {
          shown: fitted !== null,
          label: isFitted ? "Drop from build" : "Add to build",
          icon: isFitted ? "delete-outline" : "plus",
          enabled: fitted !== null,
        },
        wire: {
          shown: offButMapped,
          enabled: offButMapped && partById.has(decision.wiredPart),
        },
      },
    };
  };

  // The panel's slot: the picked Part's Output bar, or, for a Part the Dome
  // Controller moves, the sentence that says so. Empty until the droid has
  // answered with its Outputs, and with nothing picked.
  let slotKey = null;
  const paintSlot = (pick) => {
    const slot = panel.slot;
    const marker = pick ? pick.marker : null;
    const ids = marker ? marker.parts.filter((id) => partById.has(id)) : [];
    const dome = ids.length > 0 && ids.every((id) => P.isDomePart(partById.get(id)));
    const part = !dome && ids.length && answered() && chooser ? partById.get(ids[0]) : null;
    const outputs = OUTPUTS.list();
    const key = `${marker ? marker.id : ""}|${dome}|${part ? part.id : ""}|${part ? barState(outputs) : ""}`;
    if (key === slotKey) return;
    slotKey = key;
    rebuildHolding(slot, part ? part.id : "", () => {
      slot.textContent = "";
      if (dome) {
        const line = document.createElement("p");
        line.className = "bodyview-panel-why";
        line.textContent = P.domeMovesText(marker.label);
        slot.appendChild(line);
      } else if (part && P.thisImageMoves(part)) {
        const host = document.createElement("div");
        host.className = "parts-bar";
        host.appendChild(barFor(part, outputs));
        slot.appendChild(host);
      }
    });
  };

  const paintPanel = () => {
    const markerId = drawing.selected();
    if (markerId === null) {
      panel.clear();
      paintSlot(null);
      return;
    }
    const pick = describePick(markerId);
    panel.show(pick);
    paintSlot(pick);
  };

  const paintBody = () => {
    if (drawing === null) return;
    const onPicture = picture.pictureFor();
    // One kind of state at a time. This surface shows the live kind - what the
    // droid was last told. A routine's moment is the same shape through the
    // same renderer and is never mixed into this one.
    drawing.update({
      kind: view.STATE_KINDS.LIVE,
      marks: picture.marks(),
      shown: onPicture.shown,
      fitted: picture.fittedNow(),
      stamp: picture.designLabel("body"),
      domePending: onPicture.domePending,
      domeNote: onPicture.domeNote,
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
    const before = picture.fittedNow();
    return window.DroidBuild.applyDroidBuild({ fitted: nextFitted }).then((result) => {
      if (result.persisted) return;
      return window.DroidBuild.load({ refresh: true }).then((held) => {
        if (held === null) window.DroidBuild.applyDroidBuild({ fitted: before }, { persist: false });
      });
    });
  };

  const addToBuild = (ids, names) => {
    const fitted = picture.fittedNow();
    if (fitted === null) return;
    const missing = ids.filter((id) => fitted.indexOf(id) === -1);
    if (missing.length === 0) return;
    writeFitted(fitted.concat(missing))
      .then(() => {
        const now = picture.fittedNow() || [];
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
    const fitted = picture.fittedNow();
    if (fitted === null) return;
    const leaving = ids.filter((id) => fitted.indexOf(id) !== -1);
    if (leaving.length === 0) return;
    writeFitted(fitted.filter((id) => leaving.indexOf(id) === -1))
      .then(() => {
        const now = picture.fittedNow() || [];
        if (!leaving.every((id) => now.indexOf(id) === -1)) {
          showFeedback(`${names} did not reach the droid, so nothing was dropped.`, "error");
          paintBody();
          return;
        }
        // The Parts that left and that an Output still claims, by the one rule
        // every surface reads that disagreement with (data/parts_mapping.js).
        const mapped = [];
        leaving.forEach((id) => {
          const output = picture.answered() ? P.offButMapped(id, now) : null;
          if (output && mapped.indexOf(output.name) === -1) mapped.push(output.name);
        });
        const off = `${names} ${leaving.length === 1 ? "is" : "are"} off your droid now.`;
        showFeedback(
          mapped.length
            ? `${off} Still mapped to ${mapped.join(", ")}. Take it off on Wiring if the wire came off too.`
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
        const fitted = picture.fittedNow() || [];
        const leaving = group ? group.ids.filter((id) => fitted.indexOf(id) !== -1) : pick.onDroid;
        const whole = group && leaving.length === group.ids.length;
        dropFromBuild(leaving, whole ? group.label : leaving.map(partLabel).join(", "));
      } else {
        addToBuild(pick.unfitted, pick.unfitted.map(partLabel).join(", "));
      }
      return;
    }
    if (actId === "wire") {
      if (pick.offButMapped && partById.has(pick.wiredPart)) P.routeToOutput(pick.wiredPart, { off: true });
      return;
    }
    if (actId !== "toggle" || !pick.acts.toggle.enabled) return;
    started(
      picture.openClose(pick).then((result) => {
        showFeedback(result.text, result.level);
        paintBody();
      })
    );
  };

  if (view && drawingHost && railHost) {
    panel = view.mountPanel(railHost, { acts: ACTS, onAct: runAct });
    panel.slot.addEventListener("click", () => {
      sayOn = feedback;
    }, true);
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
    picture = window.PADroidPicture.caller(drawing);
    // The Fitted Parts, so the picture knows what is on this droid. One read,
    // shared with every other surface that wants the Droid Build
    // (data/droid_build.js holds it single-flight), and the picture draws from
    // whatever the droid has answered so far rather than waiting on it.
    window.DroidBuild?.load()?.then(() => paintBody());
    window.DroidBuild?.onChange?.(() => paintBody());
    paintBody();
    // The estop gates every act on this page, and it arrives on the Live
    // Reading rather than on the bench feed. Subscribed here, below the
    // definitions it calls, because the Live Reading hands a new subscriber
    // the current reading synchronously.
    window.PALiveReading.subscribe(() => paintBody());
  } else if (!view) {
    console.error("[parts] window.BodyView is missing; /body_view.js did not load");
  }

  // ---------------------------------------------------------------------------
  // Loading
  //
  // Every read of the Outputs - this page's, the follow's, a save made on
  // another surface - publishes once, and this is the one place the page
  // paints from it.
  // ---------------------------------------------------------------------------
  OUTPUTS.onChange(() => paint());

  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/droid_parts.js": "parts list",
      "/droid_part_kind.js": "parts list",
      "/dome_bearing.js": "where the dome points",
      "/outputs.js": "the outputs",
      "/parts.js": "the parts",
    });
    window.PABootstrap.registerSection("parts-outputs", loadOutputs, { label: "which part is on each output" });
  } else {
    loadOutputs().catch((error) => console.warn("[parts] outputs unavailable:", error));
  }

  // The bench feed (#318): data/outputs.js's follow of the table repaints the
  // Unused list and the picture - any Part another client or the Console moved, and
  // what every part was last told. It is this surface's, so the shell stops it
  // when the operator leaves Parts and starts it on the way back (#360).
  OUTPUTS.follow().start();
})();
