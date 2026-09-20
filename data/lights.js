// =============================================================================
// data/lights.js
//
// Lights: what lights this droid has, how each one is lit, and what it can be
// told to do (CONTEXT.md "Lights", ADR 0067). The page is every Part whose
// Part Kind is a light, read from the catalog and grouped by where it sits, so
// a row added to docs/droid-parts.yaml appears here with no code change - and
// a light nobody has fitted still appears, because a builder choreographs
// before they wire.
//
// AN LED STRIP IS NOT A THING ON THE DROID. It is a Light Type: what protoArtoo
// puts on one of its own leads to light a Part (ADR 0067). So it is never a row
// here; it is what a row's lead CARRIES, read from data/output_settings.js's
// `light` fact, and the Part on that lead inherits it. Three iterations of this
// page were rejected for building the strip as a thing, and the Light Type is
// what stops the fourth.
//
// THE TWO HALVES READ DIFFERENTLY, ON PURPOSE (ADR 0067):
//
//   a body light   protoArtoo lights it, so it names its Light Type and reads
//                  on / off / flash with brightness for "how far" - the words
//                  ADR 0049 gives every Part, so one word means one thing
//                  across the droid.
//   a dome light   the dome controller owns the hardware, so it has NO Light
//                  Type and offers that controller's own modes and colours
//                  instead, under the labels a sequence already shows
//                  (data/seq_protocol_check.js). Offering it on/off/flash
//                  would hide modes its hardware has. It is NEVER "not
//                  driven": no Output of ours drives it, which is a different
//                  sentence, and a builder commands it from here today.
//
// NOTHING HERE NAMES AN OUTPUT, a pin or a board label. Which lead carries a
// light is Wiring's answer, reached from here and never repeated.
//
// NOTHING HERE IS DEVICE TRUTH. Every reading is what protoArtoo asked for:
// the strip's colour is what it set, and a dome light's state is not claimed
// at all, because nothing reports it back (ADR 0045).
//
// ONE LIT LEAD TODAY. The controller lights a single lead, so at most one body
// light is lit and the page says so plainly. The per-lead firmware change is
// staged behind this surface; when it lands, each lead answers for itself and
// the only change here is that more than one row finds a Light Type.
// =============================================================================
(() => {
  "use strict";

  const TIMING = window.PAApplyTiming;
  const kinds = window.DroidPartKind;
  const catalog = window.DroidParts;
  const domeVocabulary = window.SeqProtocolCheck?.domeLights || null;

  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const domeHost = document.getElementById("lights-dome");
  const bodyHost = document.getElementById("lights-body");
  const domeCount = document.getElementById("lights-dome-count");
  const bodyCount = document.getElementById("lights-body-count");
  const spareNote = document.getElementById("lights-body-spare");

  // Every light the catalog declares, in its order, and the Parts they sit on
  // so a light can say where it is. Branching on the Kind is
  // data/droid_part_kind.js's answer, never an id or a name match.
  const parts = Array.isArray(catalog?.parts) ? catalog.parts : [];
  const partById = new Map(parts.map((part) => [part.id, part]));
  const lights = parts.filter((part) => Boolean(kinds?.isLight(part)));

  // What the droid has answered so far; each is null until it has.
  let leads = null;      // [{ address, parts }] from GET /api/servo/outputs
  let leadLight = null;  // { [address]: Light Type } from the Outputs' answer
  let auxLed = null;     // what the controller last set its lit lead to

  // Where a light sits, in the words a builder reads on the droid: a dome
  // light is IN the panel that carries it, by the Printed Droid shorthand
  // builders type; a body light is ON the door it is set into.
  const seat = (part) => {
    const host = part.sitsOn ? partById.get(part.sitsOn) : null;
    if (!host) return "";
    return part.half === "dome" ? `in ${host.shorthand || host.name}` : `on the ${host.name}`;
  };

  // The word a sequence already calls it by, where the catalog carries one and
  // it says something the name does not. Case-folded, or "Data panel" would be
  // printed beside "Data Panel".
  const shortName = (part) => {
    const name = String(part.name || "").toLowerCase();
    return (part.aliases || []).find((word) => word && word.toLowerCase() !== name) || "";
  };

  // A light Part inherits the Light Type of the lead it is on (ADR 0067), so
  // this is two lookups and no rule of its own: which lead drives this Part,
  // and what that lead carries.
  const lightTypeFor = (part) => {
    if (leads === null || leadLight === null) return null;
    const lead = leads.find((row) => row.parts.includes(part.id));
    return lead ? leadLight[lead.address] || null : null;
  };

  // A light the builder has not fitted still shows - the droid design carries
  // it, and a list that hides what is not fitted reads as empty on a droid
  // mid-build. It says so instead.
  const fittedNote = (part) => {
    const build = window.DroidBuild?.current?.();
    if (!build || !Array.isArray(build.fitted)) return "";
    return build.fitted.indexOf(part.id) === -1 ? "Not on your droid" : "";
  };

  const feedbackNode = () => {
    const node = element("div", "feedback");
    node.setAttribute("role", "status");
    node.setAttribute("aria-live", "polite");
    node.setAttribute("aria-atomic", "true");
    return node;
  };

  const say = (node, message, variant = "") => {
    node.textContent = message;
    node.className = variant ? `feedback ${variant}` : "feedback";
  };

  const at = () => new Date().toLocaleTimeString();

  const plate = (part, lit) => {
    const node = element("div", "light-plate");
    node.dataset.part = part.id;
    const head = element("div", "light-head");
    head.appendChild(element("span", "light-name", part.name));
    head.appendChild(element("span", "light-lit", lit));
    node.appendChild(head);
    const where = [shortName(part), seat(part)].filter(Boolean).join(" · ");
    if (where) node.appendChild(element("p", "light-where", where));
    const fitted = fittedNote(part);
    if (fitted) node.appendChild(element("p", "light-fitted", fitted));
    return node;
  };

  // ---------------------------------------------------------------------------
  // The lit lead's own setting: how long the strip is
  // ---------------------------------------------------------------------------
  // Read once when the strip starts (src/tasks/aux_led.cpp), which is what the
  // timing line beside it says.
  let savedCount = null;
  let bootCount = null;
  let countValue = 1;
  let saveTimer = null;
  let saving = false;
  let saveAgain = false;

  const adoptCount = (config) => {
    if (config?.aux_led_count === undefined) return;
    savedCount = Number(config.aux_led_count);
    if (bootCount === null) bootCount = savedCount;
    countValue = savedCount;
  };

  const saveCount = async (feedback) => {
    if (!window.PAApi) return;
    if (saving) {
      saveAgain = true;
      return;
    }
    saving = true;
    say(feedback, "Saving…");
    try {
      const result = await window.PAApi.postForm("/api/config", { aux_led_count: String(countValue) }, { timeoutMs: 5000 });
      adoptCount(result.data);
      say(feedback, `Saved at ${at()}`, "success");
      paint();
    } catch (error) {
      console.error("[lights] count save failed:", error);
      say(feedback, window.PAApi.messageFor(error), "error");
      // What the droid holds, not the length it refused.
      if (savedCount !== null) countValue = savedCount;
      paint();
    } finally {
      saving = false;
      if (saveAgain) {
        saveAgain = false;
        saveCount(feedback);
      }
    }
  };

  const countField = (feedback) => {
    const wrap = element("div", "light-count-wrap");
    const field = element("label", "field light-count");
    field.appendChild(element("span", undefined, "LEDs on the strip"));
    const input = document.createElement("input");
    input.type = "number";
    input.min = "1";
    input.max = "255";
    input.step = "1";
    input.className = "type-select";
    input.value = String(countValue);
    input.addEventListener("change", () => {
      const parsed = Number(input.value);
      countValue = Number.isFinite(parsed) ? Math.max(1, Math.min(255, Math.round(parsed))) : 1;
      input.value = String(countValue);
      clearTimeout(saveTimer);
      saveTimer = setTimeout(() => saveCount(feedback), 300);
    });
    field.appendChild(input);
    wrap.appendChild(field);
    const timing = element("p", "note apply-timing");
    TIMING.paint(timing, TIMING.AT_REBOOT, {
      pending: bootCount !== null && savedCount !== null && savedCount !== bootCount,
    });
    wrap.appendChild(timing);
    return wrap;
  };

  // ---------------------------------------------------------------------------
  // A dome light
  // ---------------------------------------------------------------------------
  // The dome controller lights it, and protoArtoo asks it for a mode and a
  // colour by forwarding the command a sequence step already sends
  // (POST /api/dome/cmd; DL:<target>:<mode>:<colour>, validated at
  // src/protocol_check.cpp). Which lights answer to one is the dome's own
  // vocabulary rather than a list here: a light offers the control when one of
  // its catalog aliases is a target the dome answers to, and says so plainly
  // when none is.
  const domeTarget = (part) =>
    (part.aliases || []).find((alias) => domeVocabulary?.targets.includes(alias)) || "";

  const picker = (label, tokens, group, current) => {
    const field = element("label", "field light-pick");
    field.appendChild(element("span", undefined, label));
    const select = document.createElement("select");
    select.className = "type-select";
    tokens.forEach((token) => {
      const option = document.createElement("option");
      option.value = token;
      option.textContent = domeVocabulary.label(group, token);
      if (token === current) option.selected = true;
      select.appendChild(option);
    });
    field.appendChild(select);
    return { field, select };
  };

  const domePlate = (part) => {
    const node = plate(part, "Dome controller");
    const target = domeTarget(part);
    if (!target) {
      node.appendChild(element("p", "hint light-note", "The dome controller lights it. protoArtoo has no command for this one."));
      return node;
    }
    const mode = picker("Mode", domeVocabulary.modes, "modes", "NORMAL");
    const colour = picker("Colour", domeVocabulary.colors, "colors", "DEFAULT");
    const controls = element("div", "light-controls");
    controls.appendChild(mode.field);
    controls.appendChild(colour.field);
    node.appendChild(controls);

    const feedback = feedbackNode();
    const send = element("button", "btn", "Ask the dome");
    send.type = "button";
    send.addEventListener("click", async () => {
      if (!window.PAApi) return;
      const asked = `${domeVocabulary.label("modes", mode.select.value)}, ${domeVocabulary.label("colors", colour.select.value)}`;
      send.disabled = true;
      say(feedback, "Asking…");
      try {
        // The colour always rides along: DEFAULT is the dome's own word for
        // "the one you already use", so it is an answer rather than an
        // omission.
        await window.PAApi.postForm("/api/dome/cmd", {
          cmd: `DL:${target}:${mode.select.value}:${colour.select.value}`,
        }, { timeoutMs: 5000 });
        // What the dome does with it is the dome's. This line says what was
        // asked for and never what the light is (ADR 0045).
        say(feedback, `Asked for ${asked} at ${at()}`, "success");
      } catch (error) {
        console.error("[lights] dome command failed:", error);
        say(feedback, window.PAApi.messageFor(error), "error");
      } finally {
        send.disabled = false;
      }
    });
    const row = element("div", "button-row button-row-compact");
    row.appendChild(send);
    node.appendChild(row);
    node.appendChild(feedback);
    return node;
  };

  // ---------------------------------------------------------------------------
  // A body light
  // ---------------------------------------------------------------------------
  // protoArtoo lights it through the lead it is on, so it names that lead's
  // Light Type and reads on / off / flash with brightness (ADR 0049). The
  // commands are the controller's own: POST /api/aux-led/effect and
  // /api/aux-led/color.
  //
  // The three words map onto the firmware's four effects - on is `solid`, off
  // is `off`, flash is `blink`. A strip a sequence has left on `pulse` reads as
  // on, because it is lit, and nothing is mis-stated by saying so.
  const EFFECTS = [
    { id: "on", label: "On", effect: "solid" },
    { id: "off", label: "Off", effect: "off" },
    { id: "flash", label: "Flash", effect: "blink" },
  ];
  const wordFor = (effect) => {
    if (effect === "off" || !effect) return "off";
    return effect === "blink" ? "flash" : "on";
  };

  // Brightness is the one reading the firmware does not store: a strip has a
  // colour and no brightness field, and brightness on an LED strip IS that
  // colour scaled. So it is derived from the colour the controller holds and
  // sent back as a scaled colour, which round-trips: ask for 50% and the droid
  // reports half the colour, which reads as 50% again. The hue is kept in
  // `tint`, so a strip turned down to nothing can be turned back up to the
  // colour it had rather than to one this page invented; a strip that has
  // never been given a colour starts from white.
  let tint = { r: 255, g: 255, b: 255 };
  const brightnessOf = (colour) => Math.round((Math.max(colour.r, colour.g, colour.b) / 255) * 100);
  const scaled = (percent) => {
    const factor = Math.max(0, Math.min(100, percent)) / 100;
    const peak = Math.max(tint.r, tint.g, tint.b) || 255;
    const lift = 255 / peak;
    return {
      r: Math.round(tint.r * lift * factor),
      g: Math.round(tint.g * lift * factor),
      b: Math.round(tint.b * lift * factor),
    };
  };

  const stripColour = () => ({
    r: Math.max(0, Math.min(255, Number(auxLed?.r || 0))),
    g: Math.max(0, Math.min(255, Number(auxLed?.g || 0))),
    b: Math.max(0, Math.min(255, Number(auxLed?.b || 0))),
  });

  const ask = async (path, fields, feedback, asked) => {
    if (!window.PAApi) return;
    say(feedback, "Asking…");
    try {
      await window.PAApi.postForm(path, fields, { timeoutMs: 5000 });
      say(feedback, `${asked} at ${at()}`, "success");
    } catch (error) {
      console.error("[lights] light command failed:", error);
      say(feedback, window.PAApi.messageFor(error), "error");
    }
  };

  const litPlate = (part, type) => {
    const node = plate(part, type.label);
    const colour = stripColour();
    const feedback = feedbackNode();
    const unavailable = auxLed?.available === false;

    // What protoArtoo last set it to - never a reading off the LEDs, which
    // nothing reports back.
    const shown = element("div", "led-preview");
    const swatch = element("span", "led-swatch");
    swatch.style.backgroundColor = `rgb(${colour.r}, ${colour.g}, ${colour.b})`;
    swatch.style.opacity = unavailable || wordFor(auxLed?.effect) === "off" ? "0.35" : "1";
    shown.appendChild(swatch);
    shown.appendChild(element("span", "hint", unavailable
      ? "Recorded, but the controller could not start it"
      : `Set to ${wordFor(auxLed?.effect)}`));
    node.appendChild(shown);

    // on / off / flash, the words every Part reads (ADR 0049).
    const current = wordFor(auxLed?.effect);
    const seg = element("div", "seg light-seg");
    seg.setAttribute("role", "radiogroup");
    seg.setAttribute("aria-label", `${part.name} light`);
    EFFECTS.forEach((choice) => {
      const on = choice.id === current;
      const button = element("button", on ? "active" : "", choice.label);
      button.type = "button";
      button.setAttribute("role", "radio");
      button.setAttribute("aria-checked", on ? "true" : "false");
      button.addEventListener("click", () => {
        ask("/api/aux-led/effect", { effect: choice.effect }, feedback, `Asked for ${choice.label.toLowerCase()}`);
      });
      seg.appendChild(button);
    });
    node.appendChild(seg);

    // How far, as a light hears it.
    const level = element("label", "field light-level");
    level.appendChild(element("span", undefined, "Brightness"));
    const slider = document.createElement("input");
    slider.type = "range";
    slider.min = "0";
    slider.max = "100";
    slider.step = "5";
    slider.value = String(brightnessOf(colour));
    slider.addEventListener("change", () => {
      const next = scaled(Number(slider.value));
      ask("/api/aux-led/color", { r: String(next.r), g: String(next.g), b: String(next.b) },
        feedback, `Asked for ${slider.value}% brightness`);
    });
    level.appendChild(slider);
    node.appendChild(level);

    node.appendChild(countField(feedback));
    node.appendChild(feedback);
    return node;
  };

  const unlitPlate = (part) => {
    const node = plate(part, "Not lit");
    node.appendChild(element("p", "hint light-note", "Nothing lights it yet. Wiring says what is on each lead."));
    const link = element("a", "btn", "Open Wiring");
    link.href = "#wiring";
    const row = element("div", "button-row button-row-compact");
    row.appendChild(link);
    node.appendChild(row);
    return node;
  };

  // ---------------------------------------------------------------------------
  // Drawing
  // ---------------------------------------------------------------------------
  const paint = () => {
    const dome = lights.filter((part) => part.half === "dome");
    const body = lights.filter((part) => part.half !== "dome");

    if (domeHost) {
      const plates = element("div", "light-plates");
      dome.forEach((part) => plates.appendChild(domePlate(part)));
      domeHost.replaceChildren(plates);
    }
    if (domeCount) {
      const commandable = dome.filter((part) => domeTarget(part)).length;
      domeCount.textContent = `${dome.length} lights · ${commandable} take a command`;
    }

    if (bodyHost) {
      const plates = element("div", "light-plates");
      body.forEach((part) => {
        const type = lightTypeFor(part);
        plates.appendChild(type ? litPlate(part, type) : unlitPlate(part));
      });
      bodyHost.replaceChildren(plates);
    }
    const lit = body.filter((part) => lightTypeFor(part)).length;
    if (bodyCount) {
      bodyCount.textContent = lit === 0 ? `${body.length} lights · none lit yet` : `${body.length} lights · ${lit} lit`;
    }

    // A lead carrying a light that lights nothing declared is worth saying: the
    // strip is on and the droid cannot say what it is lighting. A Part is given
    // its lead on Parts, so that is where this points.
    if (spareNote) {
      const carried = leadLight === null ? [] : Object.values(leadLight).filter(Boolean);
      const spare = carried.length > 0 && lit === 0;
      spareNote.classList.toggle("hidden", !spare);
      if (spare) {
        spareNote.textContent = `A lead carries an ${carried[0].label} with no light on it yet. Parts is where a light is given its lead.`;
      }
    }
  };

  // ---------------------------------------------------------------------------
  // What the droid says
  // ---------------------------------------------------------------------------
  // What each lead carries is the Outputs' one saved answer
  // (data/output_settings.js). This page listens and never writes: a listener
  // hears only the next change, so it asks for the answer it may have missed -
  // ensure() reads it if nobody has, redraw() replays it if somebody already
  // did.
  window.PAOutputSettings?.onChange((state, facts) => {
    if (!state || !Array.isArray(facts)) return;
    leadLight = {};
    facts.forEach((fact) => {
      leadLight[fact.address] = fact.light || null;
    });
    paint();
  });
  window.PAOutputSettings?.ensure?.();
  window.PAOutputSettings?.redraw?.();

  // Which lead drives which Part is the droid's own table, and the same read
  // Parts and Servos make.
  const loadLeads = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    if (!api) throw new Error("no way to reach the Body Controller");
    const answer = await api.get("/api/servo/outputs");
    const table = answer?.data?.outputs;
    if (!Array.isArray(table)) throw new Error("the droid's outputs answer carries no table");
    leads = table.map((row) => ({
      address: String(row.address),
      parts: Array.isArray(row.parts) ? row.parts.map(String) : [],
    }));
    paint();
  };

  // One read serves the strip's length and the Droid Build a light reads "Not
  // on your droid" from. The Build is adopted from this payload rather than
  // fetched again, which is what DroidBuild.adopt() exists for.
  const loadConfig = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    if (!api) throw new Error("no way to reach the Body Controller");
    const result = await api.get("/api/config");
    adoptCount(result.data);
    window.DroidBuild?.adopt?.(result.data);
    paint();
  };

  const renderStatus = (status) => {
    auxLed = status?.auxLed || null;
    const colour = stripColour();
    // Remember the hue it is showing, so brightness can put it back.
    if (colour.r || colour.g || colour.b) tint = colour;
    paint();
  };

  // Rethrows, so a section run or the surface poll can tell a read that landed
  // from one that did not (#360).
  const refreshLiveStatus = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
  };

  paint();

  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/droid_parts.js": "the parts catalog",
      "/droid_part_kind.js": "the parts catalog",
      "/droid_build.js": "this droid's build",
      "/seq_protocol_check.js": "the dome's commands",
      "/output_settings.js": "what each lead carries",
      "/lights.js": "the lights",
    });
    window.PABootstrap.registerSection("lights", loadConfig, { label: "the droid's lights" });
    window.PABootstrap.registerSection("lights-leads", loadLeads, { label: "what lights each lead" });
  } else {
    loadConfig().catch((error) => console.warn("[lights] config unavailable:", error));
    loadLeads().catch((error) => console.warn("[lights] leads unavailable:", error));
  }

  // SSE-first for what the strip was set to, with visibility-aware fallback
  // polling the shell stops while another surface is on screen (ADR 0048).
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderStatus(payload);
    });
    const last = window.PAStatusStream.getLastStatus();
    if (last) {
      renderStatus(last);
    } else {
      refreshLiveStatus().catch((error) => console.warn("[lights] status read failed:", error));
    }
  } else {
    window.PASurface?.poll(refreshLiveStatus, {
      cadenceMs: 5000,
      runOnStart: true,
      refreshOnReturn: true,
    }).start();
  }
})();
