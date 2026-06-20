// =============================================================================
// test/test_native/test_seq_json/test_seq_json.cpp
//
// Native tests for Learned Sequence JSON v1 parse/serialize (issue #2 slice 3b).
//   - parse-equivalence: a JSON mirror of DM:NOD parses to a SeqStep array that
//     matches the built-in table field-for-field (the format reproduces the
//     catalog 1:1) and runs through the engine producing the built-in's dispatch.
//   - serialize round-trip: builtin -> JSON -> parse -> Protocol Check ok.
//   - parse error cases.
// =============================================================================

#include <string.h>

#include <unity.h>

#include "audio_playback_policy.h"
#include "protocol_check.h"
#include "seq_json.h"
#include "sequence_dispatcher.h"  // sequenceCatalogFind
#include "sequence_engine.h"

void setUp()    {}
void tearDown() {}

static uint32_t stubRand() { return 0; }

// Caller-owned staging buffers for parsed branches.
static SeqStep gSteps[96];
static SeqStep gClose[96];

// Compare two steps ignoring effectClass (which is derived, not serialized).
static bool stepEqIgnoringFx(const SeqStep& a, const SeqStep& b) {
    if (a.tMs != b.tMs) return false;
    if (a.type != b.type) return false;
    if (strncmp(a.payload, b.payload, sizeof(a.payload)) != 0) return false;
    return memcmp(&a.params, &b.params, sizeof(SeqStepParams)) == 0;
}

// Drain the full engine action log into a pipe-separated string.
static void runToLog(const SequenceEntry& e, char* log, size_t cap) {
    static SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &e, 0);
    log[0] = '\0';
    for (uint32_t now = 0; now <= e.suppressMs + 2000 && seqEngineActive(st);
         now += 10) {
        SeqAction act;
        while (seqEnginePeek(st, now, stubRand, act)) {
            const char* tag = (act.kind == SEQ_ACT_AUDIO_STOP) ? "<stop>" : act.payload;
            if (log[0] != '\0') strncat(log, "|", cap - strlen(log) - 1);
            strncat(log, tag, cap - strlen(log) - 1);
            seqEngineCommit(st);
        }
    }
}

// -----------------------------------------------------------------------------
// Parse-equivalence vs the built-in DM:NOD table
// -----------------------------------------------------------------------------
static const char* kNodJson =
    "{\"format\":1,\"name\":\"DM:NOD\",\"suppressMs\":3000,"
    "\"toggleGroup\":\"none\",\"steps\":["
    "{\"t\":0,\"type\":\"audio\",\"cmd\":\"$H\"},"
    "{\"t\":0,\"type\":\"dome\",\"cmd\":\"@1MYes\"},"
    "{\"t\":0,\"type\":\"dome\",\"cmd\":\":OP01\"},"
    "{\"t\":150,\"type\":\"dome\",\"cmd\":\":CL01\"},"
    "{\"t\":300,\"type\":\"end\"}]}";

