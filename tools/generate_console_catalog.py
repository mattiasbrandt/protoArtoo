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

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from registry_yaml import load_registry_yaml

def load_registry(registry_path):
    """Load and validate the action registry YAML.

    Uses registry_yaml.load_registry_yaml() rather than yaml.safe_load()
    directly: PyYAML's default resolver reads a bare on/off/yes/no as a
    Python bool, not the string the registry means (#249 - aux.action.
    led-effect's `off` enum value was corrupted to `False` by exactly this).
    """
    data = load_registry_yaml(registry_path)
    if not data or 'entries' not in data:
        raise ValueError(f"Invalid registry format in {registry_path}")
    return data['entries']

# A registry `marcduino_cmd:` is prose as often as it is a wire command: it
# holds documentation placeholders ('DM:<NAME>', '$...', 'M<nn>') and even a
# sentence ('#PASL (tx), #APSL (rx sync)'). So the raw field must never reach
# the firmware - only the rows this pattern accepts do, and every one of those
# is a literal name sequenceStart() (include/sequence_dispatcher.h) can be
# handed verbatim. The filter lives here, in the generator, so the in-image
# table carries data the Console can execute without re-deciding what it is.
NAMED_SEQUENCE_RE = re.compile(r'^DM:[A-Z0-9]+$')

def named_sequence_rows(entries):
    """Return [(operation name, DM:<NAME>)] for every action row whose
    marcduino_cmd is a literal body/dome sequence name."""
    rows = []
    for entry in entries:
        if entry.get('type') != 'action':
            continue
        cmd = entry.get('marcduino_cmd')
        if isinstance(cmd, str) and NAMED_SEQUENCE_RE.match(cmd):
            rows.append((entry['name'], cmd))
    return rows

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

// Parameter descriptor. Range/enum (has_range/range_min/range_max/
// enum_values) close #221 gap 5: docs/action-registry.yaml's `range:` and
// `values:` keys previously reached only the FS-resident help text
// (name:type:required, no bounds) and never this in-image table, so the
// Console's schema validator (include/console_args.h) had no data source
// for the "range, enum" half of "type, range, enum" argument validation.
// Populated straight from the registry's existing `range:`/`values:` keys -
// no registry content change needed, so this is a generator-only diff
// (data/console_help.txt's param encoding is unaffected and stays byte-
// identical - verified at generation time, not assumed).
typedef struct {
    const char* name;      // parameter name (e.g. "speed", "steer")
    const char* type;      // parameter type (e.g. "int16", "string", "bool")
    bool required;         // required vs optional
    bool has_range;        // true if range_min/range_max apply (numeric types)
    double range_min;
    double range_max;
    const char* const* enum_values;  // NULL-terminated allowed-value strings, or NULL
    bool write_excluded;   // registry `write_excluded: true`: the parameter exists and is
                           // documented, but the Console never accepts a value for it - a
                           // secret (docs/console-protocol.md s.4.1). `help` renders it as
                           // write-excluded rather than required/optional, and the operation's
                           // executor refuses it before its Apply Core sees it.
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
    // No readiness flag lives here. Whether an operation's executor is wired
    // is answered by running it, never advertised at discovery (ADR 0037).
    // The two availability flags above are genuine compile-time expressions;
    // readiness was not, and the field that claimed it read `true` for every
    // entry - including the ones dispatch refuses with
    // `unavailable reason=executor-not-ready`.
    uint16_t help_offset;                // offset in help file for this operation
    uint16_t help_length;                // length of help text in help file
    const char* const* fields;           // for type=status: API JSON keys this query answers
                                          // with, verbatim (NULL-terminated, or NULL). Only
                                          // meaningful when is_query is true (#223, ADR 0036).
    bool is_query;                       // for type=status: true if independently console-
                                          // queryable (registry carries fields:); false if the
                                          // row only describes a field inside another query's
                                          // response (registry carries is_query: false, #212).
                                          // Always true for non-status types.
    bool read_only;                      // registry `read_only: true`: the operation reads, but
                                          // nothing in the firmware writes the value it names, so
                                          // a write is refused with `invalid reason=read-only`
                                          // (docs/console-protocol.md s.4.2). The operation-level
                                          // counterpart of a parameter's write_excluded above,
                                          // and the same rule: the fact lives in the registry, so
                                          // the dispatcher never carries a list of names and a row
                                          // marked tomorrow is refused with no code change.
} ConsoleCatalogEntry;

