#!/usr/bin/env python3
"""Generate the firmware id table and the browser module from the parts catalog.

docs/droid-parts.yaml is the one declaration of every Part on the droid. It is
never read at runtime: it is generated into the two places that need it, which
is what keeps a Part name on screen and a Part id in firmware from drifting
apart, and what keeps either from depending on a filesystem that can go missing
(#301, #356).

    docs/droid-parts.yaml
       |-> include/droid_parts.h   the id vocabulary firmware resolves against
       '-> data/droid_parts.js     names, shorthand, aliases, position

The `control:` column decides how far each entry travels. A Part the body drives
reaches both outputs; a dome-link Part or one nothing drives yet reaches the
browser only, because the dome owns execution of panel intent under Catalog
Authority and its targets are already whitelisted there. Firmware therefore
carries only ids it can actually resolve to an Output of its own.

Modelled on tools/generate_console_catalog.py, which does the same job for
docs/action-registry.yaml: one YAML, one generator, committed outputs stamped
"DO NOT EDIT MANUALLY". Deliberately NOT modelled on
tools/check_action_registry_drift.py - a purely derived list generates, and then
has nothing left to check (#301). What can still go stale is the committed
artefact, and guarding that is #358's.

Three things this generator refuses, each because the alternative is an entry
nothing can resolve:

  - a `control:` token include/droid_part_control.inc does not declare. The
    firmware declares the control paths; the catalog does not get to invent one.
  - a firmware-bound id too long for the Part field on a Servo Output row
    (SERVO_OUTPUT_PART_ID_MAX, include/servo_output_row.h). An id that cannot be
    stored against an output is an id no output can ever claim.
  - `seeds:` that is neither a list of declared part ids nor the scalar `TBD`.
    `TBD` generates as `null` rather than as `[]`, so a consumer reaching for an
    unknown complement throws instead of quietly seeding an empty droid.

Neither refusal keeps its own copy of the firmware's list: both are read out of
the firmware headers at generation time, because a list written down twice is a
list that drifts.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from registry_yaml import load_registry_yaml

ROOT = Path(__file__).resolve().parent.parent

def rel(path):
    """Repo-relative where the path is in the tree, plain otherwise.

    The generator is pointed at a temporary tree by its own tests, and a message
    is no use if it explodes on the path it is trying to describe.
    """
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


CATALOG_PATH = ROOT / "docs" / "droid-parts.yaml"
CONTROL_MANIFEST_PATH = ROOT / "include" / "droid_part_control.inc"
SERVO_OUTPUT_ROW_PATH = ROOT / "include" / "servo_output_row.h"

FIRMWARE_OUTPUT_PATH = ROOT / "include" / "droid_parts.h"
BROWSER_OUTPUT_PATH = ROOT / "data" / "droid_parts.js"

# Where each file BELONGS, as generated text names it. Every path above is
# injectable so the generator can be aimed at a scratch tree, and these are what
# keep that from leaking: what it writes there is byte for byte what it writes
# here, which is the whole basis of comparing a committed artefact against a
# fresh run (#358).
CATALOG_NAME = rel(CATALOG_PATH)
GENERATOR_NAME = rel(__file__)
FIRMWARE_NAME = rel(FIRMWARE_OUTPUT_PATH)
BROWSER_NAME = rel(BROWSER_OUTPUT_PATH)

REGENERATION_NOTE = f"""\
After editing {CATALOG_NAME}, run this generator. It rewrites two
committed outputs:

  {FIRMWARE_NAME:<24} the id vocabulary firmware resolves a Part against
  {BROWSER_NAME:<24} the names, shorthand, aliases and position every
  {'':<24} operator surface labels a Part with

