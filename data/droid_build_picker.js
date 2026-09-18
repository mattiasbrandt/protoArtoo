// =============================================================================
// data/droid_build_picker.js
//
// The Droid Build step: which droid the builder built, stated as a Dome Design
// and a Body Design, each at its Design Variant (ADR 0047, #333, #368).
//
// ONE BUILDER, DRAWN INTO A HOST. The Droid Build is asked once during guided
// Setup and stated again wherever it is changed later (CONTEXT.md "Droid
// Build"), so this draws into whatever host element a surface hands it and
// keeps no markup of its own in any page. Configuration hosts it today, and
// guided Setup shows that same host as its step (data/setup.js); a second home
// mounts it the same way and cannot drift from the first.
//
// IT RENDERS THE CATALOG, IT DOES NOT RESTATE IT. Every card is a row of
// docs/droid-parts.yaml's `designs:` block, read from the generated
// window.DroidParts: the label, the blurb, the card kind, the halves a design
// is offered for, its variants and which one is the default. Nothing here names
// a design id.
//
// IT CALLS THE SEAM, IT NEVER WRITES CONFIG. A pick goes through
// window.DroidBuild.applyDroidBuild(), which seeds the chosen design's
// complement and never removes a Part already fitted (data/droid_build.js).
// This file has no path to POST /api/config of its own, which is what keeps a
// pick here from quietly replacing the Fitted Parts with a design's list.
//
// THREE CARD KINDS, and only two of them are controls. A `supported` card and
// the `own-build` card ("my own build") are buttons. A `roadmap` card is static
// content - not a disabled button - so there is nothing on it to press, and
// "greyed but still clickable" cannot happen (CONTEXT.md "Component Picker").
//
// THE STATED DOME DESIGN STANDS. A connected dome may report panels the stated
// design does not list, or miss ones it does. That difference is shown under
// the Dome Design and nothing else happens: the builder's statement is theirs
// to change, never the dome's to overwrite (CONTEXT.md "Dome Design", #333).
// =============================================================================
(() => {
  "use strict";

  // The two halves of the answer, and the fields each goes to the seam under.
  const HALVES = [
    { key: "dome", title: "Dome Design", designField: "domeDesign", variantField: "domeVariant" },
    { key: "body", title: "Body Design", designField: "bodyDesign", variantField: "bodyVariant" },
  ];

  // The catalog's card kinds (docs/droid-parts.yaml `card:`). Only the one
  // that is never a control is named here; every other kind is a button.
  const CARD_ROADMAP = "roadmap";

  const designs = () => window.DroidParts?.designs || [];
  const designById = (id) => designs().find((design) => design.id === id) || null;
  const preselectedDesign = () => designs().find((design) => design.preselected) || null;

  // A design names the halves it is offered for only when it is not both; no
  // key means both (docs/droid-parts.yaml `halves:`).
  const offeredFor = (half) =>
    designs().filter((design) => !Array.isArray(design.halves) || design.halves.includes(half));

  const variantOf = (design, variantId) =>
    (Array.isArray(design?.variants) ? design.variants : []).find((variant) => variant.id === variantId) || null;

  // "MK4 Complex", "MK4.1", "Own build": what a half is stated as, in the
  // catalog's own short words.
  const answerLabel = (choice) => {
    const design = designById(choice?.design);
    if (!design) return "";
    const variant = variantOf(design, choice.variant);
    return variant ? `${design.short} ${variant.label}` : design.short;
  };

  // The answer a fresh controller records: the pre-selected design at its own
  // default variant. Named on screen so MK4 is a default the builder can see,
  // never one they have to assume.
  const isDefaultAnswer = (choice) => {
    const preselected = preselectedDesign();
    return Boolean(preselected) &&
      choice?.design === preselected.id &&
      choice?.variant === (preselected.defaultVariant || "");
  };

  // The answer in the fewest words, for a rail chip.
  const summary = () => {
    const build = window.DroidBuild?.current?.();
    if (!build) return "";
    const dome = designById(build.dome.design)?.short || "";
    const body = designById(build.body.design)?.short || "";
    return dome && body ? `${dome} · ${body}` : dome || body;
  };

  // ---------------------------------------------------------------------------
  // The dome's own report, set against the stated Dome Design
  // ---------------------------------------------------------------------------
  const domeDifferenceText = () => {
    const difference = window.DomeLayout?.statedDesignDifference?.();
    if (!difference || !difference.comparable) return "";
    const clauses = [];
    if (difference.domeOnly.length > 0) clauses.push(`has ${difference.domeOnly.join(", ")}`);
    if (difference.designOnly.length > 0) clauses.push(`lacks ${difference.designOnly.join(", ")}`);
    if (clauses.length === 0) return "";
    return `The connected dome differs from ${difference.designLabel}: it ${clauses.join(" and ")}. Your answer stands until you change it.`;
  };

  // ---------------------------------------------------------------------------
  // Drawing
  // ---------------------------------------------------------------------------
  const mounts = [];
  let busy = false;

  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const pill = (text) => element("span", "status-pill pill-info", text);

  const designCard = (half, design, chosen, interactive) => {
    const roadmap = design.card === CARD_ROADMAP;
    // A roadmap card is static content, never a control: no button, no
    // listener, nothing a press could reach (CONTEXT.md "Component Picker").
    const card = element(roadmap ? "div" : "button", "droid-build-card");
    card.dataset.design = design.id;
    if (roadmap) {
      card.classList.add("availability-settled-no");
    } else {
      card.type = "button";
      card.setAttribute("role", "radio");
      card.setAttribute("aria-checked", chosen ? "true" : "false");
      if (chosen) card.classList.add("is-chosen");
      card.disabled = !interactive;
      card.addEventListener("click", () => {
        if (chosen) return;
        choose(half, design.id, design.defaultVariant || "");
      });
    }

    const head = element("span", "droid-build-card-head");
    head.appendChild(element("span", "droid-build-card-label", design.label));
    if (roadmap) head.appendChild(pill("Roadmap"));
    if (design.preselected) head.appendChild(pill("Default"));
    card.appendChild(head);
    card.appendChild(element("span", "droid-build-card-blurb", design.blurb));
    return card;
  };

  const variantRow = (half, design, choice, interactive) => {
    // The shared segmented control: one recess, the chosen answer lit.
    const row = element("div", "seg droid-build-variants");
    row.setAttribute("role", "radiogroup");
    row.setAttribute("aria-label", `${half.title} Variant`);
    design.variants.forEach((variant) => {
      const chosen = choice.variant === variant.id;
      const button = element("button", "droid-build-variant", variant.label);
      button.type = "button";
      button.dataset.variant = variant.id;
      button.setAttribute("role", "radio");
      button.setAttribute("aria-checked", chosen ? "true" : "false");
      if (chosen) button.classList.add("active");
      button.disabled = !interactive;
      button.addEventListener("click", () => {
        if (chosen) return;
        choose(half, design.id, variant.id);
      });
      row.appendChild(button);
    });
    return row;
  };

  const renderHalf = (half, build, interactive) => {
    const section = element("div", "droid-build-half");
    section.dataset.half = half.key;
    const choice = build ? build[half.key] : null;

    const head = element("div", "sect");
    head.appendChild(element("h3", "", half.title));
    let state = "finding out";
    if (choice) state = `${answerLabel(choice)}${isDefaultAnswer(choice) ? " · default" : ""}`;
    head.appendChild(element("span", "sub", state));
    section.appendChild(head);

    // Before the droid's answer has been read, the design a fresh controller
    // records is drawn as the one shown - at its default variant, with nothing
    // pressable - so the step never renders a design without its variants
    // while it waits.
    const preselected = preselectedDesign();
    const shown = choice || (preselected
      ? { design: preselected.id, variant: preselected.defaultVariant || "" }
      : null);

    const cards = element("div", "droid-build-cards");
    cards.setAttribute("role", "radiogroup");
    cards.setAttribute("aria-label", half.title);
    offeredFor(half.key).forEach((design) => {
      // One option per design: its card, and - on the design shown - its
      // variants directly under it, as the sub-selection that belongs to it
      // (operator, 2026-09-18 on #368). A design with no variants has none.
      const option = element("div", "droid-build-option");
      option.dataset.option = design.id;
      option.appendChild(designCard(half, design, choice?.design === design.id, interactive));
      if (shown?.design === design.id && Array.isArray(design.variants)) {
        option.appendChild(variantRow(half, design, shown, interactive));
      }
      cards.appendChild(option);
    });
    section.appendChild(cards);

    const chosenDesign = designById(choice?.design);

    // A complement nobody has written down is said out loud, never drawn as an
    // empty half: fitting nothing silently would read as "your dome has no
    // panels" (data/droid_build.js, unknownComplement).
    if (chosenDesign && chosenDesign.card !== CARD_ROADMAP &&
        !window.DroidBuild.complementFor(choice.design, choice.variant, half.key).known) {
      section.appendChild(element(
        "p",
        "note note-info droid-build-unknown",
        `The ${answerLabel(choice)} ${half.key} parts are not written down yet. None were fitted for it.`,
      ));
    }

    if (half.key === "dome") {
      const differs = domeDifferenceText();
      if (differs) section.appendChild(element("p", "note note-act droid-build-dome-differs", differs));
    }
    return section;
  };

  const render = (mount) => {
    const build = window.DroidBuild?.current?.() || null;
    // Nothing is a control until the droid's own answer has been read. A pick
    // before then would be applied over an empty build, and the Fitted Parts it
    // wrote back would be the design's list alone - every Part the builder had
    // added, gone.
    const interactive = Boolean(build) && !busy;

    const body = element("div", "droid-build-body");
    HALVES.forEach((half) => body.appendChild(renderHalf(half, build, interactive)));
    // Under the cards, never in a tooltip: the one sentence this step turns on.
    body.appendChild(element("p", "note note-info droid-build-boundary",
      "This seeds your parts, it does not fence them."));
    mount.body.replaceChildren(body);

    if (mount.summary) {
      mount.summary.textContent = build ? `${build.fitted.length} parts fitted` : "finding out";
    }
    mount.options.onRender?.();
  };

  const renderAll = () => mounts.forEach(render);

  const setFeedback = (message, variant = "") => {
    mounts.forEach((mount) => {
      if (!mount.feedback) return;
      mount.feedback.textContent = message;
      mount.feedback.className = variant ? `feedback ${variant}` : "feedback";
    });
  };

  // ---------------------------------------------------------------------------
  // A pick
  // ---------------------------------------------------------------------------
  const choose = async (half, designId, variantId) => {
    if (busy || !window.DroidBuild?.current?.()) return;
    busy = true;
    renderAll();
    setFeedback("Saving…");
    let result = null;
    try {
      result = await window.DroidBuild.applyDroidBuild({
        [half.designField]: designId,
        [half.variantField]: variantId,
      });
    } catch (error) {
      console.error("[droid-build] applying a pick failed:", error);
    }
    busy = false;

    if (!result || !result.persisted) {
      // The seam has already drawn the pick; the droid did not take it. Read
      // back what it actually holds rather than leave a surface showing an
      // answer the droid does not have.
      setFeedback("The droid did not save that. Showing what it holds.", "error");
      await window.DroidBuild.load({ refresh: true });
      renderAll();
      return;
    }
    renderAll();
    if (result.unknownComplement.includes(half.key)) {
      setFeedback(`Saved. No ${half.key} parts fitted: they are not written down yet.`, "success");
    } else if (result.seeded.length > 0) {
      setFeedback(`Saved. ${result.seeded.length} parts fitted.`, "success");
    } else {
      setFeedback("Saved. No parts added.", "success");
    }
  };

  /**
   * Draw the builder into a host.
   *
   * @param {object} hosts
   * @param {Element} hosts.body - where the halves and their cards go
   * @param {Element} [hosts.summary] - a section subtitle to carry the count
   * @param {Element} [hosts.feedback] - the feedback line under the builder
   * @param {object} [options]
   * @param {Function} [options.onRender] - called after every redraw
   */
  const mount = (hosts, options = {}) => {
    if (!hosts?.body) return;
    const entry = { ...hosts, options };
    mounts.push(entry);
    render(entry);
  };

  window.DroidBuild?.onChange(renderAll);
  window.DomeLayout?.onChange(renderAll);

  window.DroidBuildPicker = { mount, summary };
})();
