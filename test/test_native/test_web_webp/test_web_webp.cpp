// =============================================================================
// test/test_native/test_web_webp/test_web_webp.cpp
//
// The MIME decision for Component Picker photographs (#316).
//
// PsychicHttp's table falls back to text/plain for .webp. The owned handler
// answers image/webp instead, and only for the /<registry-id>.webp shape a
// page names. These tests cover that decision; they do not run the device
// server.
// =============================================================================
#include <unity.h>

#include <string.h>

#include "web_webp.h"

void setUp() {
}
void tearDown() {
}

void test_a_product_photograph_answers_image_webp_not_text_plain() {
    const char* type = webProductPhotoContentType("/hotrc_ds650.webp");
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_STRING("image/webp", type);
    TEST_ASSERT_TRUE(strcmp(type, "text/plain") != 0);
}

void test_the_content_type_helper_is_image_webp() {
    TEST_ASSERT_EQUAL_STRING("image/webp", webWebpContentType());
}

void test_every_registry_photograph_path_is_claimed() {
    static const char* kPaths[] = {
        "/artoo_pcb.webp",
        "/firebeetle2.webp",
        "/hotrc_ds650.webp",
        "/rc_transmitter_pwm.webp",
        "/rc_transmitter_sbus.webp",
        "/rc_transmitter_elrs.webp",
        "/xbox_controller.webp",
        "/pca9685.webp",
        "/pololu_maestro.webp",
        "/isdt_esc70.webp",
        "/syren10.webp",
        "/astropixels_plus.webp",
        "/teeces.webp",
        "/hoverboard.webp",
        "/sabertooth_2x25.webp",
        "/flipsky_mini_v6_vesc.webp",
        "/dy_sv5w.webp",
        "/mp3_trigger.webp",
        "/chirp.webp",
        "/dfplayer_mini.webp",
    };
    for (size_t i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(webPathIsProductPhoto(kPaths[i]), kPaths[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("image/webp", webProductPhotoContentType(kPaths[i]),
                                         kPaths[i]);
    }
}

void test_a_jpg_product_path_is_not_ours() {
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/hotrc_ds650.jpg"));
    TEST_ASSERT_NULL(webProductPhotoContentType("/hotrc_ds650.jpg"));
}

void test_null_and_empty_are_not_product_photographs() {
    TEST_ASSERT_FALSE(webPathIsProductPhoto(nullptr));
    TEST_ASSERT_FALSE(webPathIsProductPhoto(""));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/.webp"));
}

void test_path_traversal_and_extra_segments_are_rejected() {
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/../secrets.webp"));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/foo/bar.webp"));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/FOO.webp"));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/hotrc_ds650.webp?x=1"));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("hotrc_ds650.webp"));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/hotrc_ds650.WEBP"));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/1abc.webp"));
}

void test_an_svg_path_is_not_claimed_as_a_photograph() {
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/hotrc_ds650.svg"));
    TEST_ASSERT_NULL(webProductPhotoContentType("/hotrc_ds650.svg"));
}

void test_the_webp_suffix_is_required() {
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/hotrc_ds650"));
    TEST_ASSERT_FALSE(webPathIsProductPhoto("/hotrc_ds650.web"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_product_photograph_answers_image_webp_not_text_plain);
    RUN_TEST(test_the_content_type_helper_is_image_webp);
    RUN_TEST(test_every_registry_photograph_path_is_claimed);
    RUN_TEST(test_a_jpg_product_path_is_not_ours);
    RUN_TEST(test_null_and_empty_are_not_product_photographs);
    RUN_TEST(test_path_traversal_and_extra_segments_are_rejected);
    RUN_TEST(test_an_svg_path_is_not_claimed_as_a_photograph);
    RUN_TEST(test_the_webp_suffix_is_required);
    return UNITY_END();
}
