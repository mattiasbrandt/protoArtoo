// =============================================================================
// test/test_native/test_json_formatters/test_json_formatters.cpp
//
// Native unit tests for pure JSON formatting helpers.
// Tests: formatWifiJson, formatSerialJson, formatHealthJson.
// =============================================================================
#include <string.h>
#include <unity.h>

#include "api_helpers.h"

void setUp() {
}
void tearDown() {
}

// --- formatWifiJson() tests ---

void test_formatWifiJson_contains_apSsid() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", false, false, "", 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"apSsid\":\"protoArtoo\""));
}

void test_formatWifiJson_contains_apIp() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", false, false, "", 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"apIp\":\"192.168.4.1\""));
}

void test_formatWifiJson_staEnabled_true() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", true, false, "", 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"staEnabled\":true"));
}

void test_formatWifiJson_staEnabled_false() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", false, false, "", 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"staEnabled\":false"));
}

void test_formatWifiJson_staConnected_true() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", true, true, "10.0.0.42", -65);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"staConnected\":true"));
}

void test_formatWifiJson_staConnected_false() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", false, false, "", 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"staConnected\":false"));
}

void test_formatWifiJson_staIp_present_when_connected() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", true, true, "10.0.0.42", -65);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"staIp\":\"10.0.0.42\""));
}

void test_formatWifiJson_staIp_empty_when_disconnected() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", false, false, "", 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"staIp\":\"\""));
}

void test_formatWifiJson_wifiRssi_present() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", true, true, "10.0.0.42", -72);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"wifiRssi\":-72"));
}

void test_formatWifiJson_is_valid_json_object() {
    char out[192];
    formatWifiJson(out, sizeof(out), "protoArtoo", "192.168.4.1", false, false, "", 0);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[strlen(out) - 1]);
}

// --- formatSerialJson() tests ---

void test_formatSerialJson_dome_active_true() {
    char out[768];
    formatSerialJson(out, sizeof(out), true, 10, 20);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"active\":true"));
}

void test_formatSerialJson_dome_active_false() {
    char out[768];
    formatSerialJson(out, sizeof(out), false, 0, 0);
    const char* dome = strstr(out, "\"dome\"");
    TEST_ASSERT_NOT_NULL(dome);
    TEST_ASSERT_NOT_NULL(strstr(dome, "\"active\":false"));
}

void test_formatSerialJson_heartbeatRx() {
    char out[768];
    formatSerialJson(out, sizeof(out), true, 42, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"heartbeatRx\":42"));
}

void test_formatSerialJson_heartbeatTx() {
    char out[768];
    formatSerialJson(out, sizeof(out), true, 0, 99);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"heartbeatTx\":99"));
}

void test_formatSerialJson_debug_always_active() {
    char out[768];
    formatSerialJson(out, sizeof(out), false, 0, 0);
    const char* debug = strstr(out, "\"debug\"");
    TEST_ASSERT_NOT_NULL(debug);
    TEST_ASSERT_NOT_NULL(strstr(debug, "\"active\":true"));
}

void test_formatSerialJson_sound_always_inactive() {
    char out[768];
    formatSerialJson(out, sizeof(out), true, 0, 0);
    const char* sound = strstr(out, "\"sound\"");
    TEST_ASSERT_NOT_NULL(sound);
    TEST_ASSERT_NOT_NULL(strstr(sound, "\"active\":false"));
}

void test_formatSerialJson_is_valid_json_object() {
    char out[768];
    formatSerialJson(out, sizeof(out), false, 0, 0);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[strlen(out) - 1]);
}

// --- formatHealthJson() tests ---

void test_formatHealthJson_estop_true() {
    char out[320];
    formatHealthJson(out, sizeof(out), true, false, false, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"estop\":true"));
}

void test_formatHealthJson_estop_false() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"estop\":false"));
}

void test_formatHealthJson_sbusSignalLost_true() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, true, false, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sbusSignalLost\":true"));
}

void test_formatHealthJson_sbusHwFailsafe_true() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, true, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sbusHwFailsafe\":true"));
}

void test_formatHealthJson_webControlEnabled_true() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, true, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"webControlEnabled\":true"));
}

void test_formatHealthJson_wifiConnected_true() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, true, true, false, 100000, 90000,
                     75000UL, -65);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"wifiConnected\":true"));
}

void test_formatHealthJson_wifiClientConnected_false() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"wifiClientConnected\":false"));
}

void test_formatHealthJson_littleFsReady_true() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, true, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"littleFsReady\":true"));
}

void test_formatHealthJson_heapFree() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, false, 123456,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"heapFree\":123456"));
}

void test_formatHealthJson_heapMin() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, false, 100000,
                     77777, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"heapMin\":77777"));
}

void test_formatHealthJson_heapLargestBlock() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"heapLargestBlock\":75000"));
}

