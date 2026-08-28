// =============================================================================
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

