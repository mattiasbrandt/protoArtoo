/**
 * data/droid_parts.js
 *
 * Auto-generated from docs/droid-parts.yaml by tools/generate_droid_parts_catalog.py
 * DO NOT EDIT MANUALLY
 *
 * Source digest: sha256 b7ef78b5ec66150a022d327af3438d2b8591bc2dd80e0ce6c82c1bb58afdbefa
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
 */

(function () {
  'use strict';

  window.DroidParts = {
    "source": "docs/droid-parts.yaml",
    "generator": "tools/generate_droid_parts_catalog.py",
    "sourceSha256": "b7ef78b5ec66150a022d327af3438d2b8591bc2dd80e0ce6c82c1bb58afdbefa",
    "designs": [
      {
        "id": "mk4",
        "label": "MrBaddeley MK4",
        "short": "MK4",
        "blurb": "The MK4 astromech most builders here printed \u2014 six pie panels, fourteen side panels, three holoprojectors, and the body doors and utility arms that go with them.",
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
        "shorthand": "P5",
        "aliases": [
          "P5",
          "Dome side panel 5"
        ],
        "position": "right",
        "bearingDeg": 75.5,
        "control": "none",
        "lit": "Magic Panel"
      },
      {
        "index": 11,
        "id": "panel6",
        "section": "dome_panels",
        "name": "Dome side panel 6",
        "shorthand": "P6",
        "aliases": [
          "P6",
          "Dome side panel 6"
        ],
        "position": "front-right",
        "bearingDeg": 62.5,
        "control": "none",
        "lit": "small upper panel"
      },
      {
        "index": 12,
        "id": "panel7",
        "section": "dome_panels",
        "name": "Dome side panel 7",
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
        "shorthand": "P8",
        "aliases": [
          "P8",
          "Dome side panel 8"
        ],
        "position": "front",
        "bearingDeg": 24,
        "control": "none",
        "lit": "Rear PSI"
      },
      {
        "index": 14,
        "id": "panel9",
        "section": "dome_panels",
        "name": "Dome side panel 9",
        "shorthand": "P9",
        "aliases": [
          "P9",
          "Dome side panel 9"
        ],
        "position": "rear-left",
        "bearingDeg": 300,
        "control": "none",
        "lit": "Rear Logic Display"
      },
      {
        "index": 15,
        "id": "panel10",
        "section": "dome_panels",
        "name": "Dome side panel 10",
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
        "shorthand": "P12",
        "aliases": [
          "P12",
          "Dome side panel 12"
        ],
        "position": "rear",
        "bearingDeg": 204,
        "control": "none",
        "lit": "Front Logic Displays"
      },
      {
        "index": 18,
        "id": "panel13",
        "section": "dome_panels",
        "name": "Dome side panel 13",
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
        "shorthand": "P14",
        "aliases": [
          "P14",
          "Dome side panel 14"
        ],
        "position": "rear",
        "bearingDeg": 184,
        "control": "none",
        "lit": "Front PSI"
      },
      {
        "index": 20,
        "id": "hp1Pan",
        "section": "holoprojectors",
        "name": "Holoprojector 1 pan",
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
        "index": 21,
        "id": "hp1Tilt",
        "section": "holoprojectors",
        "name": "Holoprojector 1 tilt",
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
        "index": 22,
        "id": "hp2Pan",
        "section": "holoprojectors",
        "name": "Holoprojector 2 pan",
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
        "index": 23,
        "id": "hp2Tilt",
        "section": "holoprojectors",
        "name": "Holoprojector 2 tilt",
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
        "index": 24,
        "id": "hp3Pan",
        "section": "holoprojectors",
        "name": "Holoprojector 3 pan",
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
        "index": 25,
        "id": "hp3Tilt",
        "section": "holoprojectors",
        "name": "Holoprojector 3 tilt",
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
        "index": 26,
        "id": "domeBtn1",
        "section": "dome_fixtures",
        "name": "Dome button 1",
        "aliases": [],
        "position": "front",
        "bearingDeg": 8,
        "control": null
      },
      {
        "index": 27,
        "id": "domeBtn2",
        "section": "dome_fixtures",
        "name": "Dome button 2",
        "aliases": [],
        "position": "front",
        "bearingDeg": 20,
        "control": null
      },
      {
        "index": 28,
        "id": "chargebay",
        "section": "body_doors",
        "name": "Chargebay door",
        "aliases": [
          "Chargebay door",
          "Charge bay"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": "ChargingBayDoor"
      },
      {
        "index": 29,
        "id": "dataport",
        "section": "body_doors",
        "name": "Dataport door",
        "aliases": [
          "Dataport door",
          "Dataport"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": "DataPortDoor"
      },
      {
        "index": 30,
        "id": "doorFL",
        "section": "body_doors",
        "name": "Left body door",
        "aliases": [
          "Left body door",
          "FL door"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": "FLBreadpanDoor"
      },
      {
        "index": 31,
        "id": "doorFR",
        "section": "body_doors",
        "name": "Right body door",
        "aliases": [
          "Right body door",
          "FR door"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": "FRBreadpandoor"
      },
      {
        "index": 32,
        "id": "doorRL",
        "section": "body_doors",
        "name": "Rear-left body door",
        "aliases": [
          "Rear-left body door",
          "RL door"
        ],
        "position": "rear-left",
        "control": "none",
        "cadName": "RLBreadpanDoor"
      },
      {
        "index": 33,
        "id": "doorRR",
        "section": "body_doors",
        "name": "Rear-right body door",
        "aliases": [
          "Rear-right body door",
          "RR door"
        ],
        "position": "rear-right",
        "control": "none",
        "cadName": "RRBreadpandoor"
      },
      {
        "index": 34,
        "id": "drawer",
        "section": "body_doors",
        "name": "Drawer",
        "aliases": [
          "Drawer"
        ],
        "position": "front",
        "control": "none",
        "cadName": "Drawer"
      },
      {
        "index": 35,
        "id": "smallDoor",
        "section": "body_doors",
        "name": "Small long door",
        "aliases": [
          "Small long door"
        ],
        "position": "front",
        "control": "none",
        "cadName": "SmallLongDoor"
      },
      {
        "index": 36,
        "id": "gripArm",
        "section": "body_arms",
        "name": "Gripper arm",
        "aliases": [
          "Gripper arm"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": null
      },
      {
        "index": 37,
        "id": "gripClaw",
        "section": "body_arms",
        "name": "Gripper claw",
        "aliases": [
          "Gripper claw"
        ],
        "position": "front-left",
        "control": "none",
        "cadName": null
      },
      {
        "index": 38,
        "id": "interArm",
        "section": "body_arms",
        "name": "Interface arm",
        "aliases": [
          "Interface arm"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": null
      },
      {
        "index": 39,
        "id": "interTool",
        "section": "body_arms",
        "name": "Interface tool",
        "aliases": [
          "Interface tool"
        ],
        "position": "front-right",
        "control": "none",
        "cadName": null
      },
      {
        "index": 40,
        "id": "utilLo",
        "section": "body_arms",
        "name": "Lower utility arm",
        "aliases": [
          "Lower utility arm"
        ],
        "position": "front",
        "control": "body-ledc",
        "cadName": "LowerUtilityArm"
      },
      {
        "index": 41,
        "id": "utilUp",
        "section": "body_arms",
        "name": "Upper utility arm",
        "aliases": [
          "Upper utility arm"
        ],
        "position": "front",
        "control": "body-ledc",
        "cadName": "UpperUtilityArm"
      },
      {
        "index": 42,
        "id": "other1",
        "section": "other_slots",
        "name": "Other part 1",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 43,
        "id": "other2",
        "section": "other_slots",
        "name": "Other part 2",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 44,
        "id": "other3",
        "section": "other_slots",
        "name": "Other part 3",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 45,
        "id": "other4",
        "section": "other_slots",
        "name": "Other part 4",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 46,
        "id": "other5",
        "section": "other_slots",
        "name": "Other part 5",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 47,
        "id": "other6",
        "section": "other_slots",
        "name": "Other part 6",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 48,
        "id": "other7",
        "section": "other_slots",
        "name": "Other part 7",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 49,
        "id": "other8",
        "section": "other_slots",
        "name": "Other part 8",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 50,
        "id": "other9",
        "section": "other_slots",
        "name": "Other part 9",
        "aliases": [],
        "control": "body-ledc"
      },
      {
        "index": 51,
        "id": "other10",
        "section": "other_slots",
        "name": "Other part 10",
        "aliases": [],
        "control": "body-ledc"
      }
    ]
  };
})();
