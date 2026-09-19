// =============================================================================
// data/lights.js
//
// Lights: every light on the droid on one page (operator, 2026-09-18 on #369;
// #410). The body's LED strip - which Output carries it, how many LEDs it has
// and what colour it is showing - the dome's lights, and room for the body
// lights add-ons bring later.
//
// WHICH OUTPUT CARRIES THE STRIP IS NOT THIS FILE'S. It is one saved answer
// that Wiring also sets, drawn and saved by data/output_settings.js; this file
// mounts that view here and reads the answer back through its listener, so
// the two pages can never show two routes (operator, 2026-09-19 on #410). The
// Outputs offered, and their names, are the firmware's (GET /api/config): this
// file knows no Output.
//
// The strip's count and its live colour moved here from Configuration, and
// save exactly as they did there: the count on its own field, aux_led_count,
// 1..255, after a short settle.
//
// The dome's lights are read from the parts catalog (data/droid_parts.js,
// docs/droid-parts.yaml `dome_lights:`), as what the droid has. A light whose
// catalog row says `control: none` is one protoArtoo drives nothing of yet, and
// it says so rather than wearing a control that would do nothing.
// =============================================================================
(() => {
  "use strict";

  const TIMING = window.PAApplyTiming;

  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  // ---------------------------------------------------------------------------
  // The LED strip
  // ---------------------------------------------------------------------------
  const routeSub = document.getElementById("lights-strip-route");
  const countInput = document.getElementById("lights-strip-count");
  const countTiming = document.getElementById("lights-strip-count-timing");
  const countFeedback = document.getElementById("lights-strip-count-feedback");
  const swatch = document.getElementById("lights-strip-swatch");
  const previewText = document.getElementById("lights-strip-preview-text");
  const previewNote = document.getElementById("lights-strip-preview-note");

  // The Output carrying the strip, as the shared answer names it, and the name
  // of every Output that can carry it keyed by its aux_led_pin value - which is
  // what the live status reports the running strip by.
  let carrying = null;
  let stripNames = {};
  const stripName = (pin) => stripNames[pin] || "";

  // The count the droid last answered with, and the one this page first read.
  // The droid does not report the count it started with, so this comparison
  // resets on a reload.
  let savedCount = null;
  let bootCount = null;

  const setFeedback = (node, message, variant = "") => {
    if (!node) return;
    node.textContent = message;
    node.className = variant ? `feedback ${variant}` : "feedback";
  };

  const sanitizeCount = () => {
    if (!countInput) return 1;
    const parsed = Number(countInput.value);
    const normalized = Number.isFinite(parsed)
      ? Math.max(1, Math.min(255, Math.round(parsed)))
      : 1;
    countInput.value = String(normalized);
    return normalized;
  };

  // Where the strip is routed is a CHOSEN POSTURE, not a health signal, so the
  // subtitle takes no colour (CONTEXT.md "Status Colour"). A strip on no Output
  // has no LEDs to count, so the count waits for a route.
  const paintRoute = () => {
    if (routeSub) routeSub.textContent = carrying ? `on ${carrying.name}` : "not routed";
    if (countInput) countInput.disabled = !carrying;
  };

  // The count is read once, when the strip starts (src/tasks/aux_led.cpp).
  const paintCountTiming = () => {
    TIMING.paint(countTiming, TIMING.AT_REBOOT, {
      pending: bootCount !== null && savedCount !== null && savedCount !== bootCount,
    });
  };

  const adoptCount = (config) => {
    if (config?.aux_led_count === undefined) return;
    savedCount = Number(config.aux_led_count);
    if (bootCount === null) bootCount = savedCount;
    if (countInput) countInput.value = String(savedCount);
    sanitizeCount();
    paintCountTiming();
  };

  let saveTimer = null;
  let saving = false;
  let saveAgain = false;

  const saveCount = async () => {
    if (!window.PAApi) return;
    if (saving) {
      saveAgain = true;
      return;
    }
    saving = true;
    setFeedback(countFeedback, "Saving…");
    try {
      const result = await window.PAApi.postForm("/api/config", { aux_led_count: String(sanitizeCount()) }, { timeoutMs: 5000 });
      adoptCount(result.data);
      setFeedback(countFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[lights] count save failed:", error);
      setFeedback(countFeedback, window.PAApi.messageFor(error), "error");
      // What the droid holds, not the count it refused.
      if (savedCount !== null && countInput) countInput.value = String(savedCount);
    } finally {
      saving = false;
      if (saveAgain) {
        saveAgain = false;
        saveCount();
      }
    }
  };

  countInput?.addEventListener("change", () => {
    sanitizeCount();
    clearTimeout(saveTimer);
    saveTimer = setTimeout(saveCount, 300);
  });

  window.PAOutputSettings?.mount("strip", {
    body: document.getElementById("lights-strip-outputs"),
    feedback: document.getElementById("lights-strip-outputs-feedback"),
  });
  window.PAOutputSettings?.onChange((state, facts) => {
    if (!state || !Array.isArray(facts)) return;
    carrying = facts.find((fact) => fact.carriesStrip) || null;
    stripNames = {};
    facts.forEach((fact) => {
      if (fact.strip) stripNames[fact.strip] = fact.name;
    });
    paintRoute();
  });
  // The shared answer may already be in hand - Wiring or Servos read it first -
  // and a listener hears only the next change, so ask for this one now.
  window.PAOutputSettings?.redraw?.();
  paintRoute();

  // What the strip is showing right now, from the live status: the running
  // strip's line, its colour and its effect. The swatch is the strip's own
  // colour rather than a state, and neither sentence beside it is coloured.
  const renderPreview = (status) => {
    if (!swatch || !previewText || !previewNote) return;
    const aux = status?.auxLed;
    const pin = Number(aux?.pin || 0);
    const available = aux?.available !== false;
    const effect = String(aux?.effect || "off");
    const r = Math.max(0, Math.min(255, Number(aux?.r || 0)));
    const g = Math.max(0, Math.min(255, Number(aux?.g || 0)));
    const b = Math.max(0, Math.min(255, Number(aux?.b || 0)));

    swatch.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
    swatch.style.opacity = pin > 0 && available && effect !== "off" ? "1" : "0.35";

    if (pin === 0) {
      previewText.textContent = "";
      previewNote.textContent = "";
      return;
    }
    if (!available) {
      previewText.textContent = `LED strip on ${stripName(pin) || "its output"} unavailable`;
      previewNote.textContent = "The strip is recorded, but the controller could not start it.";
      return;
    }
    previewText.textContent = `${stripName(pin) || "LED strip"} - ${effect}`;
    previewNote.textContent = `Live colour ${r},${g},${b} with the ${effect} effect.`;
  };

  // Rethrows, so a section run or the surface poll can tell a read that landed
  // from one that did not (#360).
  const refreshLiveStatus = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderPreview(result.data);
  };

  // ---------------------------------------------------------------------------
  // The dome's lights
  // ---------------------------------------------------------------------------
  const domeHost = document.getElementById("lights-dome");
  const domeSummary = document.getElementById("lights-dome-summary");

  const paintDomeLights = () => {
    if (!domeHost) return;
    const parts = Array.isArray(window.DroidParts?.parts) ? window.DroidParts.parts : [];
    const byId = new Map(parts.map((part) => [part.id, part]));
    const lights = parts.filter((part) => part.section === "dome_lights");
    if (lights.length === 0) {
      domeHost.replaceChildren(element("p", "hint", "The parts list names no dome lights."));
      if (domeSummary) domeSummary.textContent = "none listed";
      return;
    }
    const plates = element("div", "output-plates lights-dome");
    let undriven = 0;
    lights.forEach((light) => {
      const plate = element("div", "output-plate output-setting light-plate");
      plate.dataset.part = light.id;
      const head = element("div", "output-setting-head");
      head.appendChild(element("span", "toggle-label light-name", light.name));
      // What the catalog knows drives it. Only `none` is a claim this page can
      // make - nothing protoArtoo drives - and it is made in words, with no
      // control beside it.
      if (light.control === "none") {
        undriven += 1;
        head.appendChild(element("span", "toggle-status", "Not driven"));
      }
      plate.appendChild(head);
      // Where it sits: the panel it lights, by the P-number builders print.
      const host = light.sitsOn ? byId.get(light.sitsOn) : null;
      const where = [light.aliases?.[0], host ? `in ${host.shorthand || host.name}` : ""].filter(Boolean).join(" · ");
      if (where) plate.appendChild(element("p", "light-where", where));
      plates.appendChild(plate);
    });
    domeHost.replaceChildren(plates);
    if (domeSummary) {
      domeSummary.textContent = undriven === lights.length
        ? `${lights.length} on the dome · none driven`
        : `${lights.length} on the dome · ${undriven} not driven`;
    }
  };
  paintDomeLights();

  // ---------------------------------------------------------------------------
  // Loading
  // ---------------------------------------------------------------------------
  // The count is this page's own read of the config; the route is the shared
  // module's, which reads it once for every surface that mounts it.
  const loadCount = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    if (!api) throw new Error("no way to reach the Body Controller");
    const result = await api.get("/api/config");
    adoptCount(result.data);
  };

  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/droid_parts.js": "parts list",
      "/output_settings.js": "the outputs",
      "/lights.js": "the lights",
    });
    window.PABootstrap.registerSection("lights-strip", loadCount, { label: "the LED strip" });
  } else {
    loadCount().catch((error) => console.warn("[lights] strip unavailable:", error));
  }

  // SSE-first for the live colour, with visibility-aware fallback polling that
  // the shell stops while another surface is on screen (ADR 0048, #360).
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderPreview(payload);
    });
    const last = window.PAStatusStream.getLastStatus();
    if (last) {
      renderPreview(last);
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
