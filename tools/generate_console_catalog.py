#!/usr/bin/env python3
"""
Generate console operation catalog and help text from action-registry.yaml.

This tool produces:
1. include/console_catalog.h - C++ header with operation descriptors
2. src/console/console_catalog.cpp - C++ implementation with the catalog table
3. data/console_help.txt - Help text for LittleFS (help description strings)

The catalog is the machine-readable part (names, types, argument keys, availability).
Help text (description, display_name, parameter schema, executor details) goes into the FS partition.
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

def build_rc_token_map(entries):
    """Build a map from operation name to rc_token value.

    Returns dict: operation_name -> rc_token_string
    """
    rc_token_map = {}
    for entry in entries:
        name = entry['name']
        rc_token = entry.get('rc_token')
        if rc_token:
            rc_token_map[name] = rc_token
    return rc_token_map

def generate_catalog_header(entries, output_path):
    """Generate include/console_catalog.h"""
    header = """// =============================================================================
// include/console_catalog.h
//
// Auto-generated from docs/action-registry.yaml by tools/generate_console_catalog.py
// DO NOT EDIT MANUALLY
//
// Operation Catalog - machine-readable registry for the Console module.
// Includes: canonical name, type, argument keys, availability metadata.
// Help text (description, display_name, parameter schema) is stored separately
// in LittleFS, addressed by offset and length in each entry.
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
#define CONSOLE_PARAM_TYPE_UINT8     "uint8"
#define CONSOLE_PARAM_TYPE_UINT16    "uint16"
#define CONSOLE_PARAM_TYPE_FLOAT     "float"
#define CONSOLE_PARAM_TYPE_BOOL      "bool"
#define CONSOLE_PARAM_TYPE_STRING    "string"

// Parameter descriptor (simplified - full schema is in help text)
typedef struct {
    const char* name;      // parameter name (e.g. "speed", "steer")
    const char* type;      // parameter type (e.g. "int16", "string", "bool")
    bool required;         // required vs optional
} ConsoleParamDescriptor;

