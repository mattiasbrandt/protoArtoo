// =============================================================================
// include/audio_sound_member.h
//
// The Sound Component Member's driver instances and the member -> driver map
// (ADR 0042). One question, asked from two places: which of the drivers this
// image carries runs the module the operator configured.
//
// Bound in setup() (src/main.cpp), before any task exists, because the answer
// is a table lookup and every status surface needs it whether or not AudioTask
// was spawned. Audio output is staged at reboot (ADR 0027) and the task is
// created only when it is enabled, so a binding that lived inside the task
// left `sound.status.current` naming the build-time default module while
// /api/config and the identity payload named the configured one (#380).
// Binding before any task exists also strengthens the lock-free read below
// rather than weakening it: the pointer is written once, earlier than anything
// that could read it.
//
// AudioTask (src/tasks/audio_task.cpp) stays the shell: it owns the driver
// lifecycle -- begin(), the playback calls, the audio UART claim, the module
// state it writes to RobotState -- and reads the driver from here instead of
// choosing it.
//
// Defined in src/tasks/audio_sound_member.cpp, which the native build compiles
// (platformio.ini [env:native] build_src_filter) so the resolution is provable
// without hardware. src/tasks/audio_task.cpp and src/main.cpp are not in that
// filter, which is why the map does not live in either of them.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"
#include "component_registry.h"

// -----------------------------------------------------------------------------
// ActiveSoundMember  --  what this boot runs the Sound family on.
// -----------------------------------------------------------------------------
struct ActiveSoundMember {
    // The Component Registry row the stored value resolved to, or nullptr
    // before setup() binds. componentResolveMember() substitutes the build
    // default for a value this image cannot drive, so after binding this is
    // nullptr only where the image carries no selectable Sound member at all --
    // which the static_assert in src/tasks/audio_sound_member.cpp makes
    // unbuildable.
    const ComponentPartEntry* part;
    // The driver instance that runs it. Never nullptr, before or after binding.
    AudioDriver* driver;
};

// Bind the Sound member for this boot. `storedMemberValue` is the boot-latched
// active member setup() resolved (configCacheReadActiveSoundMember()), never
// the live config value: a member saved while the droid is running takes effect
// at the next boot, exactly as a Component Toggle does (ADR 0027, ADR 0042).
//
// Called once, from setup(), before any task is created.
void audioBindSoundMember(uint8_t storedMemberValue);

// What setup() bound. Safe to read without a lock from the Core 0 web and
// console handlers and from AudioTask: the binding is written once, before any
// task exists, and never moves again.
const ActiveSoundMember& audioActiveSoundMember();
