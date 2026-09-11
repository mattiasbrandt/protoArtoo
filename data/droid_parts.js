/**
 * data/droid_parts.js
 *
 * Auto-generated from docs/droid-parts.yaml by tools/generate_droid_parts_catalog.py
 * DO NOT EDIT MANUALLY
 *
 * Source digest: sha256 3ae06f66274e5dde4fb0f2b3a4673aa2e36e982c2519cae4493f3aba9a4e3028
 *
 * Every Part on the droid, with the name to show, the Printed Droid shorthand
 * to show beside it, the aliases to match on import and search, and where to
 * find it. Data only: the app layers its own display over these, and never
 * writes back - a rename on screen can never produce a new id (#301, #356).
 *
 * Fields absent rather than empty. A `TBD` in the catalog is a declared
 * unknown - the dome CAD names and the dome-link panel numbers are unread, not
 * missing - so its key is simply not here. Two nulls that ARE emitted mean
 * something: `cadName: null` says the part has no CAD name, which is what marks
 * a Common Addition, and `control: null` says the row declares no control path
 * at all, which is a dome fixture rather than a part waiting to be wired.
 *
 * A variant whose complement is unknown carries `seeds: null`, never `[]`, so
 * reaching for it throws instead of quietly seeding an empty droid. `own`
 * carries `seeds: []`, which is a real and deliberate empty complement.
 *
 * `kind` is the Part Kind - what a Part usually IS, as opposed to what drives
 * it. Branch on this field, never on an id prefix or a name match. It is
 * advisory: it earns a Part its own treatment and lets a surface query a
 * surprising mapping, and it never refuses one. A Part with no `kind` is one
 * the catalog does not classify.
 *
 * `sitsOn` names the Part this one is carried by - a light and the dome panel
 * it lights. Such a Part takes its host's `position` and `bearingDeg` unless it
 * declares its own, so the two can never disagree about where they both are.
 */

