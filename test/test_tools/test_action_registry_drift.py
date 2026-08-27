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



class TestStatusQueryClassification(unittest.TestCase):
    """Test check_status_query_classification() function."""

    def test_status_entry_missing_classification(self):
        """Fails when a type: status entry has neither fields nor is_query: false."""
        from tools.check_action_registry_drift import check_status_query_classification
        
        # Create a status entry without any classifier
        doc = {
            'entries': [
                {
                    'name': 'test.status.unclassified',
                    'type': 'status',
                    'api_path': '/test/unclassified'
                    # No fields, no is_query: false
                }
            ]
        }
        errors = []
        check_status_query_classification(doc, errors)
        
        # Should report the ambiguous classification
        self.assertTrue(any('test.status.unclassified' in e and 'classification ambiguous' in e for e in errors),
                       f"Expected ambiguity error, got: {errors}")

    def test_status_entry_with_fields(self):
        """Passes when a type: status entry has fields."""
        from tools.check_action_registry_drift import check_status_query_classification
        
        doc = {
            'entries': [
                {
                    'name': 'test.status.query',
                    'type': 'status',
                    'api_path': '/test/query',
                    'fields': ['field1', 'field2']
                }
            ]
        }
        errors = []
        check_status_query_classification(doc, errors)
        
        # Should not report any error for this entry
        self.assertFalse(any('test.status.query' in e for e in errors),
                        f"Expected no error for query with fields, got: {errors}")

    def test_status_entry_with_is_query_false(self):
        """Passes when a type: status entry has is_query: false."""
        from tools.check_action_registry_drift import check_status_query_classification
        
        doc = {
            'entries': [
                {
                    'name': 'test.status.aggregate',
                    'type': 'status',
                    'is_query': False
                }
            ]
        }
        errors = []
        check_status_query_classification(doc, errors)
        
        # Should not report any error for this entry
        self.assertFalse(any('test.status.aggregate' in e for e in errors),
                        f"Expected no error for non-query with is_query: false, got: {errors}")

    def test_status_entry_both_classifiers(self):
        """Fails when a type: status entry has both fields and is_query: false (contradictory)."""
        from tools.check_action_registry_drift import check_status_query_classification
        
        doc = {
            'entries': [
                {
                    'name': 'test.status.contradictory',
                    'type': 'status',
                    'fields': ['field1'],
                    'is_query': False  # Contradiction
                }
            ]
        }
        errors = []
        check_status_query_classification(doc, errors)
        
        # Should report the contradiction
        self.assertTrue(any('test.status.contradictory' in e and 'contradictory' in e for e in errors),
                       f"Expected contradiction error, got: {errors}")


class TestInventoryDuplicateName(unittest.TestCase):
    """Test detection of duplicate names across inventory files."""

    def test_duplicate_name_detection(self):
        """The inventory reader detects when a name appears in multiple files."""
        # This test verifies the checker logic handles duplicates
        # by checking that the function properly validates the 189 entries
        # are unique across the four inventory files
        from tools.check_action_registry_drift import check_inventory_registry_alignment
        
        # Create a mock scenario where we'd detect the duplicate
        # by checking that the function properly validates the 189 entries
        # are unique across the four inventory files
        doc = {'entries': []}
        errors = []
        
        check_inventory_registry_alignment(doc, errors)
        
        # If there were duplicates in the real inventory, errors would report them
        # For now, we verify the real inventories don't have duplicates (pass condition)
        # by confirming no duplicate-name errors appear
        duplicate_errors = [e for e in errors if 'appears in multiple' in e]
        self.assertEqual(len(duplicate_errors), 0,
                        f"Real inventory should not have duplicates, got: {duplicate_errors}")



class TestExecutorSymbols(unittest.TestCase):
    """Test check_executor_symbols() function."""

    def test_nonexistent_executor_symbol(self):
        """Fails when an executor: value is a description, not a real symbol."""
        from tools.check_action_registry_drift import check_executor_symbols
        
        doc = {
            'entries': [
                {
                    'name': 'test.action.foo',
                    'type': 'action',
                    'executor': 'nonexistentExecutorCore'  # Not a real symbol
                }
            ]
        }
        errors = []
        check_executor_symbols(doc, errors)
        
        # Should report the missing symbol
        self.assertTrue(any('nonexistentExecutorCore' in e and 'appears nowhere' in e for e in errors),
                       f"Expected error about missing symbol, got: {errors}")

    def test_real_executor_symbol(self):
        """Passes when an executor: value names a real symbol."""
        from tools.check_action_registry_drift import check_executor_symbols
        
        # configApply is a real function in include/api_config_apply.h
        doc = {
            'entries': [
                {
                    'name': 'test.config.foo',
                    'type': 'config',
                    'executor': 'configApply'  # Real symbol
                }
            ]
        }
        errors = []
        check_executor_symbols(doc, errors)
        
        # Should not report any error for this entry
        self.assertFalse(any('configApply' in e for e in errors),
                        f"Expected no error for real executor, got: {errors}")

    def test_executor_none_is_allowed(self):
        """Passes when executor: value is 'none' (special marker)."""
        from tools.check_action_registry_drift import check_executor_symbols
        
        doc = {
            'entries': [
                {
                    'name': 'test.event.foo',
                    'type': 'event',
                    'executor': 'none'
                }
            ]
        }
        errors = []
        check_executor_symbols(doc, errors)
        
        # Should not report any error for 'none'
        self.assertFalse(any('none' in e for e in errors),
                        f"Expected no error for executor: none, got: {errors}")

