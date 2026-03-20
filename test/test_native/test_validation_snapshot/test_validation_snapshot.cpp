#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "validation_snapshot.h"

namespace {

ValidationSnapshot makeTypicalSnapshot() {
    ValidationSnapshot snapshot = {};
    snapshot.updatedMs = 12345;

    snapshot.drive.estop = false;
    snapshot.drive.webDriveExpired = false;
    snapshot.drive.sbusSignalLost = false;
    snapshot.drive.sbusHwFailsafe = false;
    snapshot.drive.failsafeSource = FS_NONE;
    snapshot.drive.failsafeCount = 3;
    snapshot.drive.triggerMs = 12000;
    snapshot.drive.zeroMs = 12015;
    snapshot.drive.triggerToZeroMs = 15;
    snapshot.drive.watchdogMs = 11980;
    snapshot.drive.triggerSource = FS_SBUS_TIMEOUT;

    snapshot.domeLink.state = "connected";
    snapshot.domeLink.hbTx = 42;
    snapshot.domeLink.hbRx = 40;
    snapshot.domeLink.lastRxMs = 85;

    snapshot.audio.enabled = true;
    snapshot.audio.active = true;
    snapshot.audio.activeMood = 14;
    snapshot.audio.randomMin = 1;
    snapshot.audio.randomMax = 120;
    snapshot.audio.intervalQuietS = 0;
    snapshot.audio.intervalMidS = 30;
    snapshot.audio.intervalFullS = 20;
    snapshot.audio.intervalAwakeS = 10;

    snapshot.rc.mode = "dual_sbus";
    snapshot.rc.timeoutMs = 200;
    snapshot.rc.sources[0] = {"sbus1", true, true, false, false, 12};
    snapshot.rc.sources[1] = {"sbus2", true, false, true, false, 350};
    snapshot.rc.sources[2] = {"pwm", false, false, false, false, 0};
    snapshot.rc.sourceCount = 3;

    return snapshot;
}

ValidationSnapshot makeWorstCaseSnapshot() {
    ValidationSnapshot snapshot = makeTypicalSnapshot();

    snapshot.updatedMs = 0xFFFFFFFFu;
    snapshot.drive.estop = true;
    snapshot.drive.webDriveExpired = true;
    snapshot.drive.sbusSignalLost = true;
    snapshot.drive.sbusHwFailsafe = true;
    snapshot.drive.failsafeSource = FS_WATCHDOG_RESET;
    snapshot.drive.failsafeCount = 0xFFFFFFFFu;
    snapshot.drive.triggerMs = 0xFFFFFFFFu;
    snapshot.drive.zeroMs = 0xFFFFFFFFu;
    snapshot.drive.triggerToZeroMs = 0xFFFFFFFFu;
    snapshot.drive.watchdogMs = 0xFFFFFFFFu;
    snapshot.drive.triggerSource = FS_WATCHDOG_RESET;

    snapshot.domeLink.state = "lost";
    snapshot.domeLink.hbTx = 0xFFFFFFFFu;
    snapshot.domeLink.hbRx = 0xFFFFFFFFu;
    snapshot.domeLink.lastRxMs = 2147483647;

    snapshot.audio.enabled = true;
    snapshot.audio.active = true;
    snapshot.audio.activeMood = 255;
    snapshot.audio.randomMin = 65535;
    snapshot.audio.randomMax = 65535;
    snapshot.audio.intervalQuietS = 3600;
    snapshot.audio.intervalMidS = 3600;
    snapshot.audio.intervalFullS = 3600;
    snapshot.audio.intervalAwakeS = 3600;

    snapshot.rc.mode = "single_sbus";
    snapshot.rc.timeoutMs = 60000;
    snapshot.rc.sources[0] = {"sbus1", true, false, true, true, 60000};
    snapshot.rc.sources[1] = {"sbus2", true, false, true, true, 60000};
    snapshot.rc.sources[2] = {"pwm", true, false, true, false, 60000};
    snapshot.rc.sourceCount = 3;

    return snapshot;
}

}  // namespace

void test_populateValidationJson_typical_valid() {
    ValidationSnapshot snapshot = makeTypicalSnapshot();

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateValidationJson(doc, snapshot));

    char out[1536] = {};
    size_t n = serializeJson(doc, out, sizeof(out));

    TEST_ASSERT_GREATER_THAN(0u, n);
    TEST_ASSERT_LESS_THAN(1536u, n);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);

    TEST_ASSERT_NOT_NULL(strstr(out, "\"drive\":{\"estop\":"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"domeLink\":{\"state\":\"connected\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"audio\":{\"enabled\":true"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"rc\":{\"mode\":\"dual_sbus\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sources\":{\"sbus1\":"));
}

void test_populateValidationJson_worst_case_fits_buffer() {
    ValidationSnapshot snapshot = makeWorstCaseSnapshot();

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateValidationJson(doc, snapshot));

    char out[1536] = {};
    size_t n = serializeJson(doc, out, sizeof(out));

    TEST_ASSERT_GREATER_THAN(0u, n);
    TEST_ASSERT_LESS_THAN(1536u, n);
}

void test_populateValidationJson_rejects_null_contract_fields() {
    ValidationSnapshot snapshot = makeTypicalSnapshot();
    snapshot.rc.mode = nullptr;

    JsonDocument doc;
    TEST_ASSERT_FALSE(populateValidationJson(doc, snapshot));

    snapshot = makeTypicalSnapshot();
    snapshot.domeLink.state = nullptr;
    TEST_ASSERT_FALSE(populateValidationJson(doc, snapshot));
}

void test_populateValidationJson_key_order_matches_contract() {
    ValidationSnapshot snapshot = makeTypicalSnapshot();

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateValidationJson(doc, snapshot));

    char out[1536] = {};
    serializeJson(doc, out, sizeof(out));

    const char* pUpdated = strstr(out, "\"updatedMs\"");
    const char* pDrive = strstr(out, "\"drive\"");
    const char* pDome = strstr(out, "\"domeLink\"");
    const char* pAudio = strstr(out, "\"audio\"");
    const char* pRc = strstr(out, "\"rc\"");

    TEST_ASSERT_NOT_NULL(pUpdated);
    TEST_ASSERT_NOT_NULL(pDrive);
    TEST_ASSERT_NOT_NULL(pDome);
    TEST_ASSERT_NOT_NULL(pAudio);
    TEST_ASSERT_NOT_NULL(pRc);

    TEST_ASSERT_TRUE(pUpdated < pDrive);
    TEST_ASSERT_TRUE(pDrive < pDome);
    TEST_ASSERT_TRUE(pDome < pAudio);
    TEST_ASSERT_TRUE(pAudio < pRc);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_populateValidationJson_typical_valid);
    RUN_TEST(test_populateValidationJson_worst_case_fits_buffer);
    RUN_TEST(test_populateValidationJson_rejects_null_contract_fields);
    RUN_TEST(test_populateValidationJson_key_order_matches_contract);
    return UNITY_END();
}
