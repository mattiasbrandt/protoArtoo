// =============================================================================
// test/test_native/test_web_webp/test_web_webp.cpp
//
// The MIME decision for Component Picker photographs (#316).
//
// PsychicHttp's table falls back to text/plain for .webp. The owned handler
// answers image/webp instead, and only for the /part_<id>.webp shape a page
// names. These tests cover that decision; they do not run the device server.
// =============================================================================
#include <unity.h>

#include <string.h>

#include "web_webp.h"

void setUp() {
}
void tearDown() {
}

void test_a_part_photograph_answers_image_webp_not_text_plain() {
    const char* type = webPartPhotoContentType("/part_hotrc_ds650.webp");
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_STRING("image/webp", type);
    TEST_ASSERT_TRUE(strcmp(type, "text/plain") != 0);
}

void test_the_content_type_helper_is_image_webp() {
    TEST_ASSERT_EQUAL_STRING("image/webp", webWebpContentType());
}

void test_every_registry_photograph_path_is_claimed() {
    static const char* kPaths[] = {
        "/part_artoo_pcb.webp",
        "/part_firebeetle2.webp",
        "/part_hotrc_ds650.webp",
        "/part_rc_transmitter_pwm.webp",
        "/part_rc_transmitter_sbus.webp",
        "/part_rc_transmitter_elrs.webp",
        "/part_xbox_controller.webp",
        "/part_pca9685.webp",
        "/part_pololu_maestro.webp",
        "/part_isdt_esc70.webp",
        "/part_syren10.webp",
        "/part_astropixels_plus.webp",
        "/part_teeces.webp",
        "/part_hoverboard.webp",
        "/part_sabertooth_2x25.webp",
        "/part_flipsky_mini_v6_vesc.webp",
        "/part_dy_sv5w.webp",
        "/part_mp3_trigger.webp",
        "/part_chirp.webp",
        "/part_dfplayer_mini.webp",
    };
    for (size_t i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(webPathIsPartPhoto(kPaths[i]), kPaths[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("image/webp", webPartPhotoContentType(kPaths[i]),
                                         kPaths[i]);
    }
}

void test_a_jpg_part_path_is_not_ours() {
    // serveStatic() already knows .jpg; claiming it here would steal the
    // existing board photograph at /board_artoo_esp32.jpg's cousin.
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_hotrc_ds650.jpg"));
    TEST_ASSERT_NULL(webPartPhotoContentType("/part_hotrc_ds650.jpg"));
}

void test_a_webp_that_is_not_a_part_photograph_is_not_ours() {
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/board_artoo_esp32.webp"));
    TEST_ASSERT_NULL(webPartPhotoContentType("/board_artoo_esp32.webp"));
}

void test_null_and_empty_are_not_part_photographs() {
    TEST_ASSERT_FALSE(webPathIsPartPhoto(nullptr));
    TEST_ASSERT_FALSE(webPathIsPartPhoto(""));
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_.webp"));
}

void test_path_traversal_and_extra_segments_are_rejected() {
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_../secrets.webp"));
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_foo/bar.webp"));
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_FOO.webp"));
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_hotrc_ds650.webp?x=1"));
    TEST_ASSERT_FALSE(webPathIsPartPhoto("part_hotrc_ds650.webp"));
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_hotrc_ds650.WEBP"));
}

void test_an_svg_drawing_path_is_not_claimed_as_a_photograph() {
    // Line drawings are #382's and may live at /part_<id>.svg on the legacy
    // set. This handler is the WebP photograph path only.
    TEST_ASSERT_FALSE(webPathIsPartPhoto("/part_hotrc_ds650.svg"));
    TEST_ASSERT_NULL(webPartPhotoContentType("/part_hotrc_ds650.svg"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_part_photograph_answers_image_webp_not_text_plain);
    RUN_TEST(test_the_content_type_helper_is_image_webp);
    RUN_TEST(test_every_registry_photograph_path_is_claimed);
    RUN_TEST(test_a_jpg_part_path_is_not_ours);
    RUN_TEST(test_a_webp_that_is_not_a_part_photograph_is_not_ours);
    RUN_TEST(test_null_and_empty_are_not_part_photographs);
    RUN_TEST(test_path_traversal_and_extra_segments_are_rejected);
    RUN_TEST(test_an_svg_drawing_path_is_not_claimed_as_a_photograph);
    return UNITY_END();
}