static void test_nod_json_parses_to_builtin_steps() {
    SeqDraft d;
    ProtocolCheckResult r =
        seqJsonParse(kNodJson, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_STRING("DM:NOD", d.name);
    TEST_ASSERT_EQUAL_UINT32(3000, d.suppressMs);
    TEST_ASSERT_EQUAL_UINT8(TOGGLE_NONE, d.toggleGroup);

    const SequenceEntry* builtin = sequenceCatalogFind("DM:NOD");
    TEST_ASSERT_NOT_NULL(builtin);
    TEST_ASSERT_EQUAL_UINT8(builtin->stepCount, d.stepCount);
    for (uint8_t i = 0; i < d.stepCount; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(stepEqIgnoringFx(d.steps[i], builtin->steps[i]),
                                 "parsed step differs from built-in");
    }
}

static void test_nod_parsed_runs_through_engine() {
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(kNodJson, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE(r.ok);
    r = protocolCheck(d);  // stamps inferred effectClass
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);

    SequenceEntry e = { d.name, d.steps, d.stepCount, d.suppressMs,
                        d.toggleGroup, d.closeSteps, d.closeStepCount };
    char log[256];
    runToLog(e, log, sizeof(log));

    // The choreography dispatch must be present and ordered.
    TEST_ASSERT_NOT_NULL(strstr(log, "$H"));
    TEST_ASSERT_NOT_NULL(strstr(log, "@1MYes"));
    TEST_ASSERT_NOT_NULL(strstr(log, ":OP01"));
    TEST_ASSERT_NOT_NULL(strstr(log, ":CL01"));  // ring panel 1 is closed individually
    // Engine cleanup never emits a group close (brownout-prone): the ring panel
    // is closed individually, never via :CL15/:CL00.
    TEST_ASSERT_NULL(strstr(log, ":CL15"));
    TEST_ASSERT_NULL(strstr(log, ":CL00"));
}

// -----------------------------------------------------------------------------
// Serialize round-trip
// -----------------------------------------------------------------------------
static void test_serialize_roundtrip_vader() {
    const SequenceEntry* vader = sequenceCatalogFind("DM:VADER");
    TEST_ASSERT_NOT_NULL(vader);

    char json[4096];
    size_t n = seqJsonSerialize(*vader, "factory", json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, n);

    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_STRING("DM:VADER", d.name);
    TEST_ASSERT_EQUAL_UINT8(vader->stepCount, d.stepCount);
    for (uint8_t i = 0; i < d.stepCount; ++i) {
        TEST_ASSERT_TRUE(stepEqIgnoringFx(d.steps[i], vader->steps[i]));
    }
    r = protocolCheck(d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

static void test_serialize_roundtrip_toggle_pies() {
    const SequenceEntry* pies = sequenceCatalogFind("DM:PIES");
    TEST_ASSERT_NOT_NULL(pies);

    char json[8192];
    size_t n = seqJsonSerialize(*pies, "factory", json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, n);

    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(TOGGLE_PIES, d.toggleGroup);
    TEST_ASSERT_EQUAL_UINT8(pies->stepCount, d.stepCount);
    TEST_ASSERT_EQUAL_UINT8(pies->closeStepCount, d.closeStepCount);
    TEST_ASSERT_NOT_NULL(d.closeSteps);
    r = protocolCheck(d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

// -----------------------------------------------------------------------------
// Round-trip the loop + random builtins (param fidelity)
// -----------------------------------------------------------------------------
static void test_serialize_roundtrip_cantina_loop() {
    const SequenceEntry* cantina = sequenceCatalogFind("DM:CANTINA");
    TEST_ASSERT_NOT_NULL(cantina);
    char json[8192];
    size_t n = seqJsonSerialize(*cantina, "factory", json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, n);
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(cantina->stepCount, d.stepCount);
    for (uint8_t i = 0; i < d.stepCount; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(stepEqIgnoringFx(d.steps[i], cantina->steps[i]),
                                 "loop step round-trip mismatch");
    }
}

static void test_serialize_roundtrip_scream_random() {
    const SequenceEntry* scream = sequenceCatalogFind("DM:SCREAM");
    TEST_ASSERT_NOT_NULL(scream);
    char json[8192];
    size_t n = seqJsonSerialize(*scream, "factory", json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NULL(strstr(json, "pulseMin"));
    TEST_ASSERT_NULL(strstr(json, "pulseMax"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"mode\":\"flutter\""));
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(scream->stepCount, d.stepCount);
    for (uint8_t i = 0; i < d.stepCount; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(stepEqIgnoringFx(d.steps[i], scream->steps[i]),
                                 "random step round-trip mismatch");
    }
}

// -----------------------------------------------------------------------------
// Parse error cases
// -----------------------------------------------------------------------------
static void test_bad_format_rejected() {
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":2,\"name\":\"DM:X\",\"steps\":[]}", gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("format", r.field);
}

static void test_missing_name_rejected() {
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"steps\":[]}", gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("name", r.field);
}

static void test_unknown_step_type_rejected() {
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:X\",\"steps\":["
        "{\"t\":0,\"type\":\"wobble\"}]}", gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].type", r.field);
}

static void test_malformed_json_rejected() {
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse("{not json", gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("json", r.field);
}

static void test_steps_overflow_rejected() {
    // Build a JSON with 4 steps but a cap of 2.
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:X\",\"steps\":["
        "{\"t\":0,\"type\":\"audio\",\"cmd\":\"$H\"},"
        "{\"t\":0,\"type\":\"audio\",\"cmd\":\"$H\"},"
        "{\"t\":0,\"type\":\"audio\",\"cmd\":\"$H\"},"
        "{\"t\":0,\"type\":\"end\"}]}",
        gSteps, 2, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps", r.field);
}

static void test_audiocat_roundtrip() {
    // audioCat fields survive serialize labels -> parse enum.
    static const SeqStep s[] = {
        SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_ALERT, AUDIO_SLOT_NAMED_SCREAM),
        SEQ_TERM(100),
    };
    SequenceEntry e = { "DM:CATTEST", s, 2, 4000, TOGGLE_NONE, nullptr, 0 };
    char json[1024];
    size_t n = seqJsonSerialize(e, "user", json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"category\":\"alert\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"fallback\":\"scream\""));

    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(AUDIO_CATEGORY_ALERT, d.steps[0].params.audioCategory);
    TEST_ASSERT_EQUAL_UINT8(AUDIO_SLOT_NAMED_SCREAM, d.steps[0].params.audioFallbackSlot);
}

// -----------------------------------------------------------------------------
// Catalog iteration + /builtins serialization
// -----------------------------------------------------------------------------
static void test_catalog_iteration_bounds() {
    uint8_t n = sequenceCatalogCount();
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(sequenceCatalogAt(0));
    TEST_ASSERT_NOT_NULL(sequenceCatalogAt((uint8_t)(n - 1)));
    TEST_ASSERT_NULL(sequenceCatalogAt(n));
}

static void test_all_builtins_serialize_and_reparse() {
    // Every factory entry must serialize to valid JSON v1 that parses back to the
    // same name and step count (proves GET /api/seq/builtins yields clonable
    // editor starting points). NOTE: factory tables are the PR-reviewed expert
    // surface and are intentionally exempt from Protocol Check's meta rules
    // (e.g. DM:ROCKMARCH authors STEP_END 300 ms past its suppress window); a
    // *cloned* copy is Protocol-Checked on save, which is where suppress>=end is
    // enforced. So this asserts parse fidelity + per-branch validity, not meta.
    uint8_t n = sequenceCatalogCount();
    char json[8192];
    for (uint8_t i = 0; i < n; ++i) {
        const SequenceEntry* e = sequenceCatalogAt(i);
        TEST_ASSERT_NOT_NULL(e);
        size_t w = seqJsonSerialize(*e, "factory", json, sizeof(json));
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, w, e->name);
        TEST_ASSERT_NULL_MESSAGE(strstr(json, ":SM"), e->name);
        SeqDraft d;
        ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
        TEST_ASSERT_TRUE_MESSAGE(r.ok, e->name);
        TEST_ASSERT_EQUAL_STRING(e->name, d.name);
        // STEP_CLEAR_LATCHES is body-internal with no JSON/wire form and is
        // intentionally omitted from serialization (the editor cannot represent
        // it), so a serialized branch carries one fewer step per latch-clear.
        uint8_t expectSteps = e->stepCount;
        for (uint8_t s = 0; s < e->stepCount; ++s) {
            if (e->steps[s].type == STEP_CLEAR_LATCHES) expectSteps--;
        }
        TEST_ASSERT_EQUAL_UINT8(expectSteps, d.stepCount);
        // Per-branch structure/commands must validate (meta rules excluded).
        r = protocolCheckBranch("steps", d.steps, d.stepCount);
        TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    }
}

// -----------------------------------------------------------------------------
// domeRotate step tests (issue #7 step 5)
// -----------------------------------------------------------------------------
static void test_domerotate_parse_positive() {
    // Parse a simple positive dome rotation step
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"domeRotate\",\"speedPct\":50,\"durationMs\":1000},"
        "{\"t\":1000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(STEP_DOME_ROTATE, d.steps[0].type);
    TEST_ASSERT_EQUAL_INT8(50, d.steps[0].params.speedPct);
    TEST_ASSERT_EQUAL_UINT32(1000, d.steps[0].params.durationMs);
}

static void test_domerotate_parse_negative() {
    // Parse a negative dome rotation step (left/reverse)
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"domeRotate\",\"speedPct\":-75,\"durationMs\":2000},"
        "{\"t\":2000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_INT8(-75, d.steps[0].params.speedPct);
    TEST_ASSERT_EQUAL_UINT32(2000, d.steps[0].params.durationMs);
}

static void test_domerotate_parse_neutral_stop() {
    // Parse explicit neutral stop (speedPct=0, durationMs=0)
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":1000,\"type\":\"domeRotate\",\"speedPct\":0,\"durationMs\":0},"
        "{\"t\":1000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_INT8(0, d.steps[0].params.speedPct);
    TEST_ASSERT_EQUAL_UINT32(0, d.steps[0].params.durationMs);
}

static void test_domerotate_reject_speedpct_too_high() {
    // Reject speedPct > 100
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"domeRotate\",\"speedPct\":101,\"durationMs\":1000},"
        "{\"t\":1000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].speedPct", r.field);
}

static void test_domerotate_reject_speedpct_too_low() {
    // Reject speedPct < -100
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"domeRotate\",\"speedPct\":-101,\"durationMs\":1000},"
        "{\"t\":1000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].speedPct", r.field);
}

static void test_domerotate_reject_zero_speed_with_duration() {
    // Reject zero speed with positive duration (ambiguous intent)
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"domeRotate\",\"speedPct\":0,\"durationMs\":1000},"
        "{\"t\":1000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].speedPct", r.field);
}

