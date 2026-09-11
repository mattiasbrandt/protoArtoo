// =============================================================================
// test/test_native/test_audio_sound_member/test_audio_sound_member.cpp
//
// The Sound Component Member -> driver map (include/audio_sound_member.h).
//
// This is the half of #380's fix that can be proven without hardware. The
// binding site itself is src/main.cpp, which the native build does not compile;
// what lives here is the resolution it calls -- every selectable member lands
// on its own driver, and a stored value this image cannot drive lands on the
// build default rather than on whichever instance happened to be first.
//
// The defect this guards: with audio output disabled at boot the owning task is
// never created, so a resolution that ran inside AudioTask left every status
// surface naming the build-time default module. Measured on two boards, both
// reporting DY-SV5W/15 for a configured mp3_trigger (13) and chirp (63).
//
// Native build: PA_AUDIO_DRIVER is AUDIO_SOFT_UART (platformio.ini
// [env:native]), so the build default below is dy_sv5w's.
// =============================================================================
#include <unity.h>

#include <string.h>

#include "audio_sound_member.h"
#include "component_registry.h"

void setUp() {
}
void tearDown() {
}

static uint8_t memberValue(const char* id) {
    const ComponentPartEntry* part = componentPartById(id);
    TEST_ASSERT_NOT_NULL_MESSAGE(part, id);
    return part->value;
}

// The three modules the operator can choose between, with the names and
// capability words the Wave 0 bench checkpoint read off the two controllers
// (#380). Spelled out rather than derived, because the whole defect was a
// surface reporting one module's word for another's.
void test_each_selectable_member_binds_to_its_own_driver() {
    audioBindSoundMember(memberValue("dy_sv5w"));
    TEST_ASSERT_EQUAL_STRING("dy_sv5w", audioActiveSoundMember().part->id);
    TEST_ASSERT_EQUAL_STRING("DY-SV5W", audioActiveSoundMember().driver->driverName());
    TEST_ASSERT_EQUAL_UINT8(15, audioActiveSoundMember().driver->capabilities());

    audioBindSoundMember(memberValue("mp3_trigger"));
    TEST_ASSERT_EQUAL_STRING("mp3_trigger", audioActiveSoundMember().part->id);
    TEST_ASSERT_EQUAL_STRING("MP3Trigger", audioActiveSoundMember().driver->driverName());
    TEST_ASSERT_EQUAL_UINT8(13, audioActiveSoundMember().driver->capabilities());

    audioBindSoundMember(memberValue("chirp"));
    TEST_ASSERT_EQUAL_STRING("chirp", audioActiveSoundMember().part->id);
    TEST_ASSERT_EQUAL_STRING("CHIRP", audioActiveSoundMember().driver->driverName());
    TEST_ASSERT_EQUAL_UINT8(63, audioActiveSoundMember().driver->capabilities());
}

// Every selectable Sound row has an instance, and the instance is the one the
// row describes: a driver returns its own row's capability word (ADR 0042), so
// a member bound to the wrong instance reports the wrong word. Walks the
// registry rather than the three ids above, so a fourth supported module is
// covered the day its row lands.
void test_every_selectable_sound_row_binds_to_the_driver_that_row_describes() {
    uint8_t selectable = 0;
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        const ComponentPartEntry& part = COMPONENT_PARTS[i];
        if (part.category != COMPONENT_CATEGORY_SOUND || !componentPartIsSelectable(part)) {
            continue;
        }
        ++selectable;
        audioBindSoundMember(part.value);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(part.id, audioActiveSoundMember().part->id, part.id);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(part.capabilities,
                                        audioActiveSoundMember().driver->capabilities(), part.id);
    }
    TEST_ASSERT_EQUAL_UINT8(componentCategorySelectableCount(COMPONENT_CATEGORY_SOUND), selectable);
}

// A stored value this image cannot drive degrades to the build default, not to
// silence and not to a null driver: 0 is what a controller that has never been
// told stores, dfplayer_mini is a roadmap row with no driver, hoverboard is a
// real row in another family, and 200 is a member from a firmware this image
// has never carried.
void test_an_unusable_stored_value_falls_back_to_the_build_default() {
    const uint8_t unusable[] = {0, memberValue("dfplayer_mini"), memberValue("hoverboard"), 200};
    for (size_t i = 0; i < sizeof(unusable) / sizeof(unusable[0]); ++i) {
        audioBindSoundMember(unusable[i]);
        TEST_ASSERT_NOT_NULL(audioActiveSoundMember().part);
        TEST_ASSERT_EQUAL_STRING("dy_sv5w", audioActiveSoundMember().part->id);
        TEST_ASSERT_NOT_NULL(audioActiveSoundMember().driver);
        TEST_ASSERT_EQUAL_STRING("DY-SV5W", audioActiveSoundMember().driver->driverName());
        TEST_ASSERT_EQUAL_UINT8(15, audioActiveSoundMember().driver->capabilities());
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_each_selectable_member_binds_to_its_own_driver);
    RUN_TEST(test_every_selectable_sound_row_binds_to_the_driver_that_row_describes);
    RUN_TEST(test_an_unusable_stored_value_falls_back_to_the_build_default);
    return UNITY_END();
}