Editing the catalog without regenerating them leaves the firmware's vocabulary
gate and every Part name on screen stale against the catalog they came from - a
Part a builder can see and not wire, or wire and not see."""

# The sections that declare parts, in the order the generated outputs emit them.
# Emission order is the generator's to decide (#301): the ids stay authored in
# the YAML, but the order and index they come out in are derived here, so
# reordering rows in the catalog cannot renumber anything downstream.
PART_SECTIONS = (
    "dome_pies",
    "dome_panels",
    "holoprojectors",
    "dome_fixtures",
    "body_doors",
    "body_arms",
)

TOP_LEVEL_KEYS = frozenset(("designs", "other_slots")) | frozenset(PART_SECTIONS)

# Every key a part row may carry. A row with a key that is not here is a typo
# that would otherwise generate an entry silently missing a field - `postion:`
# reads as "no position" rather than as a mistake.
PART_KEYS = frozenset(
    (
        "id",
        "label",
        "cad_name",
        "shorthand",
        "aliases",
        "bearing_deg",
        "position",
        "control",
        "dome_link_panel",
        "unit",
        "axis",
        "lit",
        "note",
    )
)

DESIGN_KEYS = frozenset(("id", "label", "short", "blurb", "note", "variants", "default_variant", "seeds"))
VARIANT_KEYS = frozenset(("id", "label", "seeds"))
OTHER_SLOT_KEYS = frozenset(("count", "id_prefix", "label_prefix", "control"))

# A catalog id is an unquoted identifier, the same shape
# servoOutputPartIdIsValid() accepts on a Servo Output row.
ID_RE = re.compile(r"^[A-Za-z0-9_]+$")

# The declared unknown. It is never emitted as a value: a `TBD` field is absent
# from the generated output, and a `TBD` seed list generates as null.
TBD = "TBD"

# A key the row does not carry at all, told apart from a key it carries as null.
# `cad_name: null` is a statement - the part HAS no CAD name, which is what marks
# a Common Addition (ADR 0047) - while no `cad_name:` key at all is a row for
# which the question does not arise, and the two must not generate the same way.
ABSENT = object()

CONTROL_ROW_RE = re.compile(
    r"^PA_PART_CONTROL\(\s*([A-Z0-9_]+)\s*,\s*\"([^\"]+)\"\s*,\s*([01])\s*\)$"
)
SERVO_PART_ID_MAX_RE = re.compile(
    r"^constexpr\s+uint8_t\s+SERVO_OUTPUT_PART_ID_MAX\s*=\s*(\d+)\s*;"
)


class CatalogError(Exception):
    """Every problem found in one pass, so a broken catalog is fixed once."""

    def __init__(self, problems):
        super().__init__("\n".join(problems))
        self.problems = problems


# =============================================================================
# What the firmware defines
# =============================================================================


def load_control_paths(path=None):
    """Read include/droid_part_control.inc: the control paths firmware defines.

    Returns an ordered dict of yaml token -> (enumerator, reaches_firmware).
    Comments and the preprocessor guard are skipped; every other non-empty line
    must be exactly one manifest row, so a malformed declaration cannot quietly
    drop a control path out of the vocabulary this generator validates against.
    """
    path = path or CONTROL_MANIFEST_PATH
    paths = {}
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        match = CONTROL_ROW_RE.fullmatch(line)
        if not match:
            raise CatalogError(
                [f"{rel(path)}:{line_number}: not a PA_PART_CONTROL row: {line}"]
            )
        enumerator, token, reaches = match.groups()
        if token in paths:
            raise CatalogError(
                [f"{rel(path)}:{line_number}: control path {token!r} declared twice"]
            )
        paths[token] = (enumerator, reaches == "1")
    if not paths:
        raise CatalogError([f"{rel(path)} declares no control paths"])
    return paths


def load_part_id_limit(path=None):
    """Read SERVO_OUTPUT_PART_ID_MAX: how long an id a Servo Output row holds.

    Parsed rather than restated. A firmware-bound id longer than this could be
    authored in the catalog and never stored against an output, so the generator
    refuses it here and the generated header asserts the same bound at compile
    time for whoever changes the row model instead of the catalog.
    """
    path = path or SERVO_OUTPUT_ROW_PATH
    for line in path.read_text(encoding="utf-8").splitlines():
        match = SERVO_PART_ID_MAX_RE.match(line.strip())
        if match:
            return int(match.group(1))
    raise CatalogError(
        [f"{rel(path)}: could not read SERVO_OUTPUT_PART_ID_MAX"]
    )


# =============================================================================
# Reading the catalog
# =============================================================================


def natural_key(text):
    """Sort ids the way a builder reads them: panel2 before panel10.

    Every element is the same 3-tuple shape so a digit run and a word run are
    always comparable, whatever order they appear in.
    """
    return tuple(
        (0, int(part), "") if part.isdigit() else (1, 0, part.lower())
        for part in re.split(r"(\d+)", text)
        if part != ""
    )


def part_name(row):
    """The plain-English name an operator surface labels this Part with.

    Dome rows carry the Printed Droid shorthand first in `aliases` and the
    plain-English name second; body rows carry only the plain-English name; a
    fixture carries a `label`. One rule covers all three: a `label` wins, then
    the first alias that is not just the shorthand again.
    """
    label = row.get("label")
    if isinstance(label, str) and label.strip():
        return label.strip()
    shorthand = row.get("shorthand")
    for alias in row.get("aliases") or []:
        if isinstance(alias, str) and alias.strip() and alias != shorthand:
            return alias.strip()
    return None


def declared(value):
    """True when a field carries a real value rather than the declared unknown.

    `TBD` means "not known here, do not guess" and never reaches a generated
    output as a value. An explicit null is a different statement - the part HAS
    no CAD name, which is what marks a Common Addition - and does generate.
    """
    return value != TBD


def check_keys(where, mapping, allowed, problems):
    unknown = sorted(set(mapping) - allowed)
    if unknown:
        problems.append(f"{where}: unknown field(s) {unknown}; allowed: {sorted(allowed)}")


def read_parts(doc, control_paths, problems):
    """Every declared part row, in emission order, validated on the way."""
    parts = []
    seen = {}
    for section in PART_SECTIONS:
        rows = doc.get(section)
        if not isinstance(rows, list) or not rows:
            problems.append(f"{section}: missing or empty")
            continue
        section_parts = []
        for position, row in enumerate(rows):
            where = f"{section}[{position}]"
            if not isinstance(row, dict):
                problems.append(f"{where}: not a part row")
                continue
            check_keys(where, row, PART_KEYS, problems)
            part_id = row.get("id")
            if not isinstance(part_id, str) or not ID_RE.match(part_id):
                problems.append(f"{where}: id {part_id!r} is not an unquoted identifier")
                continue
            where = f"{section}/{part_id}"
            if part_id in seen:
                problems.append(f"{where}: id already declared in {seen[part_id]}")
                continue
            seen[part_id] = section
            control = row.get("control")
            if control is not None and control not in control_paths:
                problems.append(
                    f"{where}: control {control!r} is not a path the firmware defines; "
                    f"{rel(CONTROL_MANIFEST_PATH)} declares "
                    f"{sorted(control_paths)}"
                )
                continue
            name = part_name(row)
            if name is None:
                problems.append(f"{where}: no label and no alias, so nothing can name it")
                continue
            aliases = row.get("aliases") or []
            if not isinstance(aliases, list) or not all(
                isinstance(a, str) and a.strip() for a in aliases
            ):
                problems.append(f"{where}: aliases must be a list of non-empty strings")
                continue
            section_parts.append(
                {
                    "id": part_id,
                    "section": section,
                    "name": name,
                    "shorthand": row.get("shorthand"),
                    "aliases": list(aliases),
                    "position": row.get("position"),
                    "control": control,
                    "bearing_deg": row.get("bearing_deg"),
                    "cad_name": row.get("cad_name", ABSENT),
                    "dome_link_panel": row.get("dome_link_panel"),
                    "unit": row.get("unit"),
                    "axis": row.get("axis"),
                    "lit": row.get("lit"),
                }
            )
        section_parts.sort(key=lambda part: natural_key(part["id"]))
        parts.extend(section_parts)
    return parts


def read_other_slots(doc, control_paths, declared_ids, problems):
    """Mint other1..otherN, the escape hatch, from the two fields that declare it.

    The slots are ids rather than rows precisely so widening the hatch is a
    number in the catalog, and the generator - not a human keeping ten rows in
    step - hands out the index.
    """
    block = doc.get("other_slots")
    if not isinstance(block, dict):
        problems.append("other_slots: missing")
        return []
    check_keys("other_slots", block, OTHER_SLOT_KEYS, problems)
    count = block.get("count")
    prefix = block.get("id_prefix")
    label_prefix = block.get("label_prefix")
    control = block.get("control")
    if not isinstance(count, int) or isinstance(count, bool) or count < 1:
        problems.append(f"other_slots.count: {count!r} is not a positive whole number")
        return []
    if not isinstance(prefix, str) or not ID_RE.match(prefix):
        problems.append(f"other_slots.id_prefix: {prefix!r} is not an unquoted identifier")
        return []
    if not isinstance(label_prefix, str) or not label_prefix.strip():
        problems.append("other_slots.label_prefix: missing, so the slots have no name")
        return []
    if control not in control_paths:
        problems.append(
            f"other_slots.control: {control!r} is not a path the firmware defines; "
            f"{rel(CONTROL_MANIFEST_PATH)} declares {sorted(control_paths)}"
        )
        return []
    slots = []
    for number in range(1, count + 1):
        slot_id = f"{prefix}{number}"
        if slot_id in declared_ids:
            problems.append(f"other_slots: {slot_id} collides with a declared part")
            continue
        slots.append(
            {
                "id": slot_id,
                "section": "other_slots",
                "name": f"{label_prefix.strip()} {number}",
                "shorthand": None,
                "aliases": [],
                # No position word, by definition: a builder's own hardware is
                # listed beside the body view rather than placed on it
                # (ADR 0063).
                "position": None,
                "control": control,
                "bearing_deg": None,
                "cad_name": ABSENT,
                "dome_link_panel": None,
                "unit": None,
                "axis": None,
                "lit": None,
            }
        )
    return slots


def read_designs(doc, declared_ids, problems):
    """The design rows, with each complement checked against the declared parts."""
    designs = doc.get("designs")
    if not isinstance(designs, list) or not designs:
        problems.append("designs: missing or empty")
        return []

    def read_seeds(where, seeds):
        """A list of declared part ids, or the declared unknown as null."""
        if seeds == TBD:
            return None
        if not isinstance(seeds, list):
            problems.append(
                f"{where}: seeds is neither a list nor {TBD}; "
                "an unknown complement is declared, never left to a default"
            )
            return None
        unknown = [i for i in seeds if i not in declared_ids]
        if unknown:
            problems.append(f"{where}: seeds ids no part row declares: {unknown}")
            return None
        duplicated = sorted({i for i in seeds if seeds.count(i) > 1})
        if duplicated:
            problems.append(f"{where}: seeds an id twice: {duplicated}")
            return None
        return list(seeds)

    rows = []
    seen = set()
    for position, design in enumerate(designs):
        where = f"designs[{position}]"
        if not isinstance(design, dict):
            problems.append(f"{where}: not a design row")
            continue
        check_keys(where, design, DESIGN_KEYS, problems)
        design_id = design.get("id")
        if not isinstance(design_id, str) or not design_id.strip():
            problems.append(f"{where}: no id")
            continue
        where = f"designs/{design_id}"
        if design_id in seen:
            problems.append(f"{where}: declared twice")
            continue
        seen.add(design_id)
        card = {}
        for field in ("label", "short", "blurb"):
            value = design.get(field)
            if not isinstance(value, str) or not value.strip():
                problems.append(f"{where}: {field} is missing or empty")
            card[field] = value
        row = {"id": design_id, "label": card["label"], "short": card["short"],
               "blurb": card["blurb"]}

        variants = design.get("variants")
        default_variant = design.get("default_variant")
        if variants is None:
            if default_variant is not None:
                problems.append(
                    f"{where}: declares a default_variant but no variants to default to"
                )
            if "seeds" not in design:
                problems.append(f"{where}: declares no complement at all")
            else:
                row["seeds"] = read_seeds(where, design["seeds"])
            rows.append(row)
            continue

        if not isinstance(variants, list) or not variants:
            problems.append(f"{where}: variants is empty; omit the key instead")
            continue
        if "seeds" in design:
            problems.append(f"{where}: seeds both per-design and per-variant")
            continue
        variant_rows = []
        for variant_position, variant in enumerate(variants):
            variant_where = f"{where}/variants[{variant_position}]"
            if not isinstance(variant, dict):
                problems.append(f"{variant_where}: not a variant row")
                continue
            check_keys(variant_where, variant, VARIANT_KEYS, problems)
            variant_id = variant.get("id")
            label = variant.get("label")
            if not isinstance(variant_id, str) or not variant_id.strip():
                problems.append(f"{variant_where}: no id")
                continue
            if not isinstance(label, str) or not label.strip():
                problems.append(f"{where}/{variant_id}: label is missing or empty")
            if "seeds" not in variant:
                problems.append(f"{where}/{variant_id}: declares no complement")
                continue
            variant_rows.append(
                {
                    "id": variant_id,
                    "label": label,
                    "seeds": read_seeds(f"{where}/{variant_id}", variant["seeds"]),
                }
            )
        variant_ids = [v["id"] for v in variant_rows]
        if len(set(variant_ids)) != len(variant_ids):
            problems.append(f"{where}: declares a variant id twice")
        if default_variant is None:
            problems.append(
                f"{where}: declares variants but not which one a builder starts on"
            )
        elif default_variant not in variant_ids:
            problems.append(
                f"{where}: default_variant {default_variant!r} is not one of {variant_ids}"
            )
        else:
            row["defaultVariant"] = default_variant
        row["variants"] = variant_rows
        rows.append(row)
    return rows


def load_catalog(path=None, control_path=None, id_limit_path=None):
    """Read and validate the whole catalog, or raise with every problem at once."""
    path = path or CATALOG_PATH
    # The narrowed loader from #249: the catalog carries unquoted word enums
    # (`control: none`, `position: front`, `axis: pan`) exactly as the action
    # registry does, so the YAML 1.1 on/off/yes/no boolean family must not
    # coerce here either - `control: none` is the string, not Python's None.
    doc = load_registry_yaml(path)
    if not isinstance(doc, dict):
        raise CatalogError([f"{rel(path)}: not a mapping"])

    problems = []
    check_keys(rel(path), doc, TOP_LEVEL_KEYS, problems)

    control_paths = load_control_paths(control_path)
    id_limit = load_part_id_limit(id_limit_path)

    parts = read_parts(doc, control_paths, problems)
    declared_ids = {part["id"] for part in parts}
    parts.extend(read_other_slots(doc, control_paths, declared_ids, problems))
    designs = read_designs(doc, declared_ids, problems)

    for part in parts:
        control = part["control"]
        if control is None:
            continue
        if control_paths[control][1] and len(part["id"]) > id_limit:
            problems.append(
                f"{part['section']}/{part['id']}: reaches firmware but its id is "
                f"{len(part['id'])} characters, and a Servo Output row holds "
                f"{id_limit} ({rel(SERVO_OUTPUT_ROW_PATH)})"
            )

    if problems:
        raise CatalogError(problems)

    for index, part in enumerate(parts):
        part["index"] = index
    return {
        "parts": parts,
        "designs": designs,
        "control_paths": control_paths,
        "digest": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


# =============================================================================
# The firmware id table
# =============================================================================


def firmware_parts(catalog):
    """The parts the body drives, which are the only ids firmware can resolve."""
    control_paths = catalog["control_paths"]
    return [
        part
        for part in catalog["parts"]
        if part["control"] is not None and control_paths[part["control"]][1]
    ]


def provenance(catalog, name, what):
    """The four lines that say where a generated file came from.

    The source is named by content digest rather than by commit: the commit that
    lands a regenerated output is not the commit that last touched the catalog,
    so a commit stamp would be stale in the same commit that wrote it, and #358
    byte-compares these files against a fresh generator run.

    `name` is where the file BELONGS, not where this run happens to be writing
    it, so a generator pointed at a scratch tree still produces the bytes the
    committed artefact must match.
    """
    return f"""\
