#!/usr/bin/env python3
"""
Generate console operation catalog and help text from action-registry.yaml.

This tool produces:
1. include/console_catalog.h - C++ header with operation descriptors
2. src/console/console_catalog.cpp - C++ implementation with the catalog table
3. data/console_help.txt - Help text for LittleFS (help description strings)

The catalog is the machine-readable part (names, types, schemas, availability).
Help text (description, display_name) goes into the FS partition.
"""

import yaml
import sys
from pathlib import Path

def load_registry(registry_path):
    """Load and validate the action registry YAML."""
    with open(registry_path, 'r') as f:
        data = yaml.safe_load(f)
    if not data or 'entries' not in data:
        raise ValueError(f"Invalid registry format in {registry_path}")
    return data['entries']

def generate_catalog_header(entries, output_path):
    """Generate include/console_catalog.h"""
    header = """// =============================================================================
// include/console_catalog.h
//
// Auto-generated from docs/action-registry.yaml by tools/generate_console_catalog.py
// DO NOT EDIT MANUALLY
//
// Operation Catalog - machine-readable registry for the Console module.
// Includes: canonical name, type, aliases, argument schema, availability metadata,
// executor reference. Help text (description, display_name) is stored separately
// in LittleFS.
// =============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

// Operation type values (from registry)
#define CONSOLE_CATALOG_TYPE_ACTION  "action"
#define CONSOLE_CATALOG_TYPE_STATUS  "status"
#define CONSOLE_CATALOG_TYPE_CONFIG  "config"
#define CONSOLE_CATALOG_TYPE_EVENT   "event"

// Parameter type values
#define CONSOLE_PARAM_TYPE_INT16     "int16"
#define CONSOLE_PARAM_TYPE_INT32     "int32"
#define CONSOLE_PARAM_TYPE_FLOAT     "float"
#define CONSOLE_PARAM_TYPE_BOOL      "bool"
#define CONSOLE_PARAM_TYPE_STRING    "string"

// Parameter descriptor
typedef struct {
    const char* name;
    const char* type;
    // Ranges as strings to support all numeric types (int16, int32, float, etc.)
    const char* range_min_str;
    const char* range_max_str;
    bool required;
} ConsoleParamDescriptor;

// Operation descriptor
typedef struct {
    const char* name;              // e.g. "drive.action.move"
    const char* type;              // "action", "status", "config", "event"
    const char* display_name;      // short label (e.g. "Move")
    const char* executor;          // executor function name or "none"
    const char** aliases;          // aliases for this operation (NULL-terminated)
    const ConsoleParamDescriptor* params;  // parameter descriptors (NULL-terminated)
    bool available_on_board;       // board availability
    bool available_in_build;       // build flag availability
    bool requires_web_control;     // if true, needs webControlEnabled for motion
    bool safety_critical;          // if true, subject to safety constraints
} ConsoleCatalogEntry;

// Get the complete catalog
const ConsoleCatalogEntry* consoleCatalogGetEntries(size_t* out_count);

// Find an entry by name
const ConsoleCatalogEntry* consoleCatalogFindByName(const char* name);

// Get count of operations
size_t consoleCatalogGetCount(void);

"""

    with open(output_path, 'w') as f:
        f.write(header)

