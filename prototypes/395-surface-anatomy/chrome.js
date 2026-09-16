// =============================================================================
// #398 - the Operator Shell's chrome, drawn once for the three mockups.
//
// Topbar (identity, the Latching Estop as chrome), the nav rail ordered by
// Activity Group (CONTEXT.md), and the Status Plate: eight cells in fixed
// positions and one freshness statement. The topbar additionally carries the
// mockup's board switch, which is a review control rather than product chrome;
// board.js owns it and says why the shipped image has none. Each page carries
// only its own body; this file paints the frame around it, the way
// data/shell.js does for the shipped surfaces. Nothing here talks to a
// controller: PA_STATE is fictional.
//
// Icons: an inline SVG <symbol> sprite injected once per document, so a page
// opened from file:// can <use> it (an external sprite cannot be reached
// cross-file there). Paths are Material Design Icons 7.4.47 (Pictogrammers,
// Apache 2.0), unmodified; see MDI-LICENSE.txt and the README for the list.
// =============================================================================
(() => {
  const state = Object.assign({
    droidName: "artoo",
    mode: "Driving",
    mood: "Mid-Awake",
    estop: false,
    sleep: false,
    page: document.body.dataset.page || "home",
  }, window.PA_STATE || {});

  // MDI 7.4.47 paths, keyed by their MDI name. 24x24 viewBox.
  const ICONS = {
    "view-dashboard-outline": "M19,5V7H15V5H19M9,5V11H5V5H9M19,13V19H15V13H19M9,17V19H5V17H9M21,3H13V9H21V3M11,3H3V13H11V3M21,11H13V21H21V11M11,15H3V21H11V15Z",
    "steering": "M13,19.92C14.8,19.7 16.35,18.95 17.65,17.65C18.95,16.35 19.7,14.8 19.92,13H16.92C16.7,14 16.24,14.84 15.54,15.54C14.84,16.24 14,16.7 13,16.92V19.92M10,8H14L17,11H19.92C19.67,9.05 18.79,7.38 17.27,6C15.76,4.66 14,4 12,4C10,4 8.24,4.66 6.73,6C5.21,7.38 4.33,9.05 4.08,11H7L10,8M11,19.92V16.92C10,16.7 9.16,16.24 8.46,15.54C7.76,14.84 7.3,14 7.08,13H4.08C4.3,14.77 5.05,16.3 6.35,17.6C7.65,18.9 9.2,19.67 11,19.92M12,2C14.75,2 17.1,3 19.05,4.95C21,6.9 22,9.25 22,12C22,14.75 21,17.1 19.05,19.05C17.1,21 14.75,22 12,22C9.25,22 6.9,21 4.95,19.05C3,17.1 2,14.75 2,12C2,9.25 3,6.9 4.95,4.95C6.9,3 9.25,2 12,2Z",
    "rotate-360": "M12 7C6.5 7 2 9.2 2 12C2 14.2 4.9 16.1 9 16.8V20L13 16L9 12V14.7C5.8 14.1 4 12.8 4 12C4 10.9 7 9 12 9S20 10.9 20 12C20 12.7 18.5 13.9 16 14.5V16.6C19.5 15.8 22 14.1 22 12C22 9.2 17.5 7 12 7Z",
    "volume-high": "M14,3.23V5.29C16.89,6.15 19,8.83 19,12C19,15.17 16.89,17.84 14,18.7V20.77C18,19.86 21,16.28 21,12C21,7.72 18,4.14 14,3.23M16.5,12C16.5,10.23 15.5,8.71 14,7.97V16C15.5,15.29 16.5,13.76 16.5,12M3,9V15H7L12,20V4L7,9H3Z",
    "controller-classic-outline": "M17.5,7A5.5,5.5 0 0,1 23,12.5A5.5,5.5 0 0,1 17.5,18C15.79,18 14.27,17.22 13.26,16H10.74C9.73,17.22 8.21,18 6.5,18A5.5,5.5 0 0,1 1,12.5A5.5,5.5 0 0,1 6.5,7H17.5M6.5,9A3.5,3.5 0 0,0 3,12.5A3.5,3.5 0 0,0 6.5,16C7.9,16 9.1,15.18 9.66,14H14.34C14.9,15.18 16.1,16 17.5,16A3.5,3.5 0 0,0 21,12.5A3.5,3.5 0 0,0 17.5,9H6.5M5.75,10.25H7.25V11.75H8.75V13.25H7.25V14.75H5.75V13.25H4.25V11.75H5.75V10.25M16.75,12.5A1,1 0 0,1 17.75,13.5A1,1 0 0,1 16.75,14.5A1,1 0 0,1 15.75,13.5A1,1 0 0,1 16.75,12.5M18.75,10.5A1,1 0 0,1 19.75,11.5A1,1 0 0,1 18.75,12.5A1,1 0 0,1 17.75,11.5A1,1 0 0,1 18.75,10.5Z",
    "timeline-outline": "M4 2V8H2V2H4M2 22V16H4V22H2M5 12C5 13.11 4.11 14 3 14C1.9 14 1 13.11 1 12C1 10.9 1.9 10 3 10C4.11 10 5 10.9 5 12M24 6V18C24 19.11 23.11 20 22 20H10C8.9 20 8 19.11 8 18V14L6 12L8 10V6C8 4.89 8.9 4 10 4H22C23.11 4 24 4.89 24 6M10 6V18H22V6H10Z",
    "tune-variant": "M8 13C6.14 13 4.59 14.28 4.14 16H2V18H4.14C4.59 19.72 6.14 21 8 21S11.41 19.72 11.86 18H22V16H11.86C11.41 14.28 9.86 13 8 13M8 19C6.9 19 6 18.1 6 17C6 15.9 6.9 15 8 15S10 15.9 10 17C10 18.1 9.1 19 8 19M19.86 6C19.41 4.28 17.86 3 16 3S12.59 4.28 12.14 6H2V8H12.14C12.59 9.72 14.14 11 16 11S19.41 9.72 19.86 8H22V6H19.86M16 9C14.9 9 14 8.1 14 7C14 5.9 14.9 5 16 5S18 5.9 18 7C18 8.1 17.1 9 16 9Z",
    "puzzle-outline": "M22,13.5C22,15.26 20.7,16.72 19,16.96V20A2,2 0 0,1 17,22H13.2V21.7A2.7,2.7 0 0,0 10.5,19C9,19 7.8,20.21 7.8,21.7V22H4A2,2 0 0,1 2,20V16.2H2.3C3.79,16.2 5,15 5,13.5C5,12 3.79,10.8 2.3,10.8H2V7A2,2 0 0,1 4,5H7.04C7.28,3.3 8.74,2 10.5,2C12.26,2 13.72,3.3 13.96,5H17A2,2 0 0,1 19,7V10.04C20.7,10.28 22,11.74 22,13.5M17,15H18.5A1.5,1.5 0 0,0 20,13.5A1.5,1.5 0 0,0 18.5,12H17V7H12V5.5A1.5,1.5 0 0,0 10.5,4A1.5,1.5 0 0,0 9,5.5V7H4V9.12C5.76,9.8 7,11.5 7,13.5C7,15.5 5.75,17.2 4,17.88V20H6.12C6.8,18.25 8.5,17 10.5,17C12.5,17 14.2,18.25 14.88,20H17V15Z",
    "connection": "M21.4 7.5C22.2 8.3 22.2 9.6 21.4 10.3L18.6 13.1L10.8 5.3L13.6 2.5C14.4 1.7 15.7 1.7 16.4 2.5L18.2 4.3L21.2 1.3L22.6 2.7L19.6 5.7L21.4 7.5M15.6 13.3L14.2 11.9L11.4 14.7L9.3 12.6L12.1 9.8L10.7 8.4L7.9 11.2L6.4 9.8L3.6 12.6C2.8 13.4 2.8 14.7 3.6 15.4L5.4 17.2L1.4 21.2L2.8 22.6L6.8 18.6L8.6 20.4C9.4 21.2 10.7 21.2 11.4 20.4L14.2 17.6L12.8 16.2L15.6 13.3Z",
    "wrench-outline": "M22.61,19L13.53,9.91C14.46,7.57 14,4.81 12.09,2.91C9.79,0.61 6.21,0.4 3.66,2.26L7.5,6.11L6.08,7.5L2.25,3.69C0.39,6.23 0.6,9.82 2.9,12.11C4.76,13.97 7.47,14.46 9.79,13.59L18.9,22.7C19.29,23.09 19.92,23.09 20.31,22.7L22.61,20.4C23,20 23,19.39 22.61,19M19.61,20.59L10.15,11.13C9.54,11.58 8.86,11.85 8.15,11.95C6.79,12.15 5.36,11.74 4.32,10.7C3.37,9.76 2.93,8.5 3,7.26L6.09,10.35L10.33,6.11L7.24,3C8.5,2.95 9.73,3.39 10.68,4.33C11.76,5.41 12.17,6.9 11.92,8.29C11.8,9 11.5,9.66 11.04,10.25L20.5,19.7L19.61,20.59Z",
    "wifi": "M12,21L15.6,16.2C14.6,15.45 13.35,15 12,15C10.65,15 9.4,15.45 8.4,16.2L12,21M12,3C7.95,3 4.21,4.34 1.2,6.6L3,9C5.5,7.12 8.62,6 12,6C15.38,6 18.5,7.12 21,9L22.8,6.6C19.79,4.34 16.05,3 12,3M12,9C9.3,9 6.81,9.89 4.8,11.4L6.6,13.8C8.1,12.67 9.97,12 12,12C14.03,12 15.9,12.67 17.4,13.8L19.2,11.4C17.19,9.89 14.7,9 12,9Z",
    "chip": "M6,4H18V5H21V7H18V9H21V11H18V13H21V15H18V17H21V19H18V20H6V19H3V17H6V15H3V13H6V11H3V9H6V7H3V5H6V4M11,15V18H12V15H11M13,15V18H14V15H13M15,15V18H16V15H15Z",
    "stop-circle-outline": "M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4C16.41,4 20,7.59 20,12C20,16.41 16.41,20 12,20C7.59,20 4,16.41 4,12C4,7.59 7.59,4 12,4M9,9V15H15V9",
    "power-sleep": "M18.73,18C15.4,21.69 9.71,22 6,18.64C2.33,15.31 2.04,9.62 5.37,5.93C6.9,4.25 9,3.2 11.27,3C7.96,6.7 8.27,12.39 12,15.71C13.63,17.19 15.78,18 18,18C18.25,18 18.5,18 18.73,18Z",
    "restart": "M12,4C14.1,4 16.1,4.8 17.6,6.3C20.7,9.4 20.7,14.5 17.6,17.6C15.8,19.5 13.3,20.2 10.9,19.9L11.4,17.9C13.1,18.1 14.9,17.5 16.2,16.2C18.5,13.9 18.5,10.1 16.2,7.7C15.1,6.6 13.5,6 12,6V10.6L7,5.6L12,0.6V4M6.3,17.6C3.7,15 3.3,11 5.1,7.9L6.6,9.4C5.5,11.6 5.9,14.4 7.8,16.2C8.3,16.7 8.9,17.1 9.6,17.4L9,19.4C8,19 7.1,18.4 6.3,17.6Z",
    "console-line": "M13,19V16H21V19H13M8.5,13L2.47,7H6.71L11.67,11.95C12.25,12.54 12.25,13.5 11.67,14.07L6.74,19H2.5L8.5,13Z",
    "chevron-right": "M8.59,16.58L13.17,12L8.59,7.41L10,6L16,12L10,18L8.59,16.58Z",
    "robot-outline": "M17.5 15.5C17.5 16.61 16.61 17.5 15.5 17.5S13.5 16.61 13.5 15.5 14.4 13.5 15.5 13.5 17.5 14.4 17.5 15.5M8.5 13.5C7.4 13.5 6.5 14.4 6.5 15.5S7.4 17.5 8.5 17.5 10.5 16.61 10.5 15.5 9.61 13.5 8.5 13.5M23 15V18C23 18.55 22.55 19 22 19H21V20C21 21.11 20.11 22 19 22H5C3.9 22 3 21.11 3 20V19H2C1.45 19 1 18.55 1 18V15C1 14.45 1.45 14 2 14H3C3 10.13 6.13 7 10 7H11V5.73C10.4 5.39 10 4.74 10 4C10 2.9 10.9 2 12 2S14 2.9 14 4C14 4.74 13.6 5.39 13 5.73V7H14C17.87 7 21 10.13 21 14H22C22.55 14 23 14.45 23 15M21 16H19V14C19 11.24 16.76 9 14 9H10C7.24 9 5 11.24 5 14V16H3V17H5V20H19V17H21V16Z",
    "arrow-left": "M20,11V13H8L13.5,18.5L12.08,19.92L4.16,12L12.08,4.08L13.5,5.5L8,11H20Z",
    "arrow-right": "M4,11V13H16L10.5,18.5L11.92,19.92L19.84,12L11.92,4.08L10.5,5.5L16,11H4Z",
    "play-outline": "M8.5,8.64L13.77,12L8.5,15.36V8.64M6.5,5V19L17.5,12",
    "pause": "M14,19H18V5H14M6,19H10V5H6V19Z",
  };

  const sprite = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  sprite.setAttribute("aria-hidden", "true");
  sprite.setAttribute("focusable", "false");
  sprite.style.cssText = "position:absolute;width:0;height:0;overflow:hidden";
  sprite.innerHTML = Object.entries(ICONS)
    .map(([name, d]) => `<symbol id="i-${name}" viewBox="0 0 24 24"><path d="${d}"/></symbol>`)
    .join("");
  document.body.prepend(sprite);

  const icon = (name, cls = "i") => `<svg class="${cls}" aria-hidden="true" focusable="false"><use href="#i-${name}"/></svg>`;

  // The surfaces and the Activity Groups, as CONTEXT.md states them (#288,
  // ADR 0048). Dashboard sits outside the groups as the landing; guided Setup
  // is a takeover and has no nav entry. Servos is not listed: its fate is
  // #364's, and the nav's members are decided (#288).
  const SURFACES = {
    home: ["Dashboard", "view-dashboard-outline", "dashboard.html"],
    drive: ["Foot Drive", "steering"],
    dome: ["Dome", "rotate-360"],
    sound: ["Sound", "volume-high"],
    rc: ["RC Control", "controller-classic-outline"],
    seq: ["Sequences", "timeline-outline"],
    configuration: ["Configuration", "tune-variant"],
    parts: ["Parts", "puzzle-outline", "parts.html"],
    wiring: ["Wiring", "connection"],
    maintenance: ["Maintenance", "wrench-outline"],
    wifi: ["WiFi", "wifi"],
    firmware: ["Firmware", "chip"],
  };
  const GROUPS = [
    ["Drive", "drive it, turn the dome, make some noise", ["drive", "dome", "sound", "rc"]],
    ["Perform", "author a move and play it back", ["seq", "sound", "dome"]],
    ["Configure", "say what the droid is made of", ["configuration", "parts", "wiring"]],
    ["Maintain", "check the controller over and keep it up to date", ["maintenance", "wifi", "firmware"]],
  ];
  const surfaceName = (page) => (SURFACES[page] || ["Setup"])[0];

  // No brandmark. The Codex study's one was carried here and the operator
  // dropped it on 2026-09-16, for two reasons worth keeping written down:
  // ADR 0066's identity is the INSIDE of the droid, an instrument panel, and a
  // portrait of the droid seen from outside, sitting in the top-left logo slot,
  // is the generic-web-app convention this revamp exists to leave; and it did
  // not survive its own size, drawn on a 40x44 viewBox and rendered at 26x30,
  // which put its 3-unit body slots and 4-unit dome eye at about 2 px of mud.
  // The droid's name carries that corner instead - the part that is the
  // builder's own anyway. Dashboard's line-drawn droid went the same day and
  // for a related reason (see dashboard.html), so nothing in these files is
  // original artwork any more: every glyph here is MDI.

  // The board switch. NOT product chrome: the shipped image carries one asset
  // set, chosen at build time, and has nothing to flip (board.js says where).
  // It sits in the topbar because that is where #398 asks for it, and it is
  // drawn dashed and labelled "Mockup" so nobody judges it as part of the look.
  const boardSwitch = () => {
    const b = window.PABoard;
    if (!b) return "";
    const option = ([id, cfg]) =>
      `<a href="${b.hrefFor(id)}"${id === b.id ? ' aria-current="true"' : ""}>` +
      `<span>${cfg.label}</span><small>${cfg.assetSet} · ${b.setHolds[cfg.assetSet]}</small></a>`;
    return `<div class="boardswitch" role="group" aria-label="Which board this mockup is showing">
        <span class="bs-label">Mockup<small>board</small></span>
        ${Object.entries(b.boards).map(option).join("")}
      </div>`;
  };

  // --- Topbar --------------------------------------------------------------
  const top = document.getElementById("shell-top");
  if (top) {
    const estopState = state.estop ? "Estop: latched" : "Estop: clear";
    top.className = "topbar";
    top.innerHTML = `
      <a class="brand" href="dashboard.html">
        <span><span class="brand-name">${state.droidName}</span><br><span class="brand-sub">R2-D2 Body Controller</span></span>
      </a>
      <div class="topbar-mid"><span>${surfaceName(state.page)}</span><span>/</span><b>${state.build || "MrBaddeley MK4"}</b></div>
      ${boardSwitch()}
      <div class="estop-wrap">
        <div class="estop-lines">
          <span class="estop-state" role="status">${estopState}</span>
          <span class="estop-consequence">Cuts drive now. Clear it on Foot Drive or Dashboard.</span>
        </div>
        <button class="estop" type="button" aria-label="STOP - cut drive now. Clear it on Foot Drive or Dashboard.">${icon("stop-circle-outline")}STOP</button>
      </div>`;
  }

  // --- Nav rail -------------------------------------------------------------
  const nav = document.getElementById("shell-nav");
  if (nav) {
    const link = (page) => {
      const [name, ic, href] = SURFACES[page];
      const current = page === state.page ? ' aria-current="page"' : "";
      return `<a href="${href || "#"}"${current}>${icon(ic)}${name}</a>`;
    };
    nav.className = "rail";
    nav.setAttribute("aria-label", "Operator navigation");
    nav.innerHTML =
      `<div class="rail-group">${link("home")}</div>` +
      GROUPS.map(([label, hint, members]) =>
        `<div class="rail-group" role="group" aria-label="${label}">` +
        `<p class="rail-head">${label}<small>${hint}</small></p>` +
        members.map(link).join("") + `</div>`).join("") +
      `<p class="rail-foot">v1.3.0 · fs-v1.3.0<br>artoo.local</p>`;
  }

  // --- Status Plate ---------------------------------------------------------
  // Eight cells in the order data/shell.js fixes them (#324): what bites
  // fastest first. Values, never exceptions; a chosen posture takes no colour.
  // `ok` is the green signal light and an empty class is an LED that is not
  // lit - which is what CONTROL, SLEEP and SPD carry, because a posture the
  // operator chose is a readout rather than a health claim (data/shell.js:402).
  // The shipped plate has no amber state anywhere in its vocabulary: every
  // chip reader returns live, stopped or no class at all (data/shell.js:405,
  // 414-470, 541), so no cell here is `warn` either.
  const status = document.getElementById("shell-status");
  if (status) {
    const chips = [
      ["estop", "ESTOP", state.estop ? ["stopped", "LATCHED"] : ["ok", "CLEAR"], null, "Cuts drive now"],
      ["drive", "DRIVE", state.estop ? ["stopped", "STOPPED"] : ["ok", "ARMED"], "drive"],
      ["rclink", "RC LINK", ["ok", "OK"], "rc"],
      ["control", "CONTROL", ["", "OFF"], "drive"],
      ["sleep", "SLEEP", ["", state.sleep ? "ON" : "OFF"], "home"],
      ["spd", "SPD", ["", "600"], "drive"],
      ["domelink", "DOME LINK", ["ok", "OK"], "dome"],
      ["soundlink", "SOUND LINK", ["ok", "OK"], "sound"],
    ];
    const cell = ([id, label, [cls, value], page, affordance]) => {
      const inner = `<span class="chip-label">${label}</span><span class="chip-value"><span class="dot ${cls}"></span>${value}</span>`;
      const title = page ? `Opens ${surfaceName(page)}, where this is changed` : affordance;
      return page
        ? `<a class="chip ${cls}" data-chip="${id}" href="${(SURFACES[page] || [])[2] || "#"}" title="${title}">${inner}</a>`
        : `<button class="chip chip-estop ${cls}" data-chip="${id}" type="button" title="${title}">${inner}</button>`;
    };
    status.className = "plate-region";
    status.innerHTML = `
      <div class="plate" role="group" aria-label="What the droid is doing">
        ${chips.map(cell).join("")}
        <div class="plate-fresh"><b>Last heard from the droid just now.</b><span>Press a cell to open where it is changed. ESTOP cuts drive here.</span></div>
      </div>`;
  }

  window.PA = { icon, surfaceName };
})();