// =============================================================================
// {name}
//
// Auto-generated from {CATALOG_NAME} by {GENERATOR_NAME}
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 {catalog['digest']}
//
// {what}
// ============================================================================="""


def generate_firmware_header(catalog, output_path=None):
    """Write include/droid_parts.h - the id vocabulary and nothing else."""
    output_path = Path(output_path) if output_path else FIRMWARE_OUTPUT_PATH
    parts = firmware_parts(catalog)
    ids = [part["id"] for part in parts]
    longest = max((len(i) for i in ids), default=0)
    controls = sorted(
        {catalog["control_paths"][part["control"]][0] for part in parts}
    )

    lines = [
        provenance(
            catalog,
            FIRMWARE_NAME,
            "The Droid Parts Catalog's id vocabulary, and only that. A Part is\n"
            "// identity; an Output Address is only wiring, so there is no parts table\n"
            "// in firmware beyond these ids - which Output drives which Part is\n"
            "// answered by the Servo Output rows the builder's own droid stores\n"
            "// (#301). Names, shorthand, aliases and position live in the browser\n"
            "// module this generator writes beside this file; a rename there can\n"
            "// never produce a new id here.\n"
            "//\n"
            "// Only Parts the body drives are here. A dome-link Part, or one nothing\n"
            "// drives yet, reaches the browser alone: the dome owns execution of\n"
            "// panel intent under Catalog Authority, so firmware carries only ids it\n"
            "// can resolve to an Output of its own.",
        ),
        "",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <string.h>",
        "",
        '#include "droid_part_control.h"',
        "",
        "// One per control path this file emitted a Part under. The catalog does not",
        "// get to decide which paths reach firmware: a Part generated here whose",
        "// control path the firmware does not drive fails the build rather than",
        "// shipping an id no Output can ever claim.",
    ]
    for enumerator in controls:
        lines.append(
            f"static_assert(droidPartControlReachesFirmware({enumerator}),\n"
            f'              "a control path the firmware does not drive reached the "\n'
            f'              "generated id table: {enumerator}");'
        )
    lines += [
        "",
        f"constexpr size_t DROID_PART_COUNT = {len(ids)};",
        "",
        "// The longest id here, so a consumer sizing a buffer against the vocabulary",
        "// reads the number rather than counting the table.",
        f"constexpr size_t DROID_PART_ID_MAX_LEN = {longest};",
        "",
        "inline constexpr const char* const DROID_PART_IDS[DROID_PART_COUNT] = {",
    ]
    for part in parts:
        lines.append(f'    "{part["id"]}",  // {part["section"]}')
    lines += [
        "};",
        "",
        "// -----------------------------------------------------------------------------",
        "// droidPartIdIsKnown()",
        "// The Protocol Check vocabulary gate: is this a Part this build models at all.",
        "// It answers nothing about wiring - a known Part with no Output is still a",
        "// known Part, and saying so is the whole point (#301).",
        "// -----------------------------------------------------------------------------",
        "inline bool droidPartIdIsKnown(const char* id) {",
        "    if (id == nullptr || id[0] == '\\0') {",
        "        return false;",
        "    }",
        "    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {",
        "        if (strcmp(DROID_PART_IDS[i], id) == 0) {",
        "            return true;",
        "        }",
        "    }",
        "    return false;",
        "}",
        "",
        "// -----------------------------------------------------------------------------",
        "// droidPartIdAt()",
        "// The vocabulary in emission order, for a caller listing it. Returns an empty",
        "// string past the end rather than a null nobody checks.",
        "// -----------------------------------------------------------------------------",
        "inline const char* droidPartIdAt(size_t index) {",
        "    return (index < DROID_PART_COUNT) ? DROID_PART_IDS[index] : \"\";",
        "}",
        "",
    ]
    output_path.write_text("\n".join(lines), encoding="utf-8")
    return ids