def generate_catalog_source(entries, output_path):
    """Generate src/console/console_catalog.cpp with the complete catalog table."""

    # Build the C++ source
    source = """// =============================================================================
// src/console/console_catalog.cpp
//
// Auto-generated from docs/action-registry.yaml by tools/generate_console_catalog.py
// DO NOT EDIT MANUALLY
//
// Operation Catalog - runtime table mapping operation names to descriptors,
// parameter schemas, availability metadata, and executor references.
// Help text (description, display_name) is stored in LittleFS.
// =============================================================================

#include "console_catalog.h"
#include <string.h>

// Forward declarations for executor references (will be resolved by link-time)
// extern void driveArbiterSubmit(void);
// extern void soundCommandExecutor(void);
// etc. - these are reference strings, not function pointers

"""

    # Group entries by domain
    by_domain = {}
    for entry in entries:
        domain = entry.get('domain', 'unknown')
        if domain not in by_domain:
            by_domain[domain] = []
        by_domain[domain].append(entry)

    # Generate parameter descriptor tables
    source += "// =============================================================================\n"
    source += "// Parameter Descriptors\n"
    source += "// =============================================================================\n\n"

    param_names_used = set()
    for entry in entries:
        if entry.get('params'):
            safe_name = entry['name'].replace('.', '_').replace('-', '_')
            param_var_name = f"g_params_{safe_name}"
            if param_var_name not in param_names_used:
                param_names_used.add(param_var_name)
                source += f"static const ConsoleParamDescriptor {param_var_name}[] = {{\n"
                for param in entry['params']:
                    param_type = param.get('type', 'string')
                    range_data = param.get('range', [None, None])
                    range_min_str = str(range_data[0]) if range_data[0] is not None else "NULL"
                    range_max_str = str(range_data[1]) if range_data[1] is not None else "NULL"
                    required = param.get('required', False)
                    source += f"    {{\"{param['name']}\", \"{param_type}\", {range_min_str if range_min_str == 'NULL' else f'\"{range_min_str}\"'}, {range_max_str if range_max_str == 'NULL' else f'\"{range_max_str}\"'}, {'true' if required else 'false'}}},\n"
                source += "    {NULL, NULL, NULL, NULL, false}  // terminator\n"
                source += "};\n\n"

    # Generate the main catalog table
    source += "// =============================================================================\n"
    source += "// Complete Operation Catalog\n"
    source += "// =============================================================================\n\n"
    source += "static const ConsoleCatalogEntry g_catalogEntries[] = {\n"

    for entry in entries:
        name = entry['name']
        op_type = entry.get('type', 'action')
        display_name = entry.get('display_name', name)
        executor = entry.get('executor', 'none')
        requires_web = entry.get('requires_web_control', False)
        safety_critical = entry.get('safety_critical', False)

        # Parameters
        if entry.get('params'):
            safe_name = entry['name'].replace('.', '_').replace('-', '_')
            param_var_name = f"g_params_{safe_name}"
        else:
            param_var_name = "NULL"

        # Aliases (for now, just the canonical name - could be extended)
        aliases_comment = f"  // TODO: aliases for {name}"

        source += f"    {{\n"
        source += f"        \"{name}\",\n"
        source += f"        \"{op_type}\",\n"
        source += f"        \"{display_name}\",\n"
        source += f"        \"{executor}\",\n"
        source += f"        NULL,{aliases_comment}\n"
        source += f"        {param_var_name},\n"
        source += f"        true,  // available_on_board (TODO: check board_capability)\n"
        source += f"        true,  // available_in_build (TODO: check build_flag)\n"
        source += f"        {'true' if requires_web else 'false'},  // requires_web_control\n"
        source += f"        {'true' if safety_critical else 'false'},  // safety_critical\n"
        source += f"    }},\n"

    source += "};\n\n"
    source += f"static const size_t g_catalogCount = sizeof(g_catalogEntries) / sizeof(g_catalogEntries[0]);\n\n"

    # Implement public functions
    source += """// =============================================================================
// Public API
// =============================================================================

const ConsoleCatalogEntry* consoleCatalogGetEntries(size_t* out_count) {
    if (out_count) {
        *out_count = g_catalogCount;
    }
    return g_catalogEntries;
}

const ConsoleCatalogEntry* consoleCatalogFindByName(const char* name) {
    if (!name) return NULL;
    for (size_t i = 0; i < g_catalogCount; ++i) {
        if (strcmp(g_catalogEntries[i].name, name) == 0) {
            return &g_catalogEntries[i];
        }
    }
    return NULL;
}

size_t consoleCatalogGetCount(void) {
    return g_catalogCount;
}
"""

    with open(output_path, 'w') as f:
        f.write(source)

def generate_help_text(entries, output_path):
    """Generate data/console_help.txt - help text for LittleFS.

    Format: one entry per line
    name|display_name|description

    Newlines and pipes in text are escaped:
    - \n -> \\n (literal backslash-n)
    - | -> \\| (literal backslash-pipe)
    """

    lines = []
    for entry in entries:
        name = entry['name']
        display_name = entry.get('display_name', name)
        description = entry.get('description', '')

        # Escape special characters
        # Must escape newlines before pipes
        display_name = display_name.replace('\n', '\\n').replace('|', '\\|')
        description = description.replace('\n', '\\n').replace('|', '\\|')

        lines.append(f"{name}|{display_name}|{description}\n")

    with open(output_path, 'w') as f:
        for line in lines:
            f.write(line)

    # Print stats
    print(f"Generated help text: {len(lines)} entries, {sum(len(l) for l in lines)} bytes")

def main():
    repo_root = Path(__file__).parent.parent
    registry_path = repo_root / 'docs' / 'action-registry.yaml'

    if not registry_path.exists():
        print(f"Error: Registry not found at {registry_path}", file=sys.stderr)
        sys.exit(1)

    # Load registry
    print(f"Loading registry from {registry_path}...")
    entries = load_registry(registry_path)
    print(f"Loaded {len(entries)} entries")

    # Generate files
    catalog_h = repo_root / 'include' / 'console_catalog.h'
    catalog_cpp = repo_root / 'src' / 'console' / 'console_catalog.cpp'
    help_txt = repo_root / 'data' / 'console_help.txt'

    print(f"Generating {catalog_h}...")
    generate_catalog_header(entries, catalog_h)

    print(f"Generating {catalog_cpp}...")
    generate_catalog_source(entries, catalog_cpp)

    print(f"Generating {help_txt}...")
    generate_help_text(entries, help_txt)

    print("Done!")

if __name__ == '__main__':
    main()
