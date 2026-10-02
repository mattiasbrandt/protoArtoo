// =============================================================================
// test/test_native/test_protocol_mirror/test_protocol_mirror.cpp
//
// The firmware half of test/fixtures/protocol_mirror.json. The browser half is
// tools/check_protocol_mirror.py. Both read the same cases and the same
// expect flag. A row that moves on only one side fails the side that moved.
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include <ArduinoJson.h>
#include <unity.h>

#include "protocol_check.h"
#include "seq_json.h"

static SeqStep gSteps[PC_MAX_STEPS];
static SeqStep gClose[PC_MAX_STEPS];

static const char* fixturePath() {
    const char* env = getenv("PROTOCOL_MIRROR_FIXTURE");
    if (env != nullptr && env[0] != '\0') {
        FILE* probe = fopen(env, "r");
        if (probe != nullptr) {
            fclose(probe);
            return env;
        }
    }
    static const char* candidates[] = {
        "test/fixtures/protocol_mirror.json",
        "../test/fixtures/protocol_mirror.json",
        "../../test/fixtures/protocol_mirror.json",
        "../../../test/fixtures/protocol_mirror.json",
        "../../../../test/fixtures/protocol_mirror.json",
    };
    for (const char* candidate : candidates) {
        FILE* probe = fopen(candidate, "r");
        if (probe != nullptr) {
            fclose(probe);
            return candidate;
        }
    }
    return nullptr;
}

static std::string readAll(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return {};
    std::string body;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) body.append(buf, n);
    fclose(file);
    return body;
}

static bool firmwareOk(JsonObject seq) {
    std::string body;
    serializeJson(seq, body);
    SeqDraft draft;
    ProtocolCheckResult parsed = seqJsonParse(body.c_str(), gSteps, PC_MAX_STEPS, gClose, PC_MAX_STEPS, draft);
    if (!parsed.ok) return false;
    ProtocolCheckResult checked = protocolCheck(draft);
    return checked.ok;
}

static void test_protocol_mirror_matches_the_corpus() {
    const char* path = fixturePath();
    TEST_ASSERT_NOT_NULL_MESSAGE(path, "protocol_mirror.json not found");
    const std::string body = readAll(path);
    TEST_ASSERT_FALSE_MESSAGE(body.empty(), "protocol_mirror.json is empty");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    TEST_ASSERT_TRUE_MESSAGE(!err, err.c_str());

    JsonArray cases = doc["cases"].as<JsonArray>();
    TEST_ASSERT_TRUE(cases.size() > 0);
    for (JsonObject row : cases) {
        const char* name = row["name"] | "";
        const bool expect = row["expect"] | false;
        const bool got = firmwareOk(row["seq"].as<JsonObject>());
        char message[96];
        snprintf(message, sizeof(message), "%s firmware=%d expect=%d", name, got ? 1 : 0, expect ? 1 : 0);
        TEST_ASSERT_EQUAL_MESSAGE(expect, got, message);
    }
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_protocol_mirror_matches_the_corpus);
    return UNITY_END();
}