# =============================================================================
# The browser module
# =============================================================================


def browser_part(part):
    """One part as the browser reads it: names first, absent fields absent."""
    record = {
        "index": part["index"],
        "id": part["id"],
        "section": part["section"],
        "name": part["name"],
    }
    if part["shorthand"] is not None and declared(part["shorthand"]):
        record["shorthand"] = part["shorthand"]
    record["aliases"] = part["aliases"]
    if part["position"] is not None and declared(part["position"]):
        record["position"] = part["position"]
    if part["bearing_deg"] is not None and declared(part["bearing_deg"]):
        record["bearingDeg"] = part["bearing_deg"]
    # `control` is emitted even when it is null: a row that declares no control
    # path at all is an orientation reference rather than something waiting to
    # be wired, and the browser has to be able to tell those apart.
    record["control"] = part["control"]
    # A `cad_name` that is explicitly null says the part HAS no CAD name, which
    # is what marks a Common Addition (ADR 0047); a `TBD` one says we have not
    # read it out of the design files yet and is omitted entirely.
    if part["cad_name"] is not ABSENT and declared(part["cad_name"]):
        record["cadName"] = part["cad_name"]
    if part["dome_link_panel"] is not None and declared(part["dome_link_panel"]):
        record["domeLinkPanel"] = part["dome_link_panel"]
    if part["unit"] is not None:
        record["unit"] = part["unit"]
    if part["axis"] is not None:
        record["axis"] = part["axis"]
    if part["lit"] is not None:
        record["lit"] = part["lit"]
    return record


