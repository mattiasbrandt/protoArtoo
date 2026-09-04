// =============================================================================
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
    // is answered by running it, never advertised at discovery (ADR 0035).
    // The two availability flags above are genuine compile-time expressions;
    // readiness was not, and the field that claimed it read `true` for every
    // entry - including the ones dispatch refuses with
    // `unavailable reason=executor-not-ready`.
    uint16_t help_offset;                // offset in help file for this operation
    uint16_t help_length;                // length of help text in help file
    const char* const* fields;           // for type=status: API JSON keys this query answers
                                          // with, verbatim (NULL-terminated, or NULL). Only
                                          // meaningful when is_query is true (#223, ADR 0034).
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