// Operation descriptor
typedef struct {
    const char* name;                    // e.g. "drive.action.move"
    const char* type;                    // "action", "status", "config", "event"
    const char* const* aliases;          // aliases for this operation (NULL-terminated, or NULL)
    const ConsoleParamDescriptor* params;  // parameter descriptors (NULL-terminated)
    bool available_on_board;             // board availability
    bool available_in_build;             // build flag availability
    bool requires_web_control;           // if true, needs webControlEnabled for motion
    bool safety_critical;                // if true, subject to safety constraints
    bool executor_ready;                 // true if executor function is defined and ready
    uint16_t help_offset;                // offset in help file for this operation
    uint16_t help_length;                // length of help text in help file
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

def generate_help_text(entries, output_path):
    """Generate data/console_help.txt - help text for LittleFS.

    Format: one entry per line
    name|display_name|description|executor|param1:type1:required1|param2:type2:required2|...

    Returns dict mapping name -> (offset, length)
    """
    lines = []
    offsets = {}
    current_offset = 0

    for entry in entries:
        name = entry['name']
        display_name = entry.get('display_name', name)
        description = entry.get('description', '')
        executor = entry.get('executor', 'none')

        # Escape special characters per drift checker requirements: \n for newlines, \| for pipes
        # display_name: newlines -> space (display names shouldn't span lines)
        display_name = display_name.replace('\n', ' ').replace('|', '\\|').rstrip()
        # description: escape newlines and pipes as per drift checker format
        description = description.replace('\\', '\\\\')  # escape backslashes first
        description = description.replace('\n', '\\n')   # then newlines
        description = description.replace('|', '\\|')    # then pipes

        # Build parameter list: param1:type1:required1|param2:type2:required2
        param_list = []
        for param in entry.get('params', []):
            pname = param.get('name', '')
            ptype = param.get('type', 'string')
            preq = '1' if param.get('required', False) else '0'
            param_list.append(f"{pname}:{ptype}:{preq}")
        params_str = '|'.join(param_list) if param_list else ''

        # Construct line: name|display_name|description|executor|params
        line = f"{name}|{display_name}|{description}|{executor}|{params_str}\n"

        # Record offset before adding to lines
        offsets[name] = (current_offset, len(line) - 1)  # -1 for newline
        current_offset += len(line)
        lines.append(line)

    with open(output_path, 'w') as f:
        for line in lines:
            f.write(line)

    # Print stats
    total_bytes = sum(len(l) for l in lines)
    print(f"Generated help text: {len(lines)} entries, {total_bytes} bytes")
    return offsets

def generate_catalog_source(entries, offsets, output_path):
    """Generate src/console/console_catalog.cpp with the complete catalog table.

    offsets: dict mapping entry name -> (offset, length)
    """

    # Build rc_token map for aliases
    rc_token_map = build_rc_token_map(entries)

    # Build the C++ source
    source = """// =============================================================================
// src/console/console_catalog.cpp
//
// Auto-generated from docs/action-registry.yaml by tools/generate_console_catalog.py
// DO NOT EDIT MANUALLY
//
// Operation Catalog - runtime table mapping operation names to descriptors,
// parameter schemas, availability metadata, and help text addressing.
// Help text (description, display_name, parameter schema, executor details)
// is stored in LittleFS and addressed by offset/length.
//
// Availability (available_on_board and available_in_build) is evaluated at
// compile-time via macros from include/config.h and include/board_capabilities.inc
// (ADR 0029). This allows a board that sets PA_CAP_DRIVE_BACKEND_HOVERBOARD=0
// to flip the availability of drive operations with no generator change.
// =============================================================================

#include "console_catalog.h"
#include "config.h"
#include <string.h>

"""

    # Generate alias arrays (NULL-terminated, one for each rc_token entry)
    source += "// =============================================================================\n"
    source += "// Alias Arrays (RC tokens mapped to operation names)\n"
    source += "// =============================================================================\n\n"

    aliases_count = 0
    for entry in entries:
        name = entry['name']
        rc_token = entry.get('rc_token')
        if rc_token:
            aliases_count += 1
            safe_name = name.replace('.', '_').replace('-', '_')
            alias_var_name = f"g_aliases_{safe_name}"
            source += f"static const char* const {alias_var_name}[] = {{ \"{rc_token}\", NULL }};\n"

    source += f"\n// Total alias arrays: {aliases_count}\n\n"

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
                    param_name = param.get('name', '')
                    param_type = param.get('type', 'string')
                    required = param.get('required', False)
                    source += f"    {{\"{param_name}\", \"{param_type}\", {'true' if required else 'false'}}},\n"
                source += "    {NULL, NULL, false}  // terminator\n"
                source += "};\n\n"

    # Generate the main catalog table
    source += "// =============================================================================\n"
    source += "// Complete Operation Catalog\n"
    source += "// =============================================================================\n\n"
    source += "static const ConsoleCatalogEntry g_catalogEntries[] = {\n"

    for entry in entries:
        name = entry['name']
        op_type = entry.get('type', 'action')
        executor = entry.get('executor', 'none')
        requires_web = entry.get('requires_web_control', False)
        safety_critical = entry.get('safety_critical', False)
        build_flag = entry.get('build_flag')
        domain = name.split('.')[0]

        # Availability flags are now compile-time expressions (macros)
        # available_on_board: drive domain uses PA_CAP_DRIVE_BACKEND_HOVERBOARD, others use 1
        if domain == 'drive':
            available_on_board_expr = 'PA_CAP_DRIVE_BACKEND_HOVERBOARD'
        else:
            available_on_board_expr = '1'

        # available_in_build: if entry has build_flag, use the macro; otherwise use 1
        if build_flag:
            available_in_build_expr = build_flag
        else:
            available_in_build_expr = '1'

        # Aliases: reference the alias array if one exists, else NULL
        if name in rc_token_map:
            safe_name = name.replace('.', '_').replace('-', '_')
            aliases_expr = f"g_aliases_{safe_name}"
        else:
            aliases_expr = "NULL"

        # Executor ready (simplified: assume all are ready for now)
        # TODO: check if executor function is actually defined
        executor_ready = True  # All registry entries have executors defined

        # Parameters
        if entry.get('params'):
            safe_name = entry['name'].replace('.', '_').replace('-', '_')
            param_var_name = f"g_params_{safe_name}"
        else:
            param_var_name = "NULL"

        # Help text offset and length
        if name in offsets:
            help_offset, help_length = offsets[name]
        else:
            help_offset, help_length = 0, 0

        source += f"    {{\n"
        source += f"        \"{name}\",\n"
        source += f"        \"{op_type}\",\n"
        source += f"        {aliases_expr},  // aliases\n"
        source += f"        {param_var_name},\n"
        source += f"        {available_on_board_expr},  // available_on_board\n"
        source += f"        {available_in_build_expr},  // available_in_build\n"
        source += f"        {'true' if requires_web else 'false'},  // requires_web_control\n"
        source += f"        {'true' if safety_critical else 'false'},  // safety_critical\n"
        source += f"        {'true' if executor_ready else 'false'},  // executor_ready\n"
        source += f"        {help_offset},  // help_offset\n"
        source += f"        {help_length},  // help_length\n"
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

    # Count aliases for diagnostic output
    rc_token_map = build_rc_token_map(entries)
    print(f"Found {len(rc_token_map)} entries with rc_token (aliases)")

    # Count build flags for diagnostic output
    build_flag_count = sum(1 for e in entries if e.get('build_flag'))
    print(f"Found {build_flag_count} entries with build_flag")

    # Generate files
    catalog_h = repo_root / 'include' / 'console_catalog.h'
    catalog_cpp = repo_root / 'src' / 'console' / 'console_catalog.cpp'
    help_txt = repo_root / 'data' / 'console_help.txt'

    print(f"Generating {catalog_h}...")
    generate_catalog_header(entries, catalog_h)

    print(f"Generating {help_txt}...")
    offsets = generate_help_text(entries, help_txt)

    print(f"Generating {catalog_cpp}...")
    generate_catalog_source(entries, offsets, catalog_cpp)

    print("Done!")

if __name__ == '__main__':
    main()