def generate_browser_module(catalog, output_path=None):
    """Write data/droid_parts.js - data only, in one committed module."""
    output_path = Path(output_path) if output_path else BROWSER_OUTPUT_PATH
    payload = {
        "source": CATALOG_NAME,
        "generator": GENERATOR_NAME,
        "sourceSha256": catalog["digest"],
        "designs": catalog["designs"],
        "parts": [browser_part(part) for part in catalog["parts"]],
    }
    # ASCII-escaped on purpose. The catalog's blurbs carry em dashes, and this
    # module is served off LittleFS with no charset on the response, so an
    # escape is the one spelling that survives whatever the page decides its
    # encoding is. It is the same character either way.
    # Indented to sit inside the module wrapper, so the file reads as one block
    # rather than as JSON dropped into the middle of a function.
    body = json.dumps(payload, indent=2, ensure_ascii=True).replace("\n", "\n  ")
    header = f"""\
/**
 * {BROWSER_NAME}
 *
 * Auto-generated from {CATALOG_NAME} by {GENERATOR_NAME}
 * DO NOT EDIT MANUALLY
 *
 * Source digest: sha256 {catalog['digest']}
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

(function () {{
  'use strict';

  window.DroidParts = {body};
}})();
"""
    output_path.write_text(header, encoding="utf-8")
    return payload


