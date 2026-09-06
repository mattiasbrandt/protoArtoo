"""Pinned behavior for tools/registry_yaml.py - the #249 boolean-coercion fix.

PyYAML's SafeLoader treats on/off/yes/no/y/n/true/false (any case) as the
YAML 1.1 boolean vocabulary. RegistryYamlLoader narrows that to true/false
only, so every other member of the family survives as a plain string. These
tests pin that split directly against the loader (not the registry file
content), so a change to registry_yaml.py that reopens the coercion path
fails here even if docs/action-registry.yaml happens to have no affected
value at the time.
"""

import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import yaml  # noqa: E402

import registry_yaml  # noqa: E402


YAML11_BOOL_WORDS = [
    "on", "On", "ON", "off", "Off", "OFF",
    "yes", "Yes", "YES", "no", "No", "NO",
    "y", "Y", "n", "N",
]

REAL_BOOL_WORDS = ["true", "True", "TRUE", "false", "False", "FALSE"]


class RegistryYamlLoaderBooleanNarrowing(unittest.TestCase):
    def test_yaml11_bool_words_stay_strings(self):
        """on/off/yes/no/y/n (any case) must never coerce to bool."""
        sample = "values:\n" + "".join(f"- {word}\n" for word in YAML11_BOOL_WORDS)
        doc = registry_yaml.load_registry_yaml(io.StringIO(sample))
        values = doc["values"]
        self.assertEqual(values, YAML11_BOOL_WORDS)
        for value in values:
            self.assertIsInstance(value, str, f"{value!r} should be a str, not a bool")

    def test_true_false_still_coerce_to_real_booleans(self):
        """true/false (any case) must keep coercing - required:/safety_critical:
        depend on it staying a real bool, not becoming the string "true"."""
        sample = "values:\n" + "".join(f"- {word}\n" for word in REAL_BOOL_WORDS)
        doc = registry_yaml.load_registry_yaml(io.StringIO(sample))
        values = doc["values"]
        self.assertEqual(values, [True, True, True, False, False, False])
        for value in values:
            self.assertIsInstance(value, bool)

    def test_real_registry_boolean_fields_still_load_as_bool(self):
        """Sanity check against real field names the registry depends on."""
        sample = "required: true\nsafety_critical: false\n"
        doc = registry_yaml.load_registry_yaml(io.StringIO(sample))
        self.assertIs(doc["required"], True)
        self.assertIs(doc["safety_critical"], False)

    def test_vanilla_safe_load_is_unaffected(self):
        """RegistryYamlLoader must not mutate yaml.SafeLoader/yaml.Resolver
        as a side effect of narrowing its own subclass's resolver table -
        every other consumer of yaml.safe_load() in this repo (or any other
        process importing PyYAML) keeps YAML 1.1 bool coercion."""
        plain = yaml.safe_load(io.StringIO("x: off"))
        self.assertIs(plain["x"], False)

    def test_load_registry_yaml_accepts_a_path_and_a_stream(self):
        """load_registry_yaml() must accept both call shapes the two
        registry-reading tools use (a path string, and an open handle)."""
        sample = "entries:\n- name: test\n"
        from_stream = registry_yaml.load_registry_yaml(io.StringIO(sample))
        self.assertEqual(from_stream, {"entries": [{"name": "test"}]})

        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False) as handle:
            handle.write(sample)
            temp_path = handle.name
        try:
            from_path = registry_yaml.load_registry_yaml(temp_path)
            self.assertEqual(from_path, {"entries": [{"name": "test"}]})
        finally:
            Path(temp_path).unlink()


if __name__ == "__main__":
    unittest.main()
