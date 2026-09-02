#!/usr/bin/env python3
"""Shared YAML loader for docs/action-registry.yaml (#249).

PyYAML's SafeLoader implements the YAML 1.1 implicit boolean resolver, which
covers the full `on|On|ON|off|Off|OFF|yes|Yes|YES|no|No|NO|true|True|TRUE|
false|False|FALSE` word list (see yaml.resolver.Resolver.add_implicit_resolver
call for `tag:yaml.org,2002:bool` in the installed PyYAML - read at
/usr/lib/python3.14/site-packages/yaml/resolver.py while fixing this). A bare
`off` in the registry is therefore not the string "off", it is the Python
bool `False`. That is exactly what corrupted `aux.action.led-effect`'s enum:
a prior YAML round-trip read `off` as `False` and wrote it back as `false`,
and the generator's `str(value)` then shipped the literal text "False" into
the console catalog.

The registry's single-letter `Y`/`N` enum values (sound.api.play-banked's
bank enum) survive today only because PyYAML's own resolver never implements
the YAML 1.1 spec's single-letter y/n/Y/N boolean forms - a parser
implementation quirk, not a language guarantee. A different YAML 1.1 reader
(or a future PyYAML release) is free to add them.

RegistryYamlLoader keeps the tokens the registry legitimately uses as real
booleans (`required: true`, `safety_critical: false`, ...) - true/false and
their case variants - but removes on/off/yes/no/y/n from implicit bool
resolution entirely, so those tokens always parse as plain strings unless a
YAML author explicitly tags one `!!bool`. Every tool that reads the registry
must call `load_registry_yaml()` here rather than `yaml.safe_load()`
directly, or this class of bug returns the next time someone writes an
unquoted on/off/yes/no/y/n enum value.
"""

import re

import yaml


class RegistryYamlLoader(yaml.SafeLoader):
    """SafeLoader with the boolean implicit resolver narrowed to true/false."""


# Drop every implicit resolver PyYAML's SafeLoader/Resolver registered for
# tag:yaml.org,2002:bool (the on/off/yes/no/true/false family) and replace it
# with one that recognizes only true/false and their case variants. Assigning
# `yaml_implicit_resolvers` directly (rather than mutating in place) puts the
# dict in RegistryYamlLoader.__dict__ first, so add_implicit_resolver's own
# copy-on-write check (`if not 'yaml_implicit_resolvers' in cls.__dict__`,
# yaml/resolver.py) sees it already there and appends to *this* subclass's
# copy - SafeLoader and yaml.Resolver itself are never touched.
RegistryYamlLoader.yaml_implicit_resolvers = {
    first_char: [
        (tag, regexp)
        for tag, regexp in resolvers
        if tag != 'tag:yaml.org,2002:bool'
    ]
    for first_char, resolvers in RegistryYamlLoader.yaml_implicit_resolvers.items()
}

RegistryYamlLoader.add_implicit_resolver(
    'tag:yaml.org,2002:bool',
    re.compile(r'^(?:true|True|TRUE|false|False|FALSE)$'),
    list('tTfF'),
)


def load_registry_yaml(path_or_stream):
    """Load a registry-style YAML file with boolean coercion narrowed.

    Accepts either a path (str/Path) or an already-open file handle, matching
    the two call shapes the existing tools use (`open(...).read()` vs a bare
    path).
    """
    if hasattr(path_or_stream, 'read'):
        return yaml.load(path_or_stream, Loader=RegistryYamlLoader)
    with open(path_or_stream, 'r', encoding='utf-8') as handle:
        return yaml.load(handle, Loader=RegistryYamlLoader)
