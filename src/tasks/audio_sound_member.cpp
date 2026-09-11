// =============================================================================
// src/tasks/audio_sound_member.cpp
//
// The Sound Component Member -> driver map, and the binding every audio
// surface reads. Contract and rationale: include/audio_sound_member.h.
// =============================================================================

#include "audio_sound_member.h"

#include <string.h>

#include "audio_chirp.h"
#include "audio_dy_sv5w.h"
#include "audio_mp3trigger.h"
#include "logging.h"

static const char* TAG = "Sound";

// -----------------------------------------------------------------------------
// Sound is a Component Family, and the image carries every selectable member
// -----------------------------------------------------------------------------
// Each supported sound module has a driver instance here, and which one runs is
// the Component Member -- a runtime setting, staged at reboot like a Component
// Toggle (ADR 0042). The old #if chain picked one at compile time, which made
// the Configuration page's picker a display rather than a control for anyone
// running a prebuilt release image.
//
// Instance cost, not code cost, is what made this affordable: a driver object is
// its AudioSerialIO seam and a handful of scalars, and CHIRP's ~16 KB catalog is
// heap-allocated on first discovery rather than held statically
// (include/audio_chirp.h).
//
// All three share one soft-UART TX mux (src/drivers/audio_soft_uart_tx.h), and
// they always needed to: every env compiles all of src/, so that header's
// per-translation-unit copies were never the one-driver-per-build case its
// comment claimed. Only one member is active per boot in any case, and
// AudioTask is the sole writer to PIN_AUDIO_TX.
static AudioDriverDySv5w s_dySv5w;
static AudioDriverMp3Trigger s_mp3Trigger;
static AudioDriverChirp s_chirp;

// The one place left in the firmware that maps a product id to code. Everything
// downstream asks the driver what it supports, never which one it is.
struct SoundMemberDriver {
    const char* id;        // Component Registry row id
    AudioDriver* driver;
};
static const SoundMemberDriver kSoundMemberDrivers[] = {
    {"dy_sv5w", &s_dySv5w},
    {"mp3_trigger", &s_mp3Trigger},
    {"chirp", &s_chirp},
};

// Add a selectable Sound row to include/component_registry.inc without giving it
// an instance above and the build stops here, rather than the row quietly
// becoming a member nothing can run.
static_assert(sizeof(kSoundMemberDrivers) / sizeof(kSoundMemberDrivers[0]) ==
                  componentCategorySelectableCount(COMPONENT_CATEGORY_SOUND),
              "a selectable Sound member has no driver instance in kSoundMemberDrivers");

// Resolved once in setup(), before any task is created, and never reassigned --
// which is what makes it safe for the Core 0 web and console handlers to read
// it without a lock. A member change is saved immediately and takes effect at
// the next boot, exactly as a Component Toggle does.
//
// The pre-bind driver is the first table row rather than nothing: `driver` is
// documented as never null, and setup() binds before any surface can read it.
static ActiveSoundMember s_active = {nullptr, &s_dySv5w};

void audioBindSoundMember(uint8_t storedMemberValue) {
    // componentResolveMember() has already substituted the build default for a
    // stored value this image cannot drive, so the only way to reach the error
    // below is a registry row with no instance -- which the static_assert above
    // makes unbuildable.
    s_active.part = componentResolveMember(COMPONENT_CATEGORY_SOUND, storedMemberValue);
    if (s_active.part != nullptr) {
        for (size_t i = 0; i < sizeof(kSoundMemberDrivers) / sizeof(kSoundMemberDrivers[0]); ++i) {
            if (strcmp(kSoundMemberDrivers[i].id, s_active.part->id) == 0) {
                s_active.driver = kSoundMemberDrivers[i].driver;
                return;
            }
        }
    }
    // Unreachable while the static_assert above holds. Said out loud rather than
    // left as a silent fallthrough, because the symptom would otherwise be a
    // droid playing through the wrong module with nothing in the log.
    PA_LOG_ERROR(TAG, "sound member %u has no driver instance - falling back to %s",
                 (unsigned)storedMemberValue, s_active.driver->driverName());
}

const ActiveSoundMember& audioActiveSoundMember() {
    return s_active;
}
