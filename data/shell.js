// =============================================================================
// data/shell.js
//
// The Operator Shell (ADR 0048): the persistent frame every surface is shown
// inside. It owns the topbar, the nav, the identity and the status bar, and it
// survives every navigation -- content swaps beneath it in #shell-content while
// the /api/events stream opened at boot is never torn down.
//
// Addresses are hash routes (/#drive). The browser only ever asks the device
// for /, the static handler's default file answers with the shell, and the
// fragment never reaches the ESP32 -- so nothing here needs a firmware change.
//
// The eleven .html files stay addressable: each is still the single copy of its
// surface's markup, which this file fetches and mounts, and each carries a thin
// delegate that hands a direct visit over to the shell. Nothing that links to
// one has to be rewritten.
// =============================================================================
(() => {
  // ---------------------------------------------------------------------------
  // The surfaces
  //
  // `page` is the data-page identifier and is the route: it is what the shell,
  // the CSS (body[data-page="..."]) and every surface script key on, and it is
  // deliberately NOT the operator-facing name. Renaming a surface changes
  // `name`, and adds the old spelling to `aliases` so links that were already
  // written keep opening what they name; it never touches `page` (#288).
  //
  // `name` is the one place a surface is named: the nav, the browser title and
  // the Page Recovery View's "Loading: ..." all read it, so they cannot say
  // three different things about the same screen.
  // ---------------------------------------------------------------------------
  // `icon` names a symbol in the sprite below, never a character: an operator
  // surface carries no emoji, and a glyph that is an icon inherits the text
  // colour and keeps its label beside it (ADR 0066, docs/ui-copy-voice.md).
  const SURFACES = [
    { page: "home", doc: "/dashboard.html", icon: "view-dashboard-outline", name: "Dashboard", aliases: ["dashboard"] },
    // Foot Drive in full on every operator surface, because once a body servo
    // controller and the Dome ESC are both drive controllers an unqualified
    // "Drive" names three things (#288). The old spelling was also the route,
    // so the alias is the rename record rather than a second address.
    { page: "drive", doc: "/drive.html", icon: "steering", name: "Foot Drive", aliases: ["drive"] },
    { page: "dome", doc: "/dome.html", icon: "rotate-360", name: "Dome", aliases: [] },
    { page: "sound", doc: "/sound.html", icon: "volume-high", name: "Sound", aliases: [] },
    // The #398 reference has no icon for Servos: it left Servos out of the rail
    // on purpose, because CONTEXT.md "Activity Group" does not list it and its
    // fate is #364's. This is the nearest of the paths that reference committed
    // rather than a twenty-third taken from somewhere unread.
    { page: "servo", doc: "/servo.html", icon: "robot-outline", name: "Servos", aliases: ["servos"] },
    { page: "parts", doc: "/parts.html", icon: "puzzle-outline", name: "Parts", aliases: [] },
    { page: "seq", doc: "/seq.html", icon: "timeline-outline", name: "Sequences", aliases: ["sequences"] },
    { page: "rc", doc: "/rc.html", icon: "controller-classic-outline", name: "RC Control", aliases: [] },
    // Today's Setup is the surface a droid is configured from, which is what
    // the sliders name; when C3 splits it (#288, #351) the Maintenance half
    // takes wrench-outline.
    { page: "setup", doc: "/setup.html", icon: "tune-variant", name: "Setup", aliases: [] },
    { page: "wifi", doc: "/wifi.html", icon: "wifi", name: "WiFi", aliases: [] },
    { page: "firmware", doc: "/firmware.html", icon: "chip", name: "Firmware", aliases: [] },
  ];

  // ---------------------------------------------------------------------------
  // The icons
  //
  // Material Design Icons 7.4.47 (Pictogrammers), unmodified SVG path data from
  // @mdi/svg, Apache License 2.0. The notice that travels with them, the list
  // of paths taken and why each one is here: docs/icon-set-provenance.md.
  //
  // An inline <symbol> sprite, injected once with the rest of the chrome, so a
  // <use> from any mounted surface resolves inside the one document the browser
  // ever loads. Deliberately not an external sprite file: that is a second
  // request in the opening burst the controller sheds connections in, for
  // markup that is already smaller than the request.
  //
  // Only the symbols the chrome and the Dashboard ask for are here. The rest of
  // the twenty-two the #398 reference committed are in
  // prototypes/395-surface-anatomy/chrome.js, and the slice that first needs one
  // copies its path in beside these rather than shipping a symbol nothing draws.
  // ---------------------------------------------------------------------------
  const ICONS = {
    "view-dashboard-outline": "M19,5V7H15V5H19M9,5V11H5V5H9M19,13V19H15V13H19M9,17V19H5V17H9M21,3H13V9H21V3M11,3H3V13H11V3M21,11H13V21H21V11M11,15H3V21H11V15Z",
    "steering": "M13,19.92C14.8,19.7 16.35,18.95 17.65,17.65C18.95,16.35 19.7,14.8 19.92,13H16.92C16.7,14 16.24,14.84 15.54,15.54C14.84,16.24 14,16.7 13,16.92V19.92M10,8H14L17,11H19.92C19.67,9.05 18.79,7.38 17.27,6C15.76,4.66 14,4 12,4C10,4 8.24,4.66 6.73,6C5.21,7.38 4.33,9.05 4.08,11H7L10,8M11,19.92V16.92C10,16.7 9.16,16.24 8.46,15.54C7.76,14.84 7.3,14 7.08,13H4.08C4.3,14.77 5.05,16.3 6.35,17.6C7.65,18.9 9.2,19.67 11,19.92M12,2C14.75,2 17.1,3 19.05,4.95C21,6.9 22,9.25 22,12C22,14.75 21,17.1 19.05,19.05C17.1,21 14.75,22 12,22C9.25,22 6.9,21 4.95,19.05C3,17.1 2,14.75 2,12C2,9.25 3,6.9 4.95,4.95C6.9,3 9.25,2 12,2Z",
    "rotate-360": "M12 7C6.5 7 2 9.2 2 12C2 14.2 4.9 16.1 9 16.8V20L13 16L9 12V14.7C5.8 14.1 4 12.8 4 12C4 10.9 7 9 12 9S20 10.9 20 12C20 12.7 18.5 13.9 16 14.5V16.6C19.5 15.8 22 14.1 22 12C22 9.2 17.5 7 12 7Z",
    "volume-high": "M14,3.23V5.29C16.89,6.15 19,8.83 19,12C19,15.17 16.89,17.84 14,18.7V20.77C18,19.86 21,16.28 21,12C21,7.72 18,4.14 14,3.23M16.5,12C16.5,10.23 15.5,8.71 14,7.97V16C15.5,15.29 16.5,13.76 16.5,12M3,9V15H7L12,20V4L7,9H3Z",
    "controller-classic-outline": "M17.5,7A5.5,5.5 0 0,1 23,12.5A5.5,5.5 0 0,1 17.5,18C15.79,18 14.27,17.22 13.26,16H10.74C9.73,17.22 8.21,18 6.5,18A5.5,5.5 0 0,1 1,12.5A5.5,5.5 0 0,1 6.5,7H17.5M6.5,9A3.5,3.5 0 0,0 3,12.5A3.5,3.5 0 0,0 6.5,16C7.9,16 9.1,15.18 9.66,14H14.34C14.9,15.18 16.1,16 17.5,16A3.5,3.5 0 0,0 21,12.5A3.5,3.5 0 0,0 17.5,9H6.5M5.75,10.25H7.25V11.75H8.75V13.25H7.25V14.75H5.75V13.25H4.25V11.75H5.75V10.25M16.75,12.5A1,1 0 0,1 17.75,13.5A1,1 0 0,1 16.75,14.5A1,1 0 0,1 15.75,13.5A1,1 0 0,1 16.75,12.5M18.75,10.5A1,1 0 0,1 19.75,11.5A1,1 0 0,1 18.75,12.5A1,1 0 0,1 17.75,11.5A1,1 0 0,1 18.75,10.5Z",
    "timeline-outline": "M4 2V8H2V2H4M2 22V16H4V22H2M5 12C5 13.11 4.11 14 3 14C1.9 14 1 13.11 1 12C1 10.9 1.9 10 3 10C4.11 10 5 10.9 5 12M24 6V18C24 19.11 23.11 20 22 20H10C8.9 20 8 19.11 8 18V14L6 12L8 10V6C8 4.89 8.9 4 10 4H22C23.11 4 24 4.89 24 6M10 6V18H22V6H10Z",
    "tune-variant": "M8 13C6.14 13 4.59 14.28 4.14 16H2V18H4.14C4.59 19.72 6.14 21 8 21S11.41 19.72 11.86 18H22V16H11.86C11.41 14.28 9.86 13 8 13M8 19C6.9 19 6 18.1 6 17C6 15.9 6.9 15 8 15S10 15.9 10 17C10 18.1 9.1 19 8 19M19.86 6C19.41 4.28 17.86 3 16 3S12.59 4.28 12.14 6H2V8H12.14C12.59 9.72 14.14 11 16 11S19.41 9.72 19.86 8H22V6H19.86M16 9C14.9 9 14 8.1 14 7C14 5.9 14.9 5 16 5S18 5.9 18 7C18 8.1 17.1 9 16 9Z",
    "puzzle-outline": "M22,13.5C22,15.26 20.7,16.72 19,16.96V20A2,2 0 0,1 17,22H13.2V21.7A2.7,2.7 0 0,0 10.5,19C9,19 7.8,20.21 7.8,21.7V22H4A2,2 0 0,1 2,20V16.2H2.3C3.79,16.2 5,15 5,13.5C5,12 3.79,10.8 2.3,10.8H2V7A2,2 0 0,1 4,5H7.04C7.28,3.3 8.74,2 10.5,2C12.26,2 13.72,3.3 13.96,5H17A2,2 0 0,1 19,7V10.04C20.7,10.28 22,11.74 22,13.5M17,15H18.5A1.5,1.5 0 0,0 20,13.5A1.5,1.5 0 0,0 18.5,12H17V7H12V5.5A1.5,1.5 0 0,0 10.5,4A1.5,1.5 0 0,0 9,5.5V7H4V9.12C5.76,9.8 7,11.5 7,13.5C7,15.5 5.75,17.2 4,17.88V20H6.12C6.8,18.25 8.5,17 10.5,17C12.5,17 14.2,18.25 14.88,20H17V15Z",
    "robot-outline": "M17.5 15.5C17.5 16.61 16.61 17.5 15.5 17.5S13.5 16.61 13.5 15.5 14.4 13.5 15.5 13.5 17.5 14.4 17.5 15.5M8.5 13.5C7.4 13.5 6.5 14.4 6.5 15.5S7.4 17.5 8.5 17.5 10.5 16.61 10.5 15.5 9.61 13.5 8.5 13.5M23 15V18C23 18.55 22.55 19 22 19H21V20C21 21.11 20.11 22 19 22H5C3.9 22 3 21.11 3 20V19H2C1.45 19 1 18.55 1 18V15C1 14.45 1.45 14 2 14H3C3 10.13 6.13 7 10 7H11V5.73C10.4 5.39 10 4.74 10 4C10 2.9 10.9 2 12 2S14 2.9 14 4C14 4.74 13.6 5.39 13 5.73V7H14C17.87 7 21 10.13 21 14H22C22.55 14 23 14.45 23 15M21 16H19V14C19 11.24 16.76 9 14 9H10C7.24 9 5 11.24 5 14V16H3V17H5V20H19V17H21V16Z",
    "wifi": "M12,21L15.6,16.2C14.6,15.45 13.35,15 12,15C10.65,15 9.4,15.45 8.4,16.2L12,21M12,3C7.95,3 4.21,4.34 1.2,6.6L3,9C5.5,7.12 8.62,6 12,6C15.38,6 18.5,7.12 21,9L22.8,6.6C19.79,4.34 16.05,3 12,3M12,9C9.3,9 6.81,9.89 4.8,11.4L6.6,13.8C8.1,12.67 9.97,12 12,12C14.03,12 15.9,12.67 17.4,13.8L19.2,11.4C17.19,9.89 14.7,9 12,9Z",
    "chip": "M6,4H18V5H21V7H18V9H21V11H18V13H21V15H18V17H21V19H18V20H6V19H3V17H6V15H3V13H6V11H3V9H6V7H3V5H6V4M11,15V18H12V15H11M13,15V18H14V15H13M15,15V18H16V15H15Z",
    "stop-circle-outline": "M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4C16.41,4 20,7.59 20,12C20,16.41 16.41,20 12,20C7.59,20 4,16.41 4,12C4,7.59 7.59,4 12,4M9,9V15H15V9",
    "power-sleep": "M18.73,18C15.4,21.69 9.71,22 6,18.64C2.33,15.31 2.04,9.62 5.37,5.93C6.9,4.25 9,3.2 11.27,3C7.96,6.7 8.27,12.39 12,15.71C13.63,17.19 15.78,18 18,18C18.25,18 18.5,18 18.73,18Z",
    "restart": "M12,4C14.1,4 16.1,4.8 17.6,6.3C20.7,9.4 20.7,14.5 17.6,17.6C15.8,19.5 13.3,20.2 10.9,19.9L11.4,17.9C13.1,18.1 14.9,17.5 16.2,16.2C18.5,13.9 18.5,10.1 16.2,7.7C15.1,6.6 13.5,6 12,6V10.6L7,5.6L12,0.6V4M6.3,17.6C3.7,15 3.3,11 5.1,7.9L6.6,9.4C5.5,11.6 5.9,14.4 7.8,16.2C8.3,16.7 8.9,17.1 9.6,17.4L9,19.4C8,19 7.1,18.4 6.3,17.6Z",
    "console-line": "M13,19V16H21V19H13M8.5,13L2.47,7H6.71L11.67,11.95C12.25,12.54 12.25,13.5 11.67,14.07L6.74,19H2.5L8.5,13Z",
    "chevron-right": "M8.59,16.58L13.17,12L8.59,7.41L10,6L16,12L10,18L8.59,16.58Z",
  };

  const spriteHtml = () =>
    `<svg class="sprite" aria-hidden="true" focusable="false">` +
    Object.entries(ICONS)
      .map(([name, d]) => `<symbol id="i-${name}" viewBox="0 0 24 24"><path d="${d}"/></symbol>`)
      .join("") +
    `</svg>`;

  // An icon always travels with a label, so it is hidden from assistive
  // technology: the visible text beside it is the accessible name, and a second
  // copy of that name in a title or an aria-label is a second thing to keep in
  // step (WCAG 2.5.3).
  const icon = (name, className = "i") =>
    `<svg class="${className}" aria-hidden="true" focusable="false"><use href="#i-${name}"/></svg>`;

  // ---------------------------------------------------------------------------
  // The Activity Groups
  //
  // The nav is ordered by the job a builder is doing rather than by firmware
  // subsystem -- Drive, Perform, Configure, Maintain (ADR 0048, #288). A
  // surface may appear in more than one group, because Sound and Dome are
  // reached for both when driving and when authoring: a group is a way to find
  // something, never a claim to own it, and letting a surface appear twice
  // costs a second entry pointing at the same route rather than a special case
  // in the renderer.
  //
  // A member row names a surface by its `page` identifier and never by its
  // name, so this table cannot drift from what the nav says and a rename is
  // still one field in SURFACES. A member with no SURFACES row renders
  // nothing, and a group with nothing to render draws nothing -- which is why
  // the whole table is written now, dormant rows included: the surfaces those
  // rows name land in their group the day their SURFACES row is added, with no
  // change to the nav. If one of those tickets picks a different `page`, one
  // string here is the whole retrofit.
  // ---------------------------------------------------------------------------
  const ACTIVITY_GROUPS = [
    {
      id: "drive",
      label: "Drive",
      hint: "drive it, turn the dome, make some noise",
      members: ["drive", "dome", "sound", "rc"],
    },
    {
      id: "perform",
      label: "Perform",
      hint: "author a move and play it back",
      members: ["seq", "sound", "dome"],
    },
    {
      id: "configure",
      label: "Configure",
      hint: "say what the droid is made of",
      members: [
        // Droid Build heads the group: it is the answer the rest of Configure
        // is shaped by. Dormant until the C3 group (#351 and its siblings)
        // lands it as a destination (#288).
        "droidbuild",
        // Today's Setup is the surface a droid is actually configured from --
        // Hardware Components, LED Strip, Droid Identity. It leaves this row
        // when C3 (#288) splits it into Configuration and Maintenance, not
        // before. Guided Setup, the first-run takeover that leaves the nav for
        // good (#351), is a different thing and is never in a group.
        "setup",
        // Servos stays beside Parts. What becomes of it now that Parts carries
        // the output-to-part mapping is proposed on #347 and not yet decided.
        "servo",
        "parts",
        "wiring", // dormant until C2a (#350)
      ],
    },
    {
      id: "maintain",
      label: "Maintain",
      hint: "check the controller over and keep it up to date",
      members: [
        // Dormant until the C3 group splits today's Setup into Configuration
        // and Maintenance (#288, #351).
        "maintenance",
        "wifi",
        "firmware",
      ],
    },
  ];

  const DEFAULT_PAGE = "home";

  // Setup is the guided first-run takeover, not a place to come back to, so a
  // cold boot must never land there. The reference solves this by never writing
  // the authoring desk to the durable value at all, rather than by filtering it
  // on the way out (r2d2-astromech-simulator src/js/config/workspaces.js:208),
  // and that asymmetry is the point: the runtime answer CAN be Setup -- a
  // reload of /#setup honours its address -- while the remembered answer
  // structurally cannot be.
  const NEVER_REMEMBERED = new Set(["setup"]);

  // Two maps, because an Activity Group's member row names a `page` and
  // nothing else: an alias resolves an address a builder typed, and must not
  // put a surface in a group it was never given a row in.
  const surfaceByPage = new Map(SURFACES.map((surface) => [surface.page, surface]));

  const surfaceFor = new Map(surfaceByPage);
  SURFACES.forEach((surface) => {
    surface.aliases.forEach((alias) => surfaceFor.set(alias, surface));
  });

  // Which groups hold a surface, and which surfaces no group claims. Both are
  // read from the one table above, so adding a group is adding a row.
  const pagesInGroup = new Map(ACTIVITY_GROUPS.map((group) => [group.id, new Set(group.members)]));
  const groupedPages = new Set(ACTIVITY_GROUPS.flatMap((group) => group.members));
  // Dashboard is the landing and sits outside the groups on purpose: it
  // answers what the droid is doing rather than what the builder is doing
  // (ADR 0048). Anything else no group claims is drawn beside it rather than
  // falling out of the nav, so a surface is reachable the moment it exists.
  const ungroupedSurfaces = SURFACES.filter((surface) => !groupedPages.has(surface.page));

  // The legacy address of each surface, plus the two spellings of the shell's
  // own document, so a link written before hash routes still opens what it
  // names.
  const surfaceForPath = new Map([
    ["/", surfaceFor.get(DEFAULT_PAGE)],
    ["/index.html", surfaceFor.get(DEFAULT_PAGE)],
  ]);
  SURFACES.forEach((surface) => surfaceForPath.set(surface.doc, surface));

  // ---------------------------------------------------------------------------
  // The remembered surface
  //
  // One serialised object, so a reader can assert against what was actually
  // stored rather than against the in-memory answer -- a regression that only
  // shows on the next cold boot has to be able to fail now.
  // ---------------------------------------------------------------------------
  const STORAGE_KEY = "pa.shell.v1";

  const readStored = () => {
    try {
      const raw = window.localStorage?.getItem(STORAGE_KEY);
      const parsed = raw ? JSON.parse(raw) : null;
      return parsed && typeof parsed === "object" ? parsed : {};
    } catch (_error) {
      // Storage unavailable or holding something this version cannot read:
      // start from nothing rather than failing the boot over a preference.
      return {};
    }
  };

  const writeStored = (patch) => {
    try {
      window.localStorage?.setItem(STORAGE_KEY, JSON.stringify({ ...readStored(), ...patch }));
    } catch (_error) {
      // A browser refusing storage costs the operator the memory of where they
      // were, and nothing else.
    }
  };

  const rememberSurface = (page) => {
    if (NEVER_REMEMBERED.has(page)) return;
    writeStored({ surface: page });
  };

  // The stored value is also corrected on the way in, and the correction is
  // written back: a store that says Setup -- from a hand edit, or from a
  // version that wrote it -- must stop saying Setup rather than be re-filtered
  // on every boot.
  const rememberedSurface = () => {
    const stored = readStored().surface;
    if (surfaceFor.has(stored) && !NEVER_REMEMBERED.has(stored)) return surfaceFor.get(stored);
    if (stored !== undefined) writeStored({ surface: DEFAULT_PAGE });
    return surfaceFor.get(DEFAULT_PAGE);
  };

  // ---------------------------------------------------------------------------
  // Identity
  // ---------------------------------------------------------------------------
  let identityName = "protoartoo";
  let currentSurface = null;

  const applyIdentityName = (name) => {
    identityName = String(name || "protoartoo");
    // "<surface> - <droid>", the same way round on every surface. Dashboard and
    // Sound used to put the droid first, which read as a different page rather
    // than as the same page named differently.
    const surface = currentSurface || surfaceFor.get(DEFAULT_PAGE);
    document.title = `${surface.name} - ${identityName}`;
    document.querySelectorAll("[data-identity-name]").forEach((el) => {
      el.textContent = identityName;
    });
    // The topbar's where-line, the nav and the browser title all read the same
    // field, so they cannot say three different things about one screen (#288).
    const where = document.getElementById("shell-where");
    if (where) where.textContent = surface.name;
  };

  // Layer 1 validation: ensure the identity manifest conforms to the expected shape
  // before it reaches feature availability resolvers. Protects against null, non-objects,
  // and responses missing required structure. Returns null if validation fails, otherwise
  // returns the validated identity.
  const validateIdentityShape = (identity) => {
    // Identity must be an object
    if (!identity || typeof identity !== "object" || Array.isArray(identity)) {
      return null;
    }
    // board must be a string
    if (typeof identity.board !== "string") {
      return null;
    }
    // board_capabilities and build_flags must be objects if present
    if (!identity.board_capabilities || (typeof identity.board_capabilities !== "object" || Array.isArray(identity.board_capabilities) || identity.board_capabilities === null)) {
      return null;
    }
    if (!identity.build_flags || (typeof identity.build_flags !== "object" || Array.isArray(identity.build_flags) || identity.build_flags === null)) {
      return null;
    }
    // Every present value in board_capabilities and build_flags must be a boolean
    for (const value of Object.values(identity.board_capabilities)) {
      if (typeof value !== "boolean") {
        return null;
      }
    }
    for (const value of Object.values(identity.build_flags)) {
      if (typeof value !== "boolean") {
        return null;
      }
    }
    return identity;
  };

  // Dispatches an identity outcome and keeps what it said, so a surface that
  // mounts after it happened can still be told. See replaySessionFacts().
  let lastIdentityEvent = null;

  const announceIdentity = (event) => {
    lastIdentityEvent = { type: event.type, detail: event.detail };
    if (typeof window.dispatchEvent === "function") {
      window.dispatchEvent(event);
    }
  };

  // Publish the shell's once-per-session identity result for feature consumers.
  // Identity is fetched once at boot and cached in window.PAIdentity.
  // Feature availability resolution reads this cache only and never probes endpoints
  // to discover capabilities — the manifest is authoritative and must not be rediscovered.
  // Setup listens to the event, while the cache closes late-load ordering gaps.
  // Layer 1 validation ensures the manifest conforms to the expected shape before
  // pa:identity-available is published; invalid manifests are treated as unavailable.
  const publishIdentity = (identity) => {
    const validatedIdentity = validateIdentityShape(identity);
    window.PAIdentity = validatedIdentity;
    if (validatedIdentity) {
      announceIdentity(new CustomEvent("pa:identity-available", { detail: validatedIdentity }));
    } else {
      // Invalid manifest shape is treated as unavailable
      announceIdentity(new CustomEvent("pa:identity-unavailable", { detail: { error: "invalid manifest", reason: "incompatible" } }));
    }
    return validatedIdentity;
  };

  const loadIdentity = async ({ handle = null } = {}) => {
    let result;
    try {
      const api = handle || window.PAApi;
      result = api
        ? await api.get("/api/identity")
        : { data: await fetch("/api/identity", { cache: "no-store" }).then((r) => r.json()) };
    } catch (error) {
      // Transport failure: retryable. Unchanged behaviour.
      console.warn("[shell] identity unavailable:", error);
      announceIdentity(new CustomEvent("pa:identity-unavailable", { detail: { error, reason: "no-response" } }));
      throw error;
    }

    applyIdentityName(result.data?.droidName);

    // publishIdentity is the single validation boundary and has already
    // dispatched pa:identity-unavailable if the manifest is unusable.
    if (!publishIdentity(result.data)) {
      const error = new Error("identity manifest failed validation");
      error.kind = "incompatible";
      error.status = 200;   // the response was a valid 2xx; its content was not
      throw error;          // terminal: the bootstrap maps this to failed-terminal
    }
  };

  // Facts the session settled before this surface existed. A surface mounted
  // into the shell runs its scripts long after boot, so an event it would have
  // heard as part of a page load has already fired; the identity outcome is
  // replayed to it here, the same way PAStatusStream hands a new subscriber the
  // last status it saw. Consumers render the outcome rather than counting it,
  // so hearing it again is a repaint.
  const replaySessionFacts = () => {
    if (!lastIdentityEvent) return;
    if (typeof window.dispatchEvent !== "function") return;
    window.dispatchEvent(new CustomEvent(lastIdentityEvent.type, { detail: lastIdentityEvent.detail }));
  };

  window.addEventListener("pa:identity-updated", (event) => {
    applyIdentityName(event.detail?.droidName);
    publishIdentity(event.detail);
  });

  window.PAUi = window.PAUi || {};
  if (typeof window.PAUi.setupActionText !== "function") {
    window.PAUi.setupActionText = (action) => `${action} in Setup`;
  }
  if (typeof window.PAUi.setupActionHtml !== "function") {
    window.PAUi.setupActionHtml = (action) => `${action} in <a class="setup-link" href="/setup.html">Setup</a>`;
  }

  // ---------------------------------------------------------------------------
  // The Latching Estop: what its label says, in every state
  //
  // Every state produces text, including the one before any status has arrived.
  // An estop that renders nothing while it is not engaged cannot be told from
  // one that has stopped updating, which is the whole reason the reference's
  // paint function has no blank branch (r2d2-astromech-simulator v1.79.0,
  // src/js/app/hud.js:188).
  // ---------------------------------------------------------------------------
  const ESTOP_STATE_TEXT = {
    unknown: "Estop: finding out",
    clear: "Estop: clear",
    latched: "Estop: latched",
  };

  // ---------------------------------------------------------------------------
  // The Status Plate: what the droid is doing, in eight fixed positions
  //
  // The cut is the decision, not the list. /api/status carries 112 keys and
  // the great majority are web-server internals; a chip earns its place only
  // if seeing it would change what the operator does next, which is what
  // leaves eight (#324, CONTEXT.md "Status Plate"). Telemetry -- uptime, heap,
  // signal strength, loop rate -- belongs to the Dashboard and never here,
  // and WiFi earns no chip because if WiFi is down nobody is reading this.
  //
  // Order is by what bites fastest, never by subsystem, and it is fixed: the
  // plate is read by muscle memory rather than scanned, so a chip that moves
  // costs more than a chip that is missing.
  //
  // Every chip shows a VALUE in every state -- "OFF" and "ARMED" are both
  // words, and neither branch is ever blank. A plate that is empty when all
  // is well cannot be told from one that has stopped updating, which is this
  // ticket's own definition of worse than nothing (the reference's paint
  // function has no blank branch either: r2d2-astromech-simulator v1.79.0,
  // src/js/app/hud.js:188).
  //
  // Each chip is a ROUTE: pressing it opens the surface where that thing is
  // changed, through the same hash address the nav uses, and it changes
  // nothing itself. The Latching Estop is the single exception and acts in
  // place. A chip's destination is written as a `page` identifier and its
  // operator-facing name is read back out of SURFACES, so a rename is still
  // one field and this table cannot drift from what the nav says (#288).
  // ---------------------------------------------------------------------------

  // Before the droid has said anything. One word, the same one the estop's
  // own state line uses, and no chip ever returns to it once a frame has
  // arrived: the values are then kept and it is the PLATE that says how old
  // they are. That is the difference between this and a per-chip freshness
  // marker, which #324 rejected.
  const CHIP_UNKNOWN = "FINDING OUT";

  const hasKey = (payload, key) =>
    payload !== null && typeof payload === "object" && Object.prototype.hasOwnProperty.call(payload, key);

  // The two RC receivers. rcCh3..rcCh6 are further channels of the same
  // receiver and only ever report "ready" or "standby", so they carry no link
  // state at all; rcCh1 is the drive receiver except in single_sbus + useCh2,
  // where the firmware routes the drive receiver to rcCh2 and omits rcCh1
  // entirely (src/web/web_server.cpp, the enableRcCh1 guard). Reading rcCh1
  // alone would therefore report "no RC" on a working single-SBUS droid.
  const RC_LINK_CHANNELS = ["rcCh1", "rcCh2"];

  // A chip's state class: "" is the quiet default, "live" is the thing doing
  // its job, "stopped" is something stopped or refused. A posture the
  // operator chose -- Non-RC Control, Sleep Mode -- and a component nobody
  // fitted take no class at all, because colour on this plate reports the
  // droid's health and never a choice (#327 "Status Colour").
  const chipState = (state, value) => ({ state, value });

  const PLATE_CHIPS = [
    {
      id: "estop",
      label: "ESTOP",
      // The single cell that acts rather than routes, so it carries no page.
      page: null,
      affordance: "Cuts drive now",
      read: (status) =>
        status.estop === true ? chipState("stopped", "LATCHED") : chipState("live", "CLEAR"),
    },
    {
      id: "drive",
      label: "DRIVE",
      page: "drive",
      // Every input that can hold the feet at zero is read here, and there are
      // five: the operator's latch and the watchdog-reset latch (which the
      // firmware merges into `estop`), the receiver's hardware failsafe bit,
      // the SBUS watchdog, and the web-drive timeout. Any one of them makes
      // DriveTask emit zero frames (src/drive_arbiter.cpp, failsafeIsActive()
      // || webTimedOut), so a chip reading one of the five would sit dark
      // while the droid was held still -- which is the reference's shipped
      // Bug 2 exactly: one clock guarded two channels and the summary chip
      // tested one of them (r2d2-astromech-simulator, src/js/app/hud.js:203).
      //
      // `failsafeSource` is deliberately NOT one of the five: the firmware
      // never resets it when a layer clears (src/failsafe_gate.cpp,
      // failsafeClear), so it names the last reason rather than a live one.
      read: (status) => {
        if (!hasKey(status, "drive")) return chipState("", "OFF");
        const held =
          status.estop === true ||
          status.sbusHwFailsafe === true ||
          status.sbusSignalLost === true ||
          status.webDriveExpired === true;
        return held ? chipState("stopped", "STOPPED") : chipState("live", "ARMED");
      },
    },
    {
      id: "rclink",
      label: "RC LINK",
      page: "rc",
      // The worst state across every receiver that reports one, plus the
      // hardware failsafe bit -- which is the half that would otherwise be
      // missed. A transmitter switched off makes the receiver assert failsafe
      // while it keeps sending frames, so the channel still reads "active"
      // and only `sbusHwFailsafe` says the link is dead.
      read: (status) => {
        if (status.sbusHwFailsafe === true) return chipState("stopped", "FAILSAFE");
        const states = RC_LINK_CHANNELS.filter((key) => hasKey(status, key)).map((key) => {
          const channel = status[key];
          return channel !== null && typeof channel === "object" ? channel.state : undefined;
        });
        if (states.includes("signal_lost")) return chipState("stopped", "LOST");
        if (states.includes("not_seen")) return chipState("", "NO FRAMES");
        if (states.includes("active")) return chipState("live", "OK");
        // Standard PWM inputs: the firmware publishes whether they are enabled
        // and nothing whatever about whether pulses are arriving -- PWM loss
        // submits a zero frame and triggers no failsafe layer and no key
        // (src/tasks/rc_input.cpp, dispatchStandardPwmInputs). So the chip
        // names the kind of input and claims no link, because a chip may only
        // print what something measured.
        if (states.includes("ready")) return chipState("", "PWM");
        return chipState("", states.length > 0 ? "STANDBY" : "OFF");
      },
    },
    {
      id: "control",
      label: "CONTROL",
      page: "drive",
      // Non-RC Control: the consent by which a browser, the Controller
      // Console or a sequence may command the droid. It is not persisted and
      // boots off, so "OFF" is the ordinary posture of a controller that has
      // just restarted rather than a fault -- and a chosen posture takes no
      // colour.
      read: (status) =>
        status.webControlEnabled === true ? chipState("", "ON") : chipState("", "OFF"),
    },
    {
      id: "sleep",
      label: "SLEEP",
      page: "home",
      read: (status) => (status.sleepMode === true ? chipState("", "ON") : chipState("", "OFF")),
    },
    {
      id: "spd",
      label: "SPD",
      page: "drive",
      // The cap, which is measured, and not the preset name, which may not
      // be. Writing speedLimitMax directly to a value that matches no preset
      // leaves the payload reporting "normal" regardless
      // (src/web/api_config_apply.cpp, the resolveSpeedPresetForLimit
      // fallback), so a chip printing the name would name a preset the number
      // does not belong to -- the reference's shipped Bug 1, where a chip
      // printed the only delay the app had when its label was written.
      read: (status) => {
        const cap = Number(status.speedLimitMax);
        return chipState("", Number.isFinite(cap) ? String(cap) : CHIP_UNKNOWN);
      },
    },
    {
      id: "domelink",
      label: "DOME LINK",
      page: "dome",
      // The heartbeat state, and the UART owner, which is the second half.
      // The dome shares UART2 with the sound module, and while sound holds it
      // the heartbeat cannot arrive at all -- the firmware reports that as an
      // ordinary "lost", so a chip reading only the state would say the dome
      // link died when the truth is that nobody can ask.
      read: (status) => {
        const link = status.dome_link;
        const linkState = link !== null && typeof link === "object" ? link.state : undefined;
        if (linkState === "connected") return chipState("live", "OK");
        if (linkState === "disabled") return chipState("", "OFF");
        if (link !== null && typeof link === "object" && link.uart_owner === "audio") {
          return chipState("", "SOUND HAS BUS");
        }
        if (linkState === "lost") return chipState("stopped", "LOST");
        if (linkState === "not_seen") return chipState("", "NO HEARTBEAT");
        return chipState("", CHIP_UNKNOWN);
      },
    },
    {
      id: "soundlink",
      label: "SOUND LINK",
      page: "sound",
      // link_ok is half the condition. A false link_ok means either the
      // module did not answer or the dome owns the UART and nobody could ask,
      // and only rx_status tells the two apart (src/drivers/audio_chirp.cpp,
      // classifyRxStatus). Reading link_ok alone reports a dead module for a
      // bus that is merely busy.
      read: (status) => {
        if (!hasKey(status, "audio")) return chipState("", "OFF");
        const audio = status.audio;
        if (audio === null || typeof audio !== "object") return chipState("", CHIP_UNKNOWN);
        if (audio.rx_status === "blocked_by_dome_uart") return chipState("", "DOME HAS BUS");
        if (audio.link_ok === true) return chipState("live", "OK");
        if (audio.rx_status === "no_response") return chipState("stopped", "NO ANSWER");
        return chipState("", CHIP_UNKNOWN);
      },
    },
  ];

  // A destination is named by reading SURFACES, never by restating a name
  // here: B2d renamed Drive to Foot Drive with its `page` untouched, and a
  // name written twice is a name that goes stale the next time the copy sweep
  // runs (#288).
  const chipAffordance = (chip) => {
    if (chip.page === null) return chip.affordance;
    const destination = surfaceByPage.get(chip.page);
    return destination ? `Opens ${destination.name}, where this is changed` : chip.affordance;
  };

  // The cell markup. Seven cells are anchors carrying the surface's own hash
  // address, so a press is the one navigation path the nav and every legacy
  // link already use -- there is no second router to keep in step. The estop
  // cell is a button because it acts.
  //
  // The title says what the press does and is written once, here; it is never
  // touched by the paint function, so the consequence of a click and the state
  // being reported cannot drift into each other (the reference's fixed-title
  // discipline, src/js/maestro/hw-ui.js:227). Nothing carries an aria-label:
  // the visible text IS the accessible name, so there is no second copy of
  // the state to keep in step (WCAG 2.5.3).
  const chipHtml = (chip) => {
    // Label over value, with the signal light inside the value line: the shape
    // an instrument uses, where the label is the engraving on the panel and the
    // value is what the needle says (ADR 0066).
    const inner =
      `<span class="status-chip-label">${chip.label}</span>` +
      `<span class="status-chip-value"><span class="status-chip-dot"></span>${CHIP_UNKNOWN}</span>`;
    const shared = `class="status-chip" id="chip-${chip.id}" data-chip="${chip.id}" title="${chipAffordance(chip)}"`;
    return chip.page === null
      ? `<button type="button" ${shared}>${inner}</button>`
      : `<a ${shared} href="#${chip.page}">${inner}</a>`;
  };

  // ---------------------------------------------------------------------------
  // Chrome: rendered once, and never again. Everything a navigation changes is
  // an attribute on what is already there, so nothing the shell owns is rebuilt
  // out from under a handler bound to it.
  // ---------------------------------------------------------------------------
  const shellTop = document.getElementById("shell-top");
  const shellNav = document.getElementById("shell-nav");

  // A rail entry: the surface's icon, then its name. The icon never stands on
  // its own -- it is a second reading of the word beside it, not a replacement
  // for one (ADR 0066).
  const navLink = (surface) =>
    `<a href="#${surface.page}" data-surface-link="${surface.page}">${icon(surface.icon)}${surface.name}</a>`;

  // The divider between groups is drawn rather than stored: the group's own
  // rule in data/style.css carries it, so retiring a group is deleting a row
  // and never a migration (r2d2-astromech-simulator v1.79.0,
  // src/js/config/wizard.js:2180-2182).
  const groupHtml = (group) => {
    const links = group.members
      .map((page) => surfaceByPage.get(page))
      .filter(Boolean)
      .map(navLink)
      .join("");
    // Nothing to offer, nothing drawn -- which is what keeps a dormant row
    // free until the surface it names exists.
    if (!links) return "";
    const labelId = `nav-group-${group.id}-label`;
    return `
        <div class="nav-group" data-nav-group="${group.id}" role="group" aria-labelledby="${labelId}">
          <p class="nav-group-head" id="${labelId}">
            <span class="nav-group-label">${group.label}</span>
            <span class="nav-group-hint">${group.hint}</span>
          </p>
          <div class="nav-group-links">${links}</div>
        </div>`;
  };

  const navHtml = [
    ...ungroupedSurfaces.map(navLink),
    ...ACTIVITY_GROUPS.map(groupHtml),
  ].join("");

  if (shellTop) {
    // The sprite rides with the chrome and is painted nowhere: it is the one
    // place in the document a <use> can resolve against, and every surface
    // mounted under this frame reaches it.
    shellTop.innerHTML = `
      ${spriteHtml()}
      <div class="topbar">
        <!-- The droid's name, and no mark. ADR 0066's identity is the INSIDE of
             the droid, an instrument panel; a portrait of the droid seen from
             outside, in the top-left logo slot, is the generic-web-app
             convention this look exists to leave (operator, 2026-09-16). The
             name is the part of that corner that is the builder's own anyway. -->
        <a href="#${DEFAULT_PAGE}" class="topbar-brand">
          <div>
            <!-- The droid's name is not a heading: it is the same corner on
                 every screen, and the one <h1> a document gets belongs to the
                 surface being shown. It was an <h1> while the nav was a strip
                 above a page that had no title of its own. -->
            <span class="brand-name" data-identity-name>protoartoo</span>
            <div class="subtitle">R2-D2 Body Controller</div>
          </div>
        </a>
        <!-- Where the operator is, read out of SURFACES so this cannot say a
             different thing from the nav or the browser title (#288). -->
        <div class="topbar-where"><b id="shell-where">Dashboard</b></div>
        <div class="topbar-actions" id="shell-top-actions"></div>
        <div class="topbar-right">
          <!-- The estop is chrome, not a surface's control: it is written here,
               once, so every screen is shown beneath the same one. The action
               line and the line under it are FIXED - they say what a press
               does, which the state line cannot, because the state line says
               what the droid is doing (the reference's fixed-title discipline,
               src/js/maestro/hw-ui.js:227, as visible text rather than a title
               because a title carries no affordance on a bench tablet,
               docs/ui-copy-voice.md rule 12 / ADR 0059).

               The two lines sit beside the button rather than inside it: the
               topbar has the width for them, and a button whose face is three
               lines of text stops reading as one press. -->
          <div class="shell-estop">
            <!-- The accessible name opens with the word on the face of the
                 button, so someone driving the page by voice can say what
                 they can see (WCAG 2.5.3 Label in Name) - the one control
                 where being unable to say "press STOP" would matter most. -->
            <button id="shell-estop-button" class="btn danger shell-estop-button" type="button"
                    aria-label="STOP - cut drive now. Clear it on Foot Drive or Dashboard.">
              ${icon("stop-circle-outline")}<span class="shell-estop-action">STOP</span>
            </button>
            <div class="shell-estop-lines">
              <div class="shell-estop-state" id="shell-estop-state" role="status" aria-live="polite">${ESTOP_STATE_TEXT.unknown}</div>
              <div class="shell-estop-consequence">Cuts drive - clear it on Foot Drive or Dashboard</div>
              <div class="shell-estop-feedback feedback compact-feedback" id="shell-estop-feedback" role="status" aria-live="polite" aria-atomic="true"></div>
            </div>
          </div>
        </div>
      </div>
    `;
  }

  // The nav is its own region so it can be the rail beside the work area rather
  // than a strip above it (ADR 0066). It is still written once and never again:
  // a navigation flips an attribute on a link that is already there.
  if (shellNav) {
    shellNav.setAttribute("aria-label", "Operator navigation");
    // The foot of the rail is where the shell says what is running.
    // data/footer.js writes into #fw-meta wherever the shell puts it.
    shellNav.innerHTML = `
      ${navHtml}
      <div class="rail-foot status-bar" id="conn-status">
        <div class="status-subline" id="fw-meta">Loading firmware info...</div>
      </div>
    `;
  }

  // The plate is chrome by the same rule as the estop above: written once,
  // then repainted in place. It rides on the status the session already holds
  // and asks the droid for nothing of its own -- the one /api/status read and
  // the one stream are the estop's, and a second reader would spend one of the
  // controller's three client slots to say what the first already knows.
  const shellStatus = document.getElementById("shell-status");
  if (shellStatus) {
    // The notice sits ABOVE the plate rather than in it: the eight positions are
    // fixed and read by muscle memory, and a notice that pushed one aside would
    // move the cell an operator was reaching for.
    shellStatus.innerHTML = `
      <div class="status-plate-region" id="status-plate-region" data-freshness="finding-out">
        <div class="ignored-input-notice hidden" id="ignored-input-notice" role="status" aria-live="polite">
          <span id="ignored-input-text"></span>
          <a class="ignored-input-route" id="ignored-input-route" href="#${DEFAULT_PAGE}"></a>
        </div>
        <div class="status-plate" id="status-plate" role="group" aria-label="What the droid is doing">
          ${PLATE_CHIPS.map(chipHtml).join("")}
        </div>
        <!-- The plate's ninth compartment, and deliberately NOT a ninth cell of
             it: the eight are the contract and nothing that is not a chip
             belongs among them. It sits beside them instead, divided by the
             same seam, so the plate reads as one instrument. -->
        <div class="status-plate-fresh">
          <p class="status-plate-freshness" id="status-plate-freshness" role="status" aria-live="polite">Still finding out what the droid is doing.</p>
          <p class="status-plate-affordance">Press a chip to open the screen where that thing is changed. ESTOP cuts drive right here.</p>
        </div>
      </div>
    `;
  }

  applyIdentityName(identityName);

  // Register identity load with the bootstrap if available; otherwise run it directly.
  // This ensures the identity request is routed through the bootstrap's single-slot
  // recovery mechanism rather than competing with other startup GETs.
  if (window.PABootstrap) {
    window.PABootstrap.registerSection("shell-identity", loadIdentity, {
      label: "droid identity",
    });
  } else {
    loadIdentity();
  }

  // ---------------------------------------------------------------------------
  // The Latching Estop
  //
  // The shell renders it once and never again (above), so changing screen
  // cannot take it away, cannot re-mount it and cannot clear a latch: the only
  // thing a navigation touches is what is inside #shell-content. What it shows
  // is read from the status the session already holds, never from the surface
  // that happens to be mounted -- the reference's own discipline, where the
  // bench clock follows the board being connected rather than which workspace
  // is open (r2d2-astromech-simulator v1.79.0, src/js/maestro/hw-host.js:341).
  //
  // Latching only. Releasing a latched estop stays on Drive and Dashboard,
  // where an operator went on purpose, because the direction that lets a droid
  // move again must not be one press from every screen (ADR 0048).
  // ---------------------------------------------------------------------------

  // Fallback cadence when the browser has no EventSource. The mounted surface's
  // own poll cannot serve this control: the point of it is that it outlives the
  // surface. Matches the Dashboard's fallback cadence so a no-stream session
  // asks at one rate rather than two.
  const ESTOP_POLL_MS = 3000;

  const estopButton = document.getElementById("shell-estop-button");
  const estopStateLine = document.getElementById("shell-estop-state");
  const estopFeedback = document.getElementById("shell-estop-feedback");

  // null until the droid has said something. Three answers, three texts: see
  // ESTOP_STATE_TEXT above for why there is no blank one.
  let estopLatched = null;

  const renderEstopState = () => {
    if (!estopStateLine) return;
    const key = estopLatched === null ? "unknown" : estopLatched ? "latched" : "clear";
    estopStateLine.textContent = ESTOP_STATE_TEXT[key];
    // Red is "something is stopped or refused" and nothing else colours for
    // state (#327), so the state line takes it only while the latch is set.
    // The button's own face is red at all times: that is the control's
    // identity, not a readout.
    estopStateLine.classList.toggle("is-latched", estopLatched === true);
  };

  const showEstopFeedback = (message, level = "") => {
    if (!estopFeedback) return;
    estopFeedback.textContent = message;
    estopFeedback.className = level
      ? `shell-estop-feedback feedback compact-feedback ${level}`
      : "shell-estop-feedback feedback compact-feedback";
  };

  const applyEstopStatus = (payload) => {
    if (!payload || typeof payload !== "object") return;
    estopLatched = !!payload.estop;
    renderEstopState();
  };

  // The one status read the shell owns. It hands what came back to
  // PAStatusStream rather than keeping it, so the session's last status has one
  // home and a consumer that asks later is answered from it instead of
  // fetching again.
  const readStatusOnce = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    if (!api) return;
    const result = await api.get("/api/status", { cache: "no-store" });
    if (window.PAStatusStream?.seed) window.PAStatusStream.seed(result.data);
    else applyEstopStatus(result.data);
  };

  // The device pushes a status event on a change and on nothing else, so a
  // client that connects to a quiet droid is told nothing at all. This is the
  // read that closes that gap; every later change arrives on the stream.
  const loadInitialStatus = async ({ handle = null } = {}) => {
    if (window.PAStatusStream?.getLastStatus?.()) {
      applyEstopStatus(window.PAStatusStream.getLastStatus());
      return;
    }
    await readStatusOnce({ handle });
  };

  // Deliberately unguarded against a second press while the first is in
  // flight. POST /api/estop is idempotent in the firmware -- failsafeTrigger()
  // sets a bit it may already hold (src/failsafe_gate.cpp) -- and
  // estopPostForm bypasses the request slot and never retries, so a second
  // press cannot queue behind the first. A pending guard here would swallow
  // exactly the press an operator makes because the first looked like it did
  // nothing.
  //
  // One function, two entrances: the STOP button in the topbar and the plate's
  // ESTOP chip at the foot of the page both call it. Two copies of a stop
  // could drift, and the one control where that matters most is this one.
  const requestStop = async () => {
    if (!window.PAApi) return;
    showEstopFeedback("Stopping the droid...");
    try {
      await window.PAApi.estopPostForm("/api/estop", {}, { timeoutMs: 3000 });
    } catch (error) {
      // A stop that did not reach the droid has to say so in its own line:
      // the state line still reports what the droid last told us, which is
      // not the same thing and must not be overwritten with a guess.
      showEstopFeedback(`Stop failed: ${window.PAApi.messageFor(error)} - press again`, "error");
      return;
    }
    showEstopFeedback("Stop sent", "success");
    try {
      await readStatusOnce();
    } catch (error) {
      // The stop already succeeded; only the confirmation read failed. The
      // firmware broadcasts the new status itself, so the state line catches
      // up on the stream a moment later.
      console.warn("[shell] status read after stop failed:", error);
    }
  };

  if (estopButton) {
    estopButton.addEventListener("click", requestStop);

    // Subscribed in both modes: the stream is where a change arrives, and it
    // is also what readStatusOnce() hands its answer to, so this is the one
    // path into the control whether the status was pushed or fetched.
    window.PAStatusStream?.subscribe((eventType, payload) => {
      if (eventType === "status") applyEstopStatus(payload);
    });

    if (!window.PAStatusStream?.isSupported()) {
      // Chrome, so deliberately NOT a surface-owned poll (#360): the estop
      // never unmounts and its liveness must not follow whatever screen
      // happens to be open. window.PASurface.poll() is for surfaces only.
      window.PageBootstrap?.createBackgroundPoll(
        () =>
          readStatusOnce().then(
            () => true,
            (error) => {
              console.warn("[shell] estop status poll failed:", error);
              return false;
            }
          ),
        { cadenceMs: ESTOP_POLL_MS, refreshOnReturn: true }
      ).start();
    }

    if (window.PABootstrap) {
      window.PABootstrap.registerSection("shell-status", loadInitialStatus, {
        label: "droid status",
      });
    } else {
      loadInitialStatus();
    }
  }

  // ---------------------------------------------------------------------------
  // The Status Plate: painting it, and saying how old it is
  //
  // The cells are looked up once. Nothing here queries the document again on a
  // repaint, and nothing rebuilds a cell: a chip is an attribute and a text
  // node on a node that was written at boot, which is the same discipline the
  // nav and the estop above already keep.
  // ---------------------------------------------------------------------------
  const plateRegion = document.getElementById("status-plate-region");
  const plateFreshness = document.getElementById("status-plate-freshness");
  const plateCells = new Map();
  PLATE_CHIPS.forEach((chip) => {
    const node = document.getElementById(`chip-${chip.id}`);
    if (!node) return;
    plateCells.set(chip.id, { node, value: node.querySelector(".status-chip-value") });
  });

  // One writer for every cell, so no chip can grow a rendering path of its own
  // (the reference's chip(), r2d2-astromech-simulator v1.79.0,
  // src/js/app/hud.js:188). The class is REWRITTEN rather than toggled, so a
  // state class cannot survive a repaint that no longer wants it.
  const paintPlate = (status) => {
    PLATE_CHIPS.forEach((chip) => {
      const cell = plateCells.get(chip.id);
      if (!cell) return;
      const painted = status ? chip.read(status) : chipState("", CHIP_UNKNOWN);
      cell.node.className = painted.state ? `status-chip status-chip-${painted.state}` : "status-chip";
      if (cell.value) cell.value.textContent = painted.value;
    });
  };

  // The plate's ONE freshness state. Not one per chip: everything on it
  // arrives on one stream, so its age is one fact and saying it eight times
  // repeats that fact seven times (#324).
  //
  // The age is read from the browser's own clock, not from the droid's
  // uptimeMs, and that is the point: the droid's clock is the thing that stops
  // advancing exactly when this readout starts to matter. It is the other half
  // of the reference's "wall clock, not simulated time" rule
  // (src/js/input/pad-ui.js:165).
  //
  // Only a frame the session has not seen before restamps the age. The stream
  // re-emits its cached frame when it reconnects and when a hidden tab becomes
  // visible again (data/status_stream.js), and stamping those as new would
  // have the plate claim a measurement nobody took -- at precisely the moment
  // #324 says an operator meets a stale plate most often, which is switching
  // back to the tab to look at it.
  let plateFrame = null;
  let plateFrameAt = 0;
  let plateStreamBroken = false;

  const plateAgeText = (elapsedMs) => {
    if (elapsedMs < 1500) return "just now";
    const seconds = Math.round(elapsedMs / 1000);
    if (seconds < 60) return `${seconds}s ago`;
    const minutes = Math.round(seconds / 60);
    if (minutes < 60) return `${minutes}min ago`;
    return "over an hour ago";
  };

  const renderPlateFreshness = () => {
    if (!plateRegion || !plateFreshness) return;
    if (plateFrame === null) {
      plateRegion.dataset.freshness = "finding-out";
      plateFreshness.textContent = "Still finding out what the droid is doing.";
      return;
    }
    const heard = `Last heard from the droid ${plateAgeText(Date.now() - plateFrameAt)}.`;
    // Never amber, and the values are never blanked: the operator cannot act
    // on a reconnect that is already running, and a blank plate would be the
    // presentation they meet most often (#324, #327).
    plateRegion.dataset.freshness = plateStreamBroken ? "finding-out" : "live";
    plateFreshness.textContent = plateStreamBroken
      ? `${heard} Reconnecting - these are the values it last sent.`
      : heard;
  };

  const notePlateStatus = (payload) => {
    if (!payload || typeof payload !== "object") return;
    if (payload !== plateFrame) {
      plateFrame = payload;
      plateFrameAt = Date.now();
    }
    // A frame arriving at all is the stream working, whether it is new or the
    // cached one replayed on reconnect.
    plateStreamBroken = false;
    paintPlate(payload);
    renderPlateFreshness();
    releaseSettledCauses(payload);
  };

  // ---------------------------------------------------------------------------
  // The Ignored Input Notice
  //
  // The failure this closes is silence. A control the droid cannot act on is
  // switched off with `disabled` plus aria-disabled (window.PAApi.gateControls,
  // data/web_api.js), and pressing a disabled control produces nothing at all:
  // no request, no feedback line, no log. The reference's own UX review made
  // that its FIRST finding -- "nothing on screen tells you the drive is
  // disarmed when you move a stick" -- and the operator's eyes are on the
  // droid, not on the page.
  //
  // The hook is a capture-phase `pointerdown`, and it is pointerdown because
  // that is what a browser still delivers: measured in Chromium on
  // 2026-09-12, a press on a disabled <button> dispatches pointerdown to the
  // button itself and suppresses mousedown and click entirely, so pointerdown
  // is the only event left that names what the operator pressed. It is the one
  // place the whole surface is covered, because gateControls() is how every
  // page refuses a control.
  //
  // Only the transition from not-pressing to pressing is an attempt. A second
  // finger, a repeated pointerdown during one press, or a held button must not
  // each count (r2d2-astromech-simulator v1.79.0, src/js/input/pad-ui.js:213),
  // and bursts then merge over a window of WALL CLOCK -- deliberately not the
  // droid's uptime, which stops advancing exactly when the droid feels most
  // broken (:165).
  //
  // The notice says what is OFF and never claims to have diagnosed the press:
  // the shell can see that a refused control was pressed and what the droid is
  // holding, and those are two facts rather than one. When the droid is
  // holding nothing the plate tracks, it stays quiet.
  // ---------------------------------------------------------------------------
  const NOTICE_BURST_MS = 1500;
  // How long one notice stays up. It is a receipt, not an alarm, and it comes
  // down sooner than this the moment its cause clears.
  const NOTICE_VISIBLE_MS = 6000;

  // One row per way the droid is holding still, ordered by what bites fastest,
  // and each row routes where its Status Plate chip routes -- so the notice
  // says WHAT and the chip says WHERE, to one destination rather than two.
  //
  // Two rows carry a `page` of their own and say why: the estop's chip acts in
  // place and so has no destination, while the release does (Foot Drive and
  // Dashboard); and Stationary Mode earns no chip under #324's admission rule
  // but is still a real refusal -- POST /api/drive rejects on it
  // (src/web/api_drive.cpp) -- so it names the surface its Commanded Mode
  // buttons live on.
  const IGNORED_INPUT_CAUSES = [
    {
      id: "estop",
      chip: "estop",
      page: "drive",
      says: "The estop is latched",
      active: (status) => status.estop === true,
    },
    {
      id: "feet",
      chip: "drive",
      says: "The feet are not armed",
      // The same enumeration the DRIVE chip makes, minus the estop, which has
      // its own row above and would otherwise shadow it.
      active: (status) =>
        !hasKey(status, "drive") ||
        status.sbusHwFailsafe === true ||
        status.sbusSignalLost === true ||
        status.webDriveExpired === true,
    },
    {
      id: "stationary",
      chip: null,
      page: "home",
      says: "Stationary Mode has the feet locked",
      active: (status) => status.stationary === true,
    },
    {
      id: "control",
      chip: "control",
      says: "This droid has not consented to browser control yet",
      active: (status) => status.webControlEnabled !== true,
    },
  ];

  const noticeNode = document.getElementById("ignored-input-notice");
  const noticeText = document.getElementById("ignored-input-text");
  const noticeRoute = document.getElementById("ignored-input-route");

  // Wall clock, per cause, of the last notice shown for it.
  const noticeShownAt = new Map();
  let noticeCause = null;
  let noticeHideTimer = null;
  let noticePressing = false;

  const noticePageFor = (cause) =>
    cause.page || PLATE_CHIPS.find((chip) => chip.id === cause.chip)?.page || DEFAULT_PAGE;

  const hideNotice = () => {
    noticeCause = null;
    if (noticeHideTimer !== null) {
      window.clearTimeout(noticeHideTimer);
      noticeHideTimer = null;
    }
    noticeNode?.classList.add("hidden");
  };

  const showNotice = (cause) => {
    if (!noticeNode || !noticeText || !noticeRoute) return;
    const page = noticePageFor(cause);
    const destination = surfaceByPage.get(page);
    noticeText.textContent = `That control is switched off right now. ${cause.says}.`;
    noticeRoute.textContent = `Open ${destination ? destination.name : page}, where that is changed`;
    noticeRoute.setAttribute("href", `#${page}`);
    noticeNode.classList.remove("hidden");
    noticeCause = cause.id;
    if (noticeHideTimer !== null) window.clearTimeout(noticeHideTimer);
    noticeHideTimer = window.setTimeout(hideNotice, NOTICE_VISIBLE_MS);
  };

  const reportIgnoredInput = () => {
    // Nothing has arrived yet, so there is nothing to name. The plate's own
    // freshness state is already saying so.
    if (plateFrame === null) return;
    const cause = IGNORED_INPUT_CAUSES.find((candidate) => candidate.active(plateFrame));
    // A control switched off for a reason this plate does not carry is not
    // this notice's business: it would otherwise name whatever happened to be
    // off, which is a guess dressed as a fact.
    if (!cause) return;
    const now = Date.now();
    const shownAt = noticeShownAt.get(cause.id);
    if (shownAt !== undefined && now - shownAt < NOTICE_BURST_MS) return;
    noticeShownAt.set(cause.id, now);
    showNotice(cause);
  };

  // The rate limit resets the moment the answer stops being undecided: a cause
  // that has stopped being true drops its window, so changing your mind and
  // back does not buy a second of silence. And the notice comes down with it,
  // because the door it was holding open has closed
  // (r2d2-astromech-simulator v1.79.0, src/js/config/hardware.js:878).
  const releaseSettledCauses = (status) => {
    IGNORED_INPUT_CAUSES.forEach((cause) => {
      if (cause.active(status)) return;
      noticeShownAt.delete(cause.id);
      if (noticeCause === cause.id) hideNotice();
    });
  };

  // Which refused control a press landed on -- and it takes two tries, because
  // a refused button is not merely inert to clicks, it is invisible to hit
  // testing: data/style.css puts `pointer-events: none` on `.btn:disabled` and
  // `.btn[aria-disabled="true"]`, so the press lands on whatever is behind the
  // control and event.target names the container instead of the button.
  //
  // Measured in a browser on the shipped stylesheet, which is the only place
  // it is visible: a bare disabled button in a page with no CSS does receive
  // pointerdown on itself, so a probe without this stylesheet says the first
  // branch below is all that is needed, and it is not.
  //
  // First branch: the target itself, for a refused thing that is still
  // hit-testable -- an aria-disabled row is an ordinary element and keeps its
  // pointer events (data/rc.js's action list is one).
  //
  // Second branch: the refused control whose box holds the pointer, searched
  // inside the element the press did land on. That element is the nearest
  // hit-testable ancestor, so the control is one of its descendants.
  const refusedAt = (event) => {
    const target = event.target;
    if (!target?.closest) return null;
    const hit = target.closest('[aria-disabled="true"]');
    if (hit) return hit;
    const { clientX, clientY } = event;
    if (typeof clientX !== "number" || typeof clientY !== "number") return null;
    const candidates = target.querySelectorAll?.('[aria-disabled="true"]') || [];
    return (
      Array.from(candidates).find((candidate) => {
        const box = candidate.getBoundingClientRect?.();
        return (
          box &&
          clientX >= box.left &&
          clientX <= box.right &&
          clientY >= box.top &&
          clientY <= box.bottom
        );
      }) || null
    );
  };

  if (noticeNode) {
    document.addEventListener(
      "pointerdown",
      (event) => {
        const rising = !noticePressing;
        noticePressing = true;
        if (!rising) return;
        // aria-disabled rather than the `disabled` property: gateControls()
        // sets both, and the attribute is the one a container can carry for a
        // control that is not a form element.
        const refused = refusedAt(event);
        if (!refused) return;
        // Except when it means BUSY rather than off. The Dashboard marks a
        // control aria-disabled while its request is in flight -- the sleep
        // toggle, the Commanded Mode buttons and the Mood buttons all do
        // (data/app.js setSleepPending / setModePending / setMoodPending) --
        // and those carry .is-pending as well. Saying "that control is
        // switched off" about a control that is merely waiting for the droid
        // to answer is false, and it would fire on exactly the second press an
        // impatient operator makes.
        if (refused.classList?.contains?.("is-pending")) return;
        reportIgnoredInput();
      },
      true
    );
    const releasePointer = () => {
      noticePressing = false;
    };
    document.addEventListener("pointerup", releasePointer, true);
    document.addEventListener("pointercancel", releasePointer, true);
  }

  // ---------------------------------------------------------------------------
  // Wiring the plate and its notice
  //
  // One site, and it comes after both are fully declared: PAStatusStream hands
  // a new subscriber the last status it saw straight away, so a subscription
  // opened above this line could reach the notice's own reader before it
  // exists.
  // ---------------------------------------------------------------------------
  if (plateRegion) {
    window.PAStatusStream?.subscribe((eventType, payload) => {
      if (eventType === "status") notePlateStatus(payload);
      else if (eventType === "stream_error") {
        plateStreamBroken = true;
        renderPlateFreshness();
      }
    });

    // A display tick, not a poll: it asks the droid for nothing and rewrites
    // one line of text. Skipped while the tab is hidden, where there is nobody
    // to read it and no stream open either (Hidden Tab Pause).
    const PLATE_TICK_MS = 1000;
    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      renderPlateFreshness();
    }, PLATE_TICK_MS);

    // The one cell that acts instead of routing, wired to the same function
    // the topbar's STOP button calls. The other seven are anchors carrying a
    // hash address and need no handler at all.
    plateCells.get("estop")?.node.addEventListener("click", requestStop);
  }

  // ---------------------------------------------------------------------------
  // Mounting a surface
  // ---------------------------------------------------------------------------
  const shellContent = document.getElementById("shell-content");
  const topActions = document.getElementById("shell-top-actions");

  if (!shellContent) {
    // The shell document is the only one that carries a content region, and a
    // build that drops it leaves a frame with nowhere to put a surface. Say so
    // rather than leaving a page that silently shows nothing; the markup suite
    // is what stops this reaching a device.
    console.error("[shell] #shell-content is missing; no surface can be mounted");
    return;
  }

  // Mounting is the bootstrap's Resource Step Recovery doing the work, so
  // without it there is no router -- the same documented degraded path the
  // identity load above already takes.
  if (!window.PABootstrap?.mountResources) return;

  // A surface whose polling stopped while the operator was elsewhere comes
  // back showing what it last read. One node, shown above whichever surface
  // that is, until that surface has answered again -- so a glance cannot take
  // those values for live ones. Uncoloured on purpose: this is a Note, and
  // colour is reserved for refusals and for what the builder can act on
  // (#327, docs/ui-copy-voice.md rule 11).
  const resumedNote = document.createElement("div");
  resumedNote.className = "surface-resumed";
  resumedNote.setAttribute("role", "status");
  resumedNote.textContent =
    "Showing what this screen last read - it stopped asking while you were on another screen, and is asking again now.";

  // Ids are not unique across surfaces -- Firmware and Setup both carry
  // #reboot-button, Dashboard and Setup both carry #reboot-feedback -- so
  // exactly one surface is in the document at a time. A surface's scripts find
  // their own elements by id and nothing else's, which is the same guarantee
  // they had as separate documents.
  const mounted = new Map();

  // The wave of resources for a surface being mounted for the first time is in
  // flight until its scripts have run. A second route change during that window
  // waits: attaching the next surface would put two in the document at once,
  // and detaching the one still loading would run its scripts against DOM that
  // is no longer there, breaking it for the rest of the session.
  let mountInFlight = null;
  let pendingPage = null;

  const surfaceFromHash = () => {
    const raw = String(window.location.hash || "").replace(/^#/, "").trim().toLowerCase();
    return raw ? surfaceFor.get(raw) || null : null;
  };

  const setAddress = (page) => {
    if (window.history?.replaceState) {
      window.history.replaceState(null, "", `#${page}`);
    } else {
      window.location.hash = page;
    }
  };

  const markActive = (page) => {
    document.querySelectorAll("[data-surface-link]").forEach((link) => {
      link.classList.toggle("active", link.dataset.surfaceLink === page);
    });
    // The group marker is computed from the surface that was landed on, never
    // held as a second piece of state: a deep link from anywhere pulls its
    // group over through the one navigation path, and there is no "switch
    // group" route to keep in sync with the address
    // (r2d2-astromech-simulator v1.79.0, src/js/config/workspaces.js:195-197).
    // Every group holding the surface is marked, because a surface in two
    // groups is found in both and owned by neither. Like the active link, this
    // is an attribute flip on chrome that is already there -- the nav is never
    // re-rendered, or the estop bound inside it would go with it.
    document.querySelectorAll("[data-nav-group]").forEach((groupEl) => {
      groupEl.classList.toggle("is-current", pagesInGroup.get(groupEl.dataset.navGroup)?.has(page) === true);
    });
  };

  const attach = (surface, entry) => {
    currentSurface = surface;
    document.body.dataset.page = surface.page;
    if (window.PASurface?.isStale(surface.page)) shellContent.appendChild(resumedNote);
    shellContent.appendChild(entry.content);
    if (topActions) entry.actionNodes.forEach((node) => topActions.appendChild(node));
    markActive(surface.page);
    applyIdentityName(identityName);
    rememberSurface(surface.page);
  };

  const detach = (entry) => {
    // The nodes are kept, not discarded: a surface returned to paints what it
    // already had, and the handlers its scripts bound are still on these exact
    // elements. What it was polling has already been stopped by the caller --
    // the surface is left before it is taken off the screen.
    entry.content.remove();
    entry.actionNodes.forEach((node) => node.remove());
    resumedNote.remove();
  };

  // Everything in a surface document's <body> except the frame the shell owns.
  // The frame elements stay in each file so it still reads as a page; they are
  // simply not the surface.
  const SHELL_OWNED_IDS = new Set(["shell-top", "shell-nav", "shell-status", "shell-content"]);
  const TOPBAR_ACTIONS_ID = "topbar-actions-template";

  const parseSurfaceDocument = (html) => {
    const parsed = new DOMParser().parseFromString(String(html || ""), "text/html");
    const scripts = (parsed.documentElement?.getAttribute("data-scripts") || "")
      .split(",")
      .map((source) => source.trim())
      .filter(Boolean);

    const nodes = [];
    let actionNodes = [];
    Array.from(parsed.body?.children || []).forEach((child) => {
      if (child.id === TOPBAR_ACTIONS_ID) {
        // A surface's topbar actions belong beside the nav, not in the content
        // region. They are moved rather than copied, so the handlers the
        // surface's scripts bind to them survive an unmount. The estop is not
        // one of these: it is chrome the shell writes itself, above.
        actionNodes = Array.from(document.importNode(child.content, true).children);
        return;
      }
      if (SHELL_OWNED_IDS.has(child.id)) return;
      nodes.push(document.importNode(child, true));
    });

    return { scripts, nodes, actionNodes };
  };

  const terminalError = (message) => {
    const error = new Error(message);
    // A document that parsed but carries no surface is not going to get better
    // by being asked for again, so the bootstrap must stop retrying it.
    error.kind = "incompatible";
    error.status = 200;
    return error;
  };

  // Loads a surface's markup as a bootstrap resource, then declares the rest of
  // its wave: its own scripts, read from the document that just arrived rather
  // than restated here, and a final step that hands the session's settled facts
  // to the scripts that have just run.
  const loadSurfaceDocument = (surface, entry) => (done) => {
    const fetchDocument = window.PAApi
      ? window.PAApi.get(surface.doc).then((result) => result.data)
      : fetch(surface.doc).then((response) => response.text());

    fetchDocument
      .then((html) => {
        const { scripts, nodes, actionNodes } = parseSurfaceDocument(html);
        if (nodes.length === 0 || scripts.length === 0) {
          throw terminalError(`${surface.doc} carries no surface`);
        }
        entry.content.replaceChildren(...nodes);
        entry.actionNodes = actionNodes;
        if (topActions && currentSurface === surface) {
          actionNodes.forEach((node) => topActions.appendChild(node));
        }
        window.PABootstrap.mountResources([
          ...scripts,
          {
            name: `shell:handover:${surface.page}`,
            // Settled off the reducer's own stack, the way every other
            // resource settles, so the mount that follows a deferred route
            // change starts from a state the reducer has finished applying.
            load: (handoverDone) => {
              Promise.resolve().then(() => {
                mountInFlight = null;
                replaySessionFacts();
                handoverDone(null);
                applyPendingPage();
              });
            },
          },
        ]);
        done(null);
      })
      .catch((error) => {
        done(error);
        // A retryable failure keeps the wave in flight, which is what the Page
        // Recovery View is reporting. A terminal one is never retried, so the
        // mount is over: release the route, or the operator is stuck on a
        // surface that can never load -- and apply any route change that
        // arrived while it was loading, because the handover step that would
        // otherwise have applied it was never declared.
        if (error?.kind !== "incompatible" && error?.kind !== "device-error") return;
        mountInFlight = null;
        Promise.resolve().then(applyPendingPage);
      });
  };

  const mount = (surface) => {
    if (currentSurface === surface) return;

    // A surface may hold its own unmount open while it asks the operator
    // something -- an unsaved edit, once #289/#299 has one to protect. Nothing
    // registers a hold today; what exists here is the capability. The address
    // already names where the operator was going, so releasing the hold and
    // re-reading it is the whole resume path (see pa:surface-release below).
    if (currentSurface && window.PASurface?.unmountHeld(currentSurface.page)) return;

    // Stop asking before the screen changes, so the surface being left is not
    // still competing for the controller's three-client budget while the one
    // the operator is reading loads. Only what the browser asks for changes on
    // this path: no sequence stops, no output releases, no drive frame is
    // dropped and no latch clears (ADR 0048, #360).
    window.PASurface?.showing(surface.page);

    const previous = currentSurface ? mounted.get(currentSurface.page) : null;
    if (previous) detach(previous);

    const existing = mounted.get(surface.page);
    if (existing) {
      attach(surface, existing);
      return;
    }

    const entry = { content: document.createElement("div"), actionNodes: [] };
    entry.content.className = "surface";
    entry.content.dataset.surface = surface.page;
    mounted.set(surface.page, entry);

    // Attached before its markup arrives on purpose: the scripts behind the
    // markup bind to elements by id, and an element only has an id the document
    // can find while it is in the document.
    attach(surface, entry);

    mountInFlight = surface.page;
    window.PABootstrap.setResourceLabels?.({ [surface.doc]: surface.name });
    window.PABootstrap.mountResources([
      { name: surface.doc, load: loadSurfaceDocument(surface, entry) },
    ]);
  };

  // A route change that arrived while a mount was in flight is applied by
  // re-reading the address rather than by replaying the page that was asked
  // for: by the time the mount finishes the operator may have moved on again,
  // and the address is the one thing that always says where they are.
  const applyPendingPage = () => {
    if (pendingPage === null) return;
    pendingPage = null;
    applyRoute();
  };

  // The address decides what is shown; the remembered surface only answers when
  // the address says nothing. An address this build does not know pulls over to
  // the default rather than erroring -- a link that names a surface should open
  // something, and the one thing it must never do is leave the operator on a
  // screen that says nothing at all.
  const applyRoute = () => {
    const surface = surfaceFromHash() || rememberedSurface();
    if (mountInFlight) {
      pendingPage = surface.page;
      return;
    }
    if (surface !== currentSurface) mount(surface);
    setAddress(surface.page);
  };

  const navigateTo = (page) => {
    if (window.location.hash === `#${page}`) {
      applyRoute();
      return;
    }
    // Setting the hash is the whole navigation: it lands a history entry, so
    // back and forward work, and the hashchange below does the mounting. One
    // path in, whether the operator clicked the nav, followed a legacy link, or
    // typed the address.
    window.location.hash = page;
  };

  window.addEventListener("hashchange", applyRoute);

  // The surface on screen has answered again, so what it is showing is current
  // and the note above it comes down. Keyed on which surface answered: a poll
  // that lands just after the operator left must not clear the note the next
  // surface is wearing.
  window.addEventListener("pa:surface-fresh", (event) => {
    if (event.detail?.surface !== currentSurface?.page) return;
    resumedNote.remove();
  });

  // A surface that was holding its unmount has finished asking. Re-read the
  // address rather than replaying the page that was refused: by then the
  // operator may have moved on again, and the address is the one thing that
  // always says where they are.
  window.addEventListener("pa:surface-release", () => applyRoute());

  // A click on a link to a surface's own document is a route change, not a page
  // load. Capture phase, so a surface's own delegated handler cannot swallow it
  // first; the click still reaches that handler, it just does not reach the
  // browser's navigation. This is what lets the eleven .html addresses, and every
  // caller that writes one (window.PAUi.setupActionHtml, servo.js, the
  // disabled-reason lines in six pages), keep working unchanged.
  document.addEventListener(
    "click",
    (event) => {
      if (event.defaultPrevented || event.button !== 0) return;
      if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
      const anchor = event.target?.closest?.("a[href]");
      if (!anchor || (anchor.target && anchor.target !== "_self")) return;
      const surface = surfaceForPath.get(anchor.getAttribute("href"));
      if (!surface) return;
      event.preventDefault();
      navigateTo(surface.page);
    },
    true
  );

  applyRoute();
})();
