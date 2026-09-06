"""Focused parser coverage for nullable C++ action requirements."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check_action_registry_drift  # noqa: E402


class ActionRegistryRequirementParsing(unittest.TestCase):
    def test_requirement_fields_are_independently_nullable(self):
        source = """
const ActionEntry ACTION_REGISTRY[] = {
    { ACTION_UNIVERSAL, "universal", "Universal", "system", "Universal row", false },
    { ACTION_BOARD, "board", "Board", "system", "Board row", false, "PA_CAP_NATIVE_WIFI" },
    { ACTION_BUILD, "build", "Build", "system", "Build row", false, nullptr, "PA_HEAP_PROFILE" },
    { ACTION_BOTH, "both", "Both", "system", "Both row", true, "PA_CAP_NATIVE_WIFI", "PA_HEAP_PROFILE" },
};
"""
        with tempfile.TemporaryDirectory() as tmp:
            registry = Path(tmp) / "action_registry.cpp"
            registry.write_text(source, encoding="utf-8")
            parsed = check_action_registry_drift.parse_action_registry(registry)

        self.assertEqual(parsed["ACTION_UNIVERSAL"][5:], (None, None))
        self.assertEqual(parsed["ACTION_BOARD"][5:], ("PA_CAP_NATIVE_WIFI", None))
        self.assertEqual(parsed["ACTION_BUILD"][5:], (None, "PA_HEAP_PROFILE"))
        self.assertEqual(
            parsed["ACTION_BOTH"][5:], ("PA_CAP_NATIVE_WIFI", "PA_HEAP_PROFILE")
        )


if __name__ == "__main__":
    unittest.main()
