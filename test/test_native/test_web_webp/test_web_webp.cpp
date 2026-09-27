// =============================================================================
// test/test_native/test_web_webp/test_web_webp.cpp
//
// The MIME decision for the pictures a page requests as /<id>.webp (#316,
// #355).
//
// PsychicHttp's table falls back to text/plain for .webp. The owned handler
// answers image/webp instead, for the /<id>.webp shape a page names --
// product photographs and Droid Build pictures alike. These tests cover that
// decision; they do not run the device server.
// =============================================================================
#include <unity.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "web_webp.h"

void setUp() {
}
void tearDown() {
}

void test_the_content_type_is_image_webp_not_text_plain() {
    TEST_ASSERT_EQUAL_STRING("image/webp", webWebpContentType());
    TEST_ASSERT_TRUE(strcmp(webWebpContentType(), "text/plain") != 0);
}

void test_every_registry_photograph_path_is_claimed() {
    static const char* kPaths[] = {
        "/artoo_pcb.webp",
        "/firebeetle2.webp",
        "/hotrc_ds650.webp",
        "/rc_radio.webp",
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
        TEST_ASSERT_TRUE_MESSAGE(webWebpPictureRequestClaimed(true, kPaths[i]), kPaths[i]);
    }
}

// #355: /mrbaddeley.webp is a Droid Build picture (data/droid_parts.js
// "picture"), not a Component Registry row. When the handler was one endpoint
// per registry row it fell through to serveStatic() and answered text/plain.
void test_a_droid_build_picture_is_claimed() {
    TEST_ASSERT_TRUE(webWebpPictureRequestClaimed(true, "/mrbaddeley.webp"));
}

// Every .webp an asset set ships is a picture some page requests as
// /<name>. Walking the sets rather than listing names keeps this true as
// pictures are added; a file whose name the handler would not claim (an
// uppercase letter, a hyphen) would answer text/plain, so it fails here.
static DIR* openAssetSetsRoot(char* rootOut, size_t rootLen) {
    static const char* candidates[] = {
        "data/asset-sets",
        "../data/asset-sets",
        "../../data/asset-sets",
        "../../../data/asset-sets",
        "../../../../data/asset-sets",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        DIR* dir = opendir(candidates[i]);
        if (dir != nullptr) {
            snprintf(rootOut, rootLen, "%s", candidates[i]);
            return dir;
        }
    }
    return nullptr;
}

void test_every_picture_an_asset_set_ships_is_claimed() {
    char root[64];
    DIR* sets = openAssetSetsRoot(root, sizeof(root));
    // Fail loudly rather than pass on an empty walk from the wrong directory.
    TEST_ASSERT_NOT_NULL_MESSAGE(sets, "data/asset-sets not found from the test working directory");

    size_t pictures = 0;
    bool sawDroidBuildPicture = false;
    struct dirent* set;
    while ((set = readdir(sets)) != nullptr) {
        if (set->d_name[0] == '.') {
            continue;
        }
        char setPath[192];
        snprintf(setPath, sizeof(setPath), "%s/%s", root, set->d_name);
        DIR* files = opendir(setPath);
        if (files == nullptr) {
            continue;  // a file beside the sets, not a set
        }
        struct dirent* file;
        while ((file = readdir(files)) != nullptr) {
            const size_t len = strlen(file->d_name);
            if (len < 5 || strcmp(file->d_name + len - 5, ".webp") != 0) {
                continue;
            }
            char uri[128];
            snprintf(uri, sizeof(uri), "/%s", file->d_name);
            TEST_ASSERT_TRUE_MESSAGE(webWebpPictureRequestClaimed(true, uri), uri);
            if (strcmp(uri, "/mrbaddeley.webp") == 0) {
                sawDroidBuildPicture = true;
            }
            ++pictures;
        }
        closedir(files);
    }
    closedir(sets);

    TEST_ASSERT_TRUE_MESSAGE(pictures > 0, "no .webp found under data/asset-sets");
    TEST_ASSERT_TRUE_MESSAGE(sawDroidBuildPicture,
                             "data/asset-sets no longer ships mrbaddeley.webp; update this test");
}

// Only a GET is a picture request. Anything else falls through to the
// routes and the not-found handler, as it did with one GET endpoint per id.
void test_only_a_get_is_claimed() {
    TEST_ASSERT_FALSE(webWebpPictureRequestClaimed(false, "/mrbaddeley.webp"));
    TEST_ASSERT_FALSE(webWebpPictureRequestClaimed(false, "/hotrc_ds650.webp"));
}

void test_a_jpg_product_path_is_not_ours() {
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/hotrc_ds650.jpg"));
    TEST_ASSERT_FALSE(webWebpPictureRequestClaimed(true, "/hotrc_ds650.jpg"));
}

void test_null_and_empty_are_not_pictures() {
    TEST_ASSERT_FALSE(webPathIsWebpPicture(nullptr));
    TEST_ASSERT_FALSE(webPathIsWebpPicture(""));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/.webp"));
    TEST_ASSERT_FALSE(webWebpPictureRequestClaimed(true, nullptr));
}

void test_path_traversal_and_extra_segments_are_rejected() {
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/../secrets.webp"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/foo/bar.webp"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/FOO.webp"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/hotrc_ds650.webp?x=1"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("hotrc_ds650.webp"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/hotrc_ds650.WEBP"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/1abc.webp"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/mr%62addeley.webp"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/mr-baddeley.webp"));
    // The claim adds only the method; it never widens the path shape.
    TEST_ASSERT_FALSE(webWebpPictureRequestClaimed(true, "/../secrets.webp"));
    TEST_ASSERT_FALSE(webWebpPictureRequestClaimed(true, "/foo/bar.webp"));
}

void test_an_svg_path_is_not_claimed_as_a_picture() {
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/hotrc_ds650.svg"));
}

void test_the_webp_suffix_is_required() {
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/hotrc_ds650"));
    TEST_ASSERT_FALSE(webPathIsWebpPicture("/hotrc_ds650.web"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_content_type_is_image_webp_not_text_plain);
    RUN_TEST(test_every_registry_photograph_path_is_claimed);
    RUN_TEST(test_a_droid_build_picture_is_claimed);
    RUN_TEST(test_every_picture_an_asset_set_ships_is_claimed);
    RUN_TEST(test_only_a_get_is_claimed);
    RUN_TEST(test_a_jpg_product_path_is_not_ours);
    RUN_TEST(test_null_and_empty_are_not_pictures);
    RUN_TEST(test_path_traversal_and_extra_segments_are_rejected);
    RUN_TEST(test_an_svg_path_is_not_claimed_as_a_picture);
    RUN_TEST(test_the_webp_suffix_is_required);
    return UNITY_END();
}
