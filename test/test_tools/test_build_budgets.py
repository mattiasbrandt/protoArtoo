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

    def test_all_required_envs_have_budgets(self):
        """All P4 envs from Makefile P4_ENVS must be in budgets file."""
        required_envs = {"artoo_esp32", "firebeetle2", "firebeetle2_hosted_bench", "firebeetle2_full"}
        budgets = slice_verify.load_budgets()
        env_list = set(budgets["envs"].keys())
        missing = required_envs - env_list
        self.assertEqual(missing, set(), f"Missing budgets for: {missing}")

    def test_each_env_has_flash_ceiling(self):
        """Each environment must have a flash_budget_bytes value."""
        budgets = slice_verify.load_budgets()
        for env_name, env_budget in budgets["envs"].items():
            self.assertIn("flash_budget_bytes", env_budget,
                         f"{env_name} missing flash_budget_bytes")
            self.assertIsInstance(env_budget["flash_budget_bytes"], int,
                                 f"{env_name} flash_budget_bytes is not int")
            self.assertGreater(env_budget["flash_budget_bytes"], 0,
                              f"{env_name} flash_budget_bytes must be > 0")


class PlatformIOCoreDirSelection(unittest.TestCase):
    """Test that the correct core dir is selected for each environment."""

    def test_artoo_esp32_uses_default_core_dir(self):
        """artoo_esp32 should use ~/.platformio (classic ESP32 core dir)."""
        core_dir = slice_verify.get_platformio_core_dir("artoo_esp32")
        self.assertIn(".platformio", core_dir)
        self.assertNotIn(".platformio-p4", core_dir)

    def test_p4_envs_use_p4_core_dir(self):
        """P4 environments should use ~/.platformio-p4."""
        for env in ["firebeetle2", "firebeetle2_hosted_bench", "firebeetle2_full"]:
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
        """A build well under budget should pass."""
        # This requires a built firmware.bin, which should exist after `pio run -e artoo_esp32`
        # The gate self-tests run after the build, so firmware.bin is available.
        result = slice_verify.check_build_budget("artoo_esp32")
        self.assertTrue(result.passed, f"Budget check failed: {result.notes}")

    def test_check_budget_fails_when_over_limit(self):
        """A build over budget should fail."""
        # Create a temporary oversized firmware.bin to test budget breach detection
        project_root = Path(__file__).resolve().parents[2]
        build_dir = project_root / ".pio" / "build" / "artoo_esp32"
        build_dir.mkdir(parents=True, exist_ok=True)
        bin_file = build_dir / "firmware.bin"

        # Save original if it exists
        original_content = None
        if bin_file.exists():
            with open(bin_file, "rb") as f:
                original_content = f.read()

        try:
            # Create an oversized binary (1.7 MB, which exceeds the 1.6 MB budget)
            with open(bin_file, "wb") as f:
                f.write(b"x" * (1700000))  # 1.7 MB

            result = slice_verify.check_build_budget("artoo_esp32")
            self.assertFalse(result.passed, "Budget check should fail for oversized build")
            self.assertIn("over", " ".join(result.notes).lower(), "Notes should mention overage")

        finally:
            # Restore original if it existed
            if original_content is not None:
                with open(bin_file, "wb") as f:
                    f.write(original_content)
            elif bin_file.exists():
                bin_file.unlink()

    def test_check_budget_result_has_required_fields(self):
        """Budget check result must include label, detail, passed, and notes."""
        result = slice_verify.check_build_budget("artoo_esp32")
        self.assertIsNotNone(result.label)
        self.assertIsNotNone(result.detail)
        self.assertIsNotNone(result.passed)
        self.assertIsInstance(result.notes, list)

    def test_check_budget_detail_includes_size_percentage(self):
        """Budget check detail should show actual size and percentage."""
        result = slice_verify.check_build_budget("artoo_esp32")
        # Detail should have format like "1569152 bytes < 1600000 (98.1%)"
        self.assertIn("bytes", result.detail)
        self.assertIn("%", result.detail)


if __name__ == "__main__":
    unittest.main()
