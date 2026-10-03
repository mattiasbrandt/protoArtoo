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
// RUN ON A DROID IS A SECOND MARK, NEVER A STATE. The registry says which
// supported products the project has seen run on a real droid (CONTEXT.md
// "Confirmed on a Droid", #455). A card that has carries a quiet mark beside
// whatever its state badge says; it takes no Status Color, and it never
// orders, preselects or recommends - the cards stay in the registry's order,
// because lineup products are peers. Read off the row's own field, never off
// a product id.
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
  // When a pick of the member takes effect is its Setting's own timing, read
  // off the member's entry (data/web_api.js, #432): the sound module is bound
  // once at start (ADR 0042), so a chosen card that is not yet the one running
  // says so; the RC Radio changes nothing on the controller, so it is never
  // waiting on anything.
  const MEMBER_FIELDS = {
    sound: {
      param: "soundMember",
      saved: (config) => config?.components?.audio?.member,
    },
    radio_controller: {
      param: "rcMember",
      saved: (config) => config?.rc?.member,
    },
    // The board's GPIO alone, or a PCA9685 beside it (#444): bound once at
    // start, like the sound module.
    body_servo_controller: {
      param: "bodyServoMember",
      saved: (config) => config?.components?.bodyServo?.member,
    },
  };
  const memberTiming = (family) =>
    MEMBER_FIELDS[family] ? window.PAApi.timingOf(MEMBER_FIELDS[family].param) : null;

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
    // The Radio Controller's Not fitted answer: no radio and no receiver, a
    // droid driven from the web alone (CONTEXT.md "Radio Controller"). The
    // droid stores it as a receiver type and, in the same save, clears the
    // radio and every RC channel (configApply()); this page unticks the
    // channels too, because a save sends every tick it holds.
    notFitted: "not_fitted",
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

  // Whether a supported product has run on a real droid: the lineup's
  // `confirmed_on_droid`, a project fact beside `status` (docs/api.md). A
  // roadmap row never has - there is no driver to have run - so its own answer
  // is not asked. The two are compared strictly because a controller whose
  // firmware is older than its web assets sends no such field at all (the two
  // are uploaded separately): that card gets neither the mark nor the note,
  // since the droid has said neither.
  const CONFIRMED_MARK = "Run on a droid";
  const NOT_YET_RUN_SENTENCE = "Built. Not yet run on a droid.";
  const hasRunOnDroid = (part) => part.status === KIND_SUPPORTED && part.confirmed_on_droid === true;
  const saysNotYetRun = (part) => part.status === KIND_SUPPORTED && part.confirmed_on_droid === false;
  const confirmedMark = () => element("span", "component-confirmed", CONFIRMED_MARK);

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

  // The product on the droid in a family with no Component Member (Foot Drive,
  // Dome Rotation, Dome Controller): the one row of it this image can drive.
  // null until the lineup has answered, and null for none or for more than
  // one - a family with a choice is answered by chosenPart(), not guessed here.
  const fittedPart = (family) => {
    const selectable = partsOf(family).filter(isSelectable);
    return selectable.length === 1 ? selectable[0] : null;
  };

  const toggleFor = (entry) => (entry.toggleId ? document.getElementById(entry.toggleId) : null);

  // The Radio Controller has no Component Toggle: its Not fitted answer is the
  // receiver type above, with no radio stored beside it. A radio stored while
  // the receiver still says not fitted is a builder fitting one again - the
  // radio is picked and its RC Receiver is the next answer - so the radio
  // wins and Not fitted is not lit.
  const RADIO_FAMILY = "radio_controller";
  const radioNotFitted = () =>
    RC_RECEIVER.saved(config) === RC_RECEIVER.notFitted && !MEMBER_FIELDS[RADIO_FAMILY].saved(config);

  // Whether a family offers the Not fitted card: its toggle going off, or the
  // radio's own answer.
  const offersNotFitted = (entry) => Boolean(entry.toggleId) || entry.family === RADIO_FAMILY;

  // The channel ticks the radio's Not fitted answer turns off, by their input
  // ids: the rows Configuration declared in the block the host names.
  const channelToggleIds = (entry) => [...(entry.receiverChannels?.querySelectorAll("[data-feature-entry]") || [])]
    .map((row) => row.querySelector("input")?.id)
    .filter(Boolean);

  // A family is CHOSEN on this page when a pick writes something: its Component
  // Toggle, its Component Member, or both. A family with neither is shown, not
  // asked - the Body Controller is the board this image runs on.
  const isChoosable = (entry) => Boolean(entry.toggleId || MEMBER_FIELDS[entry.family]);

  // Which option the droid holds for a family, or null for none.
  const chosenOption = (entry) => {
    if (!config || !isChoosable(entry)) return null;
    const toggle = toggleFor(entry);
    if (toggle && !toggle.checked) return NOT_FITTED;
    if (entry.family === RADIO_FAMILY && radioNotFitted()) return NOT_FITTED;
    const member = MEMBER_FIELDS[entry.family];
    if (member) return member.saved(config) || null;
    // A toggle and no member: the family has one product this image drives,
    // and the toggle being on is that product being fitted (ADR 0042).
    return fittedPart(entry.family)?.id || null;
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
      const applies = memberTiming(entry.family);
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
      ? window.PAApplyTiming.badge(memberTiming(entry.family))
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

  // One picture frame for a product or design card, and the one lookup behind
  // it (ADR 0065): the asset set's line drawing when the page inlined a symbol
  // `art-<id>`, else the photograph `/<id>.webp`, else nothing - in a frame the
  // same size whatever fills it. Never asking which set the image was built
  // with: the document answers that.
  //
  // Two pickers draw pictures, these product cards and the Droid Build's design
  // cards (#369, data/droid_build_picker.js), and a second copy of this lookup
  // would be a second answer to keep in step: the Droid Build reads it as
  // window.PAProductArt, published below.
  //
  // The drawings a page has found. A set is fixed for the life of the image,
  // so a symbol found once is there for good - but a surface the builder has
  // left is out of the document, and a card redrawn then cannot find it. Were
  // that to fall through to a photograph, the legacy set, which carries none,
  // would leave the builder an empty frame where the drawing was (#404).
  const drawn = new Set();

  // The frame is decorative: every card names its product or design in text.
  const artFrame = (id) => {
    const box = document.createElement("span");
    box.className = "component-card-art";
    box.setAttribute("aria-hidden", "true");
    if (!id) return box;
    if (document.getElementById(`art-${id}`)) drawn.add(id);
    if (drawn.has(id)) {
      const svgNs = "http://www.w3.org/2000/svg";
      const svg = document.createElementNS(svgNs, "svg");
      svg.setAttribute("viewBox", "0 0 400 300");
      svg.setAttribute("focusable", "false");
      const use = document.createElementNS(svgNs, "use");
      use.setAttribute("href", `#art-${id}`);
      svg.appendChild(use);
      box.appendChild(svg);
      return box;
    }
    const image = document.createElement("img");
    image.alt = "";
    // A photograph this set does not carry leaves the frame empty, never
    // dimmed: a missing picture is a cosmetic gap, not a part the board
    // cannot take (CONTEXT.md "Component Picker").
    image.onerror = () => image.remove();
    // Image fetches wait for the one-shot deferred-asset sweep, so the event
    // stream opens before they compete for the controller's connections; a
    // card drawn after that sweep sets its source directly (#202).
    if (window.PAAssetsReady) image.src = `/${id}.webp`;
    else image.dataset.deferredSrc = `/${id}.webp`;
    box.appendChild(image);
    return box;
  };

  window.PAProductArt = { frame: artFrame };

  // One option, drawn as one plate: the picture, a pill saying what state it
  // is in, the mark of a product that has run on a droid, the product's name,
  // and the notes it owes - each its own short line, never one long one. The
  // plate classes are the Droid Build's (#368), which the operator approved
  // as the look of a picker card.
  const optionPlate = (entry, option, chosen, interactive) => {
    const { kind, id, name, notes, route, state, confirmed } = option;
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
    if (confirmed) head.appendChild(confirmedMark());
    face.appendChild(head);
    face.appendChild(element("span", "droid-build-card-label", name));
    notes.forEach((note) => face.appendChild(element("span", "droid-build-card-blurb", note)));
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
    if (hasRunOnDroid(part)) card.appendChild(confirmedMark());
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
      // What the state owes, then what the project has not seen yet: two
      // notes, so a card this image leaves out still says both.
      const notes = [blurb, saysNotYetRun(part) ? NOT_YET_RUN_SENTENCE : ""].filter(Boolean);
      return {
        kind: part.status === KIND_ROADMAP ? KIND_ROADMAP : KIND_SUPPORTED,
        id: part.id,
        name: part.name,
        notes,
        route,
        state,
        confirmed: hasRunOnDroid(part),
      };
    });
    if (offersNotFitted(entry)) {
      options.push({
        kind: KIND_NOT_FITTED,
        id: NOT_FITTED,
        name: "Not fitted",
        notes: [entry.notFitted].filter(Boolean),
        state: chosen === NOT_FITTED ? "declined" : "available",
        confirmed: false,
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
    // The radio's Not fitted is one answer: no receiver, and every channel off.
    const radioDeclined = entry.family === RADIO_FAMILY && optionId === NOT_FITTED;
    if (radioDeclined) params[RC_RECEIVER.param] = RC_RECEIVER.notFitted;
    const toggleIds = radioDeclined ? channelToggleIds(entry) : [entry.toggleId].filter(Boolean);
    window.PAConfiguration.applyComponentPick({
      toggleIds,
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

  // Whether the droid holds the Radio Controller's Not fitted answer, which is
  // an answer rather than "none picked yet" - ask answered() first.
  const isRadioNotFitted = () => Boolean(config) && radioNotFitted();

  // The RC Receiver the controller reads, found by the wire its rcInputMode
  // speaks, or null.
  const chosenReceiverPart = () => {
    const wire = wireOfMode(RC_RECEIVER.saved(config));
    if (!wire) return null;
    return partsOf("radio_controller").find((part) => isReceiverRow(part) && part.protocol === wire) || null;
  };

  // The product a family holds on the droid, whichever way the family answers
  // (Wiring's product cards, #458): its Component Member where the family has
  // one, else the one product this image can drive. A family with a member is
  // never answered from the lineup: an image carrying a single sound module
  // would otherwise name a module the builder never chose. null for none, and
  // until the lineup and the config have both answered.
  const productOf = (family) => (MEMBER_FIELDS[family] ? chosenPart(family) : fittedPart(family));

  // One lineup entry by its id, or null before the lineup has answered.
  const partOf = (id) => (lineup?.parts || []).find((part) => part.id === id) || null;

  const shownCard = (part) => {
    const plate = element("div", "droid-build-plate component-plate is-chosen component-plate-shown");
    plate.dataset.option = part.id;
    const face = element("div", "droid-build-card component-card");
    face.appendChild(artFrame(artIdFor(part.id)));
    if (hasRunOnDroid(part)) {
      const head = element("span", "droid-build-card-head");
      head.appendChild(confirmedMark());
      face.appendChild(head);
    }
    face.appendChild(element("span", "droid-build-card-label", part.name));
    plate.appendChild(face);
    return plate;
  };

  // Whether the fitted Foot Drive reports readings back: its family's
  // capability word, DRIVE_CAP_REPORTS_FEEDBACK (include/drive_capabilities.h,
  // #446), mirrored because a page cannot include a header. The family has no
  // Component Member, so the lineup's supported, included row is the one on
  // the droid (fittedPart()). null until the lineup has answered; false for
  // none, or for a lineup that cannot say. data/drive.js reads the same bit
  // for its wheel controller card (fittedFootDriveReportsFeedback()), from a
  // lineup read of its own, because Foot Drive does not load this file.
  const DRIVE_CAP_REPORTS_FEEDBACK = 0x01;
  const footDriveReportsFeedback = () => {
    // Asked here, not left to fittedPart(): its null is "not known yet" and
    // "none" alike, and this answer keeps the two apart.
    if (!lineup) return null;
    const fitted = fittedPart("foot_drive");
    if (!fitted) return false;
    return (Number(fitted.capabilities) & DRIVE_CAP_REPORTS_FEEDBACK) !== 0;
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
    fittedPart,
    productOf,
    partOf,
    isRadioNotFitted,
    footDriveReportsFeedback,
    shownCard,
  };
})();
