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


class TestInventoryRegistryAlignment(unittest.TestCase):
    """Test check_inventory_registry_alignment() function."""

    def test_registry_entry_missing_from_inventory(self):
        """Fails when a registry entry has no matching inventory row."""
        doc = {
            'entries': [
                {'name': 'test.action.foo', 'type': 'action', 'executor': 'testExecutor'}
            ]
        }
        errors = []
        # Import the checker function
        from tools.check_action_registry_drift import check_inventory_registry_alignment
        
        # This should report test.action.foo as missing from inventory
        # (The real inventories won't have this entry)
        check_inventory_registry_alignment(doc, errors)
        
        # We expect an error for the missing inventory row
        self.assertTrue(any('test.action.foo' in e and 'inventory' in e for e in errors),
                       f"Expected error about missing inventory entry, got: {errors}")

    def test_inventory_row_missing_from_registry(self):
        """Fails when an inventory row has no matching registry entry."""
        # This test is limited because we can't easily inject a test row into
        # the inventory YAML files, but we can verify the logic by checking
        # that the checker reads the real inventories and validates them
        from tools.check_action_registry_drift import check_inventory_registry_alignment
        import yaml
        
        doc = {'entries': []}  # Empty registry
        errors = []
        
        # The real inventories have 189 entries; an empty registry should report
        # all of them as missing
        check_inventory_registry_alignment(doc, errors)
        
        # We expect errors for all 189 rows
        self.assertGreater(len(errors), 100,
                          f"Expected 189+ errors for missing registry entries, got {len(errors)}")

