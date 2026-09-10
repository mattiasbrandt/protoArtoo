"""The `designs:` block in docs/droid-parts.yaml stays consistent with the parts (#337).

ADR 0047 put one rule in this file: a design SEEDS a complement and never
fences it. Two things follow that a reader cannot check by eye across 42 part
rows and a 38-id seed list - every seeded id must resolve to a part declared
once below, and the parts that belong to no design (the four Common Additions
with `cad_name: null`, and the `other1`..`other10` escape hatch) must stay
unclaimed. Both are asserted here, against the real file, because the catalog
is the source A1b generates from: a typo in a seed list would otherwise reach
firmware as a missing part rather than as a failure here.

`seeds: TBD` is a declared unknown, not a hole to fill in - the split between
MrBaddeley's simple and complex MK4 exports is recorded nowhere in this
repository. It is asserted as the ONLY permitted non-list form so that a
future scalar typo is still caught.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import registry_yaml  # noqa: E402

CATALOG = ROOT / "docs" / "droid-parts.yaml"

# Sections that declare parts, in the order the file carries them. Dome and
# body halves are told apart by the section a part sits in, which is why no
# part row repeats that fact.
PART_SECTIONS = (
    "dome_pies",
    "dome_panels",
    "holoprojectors",
    "dome_fixtures",
    "body_doors",
    "body_arms",
)

DESIGN_FIELDS = ("id", "label", "short", "blurb")

# The narrowed loader from #249: the catalog carries unquoted word enums
# (`control: none`, `position: front`, `axis: pan`) exactly as the action
# registry does, so the YAML 1.1 on/off/yes/no/y/n boolean family must not
# coerce here either.
def load_catalog():
    with CATALOG.open(encoding="utf-8") as handle:
        return registry_yaml.load_registry_yaml(handle)


def declared_part_ids(doc):
    ids = []
    for section in PART_SECTIONS:
        for row in doc[section]:
            ids.append(row["id"])
    return ids


def common_addition_ids(doc):
    """Parts belonging to no design: `cad_name` present and null (ADR 0047)."""
    return [
        row["id"]
        for section in PART_SECTIONS
        for row in doc[section]
        if "cad_name" in row and row["cad_name"] is None
    ]


def seed_groups(design):
    """(variant id or None, seeds) for each thing that can carry a complement."""
    if "variants" in design:
        return [(v["id"], v["seeds"]) for v in design["variants"]]
    return [(None, design["seeds"])]


class DroidPartsDesigns(unittest.TestCase):
    def setUp(self):
        self.doc = load_catalog()
        self.designs = self.doc["designs"]
        self.part_ids = declared_part_ids(self.doc)

    def test_parts_are_declared_once(self):
        self.assertEqual(len(self.part_ids), len(set(self.part_ids)))

    def test_design_rows_carry_the_four_card_fields(self):
        for design in self.designs:
            for field in DESIGN_FIELDS:
                self.assertIn(field, design, f"{design.get('id')!r} is missing {field}")
                self.assertTrue(str(design[field]).strip(), f"{design['id']}.{field} is empty")

    def test_design_and_variant_ids_are_unique(self):
        ids = [d["id"] for d in self.designs]
        self.assertEqual(len(ids), len(set(ids)))
        for design in self.designs:
            variant_ids = [v["id"] for v in design.get("variants", [])]
            self.assertEqual(len(variant_ids), len(set(variant_ids)), design["id"])

    def test_a_design_with_no_variants_omits_the_key(self):
        """No empty variant list: the second control disappears rather than emptying."""
        for design in self.designs:
            if "variants" in design:
                self.assertTrue(design["variants"], f"{design['id']} has an empty variants list")
                self.assertNotIn("seeds", design, f"{design['id']} seeds both per-design and per-variant")
            else:
                self.assertIn("seeds", design, f"{design['id']} declares no complement at all")

    def test_a_design_with_variants_declares_which_one_is_default(self):
        """#356: one `default_variant:` per design, and none where there is no axis."""
        for design in self.designs:
            if "variants" in design:
                self.assertIn(
                    "default_variant", design, f"{design['id']} declares no default_variant"
                )
                variant_ids = [v["id"] for v in design["variants"]]
                self.assertIn(
                    design["default_variant"],
                    variant_ids,
                    f"{design['id']} defaults to a variant it does not declare",
                )
            else:
                self.assertNotIn(
                    "default_variant",
                    design,
                    f"{design['id']} has no variants but declares a default",
                )

    def test_the_default_variant_seeds_something(self):
        """Nobody is pre-selected onto an unknown or empty complement (#356)."""
        for design in self.designs:
            if "variants" not in design:
                continue
            default = design["default_variant"]
            seeds = next(v["seeds"] for v in design["variants"] if v["id"] == default)
            self.assertIsInstance(
                seeds, list, f"{design['id']}/{default} is the default but its seeds are {seeds!r}"
            )
            self.assertTrue(seeds, f"{design['id']}/{default} is the default but seeds nothing")

    def test_every_seeded_id_is_a_declared_part(self):
        for design in self.designs:
            for variant_id, seeds in seed_groups(design):
                where = f"{design['id']}/{variant_id}"
                if seeds == "TBD":
                    continue
                self.assertIsInstance(seeds, list, f"{where}: seeds is neither a list nor TBD")
                self.assertEqual(len(seeds), len(set(seeds)), f"{where} seeds an id twice")
                unknown = [i for i in seeds if i not in self.part_ids]
                self.assertEqual(unknown, [], f"{where} seeds ids no part row declares: {unknown}")

    def test_common_additions_are_claimed_by_no_design(self):
        common = common_addition_ids(self.doc)
        self.assertEqual(sorted(common), ["gripArm", "gripClaw", "interArm", "interTool"])
        for design in self.designs:
            for variant_id, seeds in seed_groups(design):
                if seeds == "TBD":
                    continue
                claimed = sorted(set(seeds) & set(common))
                self.assertEqual(
                    claimed, [], f"{design['id']}/{variant_id} seeds Common Additions: {claimed}"
                )

    def test_other_slots_sit_outside_every_design(self):
        prefix = self.doc["other_slots"]["id_prefix"]
        self.assertNotIn(prefix, self.part_ids)
        for design in self.designs:
            for variant_id, seeds in seed_groups(design):
                if seeds == "TBD":
                    continue
                escapes = sorted(i for i in seeds if i.startswith(prefix))
                self.assertEqual(
                    escapes, [], f"{design['id']}/{variant_id} seeds escape-hatch slots: {escapes}"
                )

    def test_the_single_lineage_model_block_is_gone(self):
        """ADR 0047: the `model:` block becomes one design entry among several."""
        self.assertNotIn("model", self.doc)
        self.assertIn("mk4", [d["id"] for d in self.designs])


if __name__ == "__main__":
    unittest.main()
