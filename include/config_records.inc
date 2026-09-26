// =============================================================================
// include/config_records.inc
//
// Every Record, one line each (CONTEXT.md "Record"; ADR 0068, second amendment
// of 2026-09-26). Included by include/config_records.h and
// src/config_records.cpp with PA_CONFIG_RECORD defined; not a header of its own.
//
//   PA_CONFIG_RECORD(Name, Value, key)
//     Name  - the Record's name in its functions: configRecord<Name>Check() and
//             the rest of the interface config_records.h declares, defined in
//             the Record's own module (src/config_record_<name>.cpp)
//     Value - the struct the Record's values are held in
//     key   - the object GET /api/config answers it under, and the prefix of
//             every field's GET path
//
// The Apply Core, the Commit Step, GET and the NVS save and load loop over this
// list and name no Record; a third Record is one line here and its module.
// =============================================================================
PA_CONFIG_RECORD(DroidBuild, DroidBuildConfig, "droidBuild")
PA_CONFIG_RECORD(GuidedSetup, GuidedSetupConfig, "guidedSetup")
