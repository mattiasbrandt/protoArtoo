#include <string.h>
#include <unity.h>

#include "dome_link_encoding.h"

void setUp() {
}

void tearDown() {
}

void test_url_encode_keeps_unreserved_chars() {
    char out[32];
    size_t len = 0;

    TEST_ASSERT_TRUE(domeLinkUrlEncodeInto(out, sizeof(out), "AZaz09-_.~", &len));
    TEST_ASSERT_EQUAL_STRING("AZaz09-_.~", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), len);
}

void test_url_encode_space_as_plus() {
    char out[32];
    size_t len = 0;

    TEST_ASSERT_TRUE(domeLinkUrlEncodeInto(out, sizeof(out), "cmd one", &len));
    TEST_ASSERT_EQUAL_STRING("cmd+one", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), len);
}

void test_url_encode_reserved_as_percent_hex() {
    char out[32];
    size_t len = 0;

    TEST_ASSERT_TRUE(domeLinkUrlEncodeInto(out, sizeof(out), ":SE00;$", &len));
    TEST_ASSERT_EQUAL_STRING("%3ASE00%3B%24", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), len);
}

void test_url_encode_null_input_is_empty() {
    char out[8] = "x";
    size_t len = 99;

    TEST_ASSERT_TRUE(domeLinkUrlEncodeInto(out, sizeof(out), nullptr, &len));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_size_t(0, len);
}

void test_url_encode_rejects_null_output() {
    size_t len = 0;
    TEST_ASSERT_FALSE(domeLinkUrlEncodeInto(nullptr, 8, "x", &len));
}

void test_url_encode_rejects_too_small_buffer() {
    char out[4];
    size_t len = 0;

    TEST_ASSERT_FALSE(domeLinkUrlEncodeInto(out, sizeof(out), "%%%%", &len));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_url_encode_keeps_unreserved_chars);
    RUN_TEST(test_url_encode_space_as_plus);
    RUN_TEST(test_url_encode_reserved_as_percent_hex);
    RUN_TEST(test_url_encode_null_input_is_empty);
    RUN_TEST(test_url_encode_rejects_null_output);
    RUN_TEST(test_url_encode_rejects_too_small_buffer);
    return UNITY_END();
}
