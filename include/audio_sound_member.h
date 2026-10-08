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

// -----------------------------------------------------------------------------
// Sound switched off (#370, operator 2026-09-19: "Say it's off")
//
// With audio output off at boot AudioTask is never created (src/main.cpp,
// ADR 0027), so nothing drains the audio queue and nothing drives a module.
// Every operator surface answers that the same way: a status names the module
// the builder PICKED and says sound is off - never the driver bound at boot,
// which on such a board is a module nobody is using - and a play or sound
// action is refused with the reason below instead of answering "queued" onto a
// queue nothing reads. The sequence engine and the other internal callers keep
// the queue helpers' accepted-and-discarded answer: sequences still run start
// to finish, in silence.
// -----------------------------------------------------------------------------

// The refusal, in the words the operator approved. One copy, read by the
// Console and the web handlers alike.
extern const char AUDIO_SOUND_OFF_REASON[];

// Is audio output on for this boot? The boot-latched toggle, not the saved
// one: switching sound on takes effect at the next start.
bool audioSoundOn();

// What a status surface names. With sound on it is the running driver and
// what it supports; with sound off it is the picked member's product name and
// the capabilities the Component Registry declares for it.
struct SoundStatusIdentity {
    bool on;
    const char* driver;
    uint8_t capabilities;
};
SoundStatusIdentity audioSoundStatusIdentity();

// The status line a surface shows with sound off, in the words the operator
// approved: "<picked> picked", a middle dot, "sound is off". This is the tail
// after the product name, kept beside the refusal so each is one copy. GET
// /api/status writes the name and this tail straight into its body rather than
// through a line buffer on the status builder's measured frame.
extern const char AUDIO_SOUND_OFF_STATUS_TAIL[];