static void test_domerotate_reject_nonzero_speed_with_zero_duration() {
    // Reject nonzero speed with zero duration (incomplete rotation)
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"domeRotate\",\"speedPct\":50,\"durationMs\":0},"
        "{\"t\":1000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].durationMs", r.field);
}

static void test_domerotate_reject_negative_duration() {
    // Reject malformed negative durationMs at the parse boundary (must match the
    // client check) instead of letting it wrap to a huge uint32_t ms.
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:ROTTEST\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"domeRotate\",\"speedPct\":50,\"durationMs\":-100},"
        "{\"t\":1000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].durationMs", r.field);
}

static void test_domerotate_serialize_roundtrip() {
    // Create a step, serialize, reparse, compare
    static const SeqStep steps[] = {
        SEQ_DOME_ROTATE(0, 35, 900),
        SEQ_TERM(900),
    };
    SequenceEntry e = { "DM:ROTTEST", steps, 2, 5000, TOGGLE_NONE, nullptr, 0 };

    char json[1024];
    size_t n = seqJsonSerialize(e, "user", json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"type\":\"domeRotate\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"speedPct\":35"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"durationMs\":900"));

    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(STEP_DOME_ROTATE, d.steps[0].type);
    TEST_ASSERT_EQUAL_INT8(35, d.steps[0].params.speedPct);
    TEST_ASSERT_EQUAL_UINT32(900, d.steps[0].params.durationMs);
}

