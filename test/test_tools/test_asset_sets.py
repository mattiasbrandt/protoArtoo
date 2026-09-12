#!/usr/bin/env python3
"""Asset sets: which pictures a build environment's filesystem image carries.

ADR 0065 -- only what is SHOWN may differ between boards. A file declares which
builds carry it by which set directory it sits in, so the fact lives beside the
file instead of in a list that goes stale. These tests cover the staging rule, the declarations, and the
8 KiB per-photograph cap now that the default set carries pictures (#316).
"""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GZIP_FSDATA = ROOT / "tools" / "gzip_fsdata.py"
DATA = ROOT / "data"


def _config():
    from platformio.project.config import ProjectConfig

    return ProjectConfig(str(ROOT / "platformio.ini"))


class AssetSetDeclarations(unittest.TestCase):
    """Every environment that images a filesystem names exactly one set."""

    @classmethod
    def setUpClass(cls):
        try:
            cls.config = _config()
        except ImportError:  # pragma: no cover - depends on the runner
            raise unittest.SkipTest("platformio is not installed; this check needs its config parser")

    def test_every_env_resolves_to_a_known_set(self):
        known = {"legacy", "default"}
        for env in self.config.envs():
            if self.config.get(f"env:{env}", "platform", "") == "native":
                continue
            value = self.config.get(f"env:{env}", "custom_asset_set", "default")
            self.assertIn(value, known, f"{env} names asset set {value!r}, which is not one of {known}")

    def test_artoo_carries_legacy_and_p4_carries_default(self):
        """The P4 family extends env:artoo_esp32, so without its own declaration it
        would silently inherit the 4 MB board's set -- the exact trap this checks."""
        self.assertEqual(self.config.get("env:artoo_esp32", "custom_asset_set", "default"), "legacy")
        for env in ("firebeetle2", "firebeetle2_bringup"):
            self.assertEqual(
                self.config.get(f"env:{env}", "custom_asset_set", "default"),
                "default",
                f"{env} inherited the artoo-esp32 asset set instead of declaring its own",
            )

    def test_every_artoo_variant_inherits_legacy(self):
        for env in self.config.envs():
            if not env.startswith("artoo_esp32"):
                continue
            self.assertEqual(
                self.config.get(f"env:{env}", "custom_asset_set", "default"),
                "legacy",
                f"{env} does not carry the legacy set",
            )


class AssetSetStaging(unittest.TestCase):
    """The staging rule in tools/gzip_fsdata.py."""

    def test_set_directories_are_not_imaged_as_ordinary_data(self):
        """A set directory is staged by its own pass. Were it also walked as part
        of the common tree, every build would carry every set -- which is the cost
        this whole mechanism exists to avoid."""
        source = GZIP_FSDATA.read_text(encoding="utf-8")
        self.assertIn("ASSET_SETS_DIR", source)
        self.assertIn("rel.startswith(ASSET_SETS_DIR + os.sep)", source)

    def test_a_named_set_that_does_not_exist_is_a_hard_failure(self):
        """A typo must not ship an image quietly missing every picture, on a
        surface whose pictures are what an operator selects by."""
        source = GZIP_FSDATA.read_text(encoding="utf-8")
        self.assertIn("raise SystemExit", source)
        self.assertIn("custom_asset_set is", source)

    def test_both_sets_exist_together(self):
        """An environment naming a missing set fails the build, so both
        directories have to be present once either is. Photographs live in
        default (#316); drawings in legacy are #382's."""
        sets_root = DATA / "asset-sets"
        self.assertTrue(sets_root.is_dir(), "data/asset-sets/ must exist")
        present = sorted(d.name for d in sets_root.iterdir() if d.is_dir())
        self.assertEqual(
            present,
            ["default", "legacy"],
            "both sets must exist together: an environment naming a missing set fails the build",
        )

    def test_default_photographs_fit_two_littlefs_blocks(self):
        """8 KiB is two 4 KiB blocks. 9 KiB would be three (#316, ADR 0065)."""
        default = DATA / "asset-sets" / "default"
        photos = sorted(default.glob("*.webp"))
        self.assertGreaterEqual(len(photos), 1)
        for path in photos:
            size = path.stat().st_size
            self.assertLessEqual(
                size,
                8192,
                f"{path.name} is {size} B, over the 8 KiB block-boundary cap",
            )


if __name__ == "__main__":
    unittest.main()