void test_formatHealthJson_wifiRssi_negative() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, true, true, false, 100000, 90000,
                     75000UL, -72);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"wifiRssi\":-72"));
}

void test_formatHealthJson_wifiRssi_zero_when_disconnected() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"wifiRssi\":0"));
}

void test_formatHealthJson_is_valid_json_object() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, false, false, false, 100000,
                     90000, 75000UL, 0);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[strlen(out) - 1]);
}

void test_formatHealthJson_is_valid_json_with_largest_block() {
    char out[320];
    formatHealthJson(out, sizeof(out), false, false, false, false, true, false, true, 200000,
                     180000, 75000UL, -70);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[strlen(out) - 1]);
}

void test_deriveWiFiConnectivityFields_ap_only_no_clients() {
    WiFiConnectivityFields fields = deriveWiFiConnectivityFields(true, false, 0U, -65);
    TEST_ASSERT_TRUE(fields.wifiConnected);
    TEST_ASSERT_FALSE(fields.wifiClientConnected);
    TEST_ASSERT_EQUAL(0, fields.wifiRssi);
}

void test_deriveWiFiConnectivityFields_ap_only_with_client() {
    WiFiConnectivityFields fields = deriveWiFiConnectivityFields(true, false, 2U, -65);
    TEST_ASSERT_TRUE(fields.wifiConnected);
    TEST_ASSERT_TRUE(fields.wifiClientConnected);
    TEST_ASSERT_EQUAL(0, fields.wifiRssi);
}

void test_deriveWiFiConnectivityFields_sta_only_connected() {
    WiFiConnectivityFields fields = deriveWiFiConnectivityFields(false, true, 0U, -72);
    TEST_ASSERT_TRUE(fields.wifiConnected);
    TEST_ASSERT_FALSE(fields.wifiClientConnected);
    TEST_ASSERT_EQUAL(-72, fields.wifiRssi);
}

void test_deriveWiFiConnectivityFields_disconnected() {
    WiFiConnectivityFields fields = deriveWiFiConnectivityFields(false, false, 0U, -80);
    TEST_ASSERT_FALSE(fields.wifiConnected);
    TEST_ASSERT_FALSE(fields.wifiClientConnected);
    TEST_ASSERT_EQUAL(0, fields.wifiRssi);
}


int main() {
    UNITY_BEGIN();

    RUN_TEST(test_formatWifiJson_contains_apSsid);
    RUN_TEST(test_formatWifiJson_contains_apIp);
    RUN_TEST(test_formatWifiJson_staEnabled_true);
    RUN_TEST(test_formatWifiJson_staEnabled_false);
    RUN_TEST(test_formatWifiJson_staConnected_true);
    RUN_TEST(test_formatWifiJson_staConnected_false);
    RUN_TEST(test_formatWifiJson_staIp_present_when_connected);
    RUN_TEST(test_formatWifiJson_staIp_empty_when_disconnected);
    RUN_TEST(test_formatWifiJson_is_valid_json_object);

    RUN_TEST(test_formatSerialJson_dome_active_true);
    RUN_TEST(test_formatSerialJson_dome_active_false);
    RUN_TEST(test_formatSerialJson_heartbeatRx);
    RUN_TEST(test_formatSerialJson_heartbeatTx);
    RUN_TEST(test_formatSerialJson_debug_always_active);
    RUN_TEST(test_formatSerialJson_sound_always_inactive);
    RUN_TEST(test_formatSerialJson_is_valid_json_object);

    RUN_TEST(test_deriveWiFiConnectivityFields_ap_only_no_clients);
    RUN_TEST(test_deriveWiFiConnectivityFields_ap_only_with_client);
    RUN_TEST(test_deriveWiFiConnectivityFields_sta_only_connected);
    RUN_TEST(test_deriveWiFiConnectivityFields_disconnected);

    RUN_TEST(test_formatHealthJson_estop_true);
    RUN_TEST(test_formatHealthJson_estop_false);
    RUN_TEST(test_formatHealthJson_sbusSignalLost_true);
    RUN_TEST(test_formatHealthJson_sbusHwFailsafe_true);
    RUN_TEST(test_formatHealthJson_webControlEnabled_true);
    RUN_TEST(test_formatHealthJson_wifiConnected_true);
    RUN_TEST(test_formatHealthJson_wifiClientConnected_false);
    RUN_TEST(test_formatHealthJson_littleFsReady_true);
    RUN_TEST(test_formatHealthJson_heapFree);
    RUN_TEST(test_formatHealthJson_heapMin);
    RUN_TEST(test_formatHealthJson_heapLargestBlock);
    RUN_TEST(test_formatHealthJson_wifiRssi_negative);
    RUN_TEST(test_formatHealthJson_wifiRssi_zero_when_disconnected);
    RUN_TEST(test_formatHealthJson_is_valid_json_object);
    RUN_TEST(test_formatHealthJson_is_valid_json_with_largest_block);

    return UNITY_END();
}
