// =============================================================================
// data/component_picker.js
//
// The Component Picker: which product is actually fitted in each Component
// Family, chosen from cards (CONTEXT.md "Component Picker", #297, #369).
//
// ONE BUILDER, DRAWN INTO A HOST. Every Hardware components category on
// Configuration is a data-setup-step host, and guided Setup shows that same
// host as its step (data/setup.js), so the two homes are one piece of DOM and
// cannot show different choices. Configuration mounts this once; nothing in
// Setup draws a card of its own.
//
// IT RENDERS THE REGISTRY, IT DOES NOT RESTATE IT. The lineup is read from the
// controller (GET /api/identity/components): every Component Registry row is a
// card, whatever its status, so a product nothing drives yet is visible as
// planned rather than missing. What a host adds is only what the registry
// cannot know: which Component Toggle stands behind the family on this page,
// and what still works when it is off. Product ids appear in one place here,
// PRODUCT_SUB_SELECTIONS, which says what a card carries under it.
//
// THREE CARD KINDS, each an option id. A `supported` card is a product, a
// `roadmap` card is a product we intend to carry, and `not-fitted` is the
// answer "nothing in this category" - a card, not a checkbox. The kind never
// drives the badge: a card's STATE does (chosen, planned, not included), which
// is what keeps a planned card from reading as a declined one.
// A roadmap card is static content, never a disabled button, so there is
// nothing on it to press.
//
// PICKING IS APPLYING. A pick goes to the controller at once through
// Configuration's own save (window.PAConfiguration), the same save a toggle
// uses; there is no staging buffer anywhere, and the cards are redrawn from
// what the droid answered.
//
// A PICTURE AND SELECTABILITY ARE TWO LOOKUPS. The drawing, else the
// photograph, else an empty frame of the same size - and nothing about the
// picture touches whether the card can be picked, so a card missing its photo
// never reads as greyed (ADR 0065).
// =============================================================================
(() => {
  "use strict";

  const KIND_SUPPORTED = "supported";
  const KIND_ROADMAP = "roadmap";
  const KIND_NOT_FITTED = "not-fitted";

  // The one option id that is not a registry row.
  const NOT_FITTED = "not-fitted";

  // A family with a Component Member: the field a pick is written under and
  // where the saved choice is read back. The registry names the family's NVS
  // key; the form field and the config payload's shape are this page's API
  // (docs/api.md "POST /api/config").
  //
  // `applies`: when a pick of the member takes effect, in the one timing
  // vocabulary (data/apply_timing.js, #370). The sound module is bound once at
  // start (ADR 0042), so a chosen card that is not yet the one running says so;
  // the RC Radio is a statement of which product the builder holds and changes
  // nothing on the controller, so it is never waiting on anything.
  const MEMBER_FIELDS = {
    sound: {
      param: "soundMember",
      saved: (config) => config?.components?.audio?.member,
      applies: window.PAApplyTiming.AT_REBOOT,
    },
    radio_controller: {
      param: "rcMember",
      saved: (config) => config?.rc?.member,
      applies: window.PAApplyTiming.IMMEDIATE,
    },
  };

  // The RC Receiver a chosen RC Radio talks to (CONTEXT.md "RC Radio", "RC
  // Receiver"). The receivers are rows of the Radio Controller family, told
  // apart from the radios by the wire they declare, and picking one writes
  // the controller's rcInputMode. `modes[0]` is what a pick writes; a second
  // SBUS receiver is the one choice that is not a receiver product, so it
  // sits under a chosen SBUS as its own small choice (operator, 2026-09-18 on
  // #369).
  const RC_RECEIVER = {
    heading: "RC Receiver",
    param: "rcInputMode",
    saved: (config) => config?.rc?.inputMode,
    wires: {
      standard_pwm: { short: "PWM", modes: ["standard_pwm"] },
      sbus: { short: "SBUS", modes: ["single_sbus", "dual_sbus"] },
      crsf: { short: "ELRS", modes: ["elrs"], caption: "Not read yet" },
    },
    // Which channel ticks the chosen receiver reads, by mode - the firmware's
    // own rule, rcSourceEnabledForMode() (src/web/rc_diagnostics_snapshot.cpp):
    // PWM reads CH1-CH6, one SBUS receiver rides on CH1 or on CH2 when the RC
    // page routes it there, two SBUS receivers need CH1 and CH2, and ELRS reads
    // nothing. A tick the mode needs is never hidden: an SBUS receiver with its
    // input off reads nothing.
    channels: (mode, config) => {
      if (mode === "standard_pwm") return [1, 2, 3, 4, 5, 6];
      if (mode === "single_sbus") return config?.rc?.sbus?.recvCh2 ? [2] : [1];
      if (mode === "dual_sbus") return [1, 2];
      return [];
    },
    channelsLabel: "Channels",
    second: {
      wire: "sbus",
      label: "Second SBUS receiver",
      options: [
        { mode: "single_sbus", label: "Not fitted" },
        { mode: "dual_sbus", label: "Fitted" },
      ],
    },
  };

  // What a product's card carries under it. Declared once, beside the lineup,
  // so it arrives with the product row rather than living in the drawing code.
  //   receivers  the RC Receiver wires this radio can be answered with; one
  //              wire is settled and written with the radio (the HotRC DS-650
  //              is an SBUS radio, operator 2026-09-18 on #369)
  //   planned    a choice that belongs to a roadmap product, shown as planned:
  //              nothing is stored and no firmware value exists for it
  //   borrowsArt the family whose running member's picture this card shows:
  //              the body controller board's GPIO is that board, so its card
  //              is pictured with whichever Body Controller this image runs on
  //              (operator, 2026-09-18 on #369)
  const PRODUCT_SUB_SELECTIONS = {
    esp32_gpio_ledc: { borrowsArt: "body_controller" },
    hotrc_ds650: { receivers: ["sbus"] },
    rc_radio: { receivers: ["standard_pwm", "sbus", "crsf"] },
    xbox_controller: { planned: { label: "Connection", options: ["Wired USB", "Wireless adapter USB"] } },
  };

  const ROADMAP_SENTENCE = "We intend to carry it. Not yet.";

  let lineup = null;
  let config = null;
  const mounts = [];
  const listeners = new Set();

  // ---------------------------------------------------------------------------
  // What the lineup and the droid say
  // ---------------------------------------------------------------------------
  const partsOf = (family) => (lineup?.parts || []).filter((part) => part.category === family);
  // An RC Receiver row is drawn under a radio, never as a card of its own.
  const isReceiverRow = (part) => Object.hasOwn(RC_RECEIVER.wires, part.protocol);
  const cardPartsOf = (family) => partsOf(family).filter((part) => !isReceiverRow(part));
  // The product whose picture a card shows: its own, or - where it borrows -
  // the member of that family this image runs on, found in the lineup rather
  // than from identity.board, which names the build and not the product.
  const artIdFor = (id) => {
    const family = PRODUCT_SUB_SELECTIONS[id]?.borrowsArt;
    if (!family) return id;
    return artPartFor(id)?.id || null;
  };
  // The lineup entry behind that picture, so a caller that names the product
  // it pictures reads the same row (Wiring's board, #411).
  const artPartFor = (id) => {
    const family = PRODUCT_SUB_SELECTIONS[id]?.borrowsArt;
    if (!family) return (lineup?.parts || []).find((part) => part.id === id) || null;
    return partsOf(family).find((part) => part.included === true) || null;
  };
  const wireOfMode = (mode) =>
    Object.keys(RC_RECEIVER.wires).find((wire) => RC_RECEIVER.wires[wire].modes.includes(mode)) || null;
  const categoryOf = (family) => (lineup?.categories || []).find((category) => category.id === family) || null;

  // Can the controller be told to use it. The firmware refuses anything else
  // (src/web/api_config_apply.cpp), so this is the same rule, not a second one.
  const isSelectable = (part) => part.status === KIND_SUPPORTED && part.included === true;

  const toggleFor = (entry) => (entry.toggleId ? document.getElementById(entry.toggleId) : null);

  // A family is CHOSEN on this page when a pick writes something: its Component
  // Toggle, its Component Member, or both. A family with neither is shown, not
  // asked - the Body Controller is the board this image runs on.
  const isChoosable = (entry) => Boolean(entry.toggleId || MEMBER_FIELDS[entry.family]);

  // Which option the droid holds for a family, or null for none.
  const chosenOption = (entry) => {
    if (!config || !isChoosable(entry)) return null;
    const toggle = toggleFor(entry);
    if (toggle && !toggle.checked) return NOT_FITTED;
    const member = MEMBER_FIELDS[entry.family];
    if (member) return member.saved(config) || null;
    // A toggle and no member: the family has one product this image drives,
    // and the toggle being on is that product being fitted (ADR 0042).
    const selectable = partsOf(entry.family).filter(isSelectable);
    return selectable.length === 1 ? selectable[0].id : null;
  };

  // The state of one card, and the only thing the badge reads.
  const stateOf = (entry, part, chosen) => {
    if (part.status === KIND_ROADMAP) return "planned";
    // A family shown rather than asked is a lineup of peers: the Body
    // Controller this image was not built for is another product, not one
    // this controller is missing (operator, 2026-09-12 on #369).
    if (part.included !== true) return isChoosable(entry) ? "not-included" : "available";
    if (chosen === part.id) {
      const active = categoryOf(entry.family)?.active_member;
      const applies = MEMBER_FIELDS[entry.family]?.applies;
      return applies && applies !== window.PAApplyTiming.IMMEDIATE && active && active !== part.id
        ? "chosen-waiting"
        : "chosen";
    }
    // Shown, not asked: the one product of its family this image carries. It
    // takes the chosen treatment and no chip - its name already says what it
    // is (operator, 2026-09-18 on #369).
    if (!isChoosable(entry) && partsOf(entry.family).filter(isSelectable).length === 1) return "present";
    return "available";
  };

  // "declined" is a chosen Not fitted card: the answer is lit like any other,
  // and it never says Fitted. "chosen-waiting" is a chosen member the droid
  // has not started on yet, and its badge is composed from the member's timing
  // rather than typed here.
  const BADGES = {
    chosen: "Fitted",
    declined: "",
    planned: "Roadmap",
    "not-included": "Not included",
    present: "",
    available: "",
  };
  const badgeFor = (entry, state) =>
    state === "chosen-waiting"
      ? window.PAApplyTiming.badge(MEMBER_FIELDS[entry.family].applies)
      : BADGES[state] || "";

  // The family's answer in the fewest words, for guided Setup's rail.
  const answerFor = (family) => {
    const entry = mounts.find((mount) => mount.family === family);
    if (!entry || !lineup) return "";
    if (!isChoosable(entry)) {
      const shown = cardPartsOf(family).filter(isSelectable);
      return shown.length === 1 ? shown[0].name : "";
    }
    const chosen = chosenOption(entry);
    if (chosen === NOT_FITTED) return "Not fitted";
    return partsOf(family).find((part) => part.id === chosen)?.name || "";
  };

  // ---------------------------------------------------------------------------
  // Drawing
  // ---------------------------------------------------------------------------
  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const pill = (text) => element("span", "status-pill pill-info", text);

  // The picture frame and its lookup are data/product_art.js's, shared with
  // the Droid Build's design cards.
  const artFrame = (id) => window.PAProductArt.frame(id);

  // One option, drawn as one plate: the picture, a pill saying what state it
  // is in, the product's name, and the one sentence a state owes. The plate
  // classes are the Droid Build's (#368), which the operator approved as the
  // look of a picker card.
  const optionPlate = (entry, option, chosen, interactive) => {
    const { kind, id, name, blurb, route, state } = option;
    // A press exists only where a pick writes something; a roadmap card and a
    // card in a family that is shown rather than asked are words and a picture.
    const asButton = kind !== KIND_ROADMAP && isChoosable(entry);
    const pressable = asButton && interactive && state !== "not-included";
    const artId = artIdFor(id);
    const hasPicture = kind !== KIND_NOT_FITTED && Boolean(artId) && !entry.noPicture.has(artId);

    const plate = element(kind === KIND_ROADMAP ? "article" : "div", "droid-build-plate component-plate");
    plate.dataset.option = id;
    plate.dataset.kind = kind;
    plate.dataset.state = state;
    if (state === "planned") plate.classList.add("availability-settled-no");
    if (state === "not-included") plate.classList.add("availability-change-elsewhere");
    if (chosen === id || state === "present") plate.classList.add("is-chosen");

    const face = element(asButton ? "button" : "div", "droid-build-card component-card");
    if (asButton) {
      face.type = "button";
      face.setAttribute("role", "radio");
      face.setAttribute("aria-checked", chosen === id ? "true" : "false");
      face.disabled = !pressable;
      face.addEventListener("click", () => {
        if (chosen === id) return;
        choose(entry, id);
      });
    }

    // "Not fitted", and a product that has no picture by design, are words
    // alone (operator, 2026-09-18 on #369); the grid keeps each its row's
    // height. A product whose picture this set simply lacks keeps its frame,
    // empty, so it lays out like its neighbours and never reads as greyed.
    if (hasPicture) face.appendChild(artFrame(artId));
    const head = element("span", "droid-build-card-head");
    const badge = badgeFor(entry, state);
    if (badge) head.appendChild(pill(badge));
    face.appendChild(head);
    face.appendChild(element("span", "droid-build-card-label", name));
    if (blurb) face.appendChild(element("span", "droid-build-card-blurb", blurb));
    plate.appendChild(face);
    // Outside the face, which may be a button: a link inside a button is not
    // a link anybody can press.
    if (route) {
      const link = element("a", "setup-link component-route", route.label);
      link.setAttribute("href", route.href);
      plate.appendChild(link);
    }

    const sub = PRODUCT_SUB_SELECTIONS[id];
    if (sub?.receivers && chosen === id) plate.appendChild(receiverBlock(entry, sub, interactive));
    if (sub?.planned) plate.appendChild(plannedBlock(sub.planned));
    return plate;
  };

  // The sub-selection's frame: under the card's words, behind a seam, the way
  // the Droid Build draws a design's variants (#368).
  const subBlock = (label) => {
    const block = element("div", "droid-build-variant-block component-sub");
    block.appendChild(element("span", "droid-build-variant-label", label));
    return block;
  };

  // A chosen radio's RC Receiver: small receiver cards when the radio can be
  // any of several, one settled line when it is only one, and under an SBUS
  // answer the second-receiver choice.
  const receiverBlock = (entry, sub, interactive) => {
    const mode = RC_RECEIVER.saved(config);
    const wire = wireOfMode(mode);
    let block;
    if (sub.receivers.length === 1) {
      const settled = sub.receivers[0];
      block = subBlock(`${RC_RECEIVER.heading}: ${RC_RECEIVER.wires[settled].short}`);
    } else {
      block = subBlock(RC_RECEIVER.heading);
      const cards = element("div", "component-receivers");
      cards.setAttribute("role", "radiogroup");
      cards.setAttribute("aria-label", RC_RECEIVER.heading);
      partsOf(entry.family)
        .filter((part) => isReceiverRow(part) && sub.receivers.includes(part.protocol))
        .forEach((part) => cards.appendChild(receiverCard(entry, part, wire, interactive)));
      block.appendChild(cards);
    }
    const channels = receiverChannels(entry, mode, sub.receivers.includes(wire));
    if (channels) block.appendChild(channels);
    if (wire === RC_RECEIVER.second.wire && sub.receivers.includes(wire)) {
      block.appendChild(secondReceiverRow(mode, interactive));
    }
    return block;
  };

  // The channel ticks the chosen receiver reads, moved in from the page (the
  // host names the block) and filtered to the rows its mode needs. The rows
  // are Configuration's own inputs, so their keys and save do not change.
  const receiverChannels = (entry, mode, answered) => {
    const plates = entry.receiverChannels;
    if (!plates || !answered) return null;
    const wanted = RC_RECEIVER.channels(mode, config);
    if (wanted.length === 0) return null;
    plates.querySelectorAll("[data-feature-entry]").forEach((row) => {
      const channel = Number(String(row.dataset.featureEntry).replace(/^.*enable_rc_ch/, ""));
      row.hidden = !wanted.includes(channel);
    });
    const wrap = element("div", "component-sub-channels");
    wrap.appendChild(element("span", "droid-build-variant-label", RC_RECEIVER.channelsLabel));
    wrap.appendChild(plates);
    return wrap;
  };

  const receiverCard = (entry, part, wire, interactive) => {
    const on = part.protocol === wire;
    const card = element("button", "component-receiver");
    card.type = "button";
    card.dataset.option = part.id;
    card.setAttribute("role", "radio");
    card.setAttribute("aria-checked", on ? "true" : "false");
    if (on) card.classList.add("is-chosen");
    card.disabled = !interactive || !isSelectable(part);
    card.addEventListener("click", () => {
      if (on) return;
      pickReceiver(part.protocol);
    });
    if (!entry.noPicture.has(part.id)) card.appendChild(artFrame(part.id));
    card.appendChild(element("span", "component-receiver-label", part.name));
    const caption = RC_RECEIVER.wires[part.protocol].caption;
    if (caption) card.appendChild(element("span", "component-receiver-caption", caption));
    return card;
  };

  const secondReceiverRow = (mode, interactive) => {
    const { second } = RC_RECEIVER;
    const wrap = element("div", "component-sub-second");
    wrap.appendChild(element("span", "droid-build-variant-label", second.label));
    const row = element("div", "seg droid-build-variants");
    row.setAttribute("role", "radiogroup");
    row.setAttribute("aria-label", second.label);
    second.options.forEach((option) => {
      const on = mode === option.mode;
      const button = element("button", "droid-build-variant", option.label);
      button.type = "button";
      button.dataset.variant = option.mode;
      button.setAttribute("role", "radio");
      button.setAttribute("aria-checked", on ? "true" : "false");
      if (on) button.classList.add("active");
      button.disabled = !interactive;
      button.addEventListener("click", () => {
        if (on) return;
        window.PAConfiguration?.applyComponentPick({ params: { [RC_RECEIVER.param]: option.mode } });
      });
      row.appendChild(button);
    });
    wrap.appendChild(row);
    return wrap;
  };

  // A roadmap product's own choice, shown as planned: spans in the segmented
  // row, not buttons, because nothing can be chosen yet and nothing is stored.
  const plannedBlock = (planned) => {
    const block = subBlock(planned.label);
    const row = element("div", "seg droid-build-variants component-planned-row");
    row.setAttribute("aria-label", planned.label);
    planned.options.forEach((label) => {
      const option = element("span", "droid-build-variant component-link-roadmap", label);
      option.title = ROADMAP_SENTENCE;
      row.appendChild(option);
    });
    block.appendChild(row);
    return block;
  };

  // A Body Controller this image was not built for. Choosing one is not a
  // setting at all: every board runs its own build, so the move is an upload,
  // and the card says so with the route to it (operator, 2026-09-19 on #371).
  // Only this family - a product "not in this build" anywhere else is a driver
  // this image left out, and keeps that meaning. It is a fact about which image
  // is running, never a capability the board lacks (ADR 0065).
  const OTHER_BOARD = {
    family: "body_controller",
    blurb: (name) => `Needs its own firmware. Upload the ${name} build to switch.`,
    route: { href: "#firmware", label: "Open Firmware" },
  };
  const isOtherBoard = (entry, part) =>
    entry.family === OTHER_BOARD.family && part.status !== KIND_ROADMAP && part.included !== true;

  const optionsFor = (entry, chosen) => {
    const options = cardPartsOf(entry.family).map((part) => {
      const state = stateOf(entry, part, chosen);
      let blurb = "";
      let route = null;
      if (state === "planned") blurb = ROADMAP_SENTENCE;
      else if (isOtherBoard(entry, part)) {
        blurb = OTHER_BOARD.blurb(part.name);
        route = OTHER_BOARD.route;
      } else if (state === "not-included") {
        blurb = window.PAFeatureAvailability?.reasonFor("not-in-this-build", part.name) || "";
        // The same next move the seam gives every other surface for this
        // family: a driver this image left out arrives with a different image
        // (data/feature_availability.js). OTHER_BOARD above routes there too,
        // with its own sentence, because a board is not a driver.
        route = window.PAFeatureAvailability?.routeFor("not-in-this-build") || null;
      }
      return {
        kind: part.status === KIND_ROADMAP ? KIND_ROADMAP : KIND_SUPPORTED,
        id: part.id,
        name: part.name,
        blurb,
        route,
        state,
      };
    });
    if (entry.toggleId) {
      options.push({
        kind: KIND_NOT_FITTED,
        id: NOT_FITTED,
        name: "Not fitted",
        blurb: entry.notFitted,
        state: chosen === NOT_FITTED ? "declined" : "available",
      });
    }
    return options;
  };

  const render = (entry) => {
    const { host } = entry;
    if (!lineup) {
      host.replaceChildren(element("p", "hint", "Reading the lineup from the droid…"));
      return;
    }
    const toggle = toggleFor(entry);
    // Nothing is a control until the droid's own answer has been read, and
    // never while Configuration says the toggle behind it cannot be changed
    // on this controller.
    const interactive = Boolean(config) && !(toggle && toggle.disabled);
    const chosen = chosenOption(entry);

    const cards = element("div", "droid-build-cards component-cards");
    cards.setAttribute("role", isChoosable(entry) ? "radiogroup" : "group");
    cards.setAttribute("aria-label", categoryOf(entry.family)?.name || entry.family);
    optionsFor(entry, chosen).forEach((option) => {
      cards.appendChild(optionPlate(entry, option, chosen, interactive));
    });
    host.replaceChildren(cards);
  };

  const renderAll = () => {
    mounts.forEach(render);
    listeners.forEach((listener) => listener());
  };

  // ---------------------------------------------------------------------------
  // A pick
  // ---------------------------------------------------------------------------
  const choose = (entry, optionId) => {
    if (!config || !window.PAConfiguration) return;
    const member = MEMBER_FIELDS[entry.family];
    const params = {};
    if (member && optionId !== NOT_FITTED) params[member.param] = optionId;
    // A radio that can only be answered with one RC Receiver writes it in the
    // same save, unless the droid already has it (a second SBUS receiver is
    // kept).
    const settled = PRODUCT_SUB_SELECTIONS[optionId]?.receivers;
    if (settled?.length === 1 && wireOfMode(RC_RECEIVER.saved(config)) !== settled[0]) {
      params[RC_RECEIVER.param] = RC_RECEIVER.wires[settled[0]].modes[0];
    }
    window.PAConfiguration.applyComponentPick({
      toggleId: entry.toggleId || "",
      enabled: optionId !== NOT_FITTED,
      params,
    });
    // Drawn from the toggle straight away; the member is drawn once the droid
    // has answered, because until then it is not the droid's answer.
    renderAll();
  };

  // An RC Receiver pick: the wire's first mode, unless the droid already reads
  // that wire (which keeps a second SBUS receiver).
  const pickReceiver = (wire) => {
    if (!config || !window.PAConfiguration) return;
    if (wireOfMode(RC_RECEIVER.saved(config)) === wire) return;
    window.PAConfiguration.applyComponentPick({
      params: { [RC_RECEIVER.param]: RC_RECEIVER.wires[wire].modes[0] },
    });
  };

  // ---------------------------------------------------------------------------
  // Feeding it
  // ---------------------------------------------------------------------------

  // The config payload Configuration read or saved. The toggles are read from
  // Configuration's own controls, which that payload has just set; the member
  // is read from here.
  const adopt = (payload) => {
    config = payload || null;
    renderAll();
  };

  const loadLineup = async () => {
    const result = await window.PAApi.get("/api/identity/components", { timeoutMs: 5000 });
    lineup = result.data;
    renderAll();
    return true;
  };

  /**
   * Draw the picker into every family host under a root.
   *
   * A host is an element carrying data-component-family (the registry
   * category id). data-component-toggle names the id of the Component Toggle
   * behind it on this page, and data-component-not-fitted says what still works
   * when it is off; a host without a toggle or a member is shown, not asked.
   * data-component-no-picture lists the products drawn as words alone.
   *
   * @param {ParentNode} root
   */
  const mount = (root) => {
    root.querySelectorAll("[data-component-family]").forEach((host) => {
      const entry = {
        host,
        family: host.dataset.componentFamily,
        toggleId: host.dataset.componentToggle || "",
        notFitted: host.dataset.componentNotFitted || "",
        // Products that have no picture in any asset set, by design rather
        // than for want of one (test/test_tools/test_product_art.py
        // NO_PICTURE says the same).
        noPicture: new Set((host.dataset.componentNoPicture || "").split(/\s+/).filter(Boolean)),
        // The block of channel ticks a chosen RC Receiver carries, found once
        // while it is still where the page declared it.
        receiverChannels: host.dataset.componentReceiverChannels
          ? document.getElementById(host.dataset.componentReceiverChannels)
          : null,
      };
      mounts.push(entry);
      render(entry);
    });
  };

  const onChange = (listener) => {
    listeners.add(listener);
    return () => listeners.delete(listener);
  };

  if (window.PABootstrap) {
    window.PABootstrap.registerSection("component-lineup", loadLineup, { label: "the component lineup" });
  } else if (window.PAApi) {
    loadLineup().catch((error) => {
      console.error("[component-picker] lineup read failed:", error);
    });
  }

  // ---------------------------------------------------------------------------
  // What another surface shows of a family: the product the droid holds, as a
  // card drawn here - the same plate, picture and name a picker card has - but
  // read-only. The RC page shows the radio and the receiver this way, and they
  // are chosen only on Configuration (operator, 2026-09-19 on #412).
  // ---------------------------------------------------------------------------

  // Whether the droid has answered both reads the shown cards come from - the
  // lineup and the config. Until it has, chosenPart() and chosenReceiverPart()
  // return null for "not known yet" as well as for "none picked", so a caller
  // asks this first and never reads the first as the second.
  const answered = () => Boolean(lineup && config);

  // The product a family's Component Member names, or null until the lineup and
  // the config have both answered, or when the droid holds none.
  const chosenPart = (family) => {
    const member = MEMBER_FIELDS[family]?.saved(config);
    if (!member) return null;
    return partsOf(family).find((part) => part.id === member) || null;
  };

  // The RC Receiver the controller reads, found by the wire its rcInputMode
  // speaks, or null.
  const chosenReceiverPart = () => {
    const wire = wireOfMode(RC_RECEIVER.saved(config));
    if (!wire) return null;
    return partsOf("radio_controller").find((part) => isReceiverRow(part) && part.protocol === wire) || null;
  };

  const shownCard = (part) => {
    const plate = element("div", "droid-build-plate component-plate is-chosen component-plate-shown");
    plate.dataset.option = part.id;
    const face = element("div", "droid-build-card component-card");
    face.appendChild(artFrame(artIdFor(part.id)));
    face.appendChild(element("span", "droid-build-card-label", part.name));
    plate.appendChild(face);
    return plate;
  };

  // artIdFor and artPartFor are exported for Wiring, whose diagram pictures and
  // names the board the same way a card here does (#411), so there is one
  // board-to-picture lookup.
  window.ComponentPicker = {
    mount,
    adopt,
    answerFor,
    onChange,
    artIdFor,
    artPartFor,
    answered,
    chosenPart,
    chosenReceiverPart,
    shownCard,
  };
})();
