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
// Every surface's .html file stays addressable: each is still the single copy
// of its surface's markup, which this file fetches and mounts, and each carries
// a thin delegate that hands a direct visit over to the shell. Nothing that
// links to one has to be rewritten - and a renamed surface's old file stays
// behind as a forwarder for the same reason (data/setup.html).
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
  // color and keeps its label beside it (ADR 0066, docs/ui-copy-voice.md).
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
    // on purpose, because GLOSSARY.md "Activity Group" does not list it and its
    // fate is #364's. This is the nearest of the paths that reference committed
    // rather than a twenty-third taken from somewhere unread.
    { page: "servo", doc: "/servo.html", icon: "robot-outline", name: "Servos", aliases: ["servos"] },
    { page: "parts", doc: "/parts.html", icon: "puzzle-outline", name: "Parts", aliases: [] },
    // Wiring sits beside Parts in Configure and answers the neighbouring
    // question - where does this wire go (GLOSSARY.md "Wiring"). The member row
    // in the Configure group below has been waiting for this one since #288.
    { page: "wiring", doc: "/wiring.html", icon: "connection", name: "Wiring", aliases: [] },
    // Every light on the droid: the LED strip, the dome's lights and the body
    // lights add-ons bring later (operator, 2026-09-18 on #369; #410).
    { page: "lights", doc: "/lights.html", icon: "lightbulb-outline", name: "Lights", aliases: [] },
    { page: "seq", doc: "/seq.html", icon: "timeline-outline", name: "Sequences", aliases: ["sequences"] },
    { page: "rc", doc: "/rc.html", icon: "controller-classic-outline", name: "RC Control", aliases: [] },
    // What the droid is made of, and the other half of what used to be one
    // Setup page (#288, #404). It keeps the sliders that page wore, and `setup`
    // is its alias because that page is what every link to /setup.html and
    // #setup was written to reach: the page a component is switched on from.
    // Guided Setup, the first-run run, has no row and no nav entry of its own:
    // it is drawn over this surface while the droid is not set up
    // (data/setup.js, GLOSSARY.md "Setup").
    { page: "configuration", doc: "/configuration.html", icon: "tune-variant", name: "Configuration", aliases: ["setup"] },
    // Inspecting and repairing the controller, and the one way back into
    // guided Setup (#288, #297, #404).
    { page: "maintenance", doc: "/maintenance.html", icon: "wrench-outline", name: "Maintenance", aliases: [] },
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
  // Only symbols something draws are here: the chrome's, and the icon of every
  // act on a surface (#460). The rest of the twenty-two the #398 reference
  // committed are in
  // `git show 83acf0db:prototypes/395-surface-anatomy/chrome.js` (the prototype
  // left the tree for the gitignored tasks/prototypes/ on 2026-09-18), and the
  // slice that first needs one copies its path in beside these rather than
  // shipping a symbol nothing draws.
  // ---------------------------------------------------------------------------
  const ICONS = {
    "view-dashboard-outline": "M19,5V7H15V5H19M9,5V11H5V5H9M19,13V19H15V13H19M9,17V19H5V17H9M21,3H13V9H21V3M11,3H3V13H11V3M21,11H13V21H21V11M11,15H3V21H11V15Z",
    "steering": "M13,19.92C14.8,19.7 16.35,18.95 17.65,17.65C18.95,16.35 19.7,14.8 19.92,13H16.92C16.7,14 16.24,14.84 15.54,15.54C14.84,16.24 14,16.7 13,16.92V19.92M10,8H14L17,11H19.92C19.67,9.05 18.79,7.38 17.27,6C15.76,4.66 14,4 12,4C10,4 8.24,4.66 6.73,6C5.21,7.38 4.33,9.05 4.08,11H7L10,8M11,19.92V16.92C10,16.7 9.16,16.24 8.46,15.54C7.76,14.84 7.3,14 7.08,13H4.08C4.3,14.77 5.05,16.3 6.35,17.6C7.65,18.9 9.2,19.67 11,19.92M12,2C14.75,2 17.1,3 19.05,4.95C21,6.9 22,9.25 22,12C22,14.75 21,17.1 19.05,19.05C17.1,21 14.75,22 12,22C9.25,22 6.9,21 4.95,19.05C3,17.1 2,14.75 2,12C2,9.25 3,6.9 4.95,4.95C6.9,3 9.25,2 12,2Z",
    "rotate-360": "M12 7C6.5 7 2 9.2 2 12C2 14.2 4.9 16.1 9 16.8V20L13 16L9 12V14.7C5.8 14.1 4 12.8 4 12C4 10.9 7 9 12 9S20 10.9 20 12C20 12.7 18.5 13.9 16 14.5V16.6C19.5 15.8 22 14.1 22 12C22 9.2 17.5 7 12 7Z",
    "volume-high": "M14,3.23V5.29C16.89,6.15 19,8.83 19,12C19,15.17 16.89,17.84 14,18.7V20.77C18,19.86 21,16.28 21,12C21,7.72 18,4.14 14,3.23M16.5,12C16.5,10.23 15.5,8.71 14,7.97V16C15.5,15.29 16.5,13.76 16.5,12M3,9V15H7L12,20V4L7,9H3Z",
    "controller-classic-outline": "M17.5,7A5.5,5.5 0 0,1 23,12.5A5.5,5.5 0 0,1 17.5,18C15.79,18 14.27,17.22 13.26,16H10.74C9.73,17.22 8.21,18 6.5,18A5.5,5.5 0 0,1 1,12.5A5.5,5.5 0 0,1 6.5,7H17.5M6.5,9A3.5,3.5 0 0,0 3,12.5A3.5,3.5 0 0,0 6.5,16C7.9,16 9.1,15.18 9.66,14H14.34C14.9,15.18 16.1,16 17.5,16A3.5,3.5 0 0,0 21,12.5A3.5,3.5 0 0,0 17.5,9H6.5M5.75,10.25H7.25V11.75H8.75V13.25H7.25V14.75H5.75V13.25H4.25V11.75H5.75V10.25M16.75,12.5A1,1 0 0,1 17.75,13.5A1,1 0 0,1 16.75,14.5A1,1 0 0,1 15.75,13.5A1,1 0 0,1 16.75,12.5M18.75,10.5A1,1 0 0,1 19.75,11.5A1,1 0 0,1 18.75,12.5A1,1 0 0,1 17.75,11.5A1,1 0 0,1 18.75,10.5Z",
    "timeline-outline": "M4 2V8H2V2H4M2 22V16H4V22H2M5 12C5 13.11 4.11 14 3 14C1.9 14 1 13.11 1 12C1 10.9 1.9 10 3 10C4.11 10 5 10.9 5 12M24 6V18C24 19.11 23.11 20 22 20H10C8.9 20 8 19.11 8 18V14L6 12L8 10V6C8 4.89 8.9 4 10 4H22C23.11 4 24 4.89 24 6M10 6V18H22V6H10Z",
    "tune-variant": "M8 13C6.14 13 4.59 14.28 4.14 16H2V18H4.14C4.59 19.72 6.14 21 8 21S11.41 19.72 11.86 18H22V16H11.86C11.41 14.28 9.86 13 8 13M8 19C6.9 19 6 18.1 6 17C6 15.9 6.9 15 8 15S10 15.9 10 17C10 18.1 9.1 19 8 19M19.86 6C19.41 4.28 17.86 3 16 3S12.59 4.28 12.14 6H2V8H12.14C12.59 9.72 14.14 11 16 11S19.41 9.72 19.86 8H22V6H19.86M16 9C14.9 9 14 8.1 14 7C14 5.9 14.9 5 16 5S18 5.9 18 7C18 8.1 17.1 9 16 9Z",
    // Wiring's. Copied in from the twenty-two paths the #398 prototype committed
    // rather than fetching the package again, which is the convention
    // docs/icon-set-provenance.md sets; the path itself is
    // 83acf0db:prototypes/395-surface-anatomy/chrome.js:38.
    "connection": "M21.4 7.5C22.2 8.3 22.2 9.6 21.4 10.3L18.6 13.1L10.8 5.3L13.6 2.5C14.4 1.7 15.7 1.7 16.4 2.5L18.2 4.3L21.2 1.3L22.6 2.7L19.6 5.7L21.4 7.5M15.6 13.3L14.2 11.9L11.4 14.7L9.3 12.6L12.1 9.8L10.7 8.4L7.9 11.2L6.4 9.8L3.6 12.6C2.8 13.4 2.8 14.7 3.6 15.4L5.4 17.2L1.4 21.2L2.8 22.6L6.8 18.6L8.6 20.4C9.4 21.2 10.7 21.2 11.4 20.4L14.2 17.6L12.8 16.2L15.6 13.3Z",
    "puzzle-outline": "M22,13.5C22,15.26 20.7,16.72 19,16.96V20A2,2 0 0,1 17,22H13.2V21.7A2.7,2.7 0 0,0 10.5,19C9,19 7.8,20.21 7.8,21.7V22H4A2,2 0 0,1 2,20V16.2H2.3C3.79,16.2 5,15 5,13.5C5,12 3.79,10.8 2.3,10.8H2V7A2,2 0 0,1 4,5H7.04C7.28,3.3 8.74,2 10.5,2C12.26,2 13.72,3.3 13.96,5H17A2,2 0 0,1 19,7V10.04C20.7,10.28 22,11.74 22,13.5M17,15H18.5A1.5,1.5 0 0,0 20,13.5A1.5,1.5 0 0,0 18.5,12H17V7H12V5.5A1.5,1.5 0 0,0 10.5,4A1.5,1.5 0 0,0 9,5.5V7H4V9.12C5.76,9.8 7,11.5 7,13.5C7,15.5 5.75,17.2 4,17.88V20H6.12C6.8,18.25 8.5,17 10.5,17C12.5,17 14.2,18.25 14.88,20H17V15Z",
    "robot-outline": "M17.5 15.5C17.5 16.61 16.61 17.5 15.5 17.5S13.5 16.61 13.5 15.5 14.4 13.5 15.5 13.5 17.5 14.4 17.5 15.5M8.5 13.5C7.4 13.5 6.5 14.4 6.5 15.5S7.4 17.5 8.5 17.5 10.5 16.61 10.5 15.5 9.61 13.5 8.5 13.5M23 15V18C23 18.55 22.55 19 22 19H21V20C21 21.11 20.11 22 19 22H5C3.9 22 3 21.11 3 20V19H2C1.45 19 1 18.55 1 18V15C1 14.45 1.45 14 2 14H3C3 10.13 6.13 7 10 7H11V5.73C10.4 5.39 10 4.74 10 4C10 2.9 10.9 2 12 2S14 2.9 14 4C14 4.74 13.6 5.39 13 5.73V7H14C17.87 7 21 10.13 21 14H22C22.55 14 23 14.45 23 15M21 16H19V14C19 11.24 16.76 9 14 9H10C7.24 9 5 11.24 5 14V16H3V17H5V20H19V17H21V16Z",
    "wifi": "M12,21L15.6,16.2C14.6,15.45 13.35,15 12,15C10.65,15 9.4,15.45 8.4,16.2L12,21M12,3C7.95,3 4.21,4.34 1.2,6.6L3,9C5.5,7.12 8.62,6 12,6C15.38,6 18.5,7.12 21,9L22.8,6.6C19.79,4.34 16.05,3 12,3M12,9C9.3,9 6.81,9.89 4.8,11.4L6.6,13.8C8.1,12.67 9.97,12 12,12C14.03,12 15.9,12.67 17.4,13.8L19.2,11.4C17.19,9.89 14.7,9 12,9Z",
    "chip": "M6,4H18V5H21V7H18V9H21V11H18V13H21V15H18V17H21V19H18V20H6V19H3V17H6V15H3V13H6V11H3V9H6V7H3V5H6V4M11,15V18H12V15H11M13,15V18H14V15H13M15,15V18H16V15H15Z",
    // Maintenance's, the prototype's own choice for that row, copied in from
    // 83acf0db:prototypes/395-surface-anatomy/chrome.js:39 the way Wiring's was.
    "wrench-outline": "M22.61,19L13.53,9.91C14.46,7.57 14,4.81 12.09,2.91C9.79,0.61 6.21,0.4 3.66,2.26L7.5,6.11L6.08,7.5L2.25,3.69C0.39,6.23 0.6,9.82 2.9,12.11C4.76,13.97 7.47,14.46 9.79,13.59L18.9,22.7C19.29,23.09 19.92,23.09 20.31,22.7L22.61,20.4C23,20 23,19.39 22.61,19M19.61,20.59L10.15,11.13C9.54,11.58 8.86,11.85 8.15,11.95C6.79,12.15 5.36,11.74 4.32,10.7C3.37,9.76 2.93,8.5 3,7.26L6.09,10.35L10.33,6.11L7.24,3C8.5,2.95 9.73,3.39 10.68,4.33C11.76,5.41 12.17,6.9 11.92,8.29C11.8,9 11.5,9.66 11.04,10.25L20.5,19.7L19.61,20.59Z",
    "stop-circle-outline": "M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4C16.41,4 20,7.59 20,12C20,16.41 16.41,20 12,20C7.59,20 4,16.41 4,12C4,7.59 7.59,4 12,4M9,9V15H15V9",
    "power-sleep": "M18.73,18C15.4,21.69 9.71,22 6,18.64C2.33,15.31 2.04,9.62 5.37,5.93C6.9,4.25 9,3.2 11.27,3C7.96,6.7 8.27,12.39 12,15.71C13.63,17.19 15.78,18 18,18C18.25,18 18.5,18 18.73,18Z",
    "restart": "M12,4C14.1,4 16.1,4.8 17.6,6.3C20.7,9.4 20.7,14.5 17.6,17.6C15.8,19.5 13.3,20.2 10.9,19.9L11.4,17.9C13.1,18.1 14.9,17.5 16.2,16.2C18.5,13.9 18.5,10.1 16.2,7.7C15.1,6.6 13.5,6 12,6V10.6L7,5.6L12,0.6V4M6.3,17.6C3.7,15 3.3,11 5.1,7.9L6.6,9.4C5.5,11.6 5.9,14.4 7.8,16.2C8.3,16.7 8.9,17.1 9.6,17.4L9,19.4C8,19 7.1,18.4 6.3,17.6Z",
    "console-line": "M13,19V16H21V19H13M8.5,13L2.47,7H6.71L11.67,11.95C12.25,12.54 12.25,13.5 11.67,14.07L6.74,19H2.5L8.5,13Z",
    "chevron-right": "M8.59,16.58L13.17,12L8.59,7.41L10,6L16,12L10,18L8.59,16.58Z",
    // Foot Drive's pad. The vertical pair is not among the twenty-two paths the
    // #398 reference committed, so forward and reverse are this horizontal
    // arrow turned a quarter in the stylesheet rather than two more paths taken
    // from a package nobody in this slice read (docs/icon-set-provenance.md).
    "arrow-left": "M20,11V13H8L13.5,18.5L12.08,19.92L4.16,12L12.08,4.08L13.5,5.5L8,11H20Z",
    "arrow-right": "M4,11V13H16L10.5,18.5L11.92,19.92L19.84,12L11.92,4.08L10.5,5.5L16,11H4Z",
    // Wiring's printable sheet (#411): not among the #398 twenty-two, so read
    // from @mdi/svg 7.4.47 svg/printer-outline.svg, the -outline style the
    // chrome's other icons keep.
    "printer-outline": "M19 8C20.66 8 22 9.34 22 11V17H18V21H6V17H2V11C2 9.34 3.34 8 5 8H6V3H18V8H19M8 5V8H16V5H8M16 19V15H8V19H16M18 15H20V11C20 10.45 19.55 10 19 10H5C4.45 10 4 10.45 4 11V15H6V13H18V15M19 11.5C19 12.05 18.55 12.5 18 12.5C17.45 12.5 17 12.05 17 11.5C17 10.95 17.45 10.5 18 10.5C18.55 10.5 19 10.95 19 11.5Z",
    // Lights' (#410): not among the #398 twenty-two, so read from @mdi/svg
    // 7.4.47 svg/lightbulb-outline.svg, the -outline style the chrome keeps.
    "lightbulb-outline": "M12,2A7,7 0 0,1 19,9C19,11.38 17.81,13.47 16,14.74V17A1,1 0 0,1 15,18H9A1,1 0 0,1 8,17V14.74C6.19,13.47 5,11.38 5,9A7,7 0 0,1 12,2M9,21V20H15V21A1,1 0 0,1 14,22H10A1,1 0 0,1 9,21M12,4A5,5 0 0,0 7,9C7,11.05 8.23,12.81 10,13.58V16H14V13.58C15.77,12.81 17,11.05 17,9A5,5 0 0,0 12,4Z",
    // Find by Moving, on a Part's row on Wiring (#411; operator, 2026-09-30:
    // "a magnifying glass to the \"find by moving\" button"): not among the
    // #398 twenty-two, so read from @mdi/svg 7.4.47 svg/magnify.svg.
    "magnify": "M9.5,3A6.5,6.5 0 0,1 16,9.5C16,11.11 15.41,12.59 14.44,13.73L14.71,14H15.5L20.5,19L19,20.5L14,15.5V14.71L13.73,14.44C12.59,15.41 11.11,16 9.5,16A6.5,6.5 0 0,1 3,9.5A6.5,6.5 0 0,1 9.5,3M9.5,5C7,5 5,7 5,9.5C5,12 7,14 9.5,14C12,14 14,12 14,9.5C14,7 12,5 9.5,5Z",
    // The Dashboard's Sequences (#451): Play and Stop on a Sequence's chip, and
    // the full-screen posture's way in and out. Read from @mdi/svg 7.4.47
    // svg/play.svg, svg/stop.svg, svg/fullscreen.svg, svg/fullscreen-exit.svg.
    "play": "M8,5.14V19.14L19,12.14L8,5.14Z",
    "stop": "M18,18H6V6H18V18Z",
    "fullscreen": "M5,5H10V7H7V10H5V5M14,5H19V10H17V7H14V5M17,14H19V19H14V17H17V14M10,17V19H5V14H7V17H10Z",
    "fullscreen-exit": "M14,14H19V16H16V19H14V14M5,14H10V19H8V16H5V14M8,5H10V10H5V8H8V5M19,8V10H14V5H16V8H19Z",
    // The small pin in a Sequence's chip on the Dashboard (#472): filled when
    // it is pinned, and the outline below, already here for Use as Stand
    // Down, when it is not. Read from @mdi/svg 7.4.47 svg/pin.svg.
    "pin": "M16,12V4H17V2H7V4H8V12L6,14V16H11.2V22H12.8V16H18V14L16,12Z",
    // Every action button's act (#460): one act wears one icon on every
    // surface, and the act -> icon map is docs/icon-set-provenance.md's. Read
    // from @mdi/svg 7.4.47 svg/<name>.svg, path data unmodified.
    "content-save-outline": "M17 3H5C3.89 3 3 3.9 3 5V19C3 20.1 3.89 21 5 21H19C20.1 21 21 20.1 21 19V7L17 3M19 19H5V5H16.17L19 7.83V19M12 12C10.34 12 9 13.34 9 15S10.34 18 12 18 15 16.66 15 15 13.66 12 12 12M6 6H15V10H6V6Z",
    "download-outline": "M13,5V11H14.17L12,13.17L9.83,11H11V5H13M15,3H9V9H5L12,16L19,9H15V3M19,18H5V20H19V18Z",
    "upload-outline": "M9,10V16H15V10H19L12,3L5,10H9M12,5.8L14.2,8H13V14H11V8H9.8L12,5.8M19,18H5V20H19V18Z",
    "pencil-outline": "M14.06,9L15,9.94L5.92,19H5V18.08L14.06,9M17.66,3C17.41,3 17.15,3.1 16.96,3.29L15.13,5.12L18.88,8.87L20.71,7.04C21.1,6.65 21.1,6 20.71,5.63L18.37,3.29C18.17,3.09 17.92,3 17.66,3M14.06,6.19L3,17.25V21H6.75L17.81,9.94L14.06,6.19Z",
    "delete-outline": "M6,19A2,2 0 0,0 8,21H16A2,2 0 0,0 18,19V7H6V19M8,9H16V19H8V9M15.5,4L14.5,3H9.5L8.5,4H5V6H19V4H15.5Z",
    "content-copy": "M19,21H8V7H19M19,5H8A2,2 0 0,0 6,7V21A2,2 0 0,0 8,23H19A2,2 0 0,0 21,21V7A2,2 0 0,0 19,5M16,1H4A2,2 0 0,0 2,3V17H4V3H16V1Z",
    "plus": "M19,13H13V19H11V13H5V11H11V5H13V11H19V13Z",
    "refresh": "M17.65,6.35C16.2,4.9 14.21,4 12,4A8,8 0 0,0 4,12A8,8 0 0,0 12,20C15.73,20 18.84,17.45 19.73,14H17.65C16.83,16.33 14.61,18 12,18A6,6 0 0,1 6,12A6,6 0 0,1 12,6C13.66,6 15.14,6.69 16.22,7.78L13,11H20V4L17.65,6.35Z",
    "undo": "M12.5,8C9.85,8 7.45,9 5.6,10.6L2,7V16H11L7.38,12.38C8.77,11.22 10.54,10.5 12.5,10.5C16.04,10.5 19.05,12.81 20.1,16L22.47,15.22C21.08,11.03 17.15,8 12.5,8Z",
    "redo": "M18.4,10.6C16.55,9 14.15,8 11.5,8C6.85,8 2.92,11.03 1.54,15.22L3.9,16C4.95,12.81 7.95,10.5 11.5,10.5C13.45,10.5 15.23,11.22 16.62,12.38L13,16H22V7L18.4,10.6Z",
    "restore": "M13,3A9,9 0 0,0 4,12H1L4.89,15.89L4.96,16.03L9,12H6A7,7 0 0,1 13,5A7,7 0 0,1 20,12A7,7 0 0,1 13,19C11.07,19 9.32,18.21 8.06,16.94L6.64,18.36C8.27,20 10.5,21 13,21A9,9 0 0,0 22,12A9,9 0 0,0 13,3Z",
    "check": "M21,7L9,19L3.5,13.5L4.91,12.09L9,16.17L19.59,5.59L21,7Z",
    "close": "M19,6.41L17.59,5L12,10.59L6.41,5L5,6.41L10.59,12L5,17.59L6.41,19L12,13.41L17.59,19L19,17.59L13.41,12L19,6.41Z",
    "arrow-u-left-top": "M20 13.5C20 17.09 17.09 20 13.5 20H6V18H13.5C16 18 18 16 18 13.5S16 9 13.5 9H7.83L10.91 12.09L9.5 13.5L4 8L9.5 2.5L10.92 3.91L7.83 7H13.5C17.09 7 20 9.91 20 13.5Z",
    "arrow-u-right-top": "M10.5 18H18V20H10.5C6.91 20 4 17.09 4 13.5S6.91 7 10.5 7H16.17L13.08 3.91L14.5 2.5L20 8L14.5 13.5L13.09 12.09L16.17 9H10.5C8 9 6 11 6 13.5S8 18 10.5 18Z",
    "exit-to-app": "M19,3H5C3.89,3 3,3.89 3,5V9H5V5H19V19H5V15H3V19A2,2 0 0,0 5,21H19A2,2 0 0,0 21,19V5C21,3.89 20.1,3 19,3M10.08,15.58L11.5,17L16.5,12L11.5,7L10.08,8.41L12.67,11H3V13H12.67L10.08,15.58Z",
    "record-circle-outline": "M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4M12,9A3,3 0 0,0 9,12A3,3 0 0,0 12,15A3,3 0 0,0 15,12A3,3 0 0,0 12,9Z",
    "sleep-off": "M2,5.27L3.28,4L20,20.72L18.73,22L12.73,16H9V14L9.79,13.06L2,5.27M23,12H17V10L20.39,6H17V4H23V6L19.62,10H23V12M9.82,8H15V10L13.54,11.72L9.82,8M7,20H1V18L4.39,14H1V12H7V14L3.62,18H7V20Z",
    "human-handsdown": "M12,1C10.89,1 10,1.9 10,3C10,4.11 10.89,5 12,5C13.11,5 14,4.11 14,3A2,2 0 0,0 12,1M10,6C9.73,6 9.5,6.11 9.31,6.28H9.3L4,11.59L5.42,13L9,9.41V22H11V15H13V22H15V9.41L18.58,13L20,11.59L14.7,6.28C14.5,6.11 14.27,6 14,6",
    "pin-outline": "M16,12V4H17V2H7V4H8V12L6,14V16H11.2V22H12.8V16H18V14L16,12M8.8,14L10,12.8V4H14V12.8L15.2,14H8.8Z",
    "target": "M11,2V4.07C7.38,4.53 4.53,7.38 4.07,11H2V13H4.07C4.53,16.62 7.38,19.47 11,19.93V22H13V19.93C16.62,19.47 19.47,16.62 19.93,13H22V11H19.93C19.47,7.38 16.62,4.53 13,4.07V2M11,6.08V8H13V6.09C15.5,6.5 17.5,8.5 17.92,11H16V13H17.91C17.5,15.5 15.5,17.5 13,17.92V16H11V17.91C8.5,17.5 6.5,15.5 6.08,13H8V11H6.09C6.5,8.5 8.5,6.5 11,6.08M12,11A1,1 0 0,0 11,12A1,1 0 0,0 12,13A1,1 0 0,0 13,12A1,1 0 0,0 12,11Z",
    "home-outline": "M12 5.69L17 10.19V18H15V12H9V18H7V10.19L12 5.69M12 3L2 12H5V20H11V14H13V20H19V12H22",
    "lan-connect": "M4,1C2.89,1 2,1.89 2,3V7C2,8.11 2.89,9 4,9H1V11H13V9H10C11.11,9 12,8.11 12,7V3C12,1.89 11.11,1 10,1H4M4,3H10V7H4V3M3,13V18L3,20H10V18H5V13H3M14,13C12.89,13 12,13.89 12,15V19C12,20.11 12.89,21 14,21H11V23H23V21H20C21.11,21 22,20.11 22,19V15C22,13.89 21.11,13 20,13H14M14,15H20V19H14V15Z",
    "lan-disconnect": "M4,1C2.89,1 2,1.89 2,3V7C2,8.11 2.89,9 4,9H1V11H13V9H10C11.11,9 12,8.11 12,7V3C12,1.89 11.11,1 10,1H4M4,3H10V7H4V3M14,13C12.89,13 12,13.89 12,15V19C12,20.11 12.89,21 14,21H11V23H23V21H20C21.11,21 22,20.11 22,19V15C22,13.89 21.11,13 20,13H14M3.88,13.46L2.46,14.88L4.59,17L2.46,19.12L3.88,20.54L6,18.41L8.12,20.54L9.54,19.12L7.41,17L9.54,14.88L8.12,13.46L6,15.59L3.88,13.46M14,15H20V19H14V15Z",
    "ray-start-arrow": "M23,12L19,16V13H6.83C6.42,14.17 5.31,15 4,15A3,3 0 0,1 1,12A3,3 0 0,1 4,9C5.31,9 6.42,9.83 6.83,11H19V8L23,12Z",
    "arrow-expand-horizontal": "M9,11H15V8L19,12L15,16V13H9V16L5,12L9,8V11M2,20V4H4V20H2M20,20V4H22V20H20Z",
    "arrow-collapse-horizontal": "M13,20V4H15.03V20H13M10,20V4H12.03V20H10M5,8L9.03,12L5,16V13H2V11H5V8M20,16L16,12L20,8V11H23V13H20V16Z",
    "ruler": "M1.39,18.36L3.16,16.6L4.58,18L5.64,16.95L4.22,15.54L5.64,14.12L8.11,16.6L9.17,15.54L6.7,13.06L8.11,11.65L9.53,13.06L10.59,12L9.17,10.59L10.59,9.17L13.06,11.65L14.12,10.59L11.65,8.11L13.06,6.7L14.47,8.11L15.54,7.05L14.12,5.64L15.54,4.22L18,6.7L19.07,5.64L16.6,3.16L18.36,1.39L22.61,5.64L5.64,22.61L1.39,18.36Z",
    "power-plug-off-outline": "M22.11 21.46L2.39 1.73L1.11 3L6.25 8.14C6.1 8.41 6 8.7 6 9V14.5L9.5 18V21H14.5V18L15.31 17.2L20.84 22.73L22.11 21.46M13.09 16.59L12.67 17H11.33L10.92 16.59L8 13.67V9.89L13.89 15.78L13.09 16.59M12.2 9L10.2 7H14V3H16V7C17 7 18 8 18 9V14.5L17.85 14.65L16 12.8V9.09C16 9.06 15.95 9 15.92 9H12.2M10 6.8L8 4.8V3H10V6.8Z",
    "format-horizontal-align-center": "M19,16V13H23V11H19V8L15,12L19,16M5,8V11H1V13H5V16L9,12L5,8M11,20H13V4H11V20Z",
    "minus-circle-outline": "M12,20C7.59,20 4,16.41 4,12C4,7.59 7.59,4 12,4C16.41,4 20,7.59 20,12C20,16.41 16.41,20 12,20M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M7,13H17V11H7",
    "plus-circle-outline": "M12,20C7.59,20 4,16.41 4,12C4,7.59 7.59,4 12,4C16.41,4 20,7.59 20,12C20,16.41 16.41,20 12,20M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M13,7H11V11H7V13H11V17H13V13H17V11H13V7Z",
    "swap-horizontal": "M21,9L17,5V8H10V10H17V13M7,11L3,15L7,19V16H14V14H7V11Z",
    "hand-back-right-outline": "M21 7C21 5.62 19.88 4.5 18.5 4.5C18.33 4.5 18.16 4.5 18 4.55V4C18 2.62 16.88 1.5 15.5 1.5C15.27 1.5 15.04 1.53 14.83 1.59C14.46 .66 13.56 0 12.5 0C11.27 0 10.25 .89 10.04 2.06C9.87 2 9.69 2 9.5 2C8.12 2 7 3.12 7 4.5V10.39C6.66 10.08 6.24 9.85 5.78 9.73L5 9.5C4.18 9.29 3.31 9.61 2.82 10.35C2.44 10.92 2.42 11.66 2.67 12.3L5.23 18.73C6.5 21.91 9.57 24 13 24C17.42 24 21 20.42 21 16V7M19 16C19 19.31 16.31 22 13 22C10.39 22 8.05 20.41 7.09 18L4.5 11.45L5 11.59C5.5 11.71 5.85 12.05 6 12.5L7 15H9V4.5C9 4.22 9.22 4 9.5 4S10 4.22 10 4.5V12H12V2.5C12 2.22 12.22 2 12.5 2S13 2.22 13 2.5V12H15V4C15 3.72 15.22 3.5 15.5 3.5S16 3.72 16 4V12H18V7C18 6.72 18.22 6.5 18.5 6.5S19 6.72 19 7V16Z",
    "arrow-left-right": "M6.45,17.45L1,12L6.45,6.55L7.86,7.96L4.83,11H19.17L16.14,7.96L17.55,6.55L23,12L17.55,17.45L16.14,16.04L19.17,13H4.83L7.86,16.04L6.45,17.45Z",
    "checkbox-multiple-marked-outline": "M20,16V10H22V16A2,2 0 0,1 20,18H8C6.89,18 6,17.1 6,16V4C6,2.89 6.89,2 8,2H16V4H8V16H20M10.91,7.08L14,10.17L20.59,3.58L22,5L14,13L9.5,8.5L10.91,7.08M16,20V22H4A2,2 0 0,1 2,20V7H4V20H16Z",
    "checkbox-multiple-blank-outline": "M20,16V4H8V16H20M22,16A2,2 0 0,1 20,18H8C6.89,18 6,17.1 6,16V4C6,2.89 6.89,2 8,2H20A2,2 0 0,1 22,4V16M16,20V22H4A2,2 0 0,1 2,20V7H4V20H16Z",
    "link-variant": "M10.59,13.41C11,13.8 11,14.44 10.59,14.83C10.2,15.22 9.56,15.22 9.17,14.83C7.22,12.88 7.22,9.71 9.17,7.76V7.76L12.71,4.22C14.66,2.27 17.83,2.27 19.78,4.22C21.73,6.17 21.73,9.34 19.78,11.29L18.29,12.78C18.3,11.96 18.17,11.14 17.89,10.36L18.36,9.88C19.54,8.71 19.54,6.81 18.36,5.64C17.19,4.46 15.29,4.46 14.12,5.64L10.59,9.17C9.41,10.34 9.41,12.24 10.59,13.41M13.41,9.17C13.8,8.78 14.44,8.78 14.83,9.17C16.78,11.12 16.78,14.29 14.83,16.24V16.24L11.29,19.78C9.34,21.73 6.17,21.73 4.22,19.78C2.27,17.83 2.27,14.66 4.22,12.71L5.71,11.22C5.7,12.04 5.83,12.86 6.11,13.65L5.64,14.12C4.46,15.29 4.46,17.19 5.64,18.36C6.81,19.54 8.71,19.54 9.88,18.36L13.41,14.83C14.59,13.66 14.59,11.76 13.41,10.59C13,10.2 13,9.56 13.41,9.17Z",
    "link-variant-off": "M2,5.27L3.28,4L20,20.72L18.73,22L13.9,17.17L11.29,19.78C9.34,21.73 6.17,21.73 4.22,19.78C2.27,17.83 2.27,14.66 4.22,12.71L5.71,11.22C5.7,12.04 5.83,12.86 6.11,13.65L5.64,14.12C4.46,15.29 4.46,17.19 5.64,18.36C6.81,19.54 8.71,19.54 9.88,18.36L12.5,15.76L10.88,14.15C10.87,14.39 10.77,14.64 10.59,14.83C10.2,15.22 9.56,15.22 9.17,14.83C8.12,13.77 7.63,12.37 7.72,11L2,5.27M12.71,4.22C14.66,2.27 17.83,2.27 19.78,4.22C21.73,6.17 21.73,9.34 19.78,11.29L18.29,12.78C18.3,11.96 18.17,11.14 17.89,10.36L18.36,9.88C19.54,8.71 19.54,6.81 18.36,5.64C17.19,4.46 15.29,4.46 14.12,5.64L10.79,8.97L9.38,7.55L12.71,4.22M13.41,9.17C13.8,8.78 14.44,8.78 14.83,9.17C16.2,10.54 16.61,12.5 16.06,14.23L14.28,12.46C14.23,11.78 13.94,11.11 13.41,10.59C13,10.2 13,9.56 13.41,9.17Z",
    "auto-fix": "M7.5,5.6L5,7L6.4,4.5L5,2L7.5,3.4L10,2L8.6,4.5L10,7L7.5,5.6M19.5,15.4L22,14L20.6,16.5L22,19L19.5,17.6L17,19L18.4,16.5L17,14L19.5,15.4M22,2L20.6,4.5L22,7L19.5,5.6L17,7L18.4,4.5L17,2L19.5,3.4L22,2M13.34,12.78L15.78,10.34L13.66,8.22L11.22,10.66L13.34,12.78M14.37,7.29L16.71,9.63C17.1,10 17.1,10.65 16.71,11.04L5.04,22.71C4.65,23.1 4,23.1 3.63,22.71L1.29,20.37C0.9,20 0.9,19.35 1.29,18.96L12.96,7.29C13.35,6.9 14,6.9 14.37,7.29Z",
    "shuffle-variant": "M17,3L22.25,7.5L17,12L22.25,16.5L17,21V18H14.26L11.44,15.18L13.56,13.06L15.5,15H17V12L17,9H15.5L6.5,18H2V15H5.26L14.26,6H17V3M2,6H6.5L9.32,8.82L7.2,10.94L5.26,9H2V6Z",
    "shuffle-disabled": "M16,4.5V7H5V9H16V11.5L19.5,8M16,12.5V15H5V17H16V19.5L19.5,16",
    "share-variant-outline": "M18 16.08C17.24 16.08 16.56 16.38 16.04 16.85L8.91 12.7C8.96 12.47 9 12.24 9 12S8.96 11.53 8.91 11.3L15.96 7.19C16.5 7.69 17.21 8 18 8C19.66 8 21 6.66 21 5S19.66 2 18 2 15 3.34 15 5C15 5.24 15.04 5.47 15.09 5.7L8.04 9.81C7.5 9.31 6.79 9 6 9C4.34 9 3 10.34 3 12S4.34 15 6 15C6.79 15 7.5 14.69 8.04 14.19L15.16 18.34C15.11 18.55 15.08 18.77 15.08 19C15.08 20.61 16.39 21.91 18 21.91S20.92 20.61 20.92 19C20.92 17.39 19.61 16.08 18 16.08M18 4C18.55 4 19 4.45 19 5S18.55 6 18 6 17 5.55 17 5 17.45 4 18 4M6 13C5.45 13 5 12.55 5 12S5.45 11 6 11 7 11.45 7 12 6.55 13 6 13M18 20C17.45 20 17 19.55 17 19S17.45 18 18 18 19 18.45 19 19 18.55 20 18 20Z",
    "music-note-off-outline": "M14 7H18V3H12V7.61L14 9.61M12 10.44L4.41 2.86L3 4.27L12 13.27V13.55A3.94 3.94 0 0 0 8.67 13.23A4 4 0 0 0 10.65 20.95A4.1 4.1 0 0 0 14 16.85V15.27L19.73 21L21.14 19.59M10 19A2 2 0 1 1 12 17A2 2 0 0 1 10 19Z",
    "grid": "M10,4V8H14V4H10M16,4V8H20V4H16M16,10V14H20V10H16M16,16V20H20V16H16M14,20V16H10V20H14M8,20V16H4V20H8M8,14V10H4V14H8M8,8V4H4V8H8M10,14H14V10H10V14M4,2H20A2,2 0 0,1 22,4V20A2,2 0 0,1 20,22H4C2.92,22 2,21.1 2,20V4A2,2 0 0,1 4,2Z",
    "metronome": "M12,1.75L8.57,2.67L4.06,19.53C4.03,19.68 4,19.84 4,20C4,21.11 4.89,22 6,22H18C19.11,22 20,21.11 20,20C20,19.84 19.97,19.68 19.94,19.53L18.58,14.42L17,16L17.2,17H13.41L16.25,14.16L14.84,12.75L10.59,17H6.8L10.29,4H13.71L15.17,9.43L16.8,7.79L15.43,2.67L12,1.75M11.25,5V14.75L12.75,13.25V5H11.25M19.79,7.8L16.96,10.63L16.25,9.92L14.84,11.34L17.66,14.16L19.08,12.75L18.37,12.04L21.2,9.21L19.79,7.8Z",
    "gesture-tap": "M10,9A1,1 0 0,1 11,8A1,1 0 0,1 12,9V13.47L13.21,13.6L18.15,15.79C18.68,16.03 19,16.56 19,17.14V21.5C18.97,22.32 18.32,22.97 17.5,23H11C10.62,23 10.26,22.85 10,22.57L5.1,18.37L5.84,17.6C6.03,17.39 6.3,17.28 6.58,17.28H6.8L10,19V9M11,5A4,4 0 0,1 15,9C15,10.5 14.2,11.77 13,12.46V11.24C13.61,10.69 14,9.89 14,9A3,3 0 0,0 11,6A3,3 0 0,0 8,9C8,9.89 8.39,10.69 9,11.24V12.46C7.8,11.77 7,10.5 7,9A4,4 0 0,1 11,5Z",
    "waveform": "M22 12L20 13L19 14L18 13L17 16L16 13L15 21L14 13L13 15L12 13L11 17L10 13L9 22L8 13L7 19L6 13L5 14L4 13L2 12L4 11L5 10L6 11L7 5L8 11L9 2L10 11L11 7L12 11L13 9L14 11L15 3L16 11L17 8L18 11L19 10L20 11L22 12Z",
    "arrow-split-vertical": "M18,16V13H15V22H13V2H15V11H18V8L22,12L18,16M2,12L6,16V13H9V22H11V2H9V11H6V8L2,12Z",
    "transfer": "M8 4A2 2 0 0 0 6 6V10H8V6H16V9H13.5L17 12.5L20.5 9H18V6A2 2 0 0 0 16 4H8M3 12V14H11V12H3M3 15V17H11V15H3M13 15V17H21V15H13M3 18V20H11V18H3M13 18V20H21V18H13Z",
    "compass-outline": "M7,17L10.2,10.2L17,7L13.8,13.8L7,17M12,11.1A0.9,0.9 0 0,0 11.1,12A0.9,0.9 0 0,0 12,12.9A0.9,0.9 0 0,0 12.9,12A0.9,0.9 0 0,0 12,11.1M12,2A10,10 0 0,1 22,12A10,10 0 0,1 12,22A10,10 0 0,1 2,12A10,10 0 0,1 12,2M12,4A8,8 0 0,0 4,12A8,8 0 0,0 12,20A8,8 0 0,0 20,12A8,8 0 0,0 12,4Z",
    "eraser": "M16.24,3.56L21.19,8.5C21.97,9.29 21.97,10.55 21.19,11.34L12,20.53C10.44,22.09 7.91,22.09 6.34,20.53L2.81,17C2.03,16.21 2.03,14.95 2.81,14.16L13.41,3.56C14.2,2.78 15.46,2.78 16.24,3.56M4.22,15.58L7.76,19.11C8.54,19.9 9.8,19.9 10.59,19.11L14.12,15.58L9.17,10.63L4.22,15.58Z",
  };

  const spriteHtml = () =>
    `<svg class="sprite" aria-hidden="true" focusable="false">` +
    Object.entries(ICONS)
      .map(([name, d]) => `<symbol id="i-${name}" viewBox="0 0 24 24"><path d="${d}"/></symbol>`)
      .join("") +
    `</svg>`;

  // An icon always travels with a label, so it is hidden from assistive
  // technology: the text beside it is the accessible name - on an act, the
  // .act-label the stylesheet may be hiding (below) - and a second copy of that
  // name in a title or an aria-label is a second thing to keep in step
  // (WCAG 2.5.3).
  const icon = (name, className = "i") =>
    `<svg class="${className}" aria-hidden="true" focusable="false"><use href="#i-${name}"/></svg>`;

  // ---------------------------------------------------------------------------
  // The act: an action button that shows its icon alone (#460)
  //
  // Icon-only is a trial the operator decides at the look, so it undoes with
  // one switch. The words stay in the button, in .act-label, and only the
  // stylesheet hides them: they are the accessible name, so no aria-label
  // copies them, and the tooltip below reads them from there rather than
  // holding a second copy.
  //
  // THE SWITCH is the class `act-words` on <html>, which ACT_WORDS sets. With
  // it every act shows its words beside its icon again, at its labelled width,
  // and the tooltip stands down; no markup changes (data/style.css, "The act").
  //
  // An act that carries class act-keeps-words shows its words whatever the
  // switch says, and has no tooltip: the ones that move the droid with nothing
  // on the page explaining them, whose words carry a count, a name or a
  // warning, that answer a question, or whose words are a state in progress
  // (docs/icon-set-provenance.md, "One act, one icon").
  // ---------------------------------------------------------------------------
  const ACT_WORDS = false;
  document.documentElement.classList.toggle("act-words", ACT_WORDS);

  // An act's face, for a button built in script: its icon, then its words.
  // `words` is plain text. The button itself also carries class icon-act.
  const actFace = (name, words) =>
    `${icon(name)}<span class="act-label">${window.PAUtils.escapeHtml(words)}</span>`;

  // An act whose words change while it is on screen changes them here, never
  // through the button's textContent, which would take the icon and the
  // words' span with it. A change of act changes its icon in the same call.
  const setAct = (button, words, name = null) => {
    const label = button.querySelector(".act-label");
    if (!label) throw new Error(`setAct: no .act-label in ${button.outerHTML.slice(0, 80)}`);
    label.textContent = words;
    if (name) button.querySelector("svg.i > use").setAttribute("href", `#i-${name}`);
  };

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
      members: ["drive", "dome", "sound", "rc"],
    },
    {
      id: "perform",
      label: "Perform",
      members: ["seq", "sound", "dome"],
    },
    {
      id: "configure",
      label: "Configure",
      members: [
        // Droid Build heads the group: it is the answer the rest of Configure
        // is shaped by. Dormant until the C3 group (#351 and its siblings)
        // lands it as a destination (#288).
        "droidbuild",
        // What the droid is made of -- Hardware Components and Droid Identity
        // (#288, #404); the LED Strip moved to Lights (#410). Guided Setup is
        // drawn over it while the droid is not set up, and is never in a group
        // of its own (#351).
        "configuration",
        // Servos stays beside Parts, and stays a page. It holds the Outputs:
        // driving each one, the calibration dial, Find by Moving and back to
        // centre, all moved there from Parts (operator, 2026-09-19 on #412),
        // while Parts keeps the part-first side and the droid picture.
        "servo",
        "parts",
        "wiring",
        "lights",
      ],
    },
    {
      id: "maintain",
      label: "Maintain",
      members: [
        // The other half of what was one Setup page (#288, #404).
        "maintenance",
        "wifi",
        "firmware",
      ],
    },
  ];

  const DEFAULT_PAGE = "home";

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
  // And the document a renamed surface used to be, read off the same rename
  // record: a link to /setup.html is a click on this page, and it opens what the
  // alias says rather than a full page load through the forwarder that file now
  // is (#404). A surface's own document always wins, so an alias can never take
  // over an address a surface is actually served from.
  SURFACES.forEach((surface) => {
    surface.aliases.forEach((alias) => {
      const legacyDoc = `/${alias}.html`;
      if (!surfaceForPath.has(legacyDoc)) surfaceForPath.set(legacyDoc, surface);
    });
  });

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

  // Every surface is a place to come back to. Guided Setup, which is not, was
  // kept out of this value while it had a route of its own; it is now drawn over
  // Configuration only while the droid is not set up, so landing there on a
  // cold boot shows it exactly when it should be shown (#404).
  const rememberSurface = (page) => {
    writeStored({ surface: page });
  };

  // The stored value is also corrected on the way in, and the correction is
  // written back: a store naming nothing this build has -- from a hand edit, or
  // from a version that had it -- must stop naming it rather than be re-filtered
  // on every boot. An old spelling is not nothing: it resolves through the
  // aliases like any address a builder typed.
  const rememberedSurface = () => {
    const stored = readStored().surface;
    if (surfaceFor.has(stored)) return surfaceFor.get(stored);
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
  // Feature Availability (data/feature_availability.js) listens to the event,
  // while the cache closes late-load ordering gaps.
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

  // "Switch it on in Configuration", for a surface whose component is off. The
  // destination is read out of SURFACES like every other place a surface is
  // named, so the page a component is switched on from is named once (#288).
  // The helpers keep their `setup` spelling because it is their callers' API -
  // Configuration is what that page was renamed to (#404) - and every link
  // written before the rename still arrives through the `setup` alias.
  const componentHome = surfaceByPage.get("configuration");
  window.PAUi = window.PAUi || {};
  if (typeof window.PAUi.setupActionText !== "function") {
    window.PAUi.setupActionText = (action) => `${action} in ${componentHome.name}`;
  }
  if (typeof window.PAUi.setupActionHtml !== "function") {
    window.PAUi.setupActionHtml = (action) =>
      `${action} in <a class="setup-link" href="${componentHome.doc}">${componentHome.name}</a>`;
  }
  window.PAUi.actFace = actFace;
  window.PAUi.setAct = setAct;

  // What the droid has reported, as the Live Reading answers it
  // (data/live_reading.js, GLOSSARY.md "Live Reading"). The plate, the estop and
  // the notice read it the way every surface does; this file decides nothing
  // about a frame on its own account, and it is the one thing that starts it.
  const LIVE = window.PALiveReading;

  // ---------------------------------------------------------------------------
  // The Latching Estop: what its label says, in every state
  //
  // Every state shows something, including the one before any status has
  // arrived, which shows the moving dots. An estop that renders nothing while
  // it is not engaged cannot be told from one that has stopped updating, which
  // is the whole reason the reference's paint function has no blank branch
  // (r2d2-astromech-simulator v1.79.0, src/js/app/hud.js:188).
  // ---------------------------------------------------------------------------
  // The line reads "Estop: " and then the answer, which is a slot of its own
  // so Waiting shows there as the moving dots (data/live_reading.js).
  const ESTOP_STATE_TEXT = {
    waiting: LIVE.slotText(LIVE.WAITING),
    clear: "clear",
    latched: "latched",
  };

  // What a press on STOP, or on the plate's ESTOP cell, does in its two
  // directions: the line beside the button (the cell's title), the button's
  // accessible name and the latched cell's. Each name opens with the word on
  // its face, STOP or ESTOP (WCAG 2.5.3 Label in Name), and the release names
  // itself as one, so a clear can never be read as a stop. The cell has no
  // name of its own while it would stop: its visible state is the name then.
  const ESTOP_PRESS = {
    stop: { label: "STOP - cuts all movement.", line: "Cuts all movement" },
    clear: {
      label: "STOP - latched. Press to release the estop.",
      line: "Press to release",
      cellLabel: "ESTOP - latched. Press to release the estop.",
    },
  };

  // ---------------------------------------------------------------------------
  // The Status Plate: what the droid is doing, in eight fixed positions
  //
  // The cut is the decision, not the list. /api/status carries 112 keys and
  // the great majority are web-server internals; a chip earns its place only
  // if seeing it would change what the operator does next, which is what
  // leaves eight (#324, GLOSSARY.md "Status Plate"). Telemetry -- uptime, heap,
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

  // The Live Reading's two words, in the plate's caps. Waiting is the
  // state before the droid has said anything, and no chip returns to it once a
  // frame has arrived -- except ESTOP, which goes back to it when contact with
  // the droid is lost. The other values are kept and it is the PLATE that says
  // how old they are. That is the difference between this and a per-chip
  // freshness marker, which #324 rejected. Unknown is a field the frames that
  // do arrive never carry.
  const CHIP_WAITING = LIVE.WAITING.toUpperCase();
  const CHIP_UNKNOWN = LIVE.UNKNOWN.toUpperCase();

  const hasKey = (payload, key) =>
    payload !== null && typeof payload === "object" && Object.prototype.hasOwnProperty.call(payload, key);

  // ---------------------------------------------------------------------------
  // Are the feet held: asked once
  //
  // The set of things that hold the feet was hand-copied between the DRIVE
  // chip and the notice's row for the same thing, and a set copied by hand
  // drifts the moment a fifth input can hold the feet and only one copy hears
  // about it. So the shell asks once, here, and the chip and the notice both
  // read the answer. Whether the estop itself is latched is the Live
  // Reading's question, asked once for every reader (#346, #419).
  // ---------------------------------------------------------------------------

  // The inputs OTHER than the estop that make DriveTask emit zero frames
  // (src/drive_arbiter.cpp, failsafeIsActive() || webTimedOut). Split out
  // rather than folded into feetAreHeld() because the notice needs exactly
  // this half: the estop has a row of its own that names the more specific
  // reason, and that row must not be shadowed by the general one.
  const feetHeldBesidesEstop = (status) =>
    LIVE.radioHoldsFeetIn(status) || status.webDriveExpired === true;

  // Everything that can hold the feet, the estop included. The DRIVE chip
  // reads this: a chip that watched one of the five would sit dark while the
  // droid was held still, which is the reference's shipped Bug 2 exactly
  // (r2d2-astromech-simulator v1.79.0, src/js/app/hud.js:203). It reads the
  // latch out of the frame, like the other four, so a lost link leaves DRIVE
  // showing the value it last had rather than inventing one.
  const feetAreHeld = (status) => LIVE.latchedIn(status) || feetHeldBesidesEstop(status);

  // A chip's state class: "" is the quiet default, "live" is the thing doing
  // its job, "stopped" is something stopped or refused. A posture the
  // operator chose -- Non-RC Control, Sleep Mode -- and a component nobody
  // fitted take no class at all, because color on this plate reports the
  // droid's health and never a choice (#327 "Status Color").
  const chipState = (state, value) => ({ state, value });

  // protoR2link and the sound link are the health-signal model's answer, not
  // this file's (data/health_signals.js, #422): the word every page shows for
  // them, in the plate's caps and short form, under the light the model gave.
  // The plate reads no line owner and no rx_status of its own.
  const LINKS = window.PAHealthSignals;
  const LINK_WORDS = { unknown: LIVE.UNKNOWN, waiting: LIVE.WAITING };
  const LINK_CHIP_CLASSES = { ok: "live", fail: "stopped" };
  const linkChip = ({ state, short }) => chipState(LINK_CHIP_CLASSES[state] || "", short.toUpperCase());

  const PLATE_CHIPS = [
    {
      id: "estop",
      label: "ESTOP",
      // The single cell that acts rather than routes, so it carries no page.
      // Its title is STOP's line and follows the latch (renderEstopCell).
      page: null,
      affordance: ESTOP_PRESS.stop.line,
      // The Live Reading's three-valued estop, not the frame's field: when
      // contact is lost the latch is not known any more, and this chip says so.
      read: (_status, reading) => {
        if (reading.estopLatched) return chipState("stopped", "LATCHED");
        if (reading.moveActsLive) return chipState("live", "CLEAR");
        return chipState("", CHIP_WAITING);
      },
    },
    {
      id: "drive",
      label: "DRIVE",
      page: "drive",
      // Every input that can hold the feet at zero, and there are five: the
      // operator's latch and the watchdog-reset latch (which the firmware
      // merges into `estop`), the receiver's hardware failsafe bit, the SBUS
      // watchdog, and the web-drive timeout. feetAreHeld() above is where that
      // set is written down, once.
      //
      // `failsafeSource` is deliberately NOT one of the five: the firmware
      // never resets it when a layer clears (src/failsafe_gate.cpp,
      // failsafeClear), so it names the last reason rather than a live one.
      read: (status) => {
        if (!hasKey(status, "drive")) return chipState("", "OFF");
        return feetAreHeld(status) ? chipState("stopped", "STOPPED") : chipState("live", "ARMED");
      },
    },
    {
      id: "rclink",
      label: "RC LINK",
      page: "rc",
      // The health-signal model's RC link, the same word table Health and
      // Wiring read (data/health_signals.js readRcLink, #399): its short
      // form in the plate's caps, under the light the model gave.
      read: (status) => linkChip(LINKS.readRcLink(status)),
    },
    {
      id: "control",
      label: "CONTROL",
      page: "drive",
      // Non-RC Control: the consent by which a browser, the Controller
      // Console or a sequence may command the droid. It is not persisted and
      // boots off, so "OFF" is the ordinary posture of a controller that has
      // just restarted rather than a fault -- and a chosen posture takes no
      // color.
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
      label: "protoR2link",
      page: "dome",
      read: (status) => linkChip(LINKS.readProtoR2link(status, LINK_WORDS)),
    },
    {
      id: "soundlink",
      label: "SOUND LINK",
      page: "sound",
      read: (status) => linkChip(LINKS.readSoundLink(status, LINK_WORDS)),
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
  // the state to keep in step (WCAG 2.5.3). The ESTOP cell is the exception,
  // because its press changes direction: it is STOP's toggle, so its title and,
  // while latched, its name say a release, as STOP's do (renderEstopCell).
  const chipHtml = (chip) => {
    // Label over value, with the signal light inside the value line: the shape
    // an instrument uses, where the label is the engraving on the panel and the
    // value is what the needle says (ADR 0066). The value's words are a slot
    // of their own beside the light, so a repaint rewrites the words and never
    // the light, and Waiting shows there as the moving dots.
    const inner =
      `<span class="status-chip-label">${chip.label}</span>` +
      `<span class="status-chip-value"><span class="status-chip-dot"></span>` +
      `<span class="status-chip-text waiting"></span></span>`;
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
               once, so every screen is shown beneath the same one. The line
               under the state line says what a press DOES, which the state
               line cannot, because the state line says what the droid is doing
               (the reference's fixed-title discipline,
               src/js/maestro/hw-ui.js:227, as visible text rather than a title
               because a title carries no affordance on a bench tablet,
               docs/ui-copy-voice.md rule 12 / ADR 0059). The button is one
               toggle (ADR 0048, 2026-09-29 amendment), so that line has two
               values - one per thing a press can do - and renderEstopState
               below picks it from the same reading that decides the press, so
               it cannot say "stop" over a press that clears.

               The two lines sit beside the button rather than inside it: the
               topbar has the width for them, and a button whose face is three
               lines of text stops reading as one press. -->
          <div class="shell-estop">
            <!-- The accessible name opens with the word on the face of the
                 button, so someone driving the page by voice can say what
                 they can see (WCAG 2.5.3 Label in Name) - the one control
                 where being unable to say "press STOP" would matter most. -->
            <button id="shell-estop-button" class="btn danger shell-estop-button" type="button"
                    aria-pressed="false" aria-label="${ESTOP_PRESS.stop.label}">
              ${icon("stop-circle-outline")}<span class="shell-estop-action">STOP</span>
            </button>
            <div class="shell-estop-lines">
              <div class="shell-estop-state" id="shell-estop-state" role="status" aria-live="polite">Estop: <span class="waiting" id="shell-estop-value">${ESTOP_STATE_TEXT.waiting}</span></div>
              <div class="shell-estop-consequence" id="shell-estop-consequence">${ESTOP_PRESS.stop.line}</div>
              <div class="shell-estop-feedback feedback compact-feedback" id="shell-estop-feedback" role="status" aria-live="polite" aria-atomic="true"></div>
            </div>
          </div>
        </div>
      </div>
    `;
  }

  // ---------------------------------------------------------------------------
  // The act's tooltip: one for the whole document (#460)
  //
  // An icon-only act is named on hover and on keyboard focus by this one
  // element, which reads the act's own .act-label each time it is shown: there
  // is no second copy of the words to drift, and never a native title. It is
  // fixed to the viewport so no card's overflow clips it, and it is hidden from
  // assistive technology, which already has the words as the button's name.
  //
  // It names the act and nothing more, and only an act whose words are hidden.
  // An act the page would otherwise explain only on hover keeps its words in
  // view (act-keeps-words, above), and why an act is switched off is visible
  // text where a surface says it (ADR 0059): the tooltip is a name, never the
  // one place an explanation lives.
  // ---------------------------------------------------------------------------
  const actTip = document.createElement("div");
  actTip.className = "act-tip";
  actTip.hidden = true;
  actTip.setAttribute("aria-hidden", "true");
  document.body.appendChild(actTip);
  let tipFor = null;

  const hideActTip = () => {
    tipFor = null;
    actTip.hidden = true;
  };

  const showActTip = (button) => {
    if (document.documentElement.classList.contains("act-words")) return;
    if (button.classList.contains("act-keeps-words")) return;
    const words = button.querySelector(".act-label")?.textContent.trim();
    if (!words) return;
    tipFor = button;
    actTip.textContent = words;
    actTip.hidden = false;
    // Above the act, centred on it; below it when the top of the window is in
    // the way, and never past either side.
    const gap = 6;
    const at = button.getBoundingClientRect();
    const tip = actTip.getBoundingClientRect();
    const top = at.top - tip.height - gap >= gap ? at.top - tip.height - gap : at.bottom + gap;
    const left = Math.min(
      Math.max(gap, at.left + (at.width - tip.width) / 2),
      document.documentElement.clientWidth - tip.width - gap
    );
    actTip.style.top = `${Math.round(top)}px`;
    actTip.style.left = `${Math.round(left)}px`;
  };

  const actOf = (node) => (node instanceof Element ? node.closest(".icon-act") : null);

  document.addEventListener("pointerover", (event) => {
    // An act repainted under the pointer is a new node: the old one's tooltip
    // goes with it rather than naming a button that is no longer there.
    if (tipFor && !tipFor.isConnected) hideActTip();
    const button = actOf(event.target);
    if (button && button !== tipFor) showActTip(button);
    else if (!button && tipFor && !tipFor.contains(event.target)) hideActTip();
  });
  document.addEventListener("pointerout", (event) => {
    const button = actOf(event.target);
    if (button && button === tipFor && !button.contains(event.relatedTarget)) hideActTip();
  });
  // Keyboard focus only: a click focuses a button too, and a pointer already
  // had its tooltip on the way in.
  document.addEventListener("focusin", (event) => {
    const button = actOf(event.target);
    if (button && button.matches(":focus-visible")) showActTip(button);
  });
  document.addEventListener("focusout", (event) => {
    if (tipFor && actOf(event.target) === tipFor) hideActTip();
  });
  // A press may change the act's words or take the act away, and a scroll
  // moves it out from under a tooltip that is fixed to the window.
  document.addEventListener("pointerdown", hideActTip, true);
  window.addEventListener("scroll", hideActTip, { capture: true, passive: true });
  // Dismissable without moving the pointer or the focus (WCAG 1.4.13).
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && tipFor) hideActTip();
  });

  // The nav is its own region so it can be the rail beside the work area rather
  // than a strip above it (ADR 0066). It is still written once and never again:
  // a navigation flips an attribute on a link that is already there.
  if (shellNav) {
    shellNav.setAttribute("aria-label", "Operator navigation");
    // The foot of the rail is where the shell says what is running: the
    // firmware and web bundle versions, written into #fw-meta below.
    shellNav.innerHTML = `
      ${navHtml}
      <div class="rail-foot status-bar" id="conn-status">
        <div class="status-subline" id="fw-meta"><span class="waiting"></span></div>
      </div>
    `;
  }

  // The plate is chrome by the same rule as the estop above: written once,
  // then repainted in place. It rides on the status the session already holds
  // and asks the droid for nothing of its own -- the one /api/status read and
  // the one stream are the Live Reading's, and a second reader would spend one
  // of the controller's three client slots to say what the first already knows.
  const shellStatus = document.getElementById("shell-status");
  if (shellStatus) {
    // The notice sits ABOVE the plate rather than in it: the eight positions are
    // fixed and read by muscle memory, and a notice that pushed one aside would
    // move the cell an operator was reaching for.
    shellStatus.innerHTML = `
      <div class="status-plate-region" id="status-plate-region" data-freshness="waiting">
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
          <p class="status-plate-freshness" id="status-plate-freshness" role="status" aria-live="polite">Waiting for the droid.</p>
        </div>
      </div>
    `;
  }

  // ---------------------------------------------------------------------------
  // The Status Plate's ONE freshness state
  //
  // Not one per chip: everything on the plate arrives on one stream, so its age
  // is one fact and saying it eight times repeats that fact seven times (#324).
  //
  // Why the plate is not showing a verified reading, and how old the one it
  // shows is, are the Live Reading's to say (reading.notHearing,
  // reading.receivedAt): there are three ways to stop hearing the droid -- the
  // stream dropping, a refused fallback poll, a failed resync read -- and one
  // way for what arrives not to be a reading, and all of them are decided
  // there, once, for every reader. A browser with no EventSource never reaches
  // a `stream_error` at all, which is how a fallback poll was once refused all
  // afternoon under a plate reading "live" (#346).
  //
  // The age is read from the browser's own clock, not from the droid's
  // uptimeMs, and that is the point: the droid's clock is the thing that stops
  // advancing exactly when this readout starts to matter. It is the other half
  // of the reference's "wall clock, not simulated time" rule
  // (r2d2-astromech-simulator v1.79.0, src/js/input/pad-ui.js:165). A frame
  // handed out again on a reconnect keeps the age it was measured at, at
  // precisely the moment #324 says an operator meets a stale plate most often.
  // ---------------------------------------------------------------------------
  const plateRegion = document.getElementById("status-plate-region");
  const plateFreshness = document.getElementById("status-plate-freshness");

  const plateAgeText = (elapsedMs) => {
    if (elapsedMs < 1500) return "just now";
    const seconds = Math.round(elapsedMs / 1000);
    if (seconds < 60) return `${seconds}s ago`;
    const minutes = Math.round(seconds / 60);
    if (minutes < 60) return `${minutes}min ago`;
    return "over an hour ago";
  };

  const renderPlateFreshness = (reading) => {
    if (!plateRegion || !plateFreshness) return;
    if (reading.status === null) {
      plateRegion.dataset.freshness = "waiting";
      plateFreshness.hidden = false;
      plateFreshness.textContent = "Waiting for the droid.";
      return;
    }
    // Never amber, and the values are never blanked: the operator cannot act
    // on a reconnect that is already running, and a blank plate would be the
    // presentation they meet most often (#324, #327).
    plateRegion.dataset.freshness = reading.notHearing === null ? "live" : "waiting";
    // Said only when the droid is not being heard (#472). The board sends a
    // status frame only when something changes (s_broadcastRequested,
    // src/web/web_server.cpp), so on a quiet healthy droid the age grows and
    // reads like a fault. Hiding it masks nothing: a lost link is raised from
    // the stream's error (stream_error -> loseContact(), data/live_reading.js),
    // never from this age. The compartment stays, so the cells do not move
    // when the line comes back.
    plateFreshness.hidden = reading.notHearing === null;
    if (reading.notHearing === null) {
      plateFreshness.textContent = "";
      return;
    }
    const heard = `Last heard from the droid ${plateAgeText(Date.now() - reading.receivedAt)}.`;
    if (reading.notHearing === "frame") {
      plateFreshness.textContent = `${heard} The droid could not report its status - these are the values it last sent.`;
      return;
    }
    plateFreshness.textContent = `${heard} Reconnecting - these are the values it last sent.`;
  };

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
  // One toggle, and the only estop control on any surface (ADR 0048,
  // 2026-09-29 amendment, the operator's overrule): a press on a droid heard
  // latched releases it, and every other press stops it. The release used to
  // live on Drive and Dashboard only; the operator tried that split on the
  // bench and chose the toggle, accepting a release one press from every
  // screen. The firmware's latch is untouched -- only where the browser offers
  // the clear moved.
  // ---------------------------------------------------------------------------

  const estopButton = document.getElementById("shell-estop-button");
  const estopStateLine = document.getElementById("shell-estop-state");
  const estopStateValue = document.getElementById("shell-estop-value");
  const estopConsequence = document.getElementById("shell-estop-consequence");
  const estopFeedback = document.getElementById("shell-estop-feedback");

  // Three answers, three texts: see ESTOP_STATE_TEXT above for why there is no
  // blank one. The answer is the Live Reading's, so a frame that never
  // mentioned the estop cannot print "Estop: clear" (#346), and a lost link
  // says it is waiting rather than repeating what the droid said before it
  // went quiet.
  //
  // The button's highlight is set HERE and nowhere else: it follows the latch
  // as the droid reports it, whoever set it -- this button, the Controller
  // Console, RC, a fault -- and never the click. A highlight painted on the
  // press would claim a latch the droid had not confirmed (ADR 0048
  // amendment).
  const renderEstopState = (reading) => {
    if (!estopStateLine || !estopStateValue) return;
    estopStateValue.textContent = ESTOP_STATE_TEXT[reading.estop];
    // Red is "something is stopped or refused" and nothing else colors for
    // state (#327). A set latch is exactly that, so the state line AND the
    // button light while it holds; the button's red outline at rest is the
    // control's identity, not a readout.
    estopStateLine.classList.toggle("is-latched", reading.estopLatched);
    if (!estopButton) return;
    const press = reading.estopLatched ? ESTOP_PRESS.clear : ESTOP_PRESS.stop;
    estopButton.classList.toggle("is-latched", reading.estopLatched);
    estopButton.setAttribute("aria-pressed", reading.estopLatched ? "true" : "false");
    estopButton.setAttribute("aria-label", press.label);
    if (estopConsequence) estopConsequence.textContent = press.line;
  };

  const showEstopFeedback = (message, level = "") => {
    if (!estopFeedback) return;
    estopFeedback.textContent = message;
    estopFeedback.className = level
      ? `shell-estop-feedback feedback compact-feedback ${level}`
      : "shell-estop-feedback feedback compact-feedback";
  };

  // Started here, before anything reads it: the stream or the one fallback
  // poll, for every surface this shell will mount (data/live_reading.js).
  LIVE.start();

  // ---------------------------------------------------------------------------
  // The rail's foot line: the firmware and web bundle versions, from the Live
  // Reading like everything else here. It asks the droid for nothing of its
  // own (#419).
  // ---------------------------------------------------------------------------
  const fwMeta = document.getElementById("fw-meta");
  if (fwMeta) {
    // The web bundle's own version, read from the file that ships with it, for
    // a firmware that does not report one in its status. null until it is read.
    let bundleVersion = null;
    let footReading = LIVE.current();

    // A version the frame carries, or the Live Reading's word for one it does
    // not: Waiting before the droid has sent a frame, Unknown after. Each
    // version is a slot, so Waiting shows there as the moving dots.
    const versionOf = (field) => {
      const value = footReading.status?.[field];
      if (value) return String(value);
      return LIVE.slotText(footReading.word(field) || LIVE.UNKNOWN);
    };

    const renderFootLine = () => {
      const fw = versionOf("firmwareVersion");
      const web = footReading.status?.fsVersion ? String(footReading.status.fsVersion) : bundleVersion || versionOf("fsVersion");
      fwMeta.innerHTML =
        `FW: <span class="mono waiting">${window.PAUtils.escapeHtml(fw)}</span><br>` +
        `FS: <span class="mono waiting">${window.PAUtils.escapeHtml(web)}</span>`;
    };

    const loadBundleVersion = async () => {
      if (!window.PAApi) return;
      try {
        const result = await window.PAApi.get("/fs-version.json", { timeoutMs: 2500, cache: "no-store" });
        if (result.data && typeof result.data === "object" && result.data.fsVersion) {
          bundleVersion = String(result.data.fsVersion);
          renderFootLine();
        }
      } catch (error) {
        // The status frame's own fsVersion still answers; this file is only the
        // fallback for a firmware that sends none.
        console.warn("[shell] /fs-version.json unavailable:", error?.message || error);
      }
    };

    // The subscription tells the reading it already holds at once, so this is
    // also the first paint.
    LIVE.subscribe((next) => {
      footReading = next;
      renderFootLine();
    });
    loadBundleVersion();
  }

  // The device pushes a status event when something calls
  // requestStatusBroadcastNow() and at no other time -- a state change, or a
  // client being admitted to the stream (src/web/api_events.cpp). Naming the
  // mechanism rather than saying "on a change" matters: the old wording is
  // what would send the next reader looking for a poll that does not exist,
  // instead of for the call site that does (#346).
  //
  // So the shell reads once at boot, as a section the bootstrap can retry, and
  // the chrome has a frame even before the stream is up; every later change
  // arrives on the stream.
  const loadInitialStatus = async ({ handle = null } = {}) => {
    if (LIVE.current().status !== null) return;
    await LIVE.read({ handle });
  };

  // What is in flight: stops (a count, because a stop is never refused and
  // two can overlap) and a release.
  let stopsInFlight = 0;
  let clearPending = false;

  // A stop is deliberately unguarded against a second press while the first
  // is in flight. POST /api/estop is idempotent in the firmware --
  // failsafeTrigger() sets a bit it may already hold (src/failsafe_gate.cpp)
  // -- and estopPostForm bypasses the request slot and never retries, so a
  // second press cannot queue behind the first. A guard that swallowed it
  // would swallow exactly the press an operator makes because the first
  // looked like it did nothing.
  //
  // One function, two entrances: the STOP button in the topbar and the plate's
  // ESTOP chip at the foot of the page both reach it through pressEstop. Two
  // copies of a stop could drift, and the one control where that matters most
  // is this one.
  const requestStop = async () => {
    if (!window.PAApi) return;
    stopsInFlight += 1;
    try {
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
        await LIVE.read();
      } catch (error) {
        // The stop already succeeded; only the confirmation read failed. The
        // firmware broadcasts the new status itself, so the state line
        // catches up on the stream a moment later.
        console.warn("[shell] status read after stop failed:", error);
      }
    } finally {
      stopsInFlight -= 1;
    }
  };

  // The release keeps the priority lane the stop has: it skips the request
  // slot and is never retried, because an operator command about drive safety
  // must not wait behind page work and must not be replayed (GLOSSARY.md,
  // Browser Request Priority).
  const requestClear = async () => {
    if (!window.PAApi) return;
    clearPending = true;
    estopButton?.classList.add("is-pending");
    showEstopFeedback("Releasing the estop...");
    try {
      await window.PAApi.estopPostForm("/api/estop/clear", {}, { timeoutMs: 3000 });
      showEstopFeedback("Estop released", "success");
      try {
        await LIVE.read();
      } catch (error) {
        // Released; only the confirmation read failed, and the stream
        // carries the new status a moment later, as it does after a stop.
        console.warn("[shell] status read after release failed:", error);
      }
    } catch (error) {
      showEstopFeedback(`Release failed: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      clearPending = false;
      estopButton?.classList.remove("is-pending");
    }
  };

  // The press is decided from the Live Reading at the moment of the press,
  // never from a flag this file keeps: only a heard latched estop makes it a
  // release, and clear, Waiting and Unknown all make it a stop, so a droid the
  // page cannot hear is never released (ADR 0048 amendment).
  //
  // Two double presses are closed off:
  // - A press while a release is in flight does nothing, so a double press
  //   cannot send a release and then a stop behind it.
  // - A press while a stop is still in flight is another stop, even if the
  //   stream has already said latched: the second half of a nervous double
  //   press on a moving droid must not be the release of the latch the first
  //   half just set.
  const pressEstop = () => {
    if (clearPending) return undefined;
    if (stopsInFlight === 0 && LIVE.current().estopLatched) return requestClear();
    return requestStop();
  };

  if (estopButton) {
    estopButton.addEventListener("click", pressEstop);
    // Pushed or fetched, a status reaches the control by this one path.
    LIVE.subscribe(renderEstopState);
  }

  if (window.PABootstrap) {
    window.PABootstrap.registerSection("shell-status", loadInitialStatus, {
      label: "droid status",
    });
  } else {
    loadInitialStatus().catch((error) => console.warn("[shell] status unavailable:", error));
  }

  // ---------------------------------------------------------------------------
  // The Status Plate: painting it, and saying how old it is
  //
  // The cells are looked up once. Nothing here queries the document again on a
  // repaint, and nothing rebuilds a cell: a chip is an attribute and a text
  // node on a node that was written at boot, which is the same discipline the
  // nav and the estop above already keep.
  // ---------------------------------------------------------------------------
  const plateCells = new Map();
  PLATE_CHIPS.forEach((chip) => {
    const node = document.getElementById(`chip-${chip.id}`);
    if (!node) return;
    plateCells.set(chip.id, { node, value: node.querySelector(".status-chip-text") });
  });

  // One writer for every cell, so no chip can grow a rendering path of its own
  // (the reference's chip(), r2d2-astromech-simulator v1.79.0,
  // src/js/app/hud.js:188). The class is REWRITTEN rather than toggled, so a
  // state class cannot survive a repaint that no longer wants it.
  const paintPlate = (reading) => {
    PLATE_CHIPS.forEach((chip) => {
      const cell = plateCells.get(chip.id);
      if (!cell) return;
      const painted = reading.status ? chip.read(reading.status, reading) : chipState("", CHIP_WAITING);
      cell.node.className = painted.state ? `status-chip status-chip-${painted.state}` : "status-chip";
      if (cell.value) cell.value.textContent = painted.value === CHIP_WAITING ? "" : painted.value;
    });
  };

  // The plate's ESTOP cell says which way its next press goes, from the same
  // reading pressEstop decides on. Its value and light are the plate's paint;
  // this writes only the press's words.
  const renderEstopCell = (reading) => {
    const cell = plateCells.get("estop")?.node;
    if (!cell) return;
    const press = reading.estopLatched ? ESTOP_PRESS.clear : ESTOP_PRESS.stop;
    cell.title = press.line;
    if (press.cellLabel) cell.setAttribute("aria-label", press.cellLabel);
    else cell.removeAttribute("aria-label");
  };

  // A frame that is not a reading changes nothing here but the freshness line:
  // the Live Reading keeps the values that WERE a reading, and keeping them
  // beside "the droid could not report its status" is the whole of "values,
  // not exceptions" under a failure (#324).
  const notePlateReading = (reading) => {
    paintPlate(reading);
    renderPlateFreshness(reading);
    releaseSettledCauses(reading);
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
  // How long one notice stays up. It is a Note, not an alarm, and it comes
  // down sooner than this the moment its cause clears.
  const NOTICE_VISIBLE_MS = 6000;

  // One row per way the droid is holding still, ordered by what bites fastest,
  // and each row routes where its Status Plate chip routes -- so the notice
  // says WHAT and the chip says WHERE, to one destination rather than two.
  //
  // Two rows differ, and say why. The estop's row routes nowhere: the release
  // is the STOP toggle in the chrome, on the screen the operator is already
  // on (ADR 0048, 2026-09-29 amendment), so it names that press instead of a
  // destination. Stationary Mode earns no chip under #324's admission rule
  // but is still a real refusal -- POST /api/drive rejects on it
  // (src/web/api_drive.cpp) -- so it carries a `page` of its own, the surface
  // its Commanded Mode buttons live on.
  const IGNORED_INPUT_CAUSES = [
    {
      id: "estop",
      chip: "estop",
      says: "The estop is latched",
      act: "Press STOP to release it",
      // The Live Reading's answer, so a lost link does not name a latch
      // nobody has heard since.
      active: (_status, reading) => reading.estopLatched,
    },
    {
      id: "feet",
      chip: "drive",
      says: "The feet are not armed",
      // The same set the DRIVE chip reads, minus the estop, which has its own
      // row above and names the more specific reason. Read from the shared
      // predicate rather than copied out again, so a sixth way to hold the
      // feet cannot land in the chip and miss this row.
      active: (status) => !hasKey(status, "drive") || feetHeldBesidesEstop(status),
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
      says: "Web control is off on this droid",
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
    // A cause changed right here names the press, and the route goes: a link
    // to another surface would send the operator away from the control.
    noticeText.textContent = cause.act
      ? `That control is switched off right now. ${cause.says}. ${cause.act}.`
      : `That control is switched off right now. ${cause.says}.`;
    noticeRoute.classList.toggle("hidden", Boolean(cause.act));
    noticeRoute.textContent = `Open ${destination ? destination.name : page}, where that is changed`;
    noticeRoute.setAttribute("href", `#${page}`);
    noticeNode.classList.remove("hidden");
    noticeCause = cause.id;
    if (noticeHideTimer !== null) window.clearTimeout(noticeHideTimer);
    noticeHideTimer = window.setTimeout(hideNotice, NOTICE_VISIBLE_MS);
  };

  const reportIgnoredInput = (refused) => {
    // A control that is off for a reason of its own says so itself, and needs
    // no reading to say it: a Sequence on the Dashboard that is no longer on
    // the droid (#451). `data-ignored-says` is the sentence, and
    // `data-ignored-page` the surface where that is changed. Its cause is
    // never released by releaseSettledCauses() below, which knows only the
    // plate's: the notice comes down on its timer, and the burst window
    // expires on its own.
    const own = refused?.dataset?.ignoredSays;
    if (own) {
      const cause = { id: `own:${own}`, says: own, page: refused.dataset.ignoredPage || DEFAULT_PAGE };
      const shownAt = noticeShownAt.get(cause.id);
      if (shownAt !== undefined && Date.now() - shownAt < NOTICE_BURST_MS) return;
      noticeShownAt.set(cause.id, Date.now());
      showNotice(cause);
      return;
    }
    // Nothing has arrived yet, so there is nothing to name. The plate's own
    // freshness state is already saying so.
    const reading = LIVE.current();
    if (reading.status === null) return;
    const cause = IGNORED_INPUT_CAUSES.find((candidate) => candidate.active(reading.status, reading));
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
  const releaseSettledCauses = (reading) => {
    if (reading.status === null) return;
    IGNORED_INPUT_CAUSES.forEach((cause) => {
      if (cause.active(reading.status, reading)) return;
      noticeShownAt.delete(cause.id);
      if (noticeCause === cause.id) hideNotice();
    });
  };

  // Which refused control a press landed on -- and it takes two tries, because
  // a refused button is usually not merely inert to clicks, it is invisible to
  // hit testing: data/style.css puts `pointer-events: none` on `.btn:disabled`
  // and `.btn[aria-disabled="true"]`, so the press lands on whatever is behind
  // the control and event.target names the container instead of the button.
  // A disabled act (#460, "The act" in data/style.css) is the exception: it
  // takes the pointer for its tooltip and its children do not, so the press
  // names the act itself and the first branch below finds it.
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
        reportIgnoredInput(refused);
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
  // One site, and it comes after both are fully declared: the Live Reading
  // hands a new subscriber the current reading straight away, so a
  // subscription opened above this line could reach the notice's own reader
  // before it exists.
  // ---------------------------------------------------------------------------
  if (plateRegion) {
    LIVE.subscribe(notePlateReading);

    // A display tick, not a poll: it asks the droid for nothing and rewrites
    // one line of text. Skipped while the tab is hidden, where there is nobody
    // to read it and no stream open either (Hidden Tab Pause).
    const PLATE_TICK_MS = 1000;
    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      renderPlateFreshness(LIVE.current());
    }, PLATE_TICK_MS);

    // The one cell that acts instead of routing, wired to the same toggle
    // the topbar's STOP button calls: a press on a droid heard latched
    // releases it, any other press stops it (#359, 2026-10-04). The other
    // seven are anchors carrying a hash address and need no handler at all.
    plateCells.get("estop")?.node.addEventListener("click", pressEstop);
    LIVE.subscribe(renderEstopCell);
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

  // Where a Receipt is shown (GLOSSARY.md "Receipt", #456): one host for every
  // surface, written by data/overlay.js. The shell owns it, beside the mounted
  // surface rather than in it, because detach() removes only the surface's
  // own node, so a receipt outlives a surface change. Inside #shell-content,
  // so it is ranked in the work area's stacking context and can never cover
  // the chrome or the estop on it (ADR 0048).
  const receiptHost = document.createElement("div");
  receiptHost.id = "shell-receipts";
  receiptHost.className = "receipts";
  receiptHost.setAttribute("role", "status");
  receiptHost.setAttribute("aria-live", "polite");
  shellContent.appendChild(receiptHost);

  // Mounting is the bootstrap's Resource Step Recovery doing the work, so
  // without it there is no router -- the same documented degraded path the
  // identity load above already takes.
  if (!window.PABootstrap?.mountResources) return;

  // A surface whose polling stopped while the operator was elsewhere comes
  // back showing what it last read. One node, shown above whichever surface
  // that is, until that surface has answered again -- so a glance cannot take
  // those values for live ones. Uncolored on purpose: this is a Note, and
  // color is reserved for refusals and for what the builder can act on
  // (#327, docs/ui-copy-voice.md rule 11).
  const resumedNote = document.createElement("div");
  resumedNote.className = "surface-resumed";
  resumedNote.setAttribute("role", "status");
  resumedNote.textContent =
    "Last reading from before you left. Asking the droid again now.";

  // Ids are not unique across surfaces -- Firmware and Maintenance both carry
  // #reboot-button, Dashboard and Maintenance both carry #reboot-feedback -- so
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

  // ---------------------------------------------------------------------------
  // The full-screen posture (#330, #451)
  //
  // A surface asks for it with a pa:posture event, and the shell clears its own
  // chrome around that surface: the nav rail, the Status Plate and the
  // topbar's name, place and acts (data/style.css, "The full-screen posture").
  // The Latching Estop is never cleared: hiding the nav never hides the estop
  // (#325, ADR 0048), so the posture keeps the topbar's estop on screen and
  // pressable, and the Ignored Input Notice above the plate too.
  //
  // It is a look, not a state - the reference's frozen kiosk
  // (r2d2-astromech-simulator v1.79.0, src/js/app/kiosk.js:30-35): entering
  // sends nothing to the droid and changes no Commanded Mode, so leaving has
  // nothing to put back. Nothing is stored and the address does not change, so
  // a reload lands on the ordinary surface (operator, 2026-09-30). The shell
  // ends it when the surface that asked is left, and on Escape.
  // ---------------------------------------------------------------------------
  let postureOwner = null;

  const setPosture = (owner) => {
    postureOwner = owner;
    document.body.classList.toggle("shell-performing", owner !== null);
    window.dispatchEvent(new CustomEvent("pa:posture-changed", { detail: { on: owner !== null } }));
  };

  window.addEventListener("pa:posture", (event) =>
    setPosture(event.detail?.on && currentSurface ? currentSurface.page : null));
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && postureOwner !== null) setPosture(null);
  });

  const mount = (surface) => {
    if (currentSurface === surface) return;

    // A surface may hold its own unmount open while it asks the operator
    // something -- Sequences does, over an unsaved edit (#441). The address
    // already names where the operator was going, so releasing the hold and
    // re-reading it is the whole resume path (see pa:surface-release below);
    // staying puts the address back (pa:surface-stay).
    if (currentSurface && window.PASurface?.unmountHeld(currentSurface.page)) return;

    // The posture belongs to the surface that asked for it, and goes with it.
    if (postureOwner !== null) setPosture(null);

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

  // A surface that was holding its unmount is staying. The address was moved
  // to where the operator was going before the surface was asked, so it names
  // a surface that is not the one on screen: put it back. In place, as
  // applyRoute() writes it, and not as a navigation: a new history entry
  // would leave the refused address one Back away, and Back would ask again.
  window.addEventListener("pa:surface-stay", () => {
    if (currentSurface) setAddress(currentSurface.page);
  });

  // A click on a link to a surface's own document is a route change, not a page
  // load. Capture phase, so a surface's own delegated handler cannot swallow it
  // first; the click still reaches that handler, it just does not reach the
  // browser's navigation. This is what lets every surface's .html address, a
  // renamed surface's old one, and every caller that writes one
  // (window.PAUi.setupActionHtml, servo.js, the disabled-reason lines in six
  // pages), keep working unchanged.
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