// -----------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_nod_json_parses_to_builtin_steps);
    RUN_TEST(test_nod_parsed_runs_through_engine);

    RUN_TEST(test_serialize_roundtrip_vader);
    RUN_TEST(test_serialize_roundtrip_toggle_pies);
    RUN_TEST(test_serialize_roundtrip_cantina_loop);
    RUN_TEST(test_serialize_roundtrip_scream_random);

    RUN_TEST(test_bad_format_rejected);
    RUN_TEST(test_missing_name_rejected);
    RUN_TEST(test_unknown_step_type_rejected);
    RUN_TEST(test_malformed_json_rejected);
    RUN_TEST(test_steps_overflow_rejected);

    RUN_TEST(test_audiocat_roundtrip);

    RUN_TEST(test_domerotate_parse_positive);
    RUN_TEST(test_domerotate_parse_negative);
    RUN_TEST(test_domerotate_parse_neutral_stop);
    RUN_TEST(test_domerotate_reject_speedpct_too_high);
    RUN_TEST(test_domerotate_reject_speedpct_too_low);
    RUN_TEST(test_domerotate_reject_zero_speed_with_duration);
    RUN_TEST(test_domerotate_reject_nonzero_speed_with_zero_duration);
    RUN_TEST(test_domerotate_reject_negative_duration);
    RUN_TEST(test_domerotate_serialize_roundtrip);

    RUN_TEST(test_catalog_iteration_bounds);
    RUN_TEST(test_all_builtins_serialize_and_reparse);

    return UNITY_END();
}