(function () {
  'use strict';

  window.DroidParts = {
    "source": "docs/droid-parts.yaml",
    "generator": "tools/generate_droid_parts_catalog.py",
    "sourceSha256": "3ae06f66274e5dde4fb0f2b3a4673aa2e36e982c2519cae4493f3aba9a4e3028",
    "designs": [
      {
        "id": "mk4",
        "label": "MrBaddeley MK4",
        "short": "MK4",
        "blurb": "The MK4 astromech most builders here printed \u2014 six pie panels, fourteen side panels, three holoprojectors, and the body doors and utility arms that go with them.",
        "preselected": true,
        "defaultVariant": "complex",
        "variants": [
          {
            "id": "simple",
            "label": "Simple",
            "seeds": null
          },
          {
            "id": "complex",
            "label": "Complex",
            "seeds": [
              "pie1",
              "pie2",
              "pie3",
              "pie4",
              "pie5",
              "pie6",
              "panel1",
              "panel2",
              "panel3",
              "panel4",
              "panel5",
              "panel6",
              "panel7",
              "panel8",
              "panel9",
              "panel10",
              "panel11",
              "panel12",
              "panel13",
              "panel14",
              "magicPanel",
              "upperPanel",
              "psiRear",
              "logicRear",
              "logicFront",
              "psiFront",
              "hp1Pan",
              "hp1Tilt",
              "hp2Pan",
              "hp2Tilt",
              "hp3Pan",
              "hp3Tilt",
              "domeBtn1",
              "domeBtn2",
              "doorFL",
              "doorFR",
              "doorRL",
              "doorRR",
              "dataport",
              "chargebay",
              "smallDoor",
              "drawer",
              "utilUp",
              "utilLo"
            ]
          }
        ]
      },
      {
        "id": "own",
        "label": "My own build",
        "short": "Own build",
        "blurb": "For a droid that is nobody's published design \u2014 nothing is fitted for you, and each part joins the list as you build it.",
        "seeds": []
      }
    ],
    "parts": [
      {
        "index": 0,
        "id": "pie1",
        "section": "dome_pies",
        "name": "Dome pie 1",
        "half": "dome",
        "shorthand": "PP1",
        "aliases": [
          "PP1",
          "Dome pie 1"
        ],
        "position": "rear-right",
        "bearingDeg": 150,
        "control": "dome-link"
      },
      {
        "index": 1,
        "id": "pie2",
        "section": "dome_pies",
        "name": "Dome pie 2",
        "half": "dome",
        "shorthand": "PP2",
        "aliases": [
          "PP2",
          "Dome pie 2"
        ],
        "position": "right",
        "bearingDeg": 90,
        "control": "dome-link"
      },
      {
        "index": 2,
        "id": "pie3",
        "section": "dome_pies",
        "name": "Dome pie 3",
        "half": "dome",
        "shorthand": "PP3",
        "aliases": [
          "PP3",
          "Dome pie 3"
        ],
        "position": "front-right",
        "bearingDeg": 30,
        "control": "dome-link"
      },
      {
        "index": 3,
        "id": "pie4",
        "section": "dome_pies",
        "name": "Dome pie 4",
        "half": "dome",
        "shorthand": "PP4",
        "aliases": [
          "PP4",
          "Dome pie 4"
        ],
        "position": "front-left",
        "bearingDeg": 330,
        "control": "dome-link"
      },
      {
        "index": 4,
        "id": "pie5",
        "section": "dome_pies",
        "name": "Dome pie 5",
        "half": "dome",
        "shorthand": "PP5",
        "aliases": [
          "PP5",
          "Dome pie 5"
        ],
        "position": "left",
        "bearingDeg": 270,
        "control": "dome-link"
      },
      {
        "index": 5,
        "id": "pie6",
        "section": "dome_pies",
        "name": "Dome pie 6",
        "half": "dome",
        "shorthand": "PP6",
        "aliases": [
          "PP6",
          "Dome pie 6"
        ],
        "position": "rear-left",
        "bearingDeg": 210,
        "control": "dome-link"
      },
      {
        "index": 6,
        "id": "panel1",
        "section": "dome_panels",
        "name": "Dome side panel 1",
        "half": "dome",
        "shorthand": "P1",
        "aliases": [
          "P1",
          "Dome side panel 1"
        ],
        "position": "rear-right",
        "bearingDeg": 142.5,
        "control": "dome-link"
      },
      {
        "index": 7,
        "id": "panel2",
        "section": "dome_panels",
        "name": "Dome side panel 2",
        "half": "dome",
        "shorthand": "P2",
        "aliases": [
          "P2",
          "Dome side panel 2"
        ],
        "position": "rear-right",
        "bearingDeg": 128,
        "control": "dome-link"
      },
      {
        "index": 8,
        "id": "panel3",
        "section": "dome_panels",
        "name": "Dome side panel 3",
        "half": "dome",
        "shorthand": "P3",
        "aliases": [
          "P3",
          "Dome side panel 3"
        ],
        "position": "right",
        "bearingDeg": 114,
        "control": "dome-link"
      },
      {
        "index": 9,
        "id": "panel4",
        "section": "dome_panels",
        "name": "Dome side panel 4",
        "half": "dome",
        "shorthand": "P4",
        "aliases": [
          "P4",
          "Dome side panel 4"
        ],
        "position": "right",
        "bearingDeg": 94,
        "control": "dome-link"
      },
      {
        "index": 10,
        "id": "panel5",
        "section": "dome_panels",
        "name": "Dome side panel 5",
        "half": "dome",
        "shorthand": "P5",
        "aliases": [
          "P5",
          "Dome side panel 5"
        ],
        "position": "right",
        "bearingDeg": 75.5,
        "control": "none"
      },
      {
        "index": 11,
        "id": "panel6",
        "section": "dome_panels",
        "name": "Dome side panel 6",
        "half": "dome",
        "shorthand": "P6",
        "aliases": [
          "P6",
          "Dome side panel 6"
        ],
        "position": "front-right",
        "bearingDeg": 62.5,
        "control": "none"
      },
      {
        "index": 12,
        "id": "panel7",
        "section": "dome_panels",
        "name": "Dome side panel 7",
        "half": "dome",
        "shorthand": "P7",
        "aliases": [
          "P7",
          "Dome side panel 7"
        ],
        "position": "front-right",
        "bearingDeg": 45,
        "control": "dome-link"
      },
      {
        "index": 13,
        "id": "panel8",
        "section": "dome_panels",
        "name": "Dome side panel 8",
        "half": "dome",
        "shorthand": "P8",
        "aliases": [
          "P8",
          "Dome side panel 8"
        ],
        "position": "front",
        "bearingDeg": 24,
        "control": "none"
      },
      {
        "index": 14,
        "id": "panel9",
        "section": "dome_panels",
        "name": "Dome side panel 9",
        "half": "dome",
        "shorthand": "P9",
        "aliases": [
          "P9",
          "Dome side panel 9"
        ],
        "position": "rear-left",
        "bearingDeg": 300,
        "control": "none"
      },
      {
        "index": 15,
        "id": "panel10",
        "section": "dome_panels",
        "name": "Dome side panel 10",
        "half": "dome",
        "shorthand": "P10",
        "aliases": [
          "P10",
          "Dome side panel 10"
        ],
        "position": "left",
        "bearingDeg": 242,
        "control": "dome-link"
      },
      {
        "index": 16,
        "id": "panel11",
        "section": "dome_panels",
        "name": "Dome side panel 11",
        "half": "dome",
        "shorthand": "P11",
        "aliases": [
          "P11",
          "Dome side panel 11"
        ],
        "position": "rear-left",
        "bearingDeg": 217,
        "control": "dome-link"
      },
      {
        "index": 17,
        "id": "panel12",
        "section": "dome_panels",
        "name": "Dome side panel 12",
        "half": "dome",
        "shorthand": "P12",
        "aliases": [
          "P12",
          "Dome side panel 12"
        ],
        "position": "rear",
        "bearingDeg": 204,
        "control": "none"
      },
      {
        "index": 18,
        "id": "panel13",
        "section": "dome_panels",
        "name": "Dome side panel 13",
        "half": "dome",
        "shorthand": "P13",
        "aliases": [
          "P13",
          "Dome side panel 13"
        ],
        "position": "rear",
        "bearingDeg": 194,
        "control": "dome-link"
      },
      {
        "index": 19,
        "id": "panel14",
        "section": "dome_panels",
        "name": "Dome side panel 14",
        "half": "dome",
        "shorthand": "P14",
        "aliases": [
          "P14",
          "Dome side panel 14"
        ],
        "position": "rear",
        "bearingDeg": 184,
        "control": "none"
      },
      {
        "index": 20,
        "id": "logicFront",
        "section": "dome_lights",
        "name": "Front Logic Displays",
        "half": "dome",
        "aliases": [
          "FLD",
          "Front display"
        ],
        "position": "rear",
        "bearingDeg": 204,
        "control": "none",
        "kind": "light",
        "sitsOn": "panel12"
      },
      {
        "index": 21,
        "id": "logicRear",
        "section": "dome_lights",
        "name": "Rear Logic Display",
        "half": "dome",
        "aliases": [
          "RLD",
          "Rear display"
        ],
        "position": "rear-left",
        "bearingDeg": 300,
        "control": "none",
        "kind": "light",
        "sitsOn": "panel9"
      },
      {
        "index": 22,
        "id": "magicPanel",
        "section": "dome_lights",
        "name": "Magic Panel",
        "half": "dome",
        "aliases": [
          "MP"
        ],
        "position": "right",
        "bearingDeg": 75.5,
        "control": "none",
        "kind": "light",
        "sitsOn": "panel5"
      },
      {
        "index": 23,
        "id": "psiFront",
        "section": "dome_lights",
        "name": "Front PSI",
        "half": "dome",
        "aliases": [
          "FPSI"
        ],
        "position": "rear",
        "bearingDeg": 184,
        "control": "none",
        "kind": "light",
        "sitsOn": "panel14"
      },
      {
        "index": 24,
        "id": "psiRear",
        "section": "dome_lights",
        "name": "Rear PSI",
        "half": "dome",
        "aliases": [
          "RPSI"
        ],
        "position": "front",
        "bearingDeg": 24,
        "control": "none",
        "kind": "light",
        "sitsOn": "panel8"
      },
      {
        "index": 25,
        "id": "upperPanel",
        "section": "dome_lights",
        "name": "Small upper panel",
        "half": "dome",
        "aliases": [],
        "position": "front-right",
        "bearingDeg": 62.5,
        "control": "none",
        "kind": "light",
        "sitsOn": "panel6"
      },
      {
        "index": 26,
        "id": "hp1Pan",
        "section": "holoprojectors",
        "name": "Holoprojector 1 pan",
        "half": "dome",
        "shorthand": "HP1-1",
        "aliases": [
          "HP1-1",
          "Holoprojector 1 pan"
        ],
        "position": "rear",
        "bearingDeg": 165,
        "control": "none",
        "unit": 1,
        "axis": "pan"
      },
      {
        "index": 27,
        "id": "hp1Tilt",
        "section": "holoprojectors",
        "name": "Holoprojector 1 tilt",
        "half": "dome",
        "shorthand": "HP1-2",
        "aliases": [
          "HP1-2",
          "Holoprojector 1 tilt"
        ],
        "position": "rear",
        "bearingDeg": 165,
        "control": "none",
        "unit": 1,
        "axis": "tilt"
      },
      {
        "index": 28,
        "id": "hp2Pan",
        "section": "holoprojectors",
        "name": "Holoprojector 2 pan",
        "half": "dome",
        "shorthand": "HP2-1",
        "aliases": [
          "HP2-1",
          "Holoprojector 2 pan"
        ],
        "position": "front",
        "bearingDeg": 350,
        "control": "none",
        "unit": 2,
        "axis": "pan"
      },
      {
        "index": 29,
        "id": "hp2Tilt",
        "section": "holoprojectors",
        "name": "Holoprojector 2 tilt",
        "half": "dome",
        "shorthand": "HP2-2",
        "aliases": [
          "HP2-2",
          "Holoprojector 2 tilt"
        ],
        "position": "front",
        "bearingDeg": 350,
        "control": "none",
        "unit": 2,
        "axis": "tilt"
      },
      {
        "index": 30,
        "id": "hp3Pan",
        "section": "holoprojectors",
        "name": "Holoprojector 3 pan",
        "half": "dome",
        "shorthand": "HP3-1",
        "aliases": [
          "HP3-1",
          "Holoprojector 3 pan"
        ],
        "position": "front-right",
        "bearingDeg": 38,
        "control": "none",
        "unit": 3,
        "axis": "pan"
      },
      {
        "index": 31,
        "id": "hp3Tilt",
        "section": "holoprojectors",
        "name": "Holoprojector 3 tilt",
        "half": "dome",
        "shorthand": "HP3-2",
        "aliases": [
          "HP3-2",
          "Holoprojector 3 tilt"
        ],
        "position": "front-right",
        "bearingDeg": 38,
        "control": "none",
        "unit": 3,
        "axis": "tilt"
      },
      {
        "index": 32,
        "id": "domeBtn1",
        "section": "dome_fixtures",
        "name": "Dome button 1",
        "half": "dome",
        "aliases": [],
        "position": "front",
        "bearingDeg": 8,
        "control": null
      },
      {
        "index": 33,
        "id": "domeBtn2",
        "section": "dome_fixtures",
        "name": "Dome button 2",
        "half": "dome",
        "aliases": [],
        "position": "front",
        "bearingDeg": 20,
        "control": null
      },
      {
        "index": 34,
        "id": "chargebay",
        "section": "body_doors",
        "name": "Chargebay door",
        "half": "body",
        "aliases": [
          "Chargebay door",
          "Charge bay"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": "ChargingBayDoor"
      },
      {
        "index": 35,
        "id": "dataport",
        "section": "body_doors",
        "name": "Dataport door",
        "half": "body",
        "aliases": [
          "Dataport door",
          "Dataport"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": "DataPortDoor"
      },
      {
        "index": 36,
        "id": "doorFL",
        "section": "body_doors",
        "name": "Left body door",
        "half": "body",
        "aliases": [
          "Left body door",
          "FL door"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": "FLBreadpanDoor"
      },
      {
        "index": 37,
        "id": "doorFR",
        "section": "body_doors",
        "name": "Right body door",
        "half": "body",
        "aliases": [
          "Right body door",
          "FR door"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": "FRBreadpandoor"
      },
      {
        "index": 38,
        "id": "doorRL",
        "section": "body_doors",
        "name": "Rear-left body door",
        "half": "body",
        "aliases": [
          "Rear-left body door",
          "RL door"
        ],
        "position": "rear-left",
        "control": "none",
        "cadName": "RLBreadpanDoor"
      },
      {
        "index": 39,
        "id": "doorRR",
        "section": "body_doors",
        "name": "Rear-right body door",
        "half": "body",
        "aliases": [
          "Rear-right body door",
          "RR door"
        ],
        "position": "rear-right",
        "control": "none",
        "cadName": "RRBreadpandoor"
      },
      {
        "index": 40,
        "id": "drawer",
        "section": "body_doors",
        "name": "Drawer",
        "half": "body",
        "aliases": [
          "Drawer"
        ],
        "position": "front",
        "control": "none",
        "cadName": "Drawer"
      },
      {
        "index": 41,
        "id": "smallDoor",
        "section": "body_doors",
        "name": "Small long door",
        "half": "body",
        "aliases": [
          "Small long door"
        ],
        "position": "front",
        "control": "none",
        "cadName": "SmallLongDoor"
      },
      {
        "index": 42,
        "id": "gripArm",
        "section": "body_arms",
        "name": "Gripper arm",
        "half": "body",
        "aliases": [
          "Gripper arm"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": null
      },
      {
        "index": 43,
        "id": "gripClaw",
        "section": "body_arms",
        "name": "Gripper claw",
        "half": "body",
        "aliases": [
          "Gripper claw"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": null
      },
      {
        "index": 44,
        "id": "interArm",
        "section": "body_arms",
        "name": "Interface arm",
        "half": "body",
        "aliases": [
          "Interface arm"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": null
      },
      {
        "index": 45,
        "id": "interTool",
        "section": "body_arms",
        "name": "Interface tool",
        "half": "body",
        "aliases": [
          "Interface tool"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": null
      },
      {
        "index": 46,
        "id": "utilLo",
        "section": "body_arms",
        "name": "Lower utility arm",
        "half": "body",
        "aliases": [
          "Lower utility arm"
        ],
        "position": "front",
        "control": "body-ledc",
        "cadName": "LowerUtilityArm"
      },
      {
        "index": 47,
        "id": "utilUp",
        "section": "body_arms",
        "name": "Upper utility arm",
        "half": "body",
        "aliases": [
          "Upper utility arm"
        ],
        "position": "front",
        "control": "body-ledc",
        "cadName": "UpperUtilityArm"
      },
      {
        "index": 48,
        "id": "other1",
        "section": "other_slots",
        "name": "Other part 1",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 49,
        "id": "other2",
        "section": "other_slots",
        "name": "Other part 2",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 50,
        "id": "other3",
        "section": "other_slots",
        "name": "Other part 3",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 51,
        "id": "other4",
        "section": "other_slots",
        "name": "Other part 4",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 52,
        "id": "other5",
        "section": "other_slots",
        "name": "Other part 5",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 53,
        "id": "other6",
        "section": "other_slots",
        "name": "Other part 6",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 54,
        "id": "other7",
        "section": "other_slots",
        "name": "Other part 7",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 55,
        "id": "other8",
        "section": "other_slots",
        "name": "Other part 8",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 56,
        "id": "other9",
        "section": "other_slots",
        "name": "Other part 9",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 57,
        "id": "other10",
        "section": "other_slots",
        "name": "Other part 10",
        "aliases": [],
        "control": "body-ledc"
      }
    ]
  };
})();
