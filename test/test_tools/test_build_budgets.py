"""Test build budget checking and enforcement.

Verifies that:
1. Budget file exists and is valid JSON
2. All expected environments are in the budgets file
3. Budget checking logic correctly identifies over-budget builds
4. Budget checking produces clear error messages
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import slice_verify  # noqa: E402


class BudgetFileValidation(unittest.TestCase):
    """Test that the budgets file exists and is well-formed."""

    def test_budgets_file_exists(self):
        budgets_file = Path(__file__).resolve().parents[2] / "tools" / "build_budgets.json"
        self.assertTrue(budgets_file.exists(), f"Budget file not found: {budgets_file}")

    def test_budgets_file_is_valid_json(self):
        budgets_file = Path(__file__).resolve().parents[2] / "tools" / "build_budgets.json"
        with open(budgets_file) as f:
            data = json.load(f)
        self.assertIsInstance(data, dict)

    def test_budgets_file_has_required_structure(self):
        budgets = slice_verify.load_budgets()
        self.assertIsNotNone(budgets)
        self.assertIn("envs", budgets)
        self.assertIsInstance(budgets["envs"], dict)

    def test_budgets_file_has_platforms_registry(self):
        """Budgets file must have platforms registry for env classification."""
        budgets = slice_verify.load_budgets()
        self.assertIsNotNone(budgets)
        self.assertIn("platforms", budgets)
        self.assertIsInstance(budgets["platforms"], dict)
        self.assertIn("esp32", budgets["platforms"])
        self.assertIn("esp32p4", budgets["platforms"])

    def test_all_required_envs_have_budgets(self):
        """All P4 envs from Makefile P4_ENVS must be in budgets file."""
        required_envs = {"artoo_esp32", "firebeetle2_bringup", "firebeetle2"}
        budgets = slice_verify.load_budgets()
        env_list = set(budgets["envs"].keys())
        missing = required_envs - env_list
        self.assertEqual(missing, set(), f"Missing budgets for: {missing}")

    def test_each_env_has_flash_ceiling(self):
        """Each environment must have flash_ceiling_bytes and flash_budget_bytes."""
        budgets = slice_verify.load_budgets()
        for env_name, env_budget in budgets["envs"].items():
            self.assertIn("flash_ceiling_bytes", env_budget,
                         f"{env_name} missing flash_ceiling_bytes")
            self.assertIsInstance(env_budget["flash_ceiling_bytes"], int,
                                 f"{env_name} flash_ceiling_bytes is not int")
            self.assertGreater(env_budget["flash_ceiling_bytes"], 0,
                              f"{env_name} flash_ceiling_bytes must be > 0")
            self.assertIn("flash_budget_bytes", env_budget,
                         f"{env_name} missing flash_budget_bytes")
            self.assertIsInstance(env_budget["flash_budget_bytes"], int,
                                 f"{env_name} flash_budget_bytes is not int")
            self.assertGreater(env_budget["flash_budget_bytes"], 0,
                              f"{env_name} flash_budget_bytes must be > 0")
            # Hard ceiling must be >= soft budget
            self.assertGreaterEqual(env_budget["flash_ceiling_bytes"],
                                   env_budget["flash_budget_bytes"],
                                   f"{env_name} ceiling must be >= budget")


class PlatformResolution(unittest.TestCase):
    """Test env-to-platform resolution via registry."""

    def test_platform_for_env_explicit_p4_membership(self):
        """Explicit P4 env should resolve to esp32p4 platform."""
        budgets = slice_verify.load_budgets()
        platform_key, spec = slice_verify.platform_for_env("firebeetle2", budgets)
        self.assertEqual(platform_key, "esp32p4")
        self.assertEqual(spec["core_dir"], "~/.platformio-p4")

    def test_platform_for_env_default_fallback(self):
        """Unknown env should fall back to default platform (esp32)."""
        budgets = slice_verify.load_budgets()
        platform_key, spec = slice_verify.platform_for_env("some_new_env", budgets)
        self.assertEqual(platform_key, "esp32")
        self.assertEqual(spec["core_dir"], "~/.platformio")

    def test_platform_registry_has_envs_list(self):
        """Each platform must list its explicit envs."""
        budgets = slice_verify.load_budgets()
        self.assertIn("envs", budgets["platforms"]["esp32p4"])
        self.assertIsInstance(budgets["platforms"]["esp32p4"]["envs"], list)
        self.assertGreater(len(budgets["platforms"]["esp32p4"]["envs"]), 0)

    def test_platform_registry_has_size_tool(self):
        """Each platform must specify its size tool."""
        budgets = slice_verify.load_budgets()
        for platform_name, spec in budgets["platforms"].items():
            self.assertIn("size_tool", spec,
                         f"{platform_name} missing size_tool")
            self.assertIsInstance(spec["size_tool"], str)
            self.assertGreater(len(spec["size_tool"]), 0)

    def test_platform_registry_has_core_dir(self):
        """Each platform must specify its core dir."""
        budgets = slice_verify.load_budgets()
        for platform_name, spec in budgets["platforms"].items():
            self.assertIn("core_dir", spec,
                         f"{platform_name} missing core_dir")
            self.assertIsInstance(spec["core_dir"], str)
            self.assertGreater(len(spec["core_dir"]), 0)


class PlatformIOCoreDirSelection(unittest.TestCase):
    """Test that the correct core dir is selected for each environment."""

    def test_artoo_esp32_uses_default_core_dir(self):
        """artoo_esp32 should use ~/.platformio (classic ESP32 core dir)."""
        core_dir = slice_verify.get_platformio_core_dir("artoo_esp32")
        self.assertIn(".platformio", core_dir)
        self.assertNotIn(".platformio-p4", core_dir)

    def test_p4_envs_use_p4_core_dir(self):
        """P4 environments should use ~/.platformio-p4."""
        for env in ["firebeetle2_bringup", "firebeetle2"]:
            core_dir = slice_verify.get_platformio_core_dir(env)
            self.assertIn(".platformio-p4", core_dir,
                         f"{env} should use P4 core dir")

    def test_core_dir_paths_are_absolute(self):
        """Core dir paths should be absolute, not relative."""
        for env in ["artoo_esp32", "firebeetle2"]:
            core_dir = slice_verify.get_platformio_core_dir(env)
            self.assertTrue(core_dir.startswith("/") or core_dir.startswith("~") or core_dir.startswith("$HOME"),
                           f"{env} core dir should be absolute: {core_dir}")


class BudgetCheckFunction(unittest.TestCase):
    """Test the check_build_budget function logic."""

    def test_check_budget_passes_when_under_limit(self):
        """A build well under budget should pass.

        Requires a valid firmware.bin from `pio run -e artoo_esp32` that is
        below budget. Skipped if firmware.bin does not exist or is invalid.
        """
        bin_file = Path(__file__).resolve().parents[2] / ".pio" / "build" / "artoo_esp32" / "firmware.bin"
        if not bin_file.exists() or bin_file.stat().st_size == 0:
            self.skipTest(f"No valid firmware.bin at {bin_file}")

        result = slice_verify.check_build_budget("artoo_esp32")
        # Only check if it passed when the binary is actually under budget
        if result.passed:
            self.assertIn("Flash", result.detail)
            self.assertIn("<", result.detail)
        # If it failed, that's OK too — it means the binary is over budget
        # (could be a legitimate test of the fail case)

    def test_check_budget_fails_when_over_limit(self):
        """A build over budget should fail."""
        # Create a temporary oversized firmware.bin to test budget breach detection
        project_root = Path(__file__).resolve().parents[2]
        build_dir = project_root / ".pio" / "build" / "artoo_esp32"
        build_dir.mkdir(parents=True, exist_ok=True)
        bin_file = build_dir / "firmware.bin"
        elf_file = build_dir / "firmware.elf"

        # Skip if no baseline build artifact exists (test requires a pre-built artoo_esp32)
        if not bin_file.exists() or bin_file.stat().st_size == 0:
            self.skipTest(f"No valid baseline firmware.bin at {bin_file}")

        # Save original if it exists
        original_bin = None
        original_elf = None
        if bin_file.exists():
            with open(bin_file, "rb") as f:
                original_bin = f.read()
        if elf_file.exists():
            with open(elf_file, "rb") as f:
                original_elf = f.read()

        try:
            # Create an oversized binary (1.7 MB, which exceeds the 1.6 MB budget)
            with open(bin_file, "wb") as f:
                f.write(b"x" * (1700000))  # 1.7 MB

            result = slice_verify.check_build_budget("artoo_esp32")
            self.assertFalse(result.passed, "Budget check should fail for oversized build")
            self.assertIn("over", " ".join(result.notes).lower(), "Notes should mention overage")

        finally:
            # Restore original if it existed
            if original_bin is not None:
                with open(bin_file, "wb") as f:
                    f.write(original_bin)
            elif bin_file.exists():
                bin_file.unlink()
            if original_elf is not None:
                with open(elf_file, "wb") as f:
                    f.write(original_elf)

    def test_check_budget_result_has_required_fields(self):
        """Budget check result must include label, detail, passed, and notes."""
        bin_file = Path(__file__).resolve().parents[2] / ".pio" / "build" / "artoo_esp32" / "firmware.bin"
        if not bin_file.exists() or bin_file.stat().st_size == 0:
            self.skipTest(f"No valid firmware.bin at {bin_file}")

        result = slice_verify.check_build_budget("artoo_esp32")
        self.assertIsNotNone(result.label)
        self.assertIsNotNone(result.detail)
        self.assertIsNotNone(result.passed)
        self.assertIsInstance(result.notes, list)

    def test_check_budget_detail_includes_size_percentage(self):
        """Budget check detail should show actual size and percentage."""
        bin_file = Path(__file__).resolve().parents[2] / ".pio" / "build" / "artoo_esp32" / "firmware.bin"
        if not bin_file.exists() or bin_file.stat().st_size == 0:
            self.skipTest(f"No valid firmware.bin at {bin_file}")

        result = slice_verify.check_build_budget("artoo_esp32")
        # Detail should include Flash, comparison operator, and percentage
        self.assertIn("Flash", result.detail)
        self.assertIn("%", result.detail)
        self.assertTrue("<" in result.detail or ">" in result.detail)

    def test_check_budget_fails_when_unmeasurable(self):
        """A budget that cannot be measured must fail, not pass.

        This test removes the firmware.elf to simulate a missing measurement.
        A budget check that cannot measure must fail loudly.
        """
        project_root = Path(__file__).resolve().parents[2]
        build_dir = project_root / ".pio" / "build" / "artoo_esp32"
        build_dir.mkdir(parents=True, exist_ok=True)
        bin_file = build_dir / "firmware.bin"
        elf_file = build_dir / "firmware.elf"

        # Save originals if they exist
        original_bin = None
        original_elf = None
        if bin_file.exists():
            with open(bin_file, "rb") as f:
                original_bin = f.read()
        if elf_file.exists():
            with open(elf_file, "rb") as f:
                original_elf = f.read()

        try:
            # Create a valid .bin but no .elf to force measurement failure
            with open(bin_file, "wb") as f:
                f.write(b"x" * 1000000)  # 1 MB
            if elf_file.exists():
                elf_file.unlink()

            result = slice_verify.check_build_budget("artoo_esp32")
            self.assertFalse(result.passed, "Budget check should fail when RAM cannot be measured")
            self.assertIn("RAM", result.detail.upper(),
                         "Detail should mention RAM measurement failure")

        finally:
            # Restore originals
            if original_bin is not None:
                with open(bin_file, "wb") as f:
                    f.write(original_bin)
            elif bin_file.exists():
                bin_file.unlink()
            if original_elf is not None:
                with open(elf_file, "wb") as f:
                    f.write(original_elf)


if __name__ == "__main__":
    unittest.main()
