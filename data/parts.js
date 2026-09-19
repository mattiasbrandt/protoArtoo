// =============================================================================
// data/parts.js
//
// Parts (ADR 0050, #347, #362): every Part on the droid, grouped the way a
// builder thinks about them, and the Output that drives each one; and every
// Output, everything it drives, and where it has been told to be. The two tables
// are the part-first and output-first projections of the one mapping
// GET /api/servo/outputs answers, read once for both, so they cannot disagree.
//
// Four rules shape this file.
//
// No row is ever hidden. A fresh droid shows every catalog Part reading
// "- not wired -", which is the honest state of a build in progress, and hiding
// a row is how an operator loses an output (#296). The groups are disjoint and
// together cover the catalog: a Part no group names lands in a last group
// rather than off the page.
//
// The table is built once and repainted in place. A row here is where live
// controls open (C1c, C1d), and a repaint that rebuilt one under the builder's
// pointer would move a panel, not merely redraw it (r2d2-astromech-simulator
// v1.79.0, src/js/maestro/hw-table.js:171-173). A repaint writes textContent,
// value, disabled and classList on nodes that already exist, and leaves the
// control the builder is holding alone until they let go of it.
//
// A move is announced before it happens, on every surface that can make one
// (#347). Taking a Part off the Output it is on names the Part, the Output it
// leaves, what that Output keeps and what the Part will move with, and asks
// with the verb. The firmware refuses a move that does not name the Output the
// Part is leaving, so a surface cannot skip the question by accident. Both
// tables here make their moves through one request(), and moveFor() and
// announcement() are exported so guided Setup and an import ask it in the same
// words.
//
// Both position marks are COMMANDED (#318, #362). The bar is the width the
// controller has put on the pin and the tick is where the move ends; nothing on
// this droid reads a servo back, so no word on the output-first table may
// present either as where a horn actually is.
//
// An Output is found by moving it (ADR 0050, #363). An unwired Part's row
// carries Find by Moving: the droid nudges each spare Output a little, one at
// a time, and the builder presses "That one" when the Part twitches. The rules
// of that run are with the code, below the move.
//
// A Part is calibrated by driving it (#291, #364, ADR 0064). An Output's row
// carries the dial: the builder drives the part until it looks right and
// presses the button for the end they are setting, and the part KEEPS being
// driven while they look and listen. The rules of that hold are with the code,
// below the run - and the two bounds that end it are the firmware's, not this
// page's.
//
// The whole droid goes back to centre on one press (#318, #365), and THE DROID
// PACES IT. This page sends one request and holds no pace at all: the Sequence
// Coordinator expands it into one Output at a time, no closer together than the
// Cadence Floor, because a safe pace held here is one a hand-edited or imported
// client could walk around. The rules are with the code, below the dial.
// =============================================================================
(() => {
  const catalog = window.DroidParts;
  const kinds = window.DroidPartKind;

  const NOT_WIRED = "– not wired –";
  // The Output Address token a move sends for "no Output" (docs/api.md).
  const NO_OUTPUT = "none";
  // The bench feed (#318). One read of the outputs answer a second repaints both
  // tables - where every Output stands, and any Part another client or the
  // Console moved - and only while Parts is on screen: the shell stops it when
  // the operator leaves (#360). A position rides this rather than /api/events,
  // which carries the estop and is not to be crowded by rows of motion.
  const POLL_MS = 1000;

  // #293's honesty tiers, as the in-use count the output-first table is headed
  // with (#318). The fourth - a Part no Output claims - is the part-first
  // table's to count. A tier is a count, never a place a row moves to: the rows
  // stay in the order the leads plug in, so nothing is regrouped under the
  // builder's pointer while they wire.
  const TIERS = [
    { id: "driving", label: "Driving parts" },
    { id: "switched-off", label: "Wired but switched off" },
    { id: "no-part", label: "Output with no part" },
  ];
  // A firmware older than this page reports no position at all, and that is not
  // the same as an Output with no pulse, so it is never counted as switched off.
  const tierOf = (output) => {
    if (output.parts.length === 0) return "no-part";
    return output.reported && output.commandedUs === null ? "switched-off" : "driving";
  };

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

  const groupHeading = (group) => {
    const [one, many] = group.unit || ["part", "parts"];
    const count = group.parts.length;
    return `${group.label} — ${count} ${count === 1 ? one : many}`;
  };

  const partById = new Map((catalog?.parts || []).map((part) => [part.id, part]));
  const partLabel = (id) => {
    const part = partById.get(id);
    if (!part) return id;
    return part.shorthand ? `${part.name} (${part.shorthand})` : part.name;
  };
  const listParts = (ids) => ids.map(partLabel).join(", ");
  const outputLabel = (output) => output.name || output.address;
  const optionText = (output) =>
    `${outputLabel(output)} · ${output.parts.length ? listParts(output.parts) : "drives nothing"}`;

  // The Output a Part is on. The firmware keeps a Part on at most one, so the
  // first answer is the only answer.
  const outputOf = (outputs, partId) => outputs.find((output) => output.parts.includes(partId)) || null;

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

  window.PAParts = Object.freeze({ NOT_WIRED, NO_OUTPUT, groupParts, moveFor, announcement });

  const tableRegion = document.getElementById("parts-table");
  const summary = document.getElementById("parts-summary");
  const feedback = document.getElementById("parts-feedback");
  const dialog = document.getElementById("parts-move-dialog");
  const dialogTitle = document.getElementById("parts-move-title");
  const dialogBody = document.getElementById("parts-move-body");
  if (!tableRegion || !summary || !dialog) return;

  if (!partById.size) {
    // A table with no rows reads as a droid with no parts. Say what broke.
    summary.textContent = "The parts list did not load, so there is nothing to show. Reload the page to try again.";
    console.error("[parts] window.DroidParts is missing; /droid_parts.js did not load");
    return;
  }

  const esc = (value) => window.PAUtils.escapeHtml(String(value));
  const showFeedback = (text, level) => window.PAUtils.showFeedback(feedback, text, level);

  // The output-first table's frame, built once and before the droid has
  // answered, so the page says it is finding out rather than showing nothing.
  // Written here rather than in the markup file, which had no room in the
  // image when this section was built (#362, before #382 reclaimed it); the
  // feedback line moves below it, since it reports a move made from either
  // table.
  const outputsSection = document.createElement("section");
  outputsSection.className = "outputs-section";
  outputsSection.innerHTML =
    // The section head, whose subtitle is the three tier counts and nothing
    // else (ADR 0066): a heading carries a count and never appears bare, and
    // the counts live in one place rather than beside a total that restates
    // them. #293's honesty tiers are what a builder counts this table by.
    `<div class="sect"><h2>Outputs</h2>` +
    `<span class="sub outputs-tiers" role="status" aria-live="polite">` +
    TIERS.map((tier) => `<span class="outputs-tier" data-tier="${tier.id}">${tier.label} — finding out</span>`).join("") +
    `</span></div>` +
    `<p class="hint">Every output, in the order the leads plug in. Pick a part in a row to put it on that output.</p>` +
    // Rule 7 of the maker voice, at the entrance: what the two marks are, and
    // what they are not. Nothing on this droid reads a servo back.
    `<p class="hint">The bar is where the servo is being driven, the tick where the move ends; both are what ` +
    `the controller <b>told</b> it. <b>Pulses off</b> leaves an output limp where it is.</p>` +
    // Back to centre, and its one line of answer. The button starts refused for
    // the reason every act on this page does: it must not run on a guess about
    // the estop, and the droid has not said yet.
    `<div class="outputs-bulk">` +
    `<button class="btn outputs-centre" type="button" ` +
    `aria-label="Put every output back to the centre recorded for it" disabled aria-disabled="true">` +
    `back to centre</button>` +
    `<p class="feedback" role="status" aria-live="polite">Each output goes to its own centre, one at a ` +
    `time, so the servos do not brown out.</p>` +
    `</div>` +
    `<div class="parts-table-wrap" id="outputs-table"></div>`;
  tableRegion.parentNode.appendChild(outputsSection);
  if (feedback) tableRegion.parentNode.appendChild(feedback);
  const outputsRegion = outputsSection.querySelector("#outputs-table");
  const tierNodes = new Map(Array.from(outputsSection.querySelectorAll("[data-tier]"), (node) => [node.dataset.tier, node]));
  // Back to centre and its one line of answer. Resolved here, with the rest of
  // the section's nodes, rather than beside the act far below: PAStatusStream
  // replays the last status to a new subscriber SYNCHRONOUSLY, so a frame that
  // has already arrived reaches the estop handler while this file is still
  // running - and a const declared after that handler would be in its temporal
  // dead zone, thrown, and swallowed by the stream's own listener guard.
  const centreBulk = outputsSection.querySelector(".outputs-bulk");
  const centreButton = centreBulk.querySelector(".outputs-centre");
  // The sentence is a plain .feedback so it takes the three outcome colours
  // every other answer on this page takes, and showFeedback() rewrites its
  // whole class list - so it is found once, here, and held, rather than looked
  // up again after the first answer has replaced the class it was found by.
  const centreSaid = centreBulk.querySelector(".feedback");

  // ---------------------------------------------------------------------------
  // Built once
  // ---------------------------------------------------------------------------
  // The Find by Moving button starts refused: a run must not start on a guess
  // about the estop, and the droid has not said yet. The first status frame
  // gates it, the way the shell's own plate says "finding out" until then.
  //
  // The act sits in its own column, beside the thing it acts on, rather than
  // under the picker in the same cell: two controls stacked in one cell made a
  // 93 px row out of the anatomy's 40 px one, measured in a browser at 1440 px.
  const rowHtml = (part) => {
    const kind = kinds ? kinds.treatmentClass(part) : "";
    const shorthand = part.shorthand ? `<span class="parts-shorthand">${esc(part.shorthand)}</span>` : "";
    const light = kinds?.isLight(part) ? `<span class="parts-kind">light</span>` : "";
    return (
      `<tr class="parts-row${kind ? ` ${kind}` : ""}" data-part="${esc(part.id)}">` +
      `<th scope="row"><span class="parts-name">${esc(part.name)}</span>${shorthand}${light}` +
      `<span class="parts-gang"></span></th>` +
      `<td><select class="parts-output" aria-label="${esc(`Output that drives ${part.name}`)}" disabled>` +
      `<option value="${NO_OUTPUT}">Finding out...</option></select></td>` +
      `<td class="parts-acts">` +
      `<button class="btn btn-sm parts-find" type="button" aria-label="${esc(`Find the output that moves ${part.name} by nudging each spare output`)}" ` +
      `disabled aria-disabled="true">Find by moving</button></td></tr>`
    );
  };

  tableRegion.innerHTML =
    `<table class="parts-table"><thead><tr><th scope="col">Part</th><th scope="col">Driven by</th>` +
    `<th scope="col" class="parts-acts">On this part</th></tr></thead>` +
    groupParts(catalog.parts)
      .map(
        (group) =>
          `<tbody data-group="${group.id}"><tr class="parts-group"><th colspan="3" scope="colgroup">` +
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
      find: node.querySelector(".parts-find"),
      addresses: null,
    });
  });

  // ---------------------------------------------------------------------------
  // The output-first table: built once per set of Outputs
  // ---------------------------------------------------------------------------
  // Every catalog Part, grouped as the part-first table groups them, so a
  // builder finds a Part the same way from either end.
  const addOptions = groupParts(catalog.parts)
    .map(
      (group) =>
        `<optgroup label="${esc(group.label)}">` +
        group.parts.map((part) => `<option value="${esc(part.id)}">${esc(partLabel(part.id))}</option>`).join("") +
        `</optgroup>`
    )
    .join("");

  // An Output nobody has named - an expander's row - shows its address as its
  // name, and a named one shows the address beside it.
  const outputRowHtml = (output) => {
    const label = outputLabel(output);
    const address = output.name ? `<span class="outputs-address">${esc(output.address)}</span>` : "";
    return (
      `<tr class="parts-row outputs-row" data-output="${esc(output.address)}">` +
      `<th scope="row"><span class="parts-name">${esc(label)}</span>${address}</th>` +
      `<td><span class="outputs-parts"></span><select class="parts-output outputs-add" ` +
      `aria-label="${esc(`Put a part on ${label}`)}"><option value="">Put a part on ${esc(label)}...</option>` +
      `${addOptions}</select></td>` +
      `<td><div class="outputs-bar" aria-hidden="true"><div class="outputs-now"></div><div class="outputs-tick"></div></div>` +
      `<span class="outputs-us"></span></td><td class="outputs-release"></td>` +
      // The acts, beside what they act on. Both start refused: neither may run
      // on a guess about the estop, and the droid has not said yet.
      `<td class="outputs-acts">` +
      `<button class="btn btn-sm outputs-calibrate" type="button" ` +
      `aria-label="${esc(`Calibrate ${label} by driving it`)}" disabled aria-disabled="true">calibrate</button>` +
      `<button class="btn btn-sm outputs-off" type="button" ` +
      `aria-label="${esc(`Take the pulse off ${label}`)}" disabled aria-disabled="true">pulses off</button>` +
      `</td></tr>`
    );
  };

  const outputRows = new Map();
  let outputAddresses = null;

  // The only rebuild, and only when the set of Outputs itself changes - which a
  // controller does across a reboot, not while this page is reading it (#318).
  const buildOutputs = (addresses) => {
    outputsRegion.innerHTML =
      `<table class="parts-table outputs-table"><thead><tr><th scope="col">Output</th><th scope="col">Drives</th>` +
      `<th scope="col">Commanded position</th><th scope="col">Output Release</th>` +
      `<th scope="col">Calibrate</th></tr></thead><tbody>` +
      outputs.map(outputRowHtml).join("") +
      `</tbody></table>`;
    outputRows.clear();
    outputsRegion.querySelectorAll("[data-output]").forEach((node) => {
      outputRows.set(node.dataset.output, {
        node,
        parts: node.querySelector(".outputs-parts"),
        bar: node.querySelector(".outputs-bar"),
        now: node.querySelector(".outputs-now"),
        tick: node.querySelector(".outputs-tick"),
        us: node.querySelector(".outputs-us"),
        release: node.querySelector(".outputs-release"),
        calibrate: node.querySelector(".outputs-calibrate"),
        off: node.querySelector(".outputs-off"),
      });
    });
    outputAddresses = addresses;
    // The buttons are built refused; this is what makes them live again on a
    // droid whose estop is clear.
    gateActs();
  };

  // Both marks against one span, so they cannot disagree about scale and the
  // gap between them is the move (r2d2-astromech-simulator v1.79.0,
  // src/js/maestro/hw-table.js:186). The span is the band the Output's widths
  // are clamped into, so a commanded width never falls off either end.
  const markAt = (us, output) => {
    const span = output.bandHiUs - output.bandLoUs;
    const fraction = span > 0 ? (us - output.bandLoUs) / span : 0;
    return `${(Math.min(1, Math.max(0, fraction)) * 100).toFixed(1)}%`;
  };

  // A row whose every Part is a light carries no travel and no release: a light
  // has neither, and a zero or an empty bar would still read as a promise about
  // movement (data/droid_part_kind.js). What an estop does to a light is an open
  // question (#318), so nothing here says it is stopped, held or released.
  const isLightRow = (output) =>
    output.parts.length > 0 && output.parts.every((id) => Boolean(kinds?.isLight(partById.get(id))));

  // Only style, textContent and classList, on nodes that already exist: a
  // repaint never rebuilds a row, so the control under the builder's pointer
  // stays where it is (hw-table.js:171-174).
  const paintOutputRow = (output) => {
    const row = outputRows.get(output.address);
    if (!row) return;
    const light = isLightRow(output);
    const pulsing = output.commandedUs !== null;
    row.node.classList.toggle("is-wired", output.parts.length > 0);
    row.node.classList.toggle("partkind-light", light);
    row.bar.classList.toggle("is-off", !pulsing);
    // Whatever the droid has just answered is current, including after an
    // estop cut a nudge short: the mark that was held back below is shown
    // again from this answer, not from the one the run had.
    row.bar.classList.remove("is-stale");
    row.parts.textContent = output.parts.length ? listParts(output.parts) : NOT_WIRED;
    row.now.style.width = pulsing ? markAt(output.commandedUs, output) : "0%";
    row.tick.style.left = pulsing ? markAt(output.targetUs, output) : "0%";
    if (!output.reported) {
      row.us.textContent = "Not reported by this firmware";
      row.release.textContent = "Not reported by this firmware";
      return;
    }
    // An Output with no pulse says so: a blank cell cannot be told from a table
    // that has stopped updating.
    if (!pulsing) row.us.textContent = "— off";
    else if (light) row.us.textContent = "A light has no position";
    else if (output.targetUs === output.commandedUs) row.us.textContent = `${output.commandedUs} µs`;
    else row.us.textContent = `${output.commandedUs} → ${output.targetUs} µs`;
    if (light) row.release.textContent = "None - a light has nothing to let go of";
    else if (output.held) row.release.textContent = "The dial is holding it";
    else if (pulsing) row.release.textContent = "Holds where it stops";
    // An Output that has gone limp says WHICH of the ways it can happen this
    // was (#364): a bound the dial ran into is not the estop letting go.
    else row.release.textContent = LIMP_SAID[output.limp] || LIMP_SAID.off;
    const driveable = isDriveable(output);
    row.calibrate.hidden = !driveable;
    row.off.hidden = !driveable;
    row.node.classList.toggle("is-held", output.held);
  };

  const paintOutputs = (addresses) => {
    if (outputAddresses !== addresses) buildOutputs(addresses);
    outputs.forEach(paintOutputRow);
    const counts = new Map(TIERS.map((tier) => [tier.id, 0]));
    outputs.forEach((output) => counts.set(tierOf(output), counts.get(tierOf(output)) + 1));
    TIERS.forEach((tier) => {
      const count = counts.get(tier.id);
      tierNodes.get(tier.id).textContent = `${tier.label} — ${count} ${count === 1 ? "output" : "outputs"}`;
    });
  };

  // ---------------------------------------------------------------------------
  // Repainted in place
  // ---------------------------------------------------------------------------
  let outputs = null; // null until the droid has answered
  let pendingPart = null; // a move waiting on the builder's answer or on the droid
  let run = null; // the Find by Moving run in progress, at most one (below)

  // The control the builder has hold of: the one focused, or the one whose move
  // is being asked about or is on its way. Its value and its options are theirs
  // until they let go.
  const held = (id, select) => id === pendingPart || document.activeElement === select;

  const paintRow = (id, row, addresses) => {
    const output = outputOf(outputs, id);
    row.node.classList.toggle("is-wired", output !== null);
    const gang = output ? output.parts.filter((other) => other !== id) : [];
    row.gang.textContent = gang.length ? ` moves with ${listParts(gang)}` : "";
    // Find by Moving is for a Part nothing drives, and a row whose run is in
    // progress shows the run in the button's place.
    row.find.hidden = output !== null || (run !== null && run.partId === id);
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
    paintOutputs(addresses);

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
    // The droid has answered again, which is the only thing a run steps on.
    stepRun();
    paintDial();
    // And the picture at the head of the surface, from the same one answer the
    // two tables were just painted from: a body view that read the droid on its
    // own clock could show a part open while the row below it said closed.
    paintBody();
  };

  const loadOutputs = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    const result = await api.get("/api/servo/outputs");
    const answer = result?.data?.outputs;
    if (!Array.isArray(answer)) throw new Error("the droid's outputs answer carries no table");
    outputs = answer.map((output) => ({
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
    paint();
  };

  // ---------------------------------------------------------------------------
  // Moving a Part
  // ---------------------------------------------------------------------------
  const send = async (move) => {
    const label = partLabel(move.part);
    pendingPart = move.part;
    const partRow = rows.get(move.part);
    if (partRow) partRow.select.disabled = true;
    showFeedback(`Moving ${label}...`);
    try {
      await window.PAApi.postForm(
        "/api/config",
        { movePart: move.part, movePartFrom: move.from, movePartTo: move.to },
        { timeoutMs: 4000 }
      );
      showFeedback(
        move.arrives ? `${label} is on ${outputLabel(move.arrives)}.` : `${label} is not on any output now.`,
        "success"
      );
    } catch (error) {
      showFeedback(`${label} did not move: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      pendingPart = null;
    }
    try {
      await loadOutputs();
    } catch (error) {
      // The table still shows what the droid said before the move; the next
      // poll catches it up.
      console.warn("[parts] reading the outputs after a move failed:", error);
      paint();
    }
  };

  let asking = null;

  // `control` is the one the builder chose with, on either table, and it is
  // where focus goes back to if they cancel.
  const ask = (move, control) => {
    const words = announcement(move);
    dialogTitle.textContent = words.title;
    dialogBody.textContent = words.body;
    asking = { move, control };
    pendingPart = move.part;
    dialog.showModal();
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
    paint();
    control?.focus?.();
  };

  // The one rule both tables move a Part by: one move at a time, a move that
  // takes a Part off another Output is asked first, and anything else goes
  // straight to the droid (#347, #362).
  const request = (move, control) => {
    if (pendingPart !== null) {
      showFeedback(`One move at a time: wait for ${partLabel(pendingPart)} to land.`, "warning");
      return;
    }
    if (move.from === move.to) return;
    if (move.announce) ask(move, control);
    else send(move);
  };

  document.getElementById("parts-move-confirm")?.addEventListener("click", () => answer(true));
  document.getElementById("parts-move-cancel")?.addEventListener("click", () => answer(false));
  // Escape is a cancel, not a dialog left open with no question on it.
  dialog.addEventListener("cancel", (event) => {
    event.preventDefault?.();
    answer(false);
  });

  tableRegion.addEventListener("change", (event) => {
    const select = event.target;
    const id = select?.closest?.("[data-part]")?.dataset.part;
    if (!id || outputs === null) return;
    request(moveFor(outputs, id, select.value), select);
  });

  outputsRegion.addEventListener("change", (event) => {
    const select = event.target;
    const address = select?.closest?.("[data-output]")?.dataset.output;
    const id = select?.value;
    if (!address || !id || outputs === null) return;
    // A pick is a request, not a state this control keeps: it goes back to its
    // prompt, and the row's Drives cell says what the droid answered.
    select.value = "";
    request(moveFor(outputs, id, address), select);
  });

  // A control that was held catches up with whatever arrived while it was.
  tableRegion.addEventListener("focusout", () => paint());

  // ---------------------------------------------------------------------------
  // Find by Moving (ADR 0050, #363)
  //
  // A builder who cannot remember which output the rear-left door is on
  // presses the button on that door's unwired row and watches the droid. The
  // page steps through the spare Outputs -- the ones driving no Part, with a
  // pulse on them -- and asks the droid to nudge each one a little, one at a
  // time; the builder presses "That one" when the Part twitches, and that is
  // the same move the row's picker makes. Nothing here holds an Output: a
  // nudge is one firmware command that goes out and comes back on its own
  // (POST /api/servo action=nudge), so a browser that dies mid-run leaves
  // the droid where it was, and Stop sends nothing further -- the nudge in
  // flight finishes its own return.
  //
  // One Output at a time, and the droid says when. The page sends the next
  // nudge only when the previous one has ended, and it knows that from the
  // answer's nudgesDone count going up -- never from watching the Output
  // move, because a whole nudge can fall between two of the bench feed's
  // one-second reads. No other pacing lives here.
  //
  // The estop ends a run. The firmware refuses and halts on its own; this
  // side stops asking and stops showing the nudged Output's last commanded
  // mark as if it were current, until the droid has answered again (after
  // r2d2-astromech-simulator v1.79.0, src/js/config/hardware.js:894, which
  // kills its freshness stamp the moment a clamp fires). While the estop is
  // latched the button is refused -- disabled plus aria-disabled, like every
  // refused control -- and the shell's own ignored-input notice names why, so
  // nothing here says it twice. No Non-RC Control consent is asked: that flag
  // has never reached POST /api/servo (ADR 0064).
  // ---------------------------------------------------------------------------
  const findButtons = () => Array.from(rows.values(), (row) => row.find);
  let estopLatched = null; // null until the droid has said

  // One panel, moved onto the row whose run it is: it replaces that row's
  // button while the run lasts and leaves when the run ends.
  const runPanel = document.createElement("span");
  runPanel.className = "parts-find-run";
  runPanel.innerHTML =
    `<span class="parts-find-text" role="status" aria-live="polite"></span>` +
    `<button class="btn accent parts-find-that" type="button">That one</button>` +
    `<button class="btn parts-find-stop" type="button">Stop</button>`;
  const runText = runPanel.querySelector(".parts-find-text");

  // The Outputs a run steps through: nothing on them, a pulse on them (an
  // Output with none cannot twitch, and the firmware would refuse it), and a
  // name the servo route takes as its arm.
  const spareOutputs = () =>
    outputs.filter((output) => output.parts.length === 0 && output.commandedUs !== null && output.name !== "");

  const gateFind = () => window.PAApi.gateControls(findButtons(), estopLatched === false);

  const endRun = (text, level) => {
    if (run === null) return;
    const { partId } = run;
    run = null;
    runPanel.remove();
    const row = rows.get(partId);
    if (row) row.find.hidden = outputOf(outputs, partId) !== null;
    if (text) showFeedback(text, level);
  };

  // The nudged Output's last commanded mark is not current any more: the
  // estop ended the nudge somewhere the run never read. Held back, not
  // guessed at, until the next answer repaints the row.
  const markNotCurrent = (address) => {
    const row = outputRows.get(address);
    if (!row) return;
    row.bar.classList.add("is-stale");
    row.us.textContent = "Stopped — finding out where it is";
  };

  // Ask the droid to nudge the next spare Output, or end the run when there
  // is none left. `before` is the count from the droid's latest answer, and a
  // later answer with a higher one is the only thing that moves the run on.
  const nudgeNext = async () => {
    const current = run;
    current.at += 1;
    const label = partLabel(current.partId);
    const count = current.candidates.length;
    if (current.at >= count) {
      endRun(
        `None of the ${count} spare ${count === 1 ? "output" : "outputs"} moved ${label} in one pass, so it stays ${NOT_WIRED}. ` +
          `Check the lead, or run it again.`,
        "warning"
      );
      return;
    }
    const address = current.candidates[current.at];
    const output = outputs.find((each) => each.address === address);
    if (!output || output.nudgesDone === null) {
      // The droid's answer changed shape under the run: a reboot, or a
      // different firmware. Nothing is asked of an Output the page cannot
      // tell has finished.
      endRun(`${address} is not in the droid's answer any more, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning");
      return;
    }
    current.address = address;
    current.before = output.nudgesDone;
    current.sending = true;
    runText.textContent =
      `Nudging ${outputLabel(output)} (${current.at + 1} of ${count}). Watch the droid, and press That one when ${label} moves.`;
    try {
      await window.PAApi.postForm("/api/servo", { arm: output.name.toLowerCase(), action: "nudge" }, { timeoutMs: 4000 });
    } catch (error) {
      if (run === current) {
        endRun(`The nudge did not reach the droid: ${window.PAApi.messageFor(error)}. ${label} stays ${NOT_WIRED}.`, "error");
      }
      return;
    }
    if (run === current) current.sending = false;
  };

  // Called with every answer from the droid. Steps on only when the Output
  // the run asked about says its nudge has ended.
  const stepRun = () => {
    if (run === null || run.sending || run.address === null) return;
    const label = partLabel(run.partId);
    const wiredTo = outputOf(outputs, run.partId);
    if (wiredTo) {
      endRun(`${label} is on ${outputLabel(wiredTo)} now, so the run stopped.`);
      return;
    }
    const output = outputs.find((each) => each.address === run.address);
    if (!output || output.nudgesDone === null) {
      endRun(`${run.address} is not in the droid's answer any more, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning");
      return;
    }
    // The Output being nudged has gone limp - pulses off from its row, one of
    // the calibration dial's bounds, or anything else that takes a pulse off a
    // pin (#364). A limp Output cannot twitch, so there is nothing left to
    // watch for: the run ENDS here rather than stepping on to the next Output.
    // Ending a nudge bumps nudgesDone, so without this the count going up would
    // read as "that one finished, try the next" and the run would carry on
    // past the thing the builder just did.
    if (output.commandedUs === null) {
      endRun(
        `${outputLabel(output)} is limp, so the run stopped. ${label} stays ${NOT_WIRED}.`,
        "warning"
      );
      return;
    }
    if (output.nudgesDone === run.before) return;
    nudgeNext();
  };

  const startRun = (partId) => {
    if (outputs === null) return;
    if (run !== null) {
      showFeedback(`One run at a time: ${partLabel(run.partId)} is being found. Stop that run first.`, "warning");
      return;
    }
    if (pendingPart !== null) {
      showFeedback(`One move at a time: wait for ${partLabel(pendingPart)} to land.`, "warning");
      return;
    }
    const candidates = spareOutputs();
    if (!candidates.length) {
      showFeedback(
        "Nothing to nudge. Every output with a pulse already drives a part.",
        "warning"
      );
      return;
    }
    if (candidates.some((output) => output.nudgesDone === null)) {
      showFeedback(
        "This firmware does not say when a nudge has ended, so Find by moving cannot step through the outputs. Update the firmware.",
        "warning"
      );
      return;
    }
    run = { partId, candidates: candidates.map((output) => output.address), at: -1, address: null, before: null, sending: false };
    const row = rows.get(partId);
    row.find.hidden = true;
    row.find.parentNode.appendChild(runPanel);
    nudgeNext();
  };

  // "That one": the same request the picker makes, so a spare Output takes the
  // Part with no question and an Output somebody wired meanwhile is asked
  // about in the same words.
  const pickThatOne = () => {
    if (run === null) return;
    const { partId, address } = run;
    const control = rows.get(partId)?.select;
    endRun();
    request(moveFor(outputs, partId, address), control);
  };

  const stopRun = () => {
    if (run === null) return;
    endRun(`Stopped. ${partLabel(run.partId)} stays ${NOT_WIRED}.`);
  };

  runPanel.querySelector(".parts-find-that").addEventListener("click", pickThatOne);
  runPanel.querySelector(".parts-find-stop").addEventListener("click", stopRun);

  tableRegion.addEventListener("click", (event) => {
    const button = event.target?.closest?.(".parts-find");
    const id = button?.closest?.("[data-part]")?.dataset.part;
    // A browser delivers no click to a disabled button; this is the rule
    // itself, not a repeat of the shell's notice: a refused control asks the
    // droid for nothing, however the click arrived.
    if (!id || button.disabled) return;
    startRun(id);
  });

  window.PAStatusStream?.subscribe((eventType, payload) => {
    if (eventType !== "status" || !payload || typeof payload !== "object") return;
    estopLatched = payload.estop === true;
    gateFind();
    if (estopLatched && run !== null) {
      const { partId, address } = run;
      if (address !== null) markNotCurrent(address);
      endRun(`The estop stopped the run. ${partLabel(partId)} stays ${NOT_WIRED}.`, "error");
    }
  });

  // Leaving Parts ends a run: the bench feed stops with the surface (#360),
  // so nothing could step it on, and a nudge sent on the way back would be
  // motion the builder did not press for. Never a hold -- leaving is always
  // allowed; this only hears that it is happening.
  window.PASurface?.holdUnmount(() => {
    if (run !== null) endRun(`The run stopped when you left Parts. ${partLabel(run.partId)} stays ${NOT_WIRED}.`);
    // And the dial lets go of its Output. ADR 0064 ends a hold when the builder
    // presses pulses off or closes the dial, and leaving the page is closing
    // it; waiting for the firmware's expiry instead would drive the part for
    // another few seconds with nobody there to watch it.
    closeDial();
    return false;
  });

  // ---------------------------------------------------------------------------
  // The calibration dial (#291, #364, ADR 0064)
  //
  // A builder drives the part until it looks right, presses a button, and that
  // becomes the end. No typing microseconds, and no guessing whether the number
  // they typed is the one the servo is holding -- the dial is already standing
  // at a width the droid is driving, so a capture is one assignment
  // (r2d2-astromech-simulator v1.79.0, src/js/maestro/setup-hw-cal.js:610).
  //
  // THE PART KEEPS BEING DRIVEN while they look and listen. That is the point
  // of the whole panel: Output Release cuts drive when a part arrives, which is
  // exactly when the builder has stopped moving it in order to look at it, and
  // a servo fighting its linkage is only audible while it is being driven. So
  // the dial takes the Output and holds it, and the firmware -- not this page --
  // bounds that hold two ways: it lets go a few seconds after these commands
  // stop arriving, and ten minutes after it took the Output whatever keeps
  // arriving. This page can refresh the first and can move neither. When either
  // fires the Output goes limp and the panel says which one it was.
  //
  // REVERSE IS A SWAP, READ BACK FROM THE NUMBERS. Nothing here stores an
  // invert flag and nothing may: the droid swaps the two ends on the row and
  // this page shows what the row then says, so the control can never disagree
  // with the numbers above it, and pressing it again is a real undo.
  //
  // Repainted in place like every other row on this page. A rebuilt slider is a
  // commanded move on a real droid.
  // ---------------------------------------------------------------------------
  // How often the hold is refreshed while the dial is open. Comfortably inside
  // the firmware's few-second expiry, so an ordinary hiccup does not drop the
  // hold, and far short of the ten-minute ceiling, which nothing sent from here
  // can move.
  const HOLD_KEEPALIVE_MS = 1000;
  // A drag fires input events far faster than the droid needs to hear about
  // them; this is short enough to feel immediate and long enough not to queue.
  const HOLD_CHANGE_MS = 50;
  // Long enough to watch a part reach an end and settle before it leaves again.
  const SWEEP_DWELL_MS = 900;
  // What `safe range` narrows to: the cautious band every component band
  // contains (SERVO_BAND_STD, include/servo_output_row.h).
  const SAFE_LO = 1000;
  const SAFE_HI = 2000;
  // One press of the fine buttons. A slider cannot be dragged to a single
  // microsecond at the bench, and a tablet has no arrow keys (ADR 0059).
  const FINE_US = 5;

  // Why an Output has no pulse, said the way a builder needs it. The two
  // firmware bounds each get their own sentence, because "the browser stopped
  // asking" and "ten minutes is up" are different things to have happened.
  const LIMP_SAID = {
    "off": "Limp - no pulse",
    "pulses-off": "Limp - pulses off",
    "expiry": "Went limp - the dial stopped asking",
    "ceiling": "Went limp - ten minutes is the most a dial holds",
    "estop": "Limp - the estop let go",
    "sleep": "Limp - sleep mode let go",
  };

  // What the band the dial opens at is, and why it is that one. The component
  // governs the clamp (ADR 0041), so the dial opens at the widest range the
  // firmware will actually drive this row -- never wider, or it would offer
  // widths the droid refuses.
  const COMPONENT_SAID = {
    mg996r: "what an MG996R takes",
    mg90s: "what an MG90S takes, the full servo range",
    rgb: "the cautious range: this is recorded as an LED strip",
    none: "the cautious range: nothing is recorded as fitted",
  };

  const bandSentence = (output) => {
    const said = COMPONENT_SAID[output.component] || COMPONENT_SAID.none;
    const unlockable = output.component === "none" || output.component === "mg996r";
    const unlock = unlockable ? " Record the part as an MG90S for the full 500-2500." : "";
    return `${output.bandLoUs}-${output.bandHiUs} µs — ${said}.${unlock}`;
  };

  // An Output a dial can drive: one with travel, and with a name the servo
  // route takes as its arm. A light has neither a position nor anything to let
  // go of, and an expander's unnamed row cannot be addressed by POST /api/servo.
  const isDriveable = (output) => !isLightRow(output) && output.name !== "";

  let dial = null; // the Output being calibrated, at most one

  const dialPanel = document.createElement("section");
  dialPanel.className = "cal-panel";
  dialPanel.hidden = true;
  dialPanel.innerHTML =
    `<h4 class="cal-title"></h4>` +
    `<p class="desc">Drive the part until it looks right, then press the end you are setting. The ` +
    `servo holds while you watch, and goes limp a few seconds after you leave or after ten minutes.</p>` +
    `<p class="cal-band"></p>` +
    `<div class="cal-drive">` +
    `<button class="btn cal-fine" type="button" data-step="-1" aria-label="Down 5 microseconds">−5 µs</button>` +
    `<input class="cal-slider" type="range" step="1" aria-label="Drive this output">` +
    `<button class="btn cal-fine" type="button" data-step="1" aria-label="Up 5 microseconds">+5 µs</button>` +
    `<span class="cal-readout"></span>` +
    `</div>` +
    `<div class="cal-acts">` +
    `<button class="btn accent cal-set" type="button" data-end="close">Set MIN</button>` +
    `<button class="btn accent cal-set" type="button" data-end="centre">Set CENTER</button>` +
    `<button class="btn accent cal-set" type="button" data-end="open">Set MAX</button>` +
    `</div>` +
    `<p class="cal-ends"></p>` +
    `<div class="cal-acts">` +
    `<button class="btn cal-reverse" type="button">reverse</button>` +
    `<span class="cal-hint">it swaps the two ends</span>` +
    `<button class="btn cal-safe" type="button">safe range</button>` +
    `<button class="btn cal-useends" type="button">use these ends</button>` +
    `<button class="btn cal-sweep" type="button">test sweep</button>` +
    `</div>` +
    `<div class="cal-acts">` +
    `<button class="btn cal-off" type="button">pulses off</button>` +
    `<button class="btn cal-resume" type="button">take it again</button>` +
    `<button class="btn cal-done" type="button">done</button>` +
    `</div>` +
    `<p class="cal-note" role="status" aria-live="polite"></p>`;
  outputsSection.appendChild(dialPanel);

  const dialTitle = dialPanel.querySelector(".cal-title");
  const dialBand = dialPanel.querySelector(".cal-band");
  const dialSlider = dialPanel.querySelector(".cal-slider");
  const dialReadout = dialPanel.querySelector(".cal-readout");
  const dialEnds = dialPanel.querySelector(".cal-ends");
  const dialNote = dialPanel.querySelector(".cal-note");
  const dialSafe = dialPanel.querySelector(".cal-safe");
  const dialUseEnds = dialPanel.querySelector(".cal-useends");
  const dialSweep = dialPanel.querySelector(".cal-sweep");
  const dialResume = dialPanel.querySelector(".cal-resume");

  const dialOutput = () =>
    (dial === null || outputs === null ? null : outputs.find((each) => each.address === dial.address) || null);

  const setNote = (text, level) => {
    dialNote.textContent = text;
    dialNote.className = level ? `cal-note ${level}` : "cal-note";
  };

  // Every act on this panel is started from a click handler and finishes later,
  // so nothing awaits it. A rejection with no handler is a control that did
  // nothing and said nothing -- the dial simply stops, which reads as a dead
  // droid rather than as a page that broke. Each act catches its own REQUEST
  // failures and says which; this catches everything else and refuses to be
  // silent about it.
  const started = (promise) =>
    promise?.catch?.((error) => {
      console.error("[parts] the dial failed:", error);
      setNote(`Something went wrong on this page: ${error && error.message ? error.message : error}`, "error");
    });

  // The span the slider covers. The component band by default -- the widest the
  // firmware will drive this row -- narrowed by `safe range` to the cautious
  // band, or by `use these ends` to the travel the builder has recorded.
  const dialRange = (output) => {
    if (dial !== null && dial.ends && output.calibrated) {
      return {
        lo: Math.min(output.openUs, output.closeUs),
        hi: Math.max(output.openUs, output.closeUs),
      };
    }
    if (dial !== null && dial.safe) {
      return { lo: Math.max(output.bandLoUs, SAFE_LO), hi: Math.min(output.bandHiUs, SAFE_HI) };
    }
    return { lo: output.bandLoUs, hi: output.bandHiUs };
  };

  // MIN and MAX name the two ends, not an ordering: after a reverse, MIN can be
  // the larger number, exactly as the reference project's dial says
  // ("ends swapped — MIN is now 1850 µs"). MIN is the row's `close` and MAX its
  // `open`, and reverse trades them, so nothing here has to sort a pair -- which
  // is how the invert flag ADR 0041 refuses stays refused.
  const endsSentence = (output) => {
    if (!output.calibrated) {
      return "No ends recorded yet. Drive the part to one and press Set MIN or Set MAX.";
    }
    return `MIN ${output.closeUs} µs · CENTER ${output.centreUs} µs · MAX ${output.openUs} µs`;
  };

  const sendServo = async (action, extra = {}) => {
    const output = dialOutput();
    if (!output) return false;
    try {
      await window.PAApi.postForm(
        "/api/servo",
        { arm: output.name.toLowerCase(), action, ...extra },
        { timeoutMs: 4000 }
      );
      return true;
    } catch (error) {
      setNote(`The droid did not take that: ${window.PAApi.messageFor(error)}`, "error");
      return false;
    }
  };

  // Every hold command carries the width the dial is standing at. The first one
  // takes the Output and starts both bounds; the rest refresh the short expiry
  // and nothing else.
  const sendHold = () => sendServo("hold", { positionUs: String(dial === null ? 0 : dial.us) });
  const sendHoldSoon = window.PAUtils.debounce(() => {
    if (dial !== null && !dial.sweeping) sendHold();
  }, HOLD_CHANGE_MS);

  let holdTimer = null;
  const stopKeepalive = () => {
    if (holdTimer === null) return;
    window.clearInterval(holdTimer);
    holdTimer = null;
  };
  // The keepalive refreshes the hold only while the droid still says it HAS the
  // Output. The moment one of the firmware's two bounds lets go, this stops
  // asking and waits for the builder to press.
  //
  // Without that, the ceiling would not exist. It releases the Output and drops
  // the hold; the very next command takes the Output afresh and starts the
  // ceiling over, so a page that kept asking would silently hold a servo for as
  // long as the tab was open -- which is the one thing ADR 0064 says the page
  // must not be able to do. Resuming is one press, and this is what makes that
  // sentence true rather than decorative.
  const startKeepalive = () => {
    stopKeepalive();
    holdTimer = window.setInterval(() => {
      if (dial !== null && dial.holding && !dial.sweeping) sendHold();
    }, HOLD_KEEPALIVE_MS);
  };

  const closeDial = ({ release = true } = {}) => {
    if (dial === null) return;
    // Closing the dial ends the hold, which is what ADR 0064 says ends it
    // besides the builder pressing pulses off. Leaving it to the expiry would
    // drive the part for three more seconds with nobody watching.
    if (release) sendServo("release");
    dial = null;
    stopKeepalive();
    dialPanel.hidden = true;
    paint();
  };

  const openDial = (address) => {
    if (outputs === null) return;
    const output = outputs.find((each) => each.address === address);
    if (!output) return;
    if (run !== null) {
      showFeedback(`One at a time: ${partLabel(run.partId)} is being found. Stop that run first.`, "warning");
      return;
    }
    if (dial !== null && dial.address !== address) closeDial();
    const band = { lo: output.bandLoUs, hi: output.bandHiUs };
    dial = {
      address,
      // Start from where the droid says the Output is standing, so the first
      // hold does not move the part at all. An Output with no pulse has no
      // position to start from, so the middle of its band is the honest guess.
      us: output.commandedUs === null ? Math.round((band.lo + band.hi) / 2) : output.commandedUs,
      safe: false,
      ends: false,
      sweeping: false,
      // The droid has the Output as far as this page knows. Cleared when an
      // answer says it let go, so the keepalive stops asking for it.
      holding: true,
    };
    dialPanel.hidden = false;
    setNote("");
    sendHold();
    startKeepalive();
    paint();
  };

  // A capture: the width the dial is standing at becomes one of this Output's
  // three recorded positions, and the row becomes measured. The droid may drag
  // the centre inside the travel the capture just described; this page does not
  // re-derive that rule, it reads the centre back and says if it moved.
  const capture = async (end) => {
    const output = dialOutput();
    if (!output) return;
    const centreBefore = output.centreUs;
    const label = end === "centre" ? "CENTER" : end === "open" ? "MAX" : "MIN";
    try {
      await window.PAApi.postForm(
        "/api/config",
        { captureOutput: output.address, captureEnd: end, captureUs: String(dial.us) },
        { timeoutMs: 4000 }
      );
    } catch (error) {
      setNote(`${label} was not recorded: ${window.PAApi.messageFor(error)}`, "error");
      return;
    }
    await refresh();
    const after = dialOutput();
    if (after && after.centreUs !== centreBefore) {
      setNote(
        `${label} is ${dial.us} µs. Centre moved to ${after.centreUs} µs — it was outside the travel you just captured.`,
        "warning"
      );
      return;
    }
    setNote(`${label} is ${dial.us} µs.`, "success");
  };

  const reverseEnds = async () => {
    const output = dialOutput();
    if (!output) return;
    try {
      await window.PAApi.postForm("/api/config", { reverseOutput: output.address }, { timeoutMs: 4000 });
    } catch (error) {
      setNote(`The ends were not swapped: ${window.PAApi.messageFor(error)}`, "error");
      return;
    }
    await refresh();
    const after = dialOutput();
    if (after) setNote(`Ends swapped — MIN is now ${after.closeUs} µs.`, "success");
  };

  // test sweep visits the ends the builder recorded and comes back to where the
  // dial was standing. It refuses on an Output with none: the dial opens at the
  // whole band, so there is nowhere sane to sweep between until ends exist, and
  // ADR 0052 gives the same shape to overshoot easing, which degrades while
  // `calibrated` is unset.
  const testSweep = async () => {
    const output = dialOutput();
    if (!output) return;
    const startedAt = dial.us;
    dial.sweeping = true;
    paint();
    const legs = [output.closeUs, output.openUs, startedAt];
    for (const target of legs) {
      if (dial === null || !dial.sweeping) return;
      dial.us = target;
      paint();
      dial.holding = true;
      if (!(await sendHold())) break;
      await new Promise((resolve) => window.setTimeout(resolve, SWEEP_DWELL_MS));
    }
    if (dial === null) return;
    dial.sweeping = false;
    dial.us = startedAt;
    paint();
    setNote(`Swept ${output.closeUs} µs to ${output.openUs} µs and back.`, "success");
  };

  // Repaint in place: style, textContent, classList, value and disabled on
  // nodes that already exist. Never innerHTML, and never the control the
  // builder has hold of.
  const paintDial = () => {
    gateActs();
    if (dial === null) {
      dialPanel.hidden = true;
      return;
    }
    const output = dialOutput();
    if (!output) {
      // The Output left the droid's answer under the dial - a reboot, or a
      // different firmware. Say so rather than driving something that is gone.
      setNote("That output is not in the droid's answer any more, so the dial closed.", "warning");
      closeDial({ release: false });
      return;
    }
    const range = dialRange(output);
    if (dial.us < range.lo) dial.us = range.lo;
    if (dial.us > range.hi) dial.us = range.hi;

    dialTitle.textContent = `Calibrating ${outputLabel(output)}`;
    dialBand.textContent = bandSentence(output);
    dialEnds.textContent = endsSentence(output);
    dialReadout.textContent = `${dial.us} µs`;
    if (document.activeElement !== dialSlider) {
      dialSlider.min = String(range.lo);
      dialSlider.max = String(range.hi);
      dialSlider.value = String(dial.us);
    }

    dialSafe.classList.toggle("is-on", dial.safe);
    dialUseEnds.classList.toggle("is-on", dial.ends);
    // `safe range` is inert on a row whose band is already the cautious one,
    // and says so rather than pretending to narrow anything.
    const alreadySafe = output.bandLoUs >= SAFE_LO && output.bandHiUs <= SAFE_HI;
    dialSafe.textContent = alreadySafe ? "safe range (already)" : "safe range";
    dialUseEnds.disabled = !output.calibrated;
    dialUseEnds.setAttribute("aria-disabled", output.calibrated ? "false" : "true");
    dialSweep.disabled = !output.calibrated || dial.sweeping;
    dialSweep.setAttribute("aria-disabled", dialSweep.disabled ? "true" : "false");

    // The Output has gone limp under the dial: one of the firmware's two
    // bounds, or the estop. The panel says which, and one press takes it back.
    const limp = output.commandedUs === null;
    // The droid has let go. Stop asking for it: the next hold would take the
    // Output afresh and restart both bounds, which is the builder's press to
    // make, not this page's to make for them.
    if (limp) dial.holding = false;
    dialResume.hidden = !limp;
    if (limp && !dial.sweeping) {
      setNote(`${LIMP_SAID[output.limp] || LIMP_SAID.off}. Press take it again to hold it once more.`, "warning");
    }
  };

  // The dial's controls and the row acts are gated on the same one fact the
  // find buttons are: a latched estop refuses everything that asks the droid to
  // move something, and the shell's own notice names why, so nothing here says
  // it twice. They gate themselves rather than riding gateFind() because the
  // output-first table is rebuilt whenever the SET of Outputs changes, so its
  // buttons have to be re-gated after each build as well as on each frame.
  const gateActs = () => {
    const live = estopLatched === false;
    window.PAApi.gateControls(Array.from(outputRows.values(), (row) => row.calibrate), live);
    window.PAApi.gateControls(Array.from(outputRows.values(), (row) => row.off), live);
    // Back to centre is an act on every Output at once, so it is refused for
    // the same one fact as the acts on a single row. The controller refuses it
    // too; this is so nothing is offered that the droid will not take.
    window.PAApi.gateControls([centreButton], live);
    window.PAApi.gateControls(Array.from(dialPanel.querySelectorAll("button")), live);
    window.PAApi.gateControls([dialSlider], live);
  };

  window.PAStatusStream?.subscribe((eventType, payload) => {
    if (eventType !== "status" || !payload || typeof payload !== "object") return;
    gateActs();
    // A latched estop has released every enabled Output (ADR 0043), so a dial
    // that was holding one is no longer holding anything. The panel stays open
    // and says so; the next answer from the droid carries the reason.
    if (payload.estop === true && dial !== null) {
      dial.sweeping = false;
      setNote("The estop let go of every output. Clear it, then press take it again.", "error");
    }
    // And it ends a back-to-centre sweep, wherever it had got to (#365). Every
    // enabled Output has just been released, so NO row's commanded mark is
    // current any longer - the droid is driving none of them, and where each
    // part came to rest is whatever gravity and friction decided. Held back
    // rather than guessed at, until the droid answers again (after
    // r2d2-astromech-simulator v1.79.0, src/js/config/hardware.js:894, which
    // stops its own packet clock the moment a clamp fires so the HUD cannot go
    // on reading live).
    if (payload.estop === true && outputs !== null) {
      outputs.forEach((output) => markNotCurrent(output.address));
      window.PAUtils.showFeedback(
        centreSaid,
        "The estop let go of every output. Nothing is being driven, so nothing is going back to centre.",
        "error"
      );
    }
  });

  dialSlider.addEventListener("input", () => {
    if (dial === null) return;
    dial.us = Number(dialSlider.value);
    dialReadout.textContent = `${dial.us} µs`;
    sendHoldSoon();
  });

  dialPanel.addEventListener("click", (event) => {
    const button = event.target?.closest?.("button");
    if (!button || button.disabled || dial === null) return;
    if (button.classList.contains("cal-fine")) {
      const output = dialOutput();
      if (!output) return;
      const range = dialRange(output);
      const next = dial.us + Number(button.dataset.step) * FINE_US;
      dial.us = Math.min(range.hi, Math.max(range.lo, next));
      paintDial();
      sendHoldSoon();
      return;
    }
    if (button.classList.contains("cal-set")) {
      started(capture(button.dataset.end));
      return;
    }
    if (button.classList.contains("cal-reverse")) {
      started(reverseEnds());
      return;
    }
    if (button === dialSafe) {
      const output = dialOutput();
      if (output && output.bandLoUs >= SAFE_LO && output.bandHiUs <= SAFE_HI) {
        setNote("This output is already on the cautious range, so safe range has nothing to narrow.", "warning");
        return;
      }
      dial.safe = !dial.safe;
      if (dial.safe) dial.ends = false;
      paintDial();
      return;
    }
    if (button === dialUseEnds) {
      dial.ends = !dial.ends;
      if (dial.ends) dial.safe = false;
      paintDial();
      return;
    }
    if (button === dialSweep) {
      const output = dialOutput();
      if (output && !output.calibrated) {
        setNote("Nothing to sweep between yet: record MIN and MAX first.", "warning");
        return;
      }
      started(testSweep());
      return;
    }
    if (button.classList.contains("cal-off")) {
      started(pulsesOff(dial.address));
      return;
    }
    if (button === dialResume) {
      // One press re-takes the Output, which restarts both firmware bounds.
      dial.holding = true;
      started(sendHold());
      setNote("Holding it again.", "success");
      return;
    }
    if (button.classList.contains("cal-done")) closeDial();
  });

  // ---------------------------------------------------------------------------
  // Back to centre (#318, #365)
  //
  // One press, one request, and the droid paces the sweep itself. NOTHING about
  // the pace is on this side: the controller expands the press into one Output
  // at a time and holds the Cadence Floor between them, because a safe pace this
  // page held is one a hand-edited or imported client could walk around. So
  // there is no timer here, no queue, and no progress model - the bench feed's
  // once-a-second read shows the bars moving, exactly as it does for a move made
  // any other way.
  //
  // The line counts from the rows this page already has, with the signal the row
  // already carries: a row whose every Part is a light has no centre to go back
  // to, and the controller skips it for the same reason (isLightRow here; the
  // component recorded on the row, firmware side). isLightRow rather than
  // isDriveable, because the sweep does not go through POST /api/servo and so
  // does not need a row to have a name the servo route takes as its arm.
  // ---------------------------------------------------------------------------
  const centreAll = async () => {
    if (outputs === null) return;
    const lights = outputs.filter(isLightRow).length;
    const going = outputs.length - lights;
    try {
      await window.PAApi.postForm("/api/servo/centre", {}, { timeoutMs: 4000 });
    } catch (error) {
      window.PAUtils.showFeedback(
        centreSaid,
        `Nothing is going back to centre: ${window.PAApi.messageFor(error)}`,
        "error"
      );
      return;
    }
    const said =
      `${going} ${going === 1 ? "output is" : "outputs are"} going back to centre, one at a time.` +
      (lights ? ` ${lights} skipped — a light has no centre.` : "");
    window.PAUtils.showFeedback(centreSaid, said, "success");
    refresh();
  };

  centreButton.addEventListener("click", () => {
    // A browser delivers no click to a disabled button; this is the rule
    // itself, the way both tables state it for their own acts.
    if (centreButton.disabled) return;
    started(centreAll());
  });

  // ---------------------------------------------------------------------------
  // pulses off, from a row or from the dial
  //
  // The Output goes limp where it is, at once. Reachable from a row so that it
  // is reachable DURING a Find by Moving run (#363's inherited criterion): the
  // run is nudging that Output, and taking the pulse off it ends the run's
  // motion. The surface says which of the two happened, because "the run
  // stopped" and "the output is limp" are different facts and the builder has
  // just caused both.
  // ---------------------------------------------------------------------------
  const pulsesOff = async (address) => {
    if (outputs === null) return;
    const output = outputs.find((each) => each.address === address);
    if (!output) return;
    const label = outputLabel(output);
    const findingPart = run !== null && run.address === address ? run.partId : null;
    if (findingPart !== null) {
      // The nudged Output's last commanded mark is not current any more: the
      // release ended the nudge somewhere the run never read.
      markNotCurrent(address);
      endRun();
    }
    try {
      await window.PAApi.postForm(
        "/api/servo",
        { arm: output.name.toLowerCase(), action: "release" },
        { timeoutMs: 4000 }
      );
    } catch (error) {
      const said = `${label} did not let go: ${window.PAApi.messageFor(error)}`;
      if (dial !== null && dial.address === address) setNote(said, "error");
      else showFeedback(said, "error");
      return;
    }
    // Which of the two things happened is said, because the builder has just
    // caused both and they are different facts (#363's inherited criterion).
    const said =
      findingPart !== null
        ? `${label} is limp, and that stopped the run finding ${partLabel(findingPart)}. ${partLabel(findingPart)} stays ${NOT_WIRED}.`
        : `${label} is limp — nothing is driving it, so it will sit wherever it is.`;
    if (dial !== null && dial.address === address) setNote(said, "success");
    showFeedback(said, findingPart !== null ? "warning" : "success");
    refresh();
  };

  // One read the dial and a capture both wait on, so what the panel shows after
  // an act is what the droid answered rather than what this page assumed.
  const refresh = () =>
    loadOutputs().catch((error) => {
      console.warn("[parts] reading the outputs failed:", error);
    });

  outputsRegion.addEventListener("click", (event) => {
    const button = event.target?.closest?.("button");
    const address = button?.closest?.("[data-output]")?.dataset.output;
    // A browser delivers no click to a disabled button; this is the rule
    // itself, the way the part-first table states it for Find by moving.
    if (!address || button.disabled) return;
    if (button.classList.contains("outputs-calibrate")) openDial(address);
    else if (button.classList.contains("outputs-off")) started(pulsesOff(address));
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
  //   Add to build        the Part joins the Fitted Parts (ADR 0047). Offered
  //                       only on a Part the droid does not carry.
  //   Give it an output   routes to this Part's row in the part-first table
  //                       and puts the cursor in its picker. Deliberately NOT a
  //                       third picker of its own: the mapping has two
  //                       projections of one table on this page and a third
  //                       would be a surface that can disagree with them
  //                       (CONTEXT.md "Parts"). Offered only where nothing is
  //                       mapped yet.
  //
  // No Non-RC Control consent is asked for any of them - that flag has never
  // reached POST /api/servo (ADR 0064).
  // ---------------------------------------------------------------------------
  const view = window.BodyView;
  const drawingHost = document.getElementById("bodyview-drawing");
  const railHost = document.getElementById("bodyview-panel");

  const ACTS = [
    { id: "toggle", label: "Open it" },
    { id: "fit", label: "Add to build" },
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
      domeNote = "No dome drawing on this controller.";
    }
    return { shown, domePending, domeNote };
  };

  // The Output a body Part's Open it would go through. POST /api/servo
  // addresses an Output by the name a builder already knows it by, so an
  // expander's unnamed row cannot be reached from here at all - the same bound
  // the calibration dial keeps.
  const servoOutputFor = (partId) => {
    const output = outputs === null ? null : outputOf(outputs, partId);
    if (!output || output.name === "" || isLightRow(output) || !output.calibrated) return null;
    return output;
  };

  // Everything the panel says about a pick, and the one sentence that says why
  // an act cannot run. Every refusal names the next move.
  const describePick = (markerId) => {
    const marker = drawing.markerOf(markerId);
    const parts = marker.parts;
    const fitted = fittedNow();
    const unfitted = fitted === null ? [] : parts.filter((id) => fitted.indexOf(id) === -1);
    const isFitted = unfitted.length < parts.length;
    const own = parts.filter((id) => !kinds?.isLight(partById.get(id)));
    const wiredPart = own.concat(parts).find((id) => outputs !== null && outputOf(outputs, id) !== null) || null;
    const output = wiredPart === null ? null : outputOf(outputs, wiredPart);
    const servoOutput = wiredPart === null ? null : servoOutputFor(wiredPart);
    const unwired = parts.filter((id) => outputs !== null && outputOf(outputs, id) === null);
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
      why = "Ends not measured yet. Press Calibrate on its output, below.";
    } else if (output && output.name === "") {
      why = "Its output has no name this page can send to. Use the output's row below.";
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
        fit: { shown: unfitted.length > 0, enabled: unfitted.length > 0 },
        wire: {
          shown: !marker.panTilt && !marker.target && unwired.length > 0,
          enabled: parts.some((id) => rows.has(id)),
        },
      },
      why,
      marker,
      open,
      output: servoOutput,
      unfitted,
      unwired,
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

  const addToBuild = (ids, names) => {
    const fitted = fittedNow();
    if (fitted === null) return;
    const missing = ids.filter((id) => fitted.indexOf(id) === -1);
    if (missing.length === 0) return;
    window.DroidBuild.applyDroidBuild({ fitted: fitted.concat(missing) })
      .then((result) => {
        showFeedback(
          result.persisted
            ? `${names} ${missing.length === 1 ? "is" : "are"} on your droid now.`
            : `${names} did not reach the droid, so nothing was added.`,
          result.persisted ? "success" : "error"
        );
        paintBody();
      })
      .catch((error) => {
        showFeedback(`${names} was not added: ${window.PAApi.messageFor(error)}`, "error");
      });
  };

  const runAct = (actId) => {
    const markerId = drawing.selected();
    if (markerId === null) return;
    const pick = describePick(markerId);
    if (actId === "fit") {
      addToBuild(pick.unfitted, pick.unfitted.map(partLabel).join(", "));
      return;
    }
    if (actId === "wire") {
      // A route, not a write: the picker on this Part's own row is where an
      // Output is chosen, and it is the same control either table uses.
      const target = pick.unwired[0] || pick.marker.parts[0];
      const row = rows.get(target);
      if (!row) return;
      row.node.scrollIntoView?.({ block: "center" });
      row.select.focus();
      showFeedback(`Choose the output that moves ${partLabel(target)} in its row below.`);
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
          { arm: pick.output.name.toLowerCase(), action: verb },
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
  // Parts and starts it on the way back (#360).
  window.PASurface?.poll(
    () =>
      loadOutputs().then(
        () => true,
        (error) => {
          console.warn("[parts] outputs poll failed:", error);
          return false;
        }
      ),
    { cadenceMs: POLL_MS, refreshOnReturn: true }
  ).start();
})();
