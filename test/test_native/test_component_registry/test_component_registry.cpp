// =============================================================================
// test/test_native/test_component_registry/test_component_registry.cpp
//
// The Component Registry's derived answers (include/component_registry.h).
//
// What earns its place here is the arithmetic the rest of the firmware rests
// on -- which families have a Component Member, what a stored member resolves
// to, and that a roadmap row never looks selectable. The row contents
// themselves are #303's lineup and are checked for internal consistency by
// test/test_tools/test_component_registry_drift.py, which reads them as text.
//
// Native build: PA_BOARD is PA_BOARD_ARTOO_ESP32 and PA_AUDIO_DRIVER is
// AUDIO_SOFT_UART (platformio.ini [env:native]), so the Body Controller
// expectations and the default sound member below are artoo-esp32's.
// =============================================================================
#include <unity.h>

#include <string.h>

#include "component_registry.h"

void setUp() {
}
void tearDown() {
}

// A Component Member setting exists ONLY where identity reports more than one
// selectable member (CONTEXT.md "Component Member"). This is the invariant the
// manifest's member_key column has to answer to -- a family that declares a key
// without a second member would put a chooser in front of a builder with
// nothing to choose.
void test_a_declared_member_key_means_more_than_one_selectable_member() {
    for (size_t i = 0; i < COMPONENT_CATEGORY_TABLE_SIZE; ++i) {
        const ComponentCategoryEntry& cat = COMPONENT_CATEGORIES[i];
        if (cat.memberKey != nullptr) {
            TEST_ASSERT_TRUE_MESSAGE(componentCategoryHasMemberSetting(cat.id), cat.token);
        }
    }
}

// Sound is that family today, and the whole point of carrying three drivers.
void test_sound_is_the_family_with_a_member_setting() {
    const ComponentCategoryEntry* sound = componentCategory(COMPONENT_CATEGORY_SOUND);
    TEST_ASSERT_NOT_NULL(sound);
    TEST_ASSERT_EQUAL_STRING("snd_member", sound->memberKey);
    TEST_ASSERT_EQUAL_UINT8(3, componentCategorySelectableCount(COMPONENT_CATEGORY_SOUND));
}

// Foot Drive is the contrast case ADR 0042's own table names: one part this
// image can drive, so no member and no picker.
void test_foot_drive_has_one_selectable_member_and_no_member_setting() {
    const ComponentCategoryEntry* drive = componentCategory(COMPONENT_CATEGORY_FOOT_DRIVE);
    TEST_ASSERT_NOT_NULL(drive);
    TEST_ASSERT_NULL(drive->memberKey);
    TEST_ASSERT_EQUAL_UINT8(1, componentCategorySelectableCount(COMPONENT_CATEGORY_FOOT_DRIVE));
}

// A roadmap part is present in identity with no driver -- the reversal ADR 0042
// recorded on #302's third pass. Present, and never selectable.
void test_every_roadmap_row_is_present_and_carries_no_driver() {
    size_t roadmap = 0;
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        if (COMPONENT_PARTS[i].status == COMPONENT_STATUS_ROADMAP) {
            ++roadmap;
            TEST_ASSERT_FALSE_MESSAGE(COMPONENT_PARTS[i].included, COMPONENT_PARTS[i].id);
            TEST_ASSERT_FALSE_MESSAGE(componentPartIsSelectable(COMPONENT_PARTS[i]),
                                      COMPONENT_PARTS[i].id);
        }
    }
    TEST_ASSERT_EQUAL_size_t(9, roadmap);
}

// DFPlayer Mini is the named case: AUDIO_DFPLAYER used to be an #error, and is
// now a row in the image that nothing drives.
void test_dfplayer_mini_is_a_row_with_no_driver() {
    const ComponentPartEntry* part = componentPartById("dfplayer_mini");
    TEST_ASSERT_NOT_NULL(part);
    TEST_ASSERT_EQUAL_UINT8(COMPONENT_STATUS_ROADMAP, part->status);
    TEST_ASSERT_FALSE(part->included);
    TEST_ASSERT_EQUAL_UINT8(COMPONENT_CATEGORY_SOUND, part->category);
}

// Each sound driver returns its own row's capability word, so this is the one
// place the three numbers are pinned. 0x0F / 0x0D / 0x3F are the values the
// drivers shipped before the registry owned them (#340).
void test_sound_rows_declare_the_capability_words_their_drivers_return() {
    TEST_ASSERT_EQUAL_UINT8(0x0F, componentPartCapabilities("dy_sv5w"));
    TEST_ASSERT_EQUAL_UINT8(0x0D, componentPartCapabilities("mp3_trigger"));
    TEST_ASSERT_EQUAL_UINT8(0x3F, componentPartCapabilities("chirp"));
    TEST_ASSERT_EQUAL_UINT8(0x00, componentPartCapabilities("dfplayer_mini"));
    // An id no row declares answers the same as a row declaring nothing.
    TEST_ASSERT_EQUAL_UINT8(0x00, componentPartCapabilities("no_such_product"));
}

