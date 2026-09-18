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
// planned rather than missing. Nothing here names a product id. What a host
// adds is only what the registry cannot know: which Component Toggle stands
// behind the family on this page, and what still works when it is off.
//
// THREE CARD KINDS, each an option id. A `supported` card is a product, a
// `roadmap` card is a product we intend to carry, and `not-fitted` is the
// answer "nothing in this category" - a card, not a checkbox. The kind never
// drives the badge: a card's STATE does (chosen, planned, not included, on this
// board), which is what keeps a planned card from reading as a declined one.
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
  const MEMBER_FIELDS = {
    sound: {
      param: "soundMember",
      saved: (config) => config?.components?.audio?.member,
    },
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
  const categoryOf = (family) => (lineup?.categories || []).find((category) => category.id === family) || null;

  // Can the controller be told to use it. The firmware refuses anything else
  // (src/web/api_config_apply.cpp), so this is the same rule, not a second one.
  const isSelectable = (part) => part.status === KIND_SUPPORTED && part.included === true;

  // Is there a picture of it. Asked of the document, never of the build: the
  // legacy asset set inlines a symbol per product, the default set's partial is
  // empty and the photograph is served instead (ADR 0065).
  const artSymbolFor = (id) => document.getElementById(`art-${id}`);

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
      return MEMBER_FIELDS[entry.family] && active && active !== part.id ? "chosen-after-restart" : "chosen";
    }
    // Shown, not asked: the one product of its family this image carries.
    if (!isChoosable(entry) && partsOf(entry.family).filter(isSelectable).length === 1) return "on-this-board";
    return "available";
  };

  const BADGES = {
    chosen: "Fitted",
    "chosen-after-restart": "After restart",
    planned: "Roadmap",
    "not-included": "Not included",
    "on-this-board": "On this board",
    available: "",
  };

  // The family's answer in the fewest words, for guided Setup's rail.
  const answerFor = (family) => {
    const entry = mounts.find((mount) => mount.family === family);
    if (!entry || !lineup) return "";
    if (!isChoosable(entry)) {
      const shown = partsOf(family).filter(isSelectable);
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

  // The picture frame. Always drawn, the same size whatever fills it, and
  // decorative: the product's name is in text on the card.
  const artFrame = (id) => {
    const frame = element("span", "component-card-art");
    frame.setAttribute("aria-hidden", "true");
    if (!id) return frame;
    if (artSymbolFor(id)) {
      const svgNs = "http://www.w3.org/2000/svg";
      const svg = document.createElementNS(svgNs, "svg");
      svg.setAttribute("viewBox", "0 0 400 300");
      svg.setAttribute("focusable", "false");
      const use = document.createElementNS(svgNs, "use");
      use.setAttribute("href", `#art-${id}`);
      svg.appendChild(use);
      frame.appendChild(svg);
      return frame;
    }
    const image = element("img");
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
    frame.appendChild(image);
    return frame;
  };

  // One option, drawn as one plate: the picture, a pill saying what state it
  // is in, the product's name, and the one sentence a state owes. The plate
  // classes are the Droid Build's (#368), which the operator approved as the
  // look of a picker card.
  const optionPlate = (entry, option, chosen, interactive) => {
    const { kind, id, name, blurb, state } = option;
    // A press exists only where a pick writes something; a roadmap card and a
    // card in a family that is shown rather than asked are words and a picture.
    const asButton = kind !== KIND_ROADMAP && isChoosable(entry);
    const pressable = asButton && interactive && state !== "not-included";

    const plate = element(kind === KIND_ROADMAP ? "article" : "div", "droid-build-plate component-plate");
    plate.dataset.option = id;
    plate.dataset.kind = kind;
    plate.dataset.state = state;
    if (state === "planned") plate.classList.add("availability-settled-no");
    if (state === "not-included") plate.classList.add("availability-change-elsewhere");
    if (chosen === id) plate.classList.add("is-chosen");

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

    face.appendChild(artFrame(kind === KIND_NOT_FITTED ? null : id));
    const head = element("span", "droid-build-card-head");
    const badge = BADGES[state] || "";
    if (badge) head.appendChild(pill(badge));
    face.appendChild(head);
    face.appendChild(element("span", "droid-build-card-label", name));
    if (blurb) face.appendChild(element("span", "droid-build-card-blurb", blurb));
    plate.appendChild(face);
    return plate;
  };

  const optionsFor = (entry, chosen) => {
    const options = partsOf(entry.family).map((part) => {
      const state = stateOf(entry, part, chosen);
      let blurb = "";
      if (state === "planned") blurb = ROADMAP_SENTENCE;
      else if (state === "not-included") {
        blurb = window.PAFeatureAvailability?.reasonFor("not-in-this-build", part.name) || "";
      }
      return { kind: part.status === KIND_ROADMAP ? KIND_ROADMAP : KIND_SUPPORTED, id: part.id, name: part.name, blurb, state };
    });
    if (entry.toggleId) {
      options.push({
        kind: KIND_NOT_FITTED,
        id: NOT_FITTED,
        name: "Not fitted",
        blurb: entry.notFitted,
        state: chosen === NOT_FITTED ? "chosen" : "available",
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
    window.PAConfiguration.applyComponentPick({
      toggleId: entry.toggleId || "",
      enabled: optionId !== NOT_FITTED,
      params,
    });
    // Drawn from the toggle straight away; the member is drawn once the droid
    // has answered, because until then it is not the droid's answer.
    renderAll();
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
   * when it is off; a host without a toggle is shown, not asked.
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

  window.ComponentPicker = { mount, adopt, answerFor, onChange };
})();
