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
  // The markup file is full (its image block has no room), so this is written
  // here; the feedback line moves below it, since it reports a move made from
  // either table.
  const outputsSection = document.createElement("section");
  outputsSection.className = "outputs-section";
  outputsSection.innerHTML =
    `<h4>🔌 What each output drives</h4>` +
    `<p class="desc">Every output on the controller, in the order the leads plug in, with every part on it - ` +
    `a lead split to two doors names both. Pick a part in a row to put it on that output; if the part is on ` +
    `another output, this page asks before it moves it. The bar is where the controller is driving that servo ` +
    `right now and the tick is where the move ends, so the gap between them is the move still to go. Both are ` +
    `what the controller told the servo, not a reading: nothing on this droid can feel where a servo really is, ` +
    `so a jammed one shows exactly what a free one does. The table asks the droid once a second while this page ` +
    `is open.</p>` +
    `<p class="outputs-tiers" role="status" aria-live="polite">` +
    TIERS.map((tier) => `<span class="outputs-tier" data-tier="${tier.id}">${tier.label} — finding out</span>`).join("") +
    `</p><div class="parts-table-wrap" id="outputs-table"></div>`;
  tableRegion.parentNode.appendChild(outputsSection);
  if (feedback) tableRegion.parentNode.appendChild(feedback);
  const outputsRegion = outputsSection.querySelector("#outputs-table");
  const tierNodes = new Map(Array.from(outputsSection.querySelectorAll("[data-tier]"), (node) => [node.dataset.tier, node]));

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
      `<span class="outputs-us"></span></td><td class="outputs-release"></td></tr>`
    );
  };

  const outputRows = new Map();
  let outputAddresses = null;

  // The only rebuild, and only when the set of Outputs itself changes - which a
  // controller does across a reboot, not while this page is reading it (#318).
  const buildOutputs = (addresses) => {
    outputsRegion.innerHTML =
      `<table class="parts-table outputs-table"><thead><tr><th scope="col">Output</th><th scope="col">Drives</th>` +
      `<th scope="col">Commanded position</th><th scope="col">Output Release</th></tr></thead><tbody>` +
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
      });
    });
    outputAddresses = addresses;
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
    else row.release.textContent = pulsing ? "Holds where it stops" : "Limp - no pulse";
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

  // The control the builder has hold of: the one focused, or the one whose move
  // is being asked about or is on its way. Its value and its options are theirs
  // until they let go.
  const held = (id, select) => id === pendingPart || document.activeElement === select;

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
    paintOutputs(addresses);

    const wired = catalog.parts.filter((part) => outputOf(outputs, part.id) !== null).length;
    const idle = outputs.filter((output) => output.parts.length === 0).length;
    let text = `${wired} of ${catalog.parts.length} parts on an output · ${idle} of ${outputs.length} outputs driving nothing`;
    // A Part the droid drives and this page has no row for would otherwise be
    // invisible, which is the one thing this table must never be.
    const unknown = outputs.flatMap((output) => output.parts).filter((id) => !partById.has(id));
    if (unknown.length) {
      text += ` · the droid also drives ${unknown.join(", ")}, which this page's parts list does not know - upload the filesystem that matches the firmware`;
    }
    summary.textContent = text;
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