// A stored member survives a firmware change that dropped the part, and a
// member value borrowed from another family is not honoured -- both degrade to
// the build default rather than to silence.
void test_a_stored_member_resolves_or_falls_back_to_the_default() {
    const ComponentPartEntry* chirp = componentPartById("chirp");
    TEST_ASSERT_NOT_NULL(chirp);
    TEST_ASSERT_EQUAL_PTR(chirp, componentResolveMember(COMPONENT_CATEGORY_SOUND, chirp->value));

    const uint8_t fallback = componentCategoryDefaultMember(COMPONENT_CATEGORY_SOUND);
    TEST_ASSERT_EQUAL_STRING("dy_sv5w", componentPartByValue(fallback)->id);

    // A value no row carries.
    TEST_ASSERT_EQUAL_STRING("dy_sv5w", componentResolveMember(COMPONENT_CATEGORY_SOUND, 250)->id);
    // A roadmap row in the right family.
    const ComponentPartEntry* dfplayer = componentPartById("dfplayer_mini");
    TEST_ASSERT_EQUAL_STRING("dy_sv5w",
                             componentResolveMember(COMPONENT_CATEGORY_SOUND, dfplayer->value)->id);
    // A selectable row in the wrong family.
    const ComponentPartEntry* hoverboard = componentPartById("hoverboard");
    TEST_ASSERT_EQUAL_STRING("dy_sv5w",
                             componentResolveMember(COMPONENT_CATEGORY_SOUND, hoverboard->value)->id);
}

// The Body Controller rows answer from PA_BOARD rather than from a capability:
// the running image is the answer to which board is fitted.
void test_body_controller_reports_the_board_this_image_is_for() {
    TEST_ASSERT_TRUE(componentPartById("artoo_pcb")->included);
    TEST_ASSERT_FALSE(componentPartById("firebeetle2")->included);
    TEST_ASSERT_EQUAL_UINT8(1, componentCategorySelectableCount(COMPONENT_CATEGORY_BODY_CONTROLLER));
}

// A stable numeric id is what a Component Member persists, so a duplicate would
// re-point a controller that had already stored the other row's number.
void test_part_values_and_ids_are_unique_and_nonzero() {
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, COMPONENT_PARTS[i].value, COMPONENT_PARTS[i].id);
        for (size_t j = i + 1; j < COMPONENT_PART_COUNT; ++j) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(COMPONENT_PARTS[i].value, COMPONENT_PARTS[j].value,
                                          COMPONENT_PARTS[i].id);
            TEST_ASSERT_TRUE_MESSAGE(strcmp(COMPONENT_PARTS[i].id, COMPONENT_PARTS[j].id) != 0,
                                     COMPONENT_PARTS[i].id);
        }
    }
}

// #303's resolved lineup: seven categories, twenty-one products.
void test_the_registry_carries_the_whole_lineup() {
    TEST_ASSERT_EQUAL_size_t(7, COMPONENT_CATEGORY_TABLE_SIZE);
    TEST_ASSERT_EQUAL_size_t(7, (size_t)COMPONENT_CATEGORY_COUNT);
    TEST_ASSERT_EQUAL_size_t(21, COMPONENT_PART_COUNT);
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        TEST_ASSERT_NOT_NULL_MESSAGE(componentCategory(COMPONENT_PARTS[i].category),
                                     COMPONENT_PARTS[i].id);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_declared_member_key_means_more_than_one_selectable_member);
    RUN_TEST(test_sound_is_the_family_with_a_member_setting);
    RUN_TEST(test_foot_drive_has_one_selectable_member_and_no_member_setting);
    RUN_TEST(test_every_roadmap_row_is_present_and_carries_no_driver);
    RUN_TEST(test_dfplayer_mini_is_a_row_with_no_driver);
    RUN_TEST(test_sound_rows_declare_the_capability_words_their_drivers_return);
    RUN_TEST(test_a_stored_member_resolves_or_falls_back_to_the_default);
    RUN_TEST(test_body_controller_reports_the_board_this_image_is_for);
    RUN_TEST(test_part_values_and_ids_are_unique_and_nonzero);
    RUN_TEST(test_the_registry_carries_the_whole_lineup);
    return UNITY_END();
}