// Get the complete catalog
const ConsoleCatalogEntry* consoleCatalogGetEntries(size_t* out_count);

// Find an entry by name
const ConsoleCatalogEntry* consoleCatalogFindByName(const char* name);

// Get count of operations
size_t consoleCatalogGetCount(void);

// Named body/dome sequence for an operation, or NULL when the operation is not
// one. Generated from the registry's `marcduino_cmd:` for the action rows whose
// value is a literal DM:<NAME> (the dome.seq.* family) and for no others - the
// field also carries documentation placeholders and prose, which is why the
// generator filters and the firmware never sees the raw string. The dispatcher
// asks this instead of carrying a list of operation names, the same way
// read_only above keeps the config refusal in the registry.
const char* consoleCatalogSequenceFor(const char* operationName);

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

    def numeric_range(param):
        """Return (min, max) floats if `range:` is a genuine 2-element
        numeric bound, else None. The registry also spells a string enum as
        `range:` for three servo entries (target: [arm1, arm2, ...]) instead
        of `values:` - a pre-existing inconsistency this generator reads
        around rather than "fixes" (a registry content change is out of
        scope for this generator-only ticket)."""
        r = param.get('range')
        if not r or len(r) != 2:
            return None
        if not all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in r):
            return None
        return float(r[0]), float(r[1])

    def enum_source(param):
        """Return the raw enum list a param declares, from whichever of the
        registry's two spellings (`values:`, or a non-numeric `range:`) it
        uses - unified into one shape here so the catalog carries exactly
        one enum representation regardless of which YAML key produced it."""
        if 'values' in param:
            return param['values']
        if numeric_range(param) is None and param.get('range'):
            return param['range']
        return None

    # Enum-value arrays (one per param that declares one, since each param
    # in a multi-param entry can have its own enum - unlike aliases/fields,
    # which are one per operation). Enum entries are legitimately strings or
    # numbers (aux.config.led-pin, system.action.set-mood carry genuine
    # int enums) - str() renders whichever type came through so the
    # generator does not silently drop a value. A bool should never reach
    # here: load_registry() uses registry_yaml.load_registry_yaml(), which
    # narrows PyYAML's implicit resolver so a bare on/off/yes/no/y/n enum
    # value stays a string instead of coercing to True/False (#249 -
    # aux.action.led-effect's bare `off` was corrupted to boolean False by
    # exactly this coercion, upstream of this generator, before that fix).
    param_names_used = set()
    enum_names_used = set()
    for entry in entries:
        if not entry.get('params'):
            continue
        safe_name = entry['name'].replace('.', '_').replace('-', '_')
        for param in entry['params']:
            values = enum_source(param)
            if values is None:
                continue
            param_name = param.get('name', '')
            safe_param = param_name.replace('.', '_').replace('-', '_')
            enum_var_name = f"g_enum_{safe_name}_{safe_param}"
            if enum_var_name in enum_names_used:
                continue
            enum_names_used.add(enum_var_name)
            quoted = ', '.join(f"\"{str(v)}\"" for v in values)
            source += f"static const char* const {enum_var_name}[] = {{ {quoted}, NULL }};\n"

    if enum_names_used:
        source += f"\n// Total enum-value arrays: {len(enum_names_used)}\n\n"

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
                    write_excluded = param.get('write_excluded', False)
                    bounds = numeric_range(param)
                    if bounds is not None:
                        has_range = 'true'
                        range_min, range_max = bounds
                    else:
                        has_range = 'false'
                        range_min = 0.0
                        range_max = 0.0
                    if enum_source(param) is not None:
                        safe_param = param_name.replace('.', '_').replace('-', '_')
                        enum_expr = f"g_enum_{safe_name}_{safe_param}"
                    else:
                        enum_expr = "NULL"
                    source += (f"    {{\"{param_name}\", \"{param_type}\", "
                               f"{'true' if required else 'false'}, {has_range}, "
                               f"{range_min}, {range_max}, {enum_expr}, "
                               f"{'true' if write_excluded else 'false'}}},\n")
                source += "    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator\n"
                source += "};\n\n"

    # Generate field-name tables for type=status entries that carry fields:
    # (#223, ADR 0036). The registry's fields: list is the record emitter's
    # contract: names here are the API JSON keys verbatim, checked against the
    # JSON builder and the record emitter by a native test.
    source += "// =============================================================================\n"
    source += "// Status Query Field Lists (API JSON keys, verbatim)\n"
    source += "// =============================================================================\n\n"

    field_names_used = set()
    for entry in entries:
        if entry.get('fields'):
            safe_name = entry['name'].replace('.', '_').replace('-', '_')
            fields_var_name = f"g_fields_{safe_name}"
            if fields_var_name not in field_names_used:
                field_names_used.add(fields_var_name)
                quoted = ', '.join(f"\"{f}\"" for f in entry['fields'])
                source += f"static const char* const {fields_var_name}[] = {{ {quoted}, NULL }};\n"

    source += f"\n// Total field-name arrays: {len(field_names_used)}\n\n"

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
        read_only = entry.get('read_only', False)
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

        # Fields + is_query: only meaningful for type=status (#212's rule: a
        # status entry carries either fields: or is_query: false). Non-status
        # entries carry no fields and are_query defaults true (unused).
        if entry.get('fields'):
            safe_name = name.replace('.', '_').replace('-', '_')
            fields_expr = f"g_fields_{safe_name}"
            is_query = True
        elif op_type == 'status':
            fields_expr = "NULL"
            is_query = entry.get('is_query') is not False
        else:
            fields_expr = "NULL"
            is_query = True

        source += f"    {{\n"
        source += f"        \"{name}\",\n"
        source += f"        \"{op_type}\",\n"
        source += f"        {aliases_expr},  // aliases\n"
        source += f"        {param_var_name},\n"
        source += f"        {available_on_board_expr},  // available_on_board\n"
        source += f"        {available_in_build_expr},  // available_in_build\n"
        source += f"        {'true' if requires_web else 'false'},  // requires_web_control\n"
        source += f"        {'true' if safety_critical else 'false'},  // safety_critical\n"
        source += f"        {help_offset},  // help_offset\n"
        source += f"        {help_length},  // help_length\n"
        source += f"        {fields_expr},  // fields\n"
        source += f"        {'true' if is_query else 'false'},  // is_query\n"
        source += f"        {'true' if read_only else 'false'},  // read_only\n"
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

    # Named body/dome sequences: the action rows whose registry marcduino_cmd is
    # a literal DM:<NAME>. Emitted as its own small table rather than as a field
    # on every ConsoleCatalogEntry, because only these rows have one and the
    # field's raw values are not all machine-readable (see NAMED_SEQUENCE_RE).
    sequence_rows = named_sequence_rows(entries)
    source += "\n// ============================================================================="
    source += "\n// Named Body/Dome Sequences (registry marcduino_cmd, literal DM:<NAME> only)"
    source += "\n// =============================================================================\n\n"
    source += "typedef struct {\n"
    source += "    const char* operationName;\n"
    source += "    const char* sequence;\n"
    source += "} ConsoleSequenceRow;\n\n"
    source += "static const ConsoleSequenceRow g_sequenceRows[] = {\n"
    for name, cmd in sequence_rows:
        source += f"    {{\"{name}\", \"{cmd}\"}},\n"
    source += "};\n\n"
    source += ("static const size_t g_sequenceRowCount = "
               "sizeof(g_sequenceRows) / sizeof(g_sequenceRows[0]);\n\n")
    source += """const char* consoleCatalogSequenceFor(const char* operationName) {
    if (!operationName) return NULL;
    for (size_t i = 0; i < g_sequenceRowCount; ++i) {
        if (strcmp(g_sequenceRows[i].operationName, operationName) == 0) {
            return g_sequenceRows[i].sequence;
        }
    }
    return NULL;
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

    print(f"Found {len(named_sequence_rows(entries))} action entries with a literal DM: sequence")

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