# =============================================================================
# Entry point
# =============================================================================


def generate(quiet=False, catalog_path=None, firmware_path=None, browser_path=None,
             control_path=None, id_limit_path=None):
    """Regenerate both committed outputs. Raises CatalogError on a bad catalog.

    Every path is injectable so the generator can be aimed at a scratch tree
    without a copy of it existing anywhere: what it writes there is byte for
    byte what it would have written here.
    """
    catalog = load_catalog(catalog_path, control_path, id_limit_path)
    ids = generate_firmware_header(catalog, firmware_path)
    payload = generate_browser_module(catalog, browser_path)

    # Both orphan directions, the way tools/build.js checks its manifest: every
    # declared part must generate, and every generated id must trace back to a
    # declared part. A warning nobody reads is not a check, so these raise.
    declared_ids = [part["id"] for part in catalog["parts"]]
    emitted_ids = [part["id"] for part in payload["parts"]]
    if emitted_ids != declared_ids:
        raise CatalogError(
            [
                "browser module and catalog disagree about which parts exist: "
                f"missing {sorted(set(declared_ids) - set(emitted_ids))}, "
                f"unaccounted {sorted(set(emitted_ids) - set(declared_ids))}"
            ]
        )
    stray = sorted(set(ids) - set(declared_ids))
    if stray:
        raise CatalogError([f"firmware id table carries ids no part row declares: {stray}"])

    if not quiet:
        # The summary says where this run actually read and wrote, which is not
        # always where the files belong - the generated text says that.
        print(f"Loaded {rel(catalog_path or CATALOG_PATH)}: {len(declared_ids)} parts, "
              f"{len(catalog['designs'])} designs")
        print(f"Generated {rel(firmware_path or FIRMWARE_OUTPUT_PATH)}: {len(ids)} ids "
              f"({', '.join(ids) if ids else 'none'})")
        print(f"Generated {rel(browser_path or BROWSER_OUTPUT_PATH)}: "
              f"{len(emitted_ids)} parts")
    return catalog


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        epilog=REGENERATION_NOTE,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--quiet", action="store_true", help="write the outputs without the summary"
    )
    args = parser.parse_args(argv)
    try:
        generate(quiet=args.quiet)
    except CatalogError as error:
        print(f"{rel(CATALOG_PATH)} cannot be generated:", file=sys.stderr)
        for problem in error.problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
