/**
 * data/dome_panel_model.js
 *
 * FALLBACK GEOMETRY ONLY — Vendored MK4 dome panel SVG map.
 *
 * This module contains only the static SVG rendering of the AstroPixelsPlus fork's
 * MK4 dome layout (viewBox 0 0 480 480). It serves as the offline fallback when
 * the dome's `/api/dome/layout` is unavailable.
 *
 * Command mapping (forward/reverse) has been split into data/dome_command_map.js,
 * per ADR 0009. See that module for resolving canonical element IDs + capabilities
 * to Panel Intent command strings and vice versa.
 *
 * SCOPE / CONTRACT (v1.0.0): this SVG is a pragmatic static copy, NOT the long-term
 * source of truth. The dome is the real source of truth for panel geometry, identity,
 * and wired/active state. Drift checking: run `tools/check_dome_panel_drift.py`
 * to compare against the live dome's served panels.html. Drift checking is a
 * developer/operator aid, not a build gate.
 *
 * The primary path is the runtime `GET /api/dome/layout` fetch (data/dome_layout.js
 * + data/dome_layout_render.js, per ADR 0009); this module is only the fallback
 * used when that fetch is unavailable or the dome reports an unsupported schema.
 */

// Full AstroPixelsPlus dome SVG — ported verbatim from AstroPixelsPlus/data/panels.html lines 63-202
// Selectable elements (ring, pie, callouts) already carry data-target in this
// markup (see below); nothing adds it at runtime.
window.DOME_PANEL_MAP_SVG = `<svg viewBox="0 0 480 480" xmlns="http://www.w3.org/2000/svg" class="dome-svg-picker" style="width:100%;max-width:100%;display:block;margin:0 auto">
<style>
.dbg{fill:#b8bec8;stroke:#6b7280;stroke-width:1.5}
.pbg{fill:#c4c9d4}
.pr{fill:#1e3a8a;stroke:#3b82f6;stroke-width:1;cursor:pointer;transition:fill .15s;pointer-events:auto}
.pr:hover,.pr.open{fill:#ea580c}
.pr.selected{fill:#ea580c;stroke:#fff;stroke-width:2;filter:drop-shadow(0 0 3px rgba(234, 88, 12, 0.9))}
.pp{fill:#1e3a6e;stroke:#60a5fa;stroke-width:1;cursor:pointer;transition:fill .15s;pointer-events:auto}
.pp:hover,.pp.open{fill:#ea580c}
.pp.selected{fill:#ea580c;stroke:#fff;stroke-width:2;filter:drop-shadow(0 0 3px rgba(234, 88, 12, 0.9))}
.pe{fill:#c4c9d4;stroke:#9ca3af;stroke-width:.5}
.pu{fill:#cdd3dd;stroke:#6b7280;stroke-width:1;cursor:pointer;transition:fill .15s;pointer-events:auto}
.pu:hover,.pu.open{fill:#9aa3b2}
.pu.selected{fill:#7b8494;stroke:#fff;stroke-width:2;filter:drop-shadow(0 0 3px rgba(123, 132, 148, 0.9))}
.pf{fill:#aeb6c2;stroke:#4b5563;stroke-width:1;pointer-events:none}
.rl{stroke:#6b7280;stroke-width:.8;fill:none;pointer-events:none}
.cr{fill:#0b1220;stroke:#3b82f6;stroke-width:2;cursor:pointer;pointer-events:auto}
.cr:hover{fill:#0f1e40}
.cr.selected{fill:#0f1e40;stroke:#fff;stroke-width:2.5;filter:drop-shadow(0 0 3px rgba(59, 130, 246, 0.9))}
.cf{fill:#7e8aa0;stroke:#3a4250;stroke-width:1.2;pointer-events:none}
.conn-r{stroke:#3b82f6;stroke-width:.8;opacity:.6;pointer-events:none}
.conn-f{stroke:#4b5563;stroke-width:.7;opacity:.5;pointer-events:none}
.lt{font:bold 11px monospace;fill:#fff;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lp{font:bold 12px monospace;fill:#eaf2ff;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lpf{font:bold 12px monospace;fill:#0b1220;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lf{font:bold 10px sans-serif;fill:#0b1220;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lf2{font:bold 9px sans-serif;fill:#0b1220;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.hp{fill:#16a34a;stroke:#fff;stroke-width:.6;pointer-events:none}
.lh{font:bold 8px monospace;fill:#fff;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
</style>
<circle class="dbg" cx="240" cy="240" r="172"/>
<circle class="pbg" cx="240" cy="240" r="119"/>
<!-- Pie wedges -->
<path class="pp" id="pp4" data-target="P4" d="M 184.6,135.8 A 118,118 0 0 1 293.6,134.9 L 264.5,191.9 A 54,54 0 0 0 214.6,192.3 Z" role="button" tabindex="0"><title>PP4</title></path>
<path class="pu" id="pp3" data-target="P3" d="M 302.5,139.9 A 118,118 0 0 1 357.8,233.8 L 293.9,237.2 A 54,54 0 0 0 268.6,194.2 Z" role="button" tabindex="0"><title>PP3 (unserviced)</title></path>
<path class="pp" id="pp2" data-target="P2" d="M 357.9,244.1 A 118,118 0 0 1 304.3,339.0 L 269.4,285.3 A 54,54 0 0 0 294.0,241.9 Z" role="button" tabindex="0"><title>PP2</title></path>
<path class="pp" id="pp1" data-target="P1" d="M 295.4,344.2 A 118,118 0 0 1 186.4,345.1 L 215.5,288.1 A 54,54 0 0 0 265.4,287.7 Z" role="button" tabindex="0"><title>PP1</title></path>
<path class="pp" id="pp6" data-target="P6" d="M 177.5,340.1 A 118,118 0 0 1 122.2,246.2 L 186.1,242.8 A 54,54 0 0 0 211.4,285.8 Z" role="button" tabindex="0"><title>PP6</title></path>
<path class="pu" id="pp5" data-target="P5" d="M 122.1,235.9 A 118,118 0 0 1 175.7,141.0 L 210.6,194.7 A 54,54 0 0 0 186.0,238.1 Z" role="button" tabindex="0"><title>PP5 (unserviced)</title></path>
<!-- Ring panels -->
<ellipse class="hp" cx="240.0" cy="81.0" rx="14.1" ry="10.9" transform="rotate(0.0 240.0 81.0)"><title>HP2 — Rear Holo Projector</title></ellipse>
<path class="pf" id="r_p8" d="M 272.8,71.2 A 172,172 0 0 1 318.1,86.7 L 306.3,109.9 A 146,146 0 0 0 267.9,96.7 Z"><title>P8 — fixed (RPSI)</title></path>
<path class="pr" id="p7" data-target="07" d="M 331.1,94.1 A 172,172 0 0 1 391.9,159.3 L 368.9,171.5 A 146,146 0 0 0 317.4,116.2 Z" role="button" tabindex="0"><title>P7</title></path>
<path class="pf" id="r_merge" d="M 398.3,172.8 A 172,172 0 0 1 409.4,210.1 L 383.8,214.6 A 146,146 0 0 0 374.4,183.0 Z"><title>P6/MP/P5 fixed</title></path>
<path class="pr" id="p4" data-target="04" d="M 411.3,225.0 A 172,172 0 0 1 395.9,312.7 L 372.3,301.7 A 146,146 0 0 0 385.4,227.3 Z" role="button" tabindex="0"><title>P4</title></path>
<path class="pr" id="p3" data-target="03" d="M 389.0,326.0 A 172,172 0 0 1 365.8,357.3 L 346.8,339.6 A 146,146 0 0 0 366.4,313.0 Z" role="button" tabindex="0"><title>P3</title></path>
<path class="pr" id="p2" data-target="02" d="M 355.1,367.8 A 172,172 0 0 1 323.4,390.4 L 310.8,367.7 A 146,146 0 0 0 337.7,348.5 Z" role="button" tabindex="0"><title>P2</title></path>
<path class="pr" id="p1" data-target="01" d="M 310.0,397.1 A 172,172 0 0 1 272.8,408.8 L 267.9,383.3 A 146,146 0 0 0 299.4,373.4 Z" role="button" tabindex="0"><title>P1</title></path>
<ellipse class="hp" cx="237.2" cy="399.0" rx="16.5" ry="10.9" transform="rotate(181.0 237.2 399.0)"><title>HP1 — Front Holo Projector</title></ellipse>
<path class="pf" id="r_p14" d="M 198.4,406.9 A 172,172 0 0 1 154.0,389.0 L 167.0,366.4 A 146,146 0 0 0 204.7,381.7 Z"><title>P14 — fixed (FPSI)</title></path>
<path class="pr" id="p13" data-target="13" d="M 138.9,379.2 A 172,172 0 0 1 124.9,367.8 L 142.3,348.5 A 146,146 0 0 0 154.2,358.1 Z" role="button" tabindex="0"><title>P13</title></path>
<path class="pf" id="r_p12" d="M 112.2,355.1 A 172,172 0 0 1 88.1,320.7 L 111.1,308.5 A 146,146 0 0 0 131.5,337.7 Z"><title>P12 — fixed (FLDs)</title></path>
<path class="pr" id="p11" data-target="11" d="M 80.5,304.4 A 172,172 0 0 1 74.7,287.4 L 99.7,280.2 A 146,146 0 0 0 104.6,294.7 Z" role="button" tabindex="0"><title>P11</title></path>
<path class="pf" id="r_p10" d="M 70.6,269.9 A 172,172 0 0 1 88.1,159.3 L 111.1,171.5 A 146,146 0 0 0 96.2,265.4 Z"><title>P10 — wide fixed</title></path>
<path class="pf" id="r_p9" d="M 97.4,143.8 A 172,172 0 0 1 192.6,74.7 L 199.8,99.7 A 146,146 0 0 0 119.0,158.4 Z"><title>P9 — wide fixed (RLD)</title></path>
<circle class="pf" cx="240" cy="240" r="22"/>
<circle class="rl" cx="240" cy="240" r="172"/>
<circle class="rl" cx="240" cy="240" r="146" stroke-dasharray="3,2"/>
<circle class="rl" cx="240" cy="240" r="54"/>
<!-- Callouts -->
<line x1="309.3" y1="38.6" x2="301.2" y2="62.2" class="conn-f"/>
<circle class="cf" cx="309.3" cy="38.6" r="14"><title>P8 fixed</title></circle>
<text class="lf2" x="309.3" y="34.6">P8</text>
<text class="lf2" x="309.3" y="43.6">RPSI</text>
<line x1="395.8" y1="94.7" x2="377.5" y2="111.8" class="conn-r"/>
<circle class="cr" cx="395.8" cy="94.7" r="12" data-target="07" role="button" tabindex="0"><title>P7</title></circle>
<text class="lt" x="395.8" y="94.7">P7</text>
<line x1="444.2" y1="179.5" x2="420.3" y2="186.6" class="conn-f"/>
<circle class="cf" cx="444.2" cy="179.5" r="16"><title>P6/MP/P5</title></circle>
<text class="lf2" x="444.2" y="172.5">P6</text>
<text class="lf2" x="444.2" y="179.5">MP</text>
<text class="lf2" x="444.2" y="186.5">P5</text>
<line x1="449.8" y1="277.0" x2="425.1" y2="272.6" class="conn-r"/>
<circle class="cr" cx="449.8" cy="277.0" r="12" data-target="04" role="button" tabindex="0"><title>P4</title></circle>
<text class="lt" x="449.8" y="277.0">P4</text>
<line x1="411.2" y1="366.7" x2="391.1" y2="351.8" class="conn-r"/>
<circle class="cr" cx="411.2" cy="366.7" r="12" data-target="03" role="button" tabindex="0"><title>P3</title></circle>
<text class="lt" x="411.2" y="366.7">P3</text>
<line x1="363.7" y1="413.4" x2="349.2" y2="393.1" class="conn-r"/>
<circle class="cr" cx="363.7" cy="413.4" r="12" data-target="02" role="button" tabindex="0"><title>P2</title></circle>
<text class="lt" x="363.7" y="413.4">P2</text>
<line x1="304.1" y1="443.1" x2="296.5" y2="419.3" class="conn-r"/>
<circle class="cr" cx="304.1" cy="443.1" r="12" data-target="01" role="button" tabindex="0"><title>P1</title></circle>
<text class="lt" x="304.1" y="443.1">P1</text>
<line x1="160.2" y1="437.5" x2="169.6" y2="414.3" class="conn-f"/>
<circle class="cf" cx="160.2" cy="437.5" r="14"><title>P14 fixed</title></circle>
<text class="lf2" x="160.2" y="433.5">P14</text>
<text class="lf2" x="160.2" y="442.5">FPSI</text>
<line x1="106.0" y1="405.5" x2="121.7" y2="386.1" class="conn-r"/>
<circle class="cr" cx="106.0" cy="405.5" r="12" data-target="13" role="button" tabindex="0"><title>P13</title></circle>
<text class="lt" x="106.0" y="405.5">P13</text>
<line x1="65.5" y1="362.2" x2="86.0" y2="347.8" class="conn-f"/>
<circle class="cf" cx="65.5" cy="362.2" r="14"><title>P12 fixed</title></circle>
<text class="lf2" x="65.5" y="358.2">P12</text>
<text class="lf2" x="65.5" y="367.2">FLDs</text>
<line x1="38.6" y1="309.3" x2="62.2" y2="301.2" class="conn-r"/>
<circle class="cr" cx="38.6" cy="309.3" r="12" data-target="11" role="button" tabindex="0"><title>P11</title></circle>
<text class="lt" x="38.6" y="309.3">P11</text>
<line x1="29.6" y1="206.7" x2="54.3" y2="210.6" class="conn-f"/>
<circle class="cf" cx="29.6" cy="206.7" r="11"><title>P10 fixed</title></circle>
<text class="lf" x="29.6" y="206.7">P10</text>
<line x1="114.8" y1="67.7" x2="129.5" y2="87.9" class="conn-f"/>
<circle class="cf" cx="114.8" cy="67.7" r="14"><title>P9 fixed</title></circle>
<text class="lf2" x="114.8" y="63.7">P9</text>
<text class="lf2" x="114.8" y="72.7">RLD</text>
<!-- Pie labels -->
<text class="lp"  x="240.0" y="154.0">PP4</text>
<text class="lpf" x="296.0" y="216.0">PP3</text>
<text class="lp"  x="314.9" y="282.3">PP2</text>
<text class="lp"  x="240.8" y="326.0">PP1</text>
<text class="lp"  x="165.9" y="283.6">PP6</text>
<text class="lpf" x="165.1" y="197.7">PP5</text>
<!-- Holos -->
<circle class="hp" cx="320.4" cy="172.5" r="6"><title>HP3 — Top Holo on PP3</title></circle>
<text class="lh" x="320.4" y="172.5">HP3</text>
<!-- Legend -->
<g transform="translate(8,460)" style="font:bold 12px sans-serif;fill:#d5dbe6">
  <rect x="0" y="0" width="14" height="11" fill="#1e3a8a" rx="1"/><text x="18" y="9">Ring servo</text>
  <rect x="88" y="0" width="14" height="11" fill="#1e3a6e" rx="1"/><text x="106" y="9">Pie servo</text>
  <rect x="175" y="0" width="14" height="11" fill="#cdd3dd" rx="1"/><text x="193" y="9">Unserviced</text>
  <rect x="295" y="0" width="14" height="11" fill="#aeb6c2" rx="1"/><text x="313" y="9">Fixed</text>
</g>
</svg>`;
