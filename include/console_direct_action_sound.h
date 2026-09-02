// =============================================================================
// include/console_direct_action_sound.h
//
// Controller Console direct-action executors - sound domain. Split out of
// src/console/console_module.cpp by #257 so this domain's rows could be
// extended without colliding with the other domains' files; #258 wires the
// remaining sound.action.* rows #257 left open (every one the firmware
// already implements - the 12 sound.action.random-* rows are NOT here
// because they already dispatch through the generic ACTION_REGISTRY[] +
// dispatchRcTriggerActionTest() path in console_module.cpp's own
// consoleExecuteAction(), proven by test_scoped_non_motion_actions_are_not_
// executor_not_ready - adding a row here for them would be a second,
// unreachable dispatch path, not a fix).
//
// HEADER-ONLY DELIBERATELY - see include/console_direct_action_system.h's
// header comment for the full reasoning (native's fenced build_src_filter
// allowlist has no room for a new src/console/*.cpp here) and for why
// `static`, not `inline`, is the right linkage given this header has exactly
// one includer.
//
// NOT a standalone compilation unit: #include'd from src/console/
// console_module.cpp only, at the point these executors used to live, so
// their bodies can see that file's own `static` consoleEmitArgFailure() and
// consoleCommandSourceFor() by ordinary same-translation-unit visibility.
// Not included, and must not be included, from anywhere else.
// =============================================================================
#pragma once

#include <stdlib.h>
#include <string.h>

#include "console_direct_action_types.h"  // ConsoleDirectActionExecutorFn/Entry
#include "console_module.h"               // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"                 // ConsoleArgs, consoleArgsFind(), schema validation,
                                           // consoleArgsAsParamSource()
#include "console_catalog.h"              // ConsoleCatalogEntry, consoleCatalogFindByName()
#include "robot_state.h"                  // robotState, robotStateMux
#include "audio_task.h"                   // audioQueuePlayTrack(), audioQueueDollar(),
                                           // audioQueueTrackStop(), audioQueueQueryStatus(),
                                           // audioGetCapabilities() - includes audio_driver.h
                                           // transitively for AudioDriver::AUDIO_CAP_CATALOG
#include "api_audio.h"                    // AudioSetVolumeCommitOutcome, audioSetVolumeCommitApplied(),
                                           // AudioMoodMapCommitOutcome, audioMoodMapCommitApplied(),
                                           // AudioCategoryRangeCommitOutcome,
                                           // audioCategoryRangeCommitApplied() - and, transitively,
                                           // api_audio_mood_map_apply.h/api_audio_category_range_apply.h
                                           // for audioMoodMapApply()/audioCategoryRangeApply()
#include "config_cache.h"                 // ConfigSnapshot, configCacheRead()

// sound.action.play-track: track=<1..999> - the same audioQueuePlayTrack()
// call handleAudioPost()'s action=play branch makes (src/web/api_audio.cpp);
// no persist step exists to extract, so this calls the queue function
// directly, matching #222's "reuse the existing shared function verbatim"
// shape rather than #226's Commit-Step-extraction shape. Its own sleep gate
// is reproduced verbatim - robotState.sleepMode read directly under critical
// section, the same field isSleepModeActive() (private to api_audio.cpp's
// anonymous namespace) reads, matching the drive.action.move precedent
// (include/console_direct_action_drive.h) of reading RobotState directly
// rather than exporting a REST-private helper.
static void consoleExecuteSoundPlayTrack(uint32_t requestId, const char* operationName,
                                         const ConsoleArgs& args, ConsoleCommandSource source,
                                         const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    const bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    if (sleeping) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                CONSOLE_REASON_TEMPORARILY_UNAVAILABLE);
        }
        return;
    }

    char* end = nullptr;
    long track = strtol(consoleArgsFind(args, "track"), &end, 10);
    if (*end != '\0' || track < 1 || track > 999) {
        consoleEmitArgFailure(requestId, operationName, "track", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    if (!audioQueuePlayTrack((uint16_t)track, consoleCommandSourceFor(source))) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// sound.action.set-volume: volume=<0..30> - the same audioQueueSetVolume() +
// persist-as-default sequence handleAudioPost()'s action=volume branch makes
// (src/web/api_audio.cpp); audioSetVolumeCommitApplied() (include/
// api_audio.h) is the extracted ADR 0034 Commit Step both now share.
static void consoleExecuteSoundSetVolume(uint32_t requestId, const char* operationName,
                                         const ConsoleArgs& args, ConsoleCommandSource source,
                                         const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    char* end = nullptr;
    long level = strtol(consoleArgsFind(args, "volume"), &end, 10);
    if (*end != '\0' || level < 0 || level > 30) {
        consoleEmitArgFailure(requestId, operationName, "volume", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    AudioSetVolumeCommitOutcome commit =
        audioSetVolumeCommitApplied((uint8_t)level, consoleCommandSourceFor(source));
    if (!commit.queued) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }
    if (!commit.saved) {
        // "a failed NVS write is an explicit error" (criterion 3), matching
        // consoleWriteScalarConfigField()'s own !commit.persisted branch
        // (src/console/console_module.cpp) - no dedicated persistence-failure
        // reason exists in the hand-maintained ConsoleReason set.
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                CONSOLE_REASON_NONE);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED, CONSOLE_REASON_NONE);
    }
}

// Reproduces api_audio.cpp's anonymous-namespace audioCatalogSupported()
// (internal linkage - unreachable from here, a second translation unit)
// rather than exporting a REST-private helper, the same "read their logic,
// call their shared functions" precedent consoleExecuteSoundPlayTrack()
// above already set for isSleepModeActive() (also private to that same
// anonymous namespace).
static bool consoleAudioCatalogSupported() {
    return (audioGetCapabilities() & AudioDriver::AUDIO_CAP_CATALOG) != 0;
}

// Shared body for every zero-argument $-letter dollar shortcut this file
// wires (named-track plays, quiet, random-on/off, volume shortcuts): the
// same audioQueueDollar() call handleAudioPost()'s action=dollar branch
// makes (src/web/api_audio.cpp) for the identical literal command, including
// its sleep gate. AudioTask's own step core (src/tasks/audio_task_step.cpp,
// case AUDIO_CMD_DOLLAR) silently ignores a dollar command received while
// asleep regardless of caller, but handleAudioPost()'s "dollar" branch
// additionally pre-checks robotState.sleepMode so the caller sees an
// explicit refusal instead of a silently-dropped command - reproduced here
// the same way, matching consoleExecuteSoundPlayTrack()'s own sleep-gate
// precedent above. Every registry row this serves declares zero params
// (docs/action-registry.yaml), so the schema check below - reused unchanged
// from consoleExecuteSoundPlayTrack()/consoleExecuteSoundSetVolume() above -
// already does the right thing: an empty schema means any supplied argument
// is unknown, which is correct since none of these operations take one.
static void consoleExecuteSoundDollarShortcut(uint32_t requestId, const char* operationName,
                                              const ConsoleArgs& args, ConsoleCommandSource source,
                                              const ConsoleRecordSink* sink, const char* dollarCmd) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    const bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    if (sleeping) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                CONSOLE_REASON_TEMPORARILY_UNAVAILABLE);
        }
        return;
    }

    if (!audioQueueDollar(dollarCmd, consoleCommandSourceFor(source))) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// The nine named-track $-letter shortcuts (docs/action-registry.yaml's own
// $ command reference, include/audio_dollar_parser.h): each is a thin
// wrapper over consoleExecuteSoundDollarShortcut() above with its literal
// two-character command baked in, matching what dome_link.cpp's own direct
// audioQueueDollar("$R"/"$O", ...) calls already do for the same shape of
// fixed-command dispatch. sound.action.play-track-disco ($D) has no special
// case here: parseAudioDollar() itself no-ops a disco command when the
// snd_disco NVS track is 0/disabled (include/audio_dollar_parser.h), so the
// command is still genuinely QUEUED from this executor's point of view even
// when it resolves to nothing downstream - the same "queued, not
// synchronously confirmed" contract every other fire-and-forget row here
// already has.
static void consoleExecuteSoundPlayTrackScream(uint32_t requestId, const char* operationName,
                                               const ConsoleArgs& args, ConsoleCommandSource source,
                                               const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$S");
}
static void consoleExecuteSoundPlayTrackFaint(uint32_t requestId, const char* operationName,
                                              const ConsoleArgs& args, ConsoleCommandSource source,
                                              const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$F");
}
static void consoleExecuteSoundPlayTrackLeia(uint32_t requestId, const char* operationName,
                                             const ConsoleArgs& args, ConsoleCommandSource source,
                                             const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$L");
}
static void consoleExecuteSoundPlayTrackCantinaShort(uint32_t requestId, const char* operationName,
                                                     const ConsoleArgs& args,
                                                     ConsoleCommandSource source,
                                                     const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$c");
}
static void consoleExecuteSoundPlayTrackCantinaLong(uint32_t requestId, const char* operationName,
                                                    const ConsoleArgs& args,
                                                    ConsoleCommandSource source,
                                                    const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$C");
}
static void consoleExecuteSoundPlayTrackSwTheme(uint32_t requestId, const char* operationName,
                                                const ConsoleArgs& args, ConsoleCommandSource source,
                                                const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$W");
}
static void consoleExecuteSoundPlayTrackImperialMarch(uint32_t requestId, const char* operationName,
                                                       const ConsoleArgs& args,
                                                       ConsoleCommandSource source,
                                                       const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$M");
}
static void consoleExecuteSoundPlayTrackStartup(uint32_t requestId, const char* operationName,
                                                const ConsoleArgs& args, ConsoleCommandSource source,
                                                const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$B");
}
static void consoleExecuteSoundPlayTrackDisco(uint32_t requestId, const char* operationName,
                                              const ConsoleArgs& args, ConsoleCommandSource source,
                                              const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$D");
}

// sound.action.quiet ($s): stop playback and disable random/idle mood until
// reboot or Random On (ADR 0010). Reached through the SAME dollar-command
// path handleAudioPost()'s action=dollar branch would use for cmd=$s -
// audio_task.h's warning that the direct audioQueueStop() queue helper is
// "Reserved for the mood system's Quiet path ($s / SE10) - do not call from
// any other surface" is about that typed queue helper specifically, not
// about reaching the same $s semantics through the dollar-command path,
// which is the documented, supported way every other caller (web, dome_rx)
// already reaches it.
static void consoleExecuteSoundQuiet(uint32_t requestId, const char* operationName,
                                     const ConsoleArgs& args, ConsoleCommandSource source,
                                     const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$s");
}

// sound.action.random-on/-off ($R/$O): the same two literal dollar commands
// dome_link.cpp's own direct audioQueueDollar("$R"/"$O", SRC_INTERNAL) calls
// send for its internal dome-cue-driven random-mode toggling - reused here
// for the operator-facing Console row via the shared dollar-shortcut path
// above (which additionally applies the sleep gate those internal calls,
// not being operator-initiated, do not need).
static void consoleExecuteSoundRandomOn(uint32_t requestId, const char* operationName,
                                        const ConsoleArgs& args, ConsoleCommandSource source,
                                        const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$R");
}
static void consoleExecuteSoundRandomOff(uint32_t requestId, const char* operationName,
                                         const ConsoleArgs& args, ConsoleCommandSource source,
                                         const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$O");
}

// sound.action.volume-up/-down/-preset-mid/-max/-min ($+/$-/$m/$f/$p): the
// five relative/preset volume dollar shortcuts (include/audio_dollar_
// parser.h). Distinct from sound.action.set-volume above, which sets an
// absolute level through AUDIO_CMD_SET_VOLUME directly and is NOT sleep-
// gated (handleAudioPost()'s action=volume branch has no sleep check); these
// five instead go through the SAME dollar-command path as the named-track
// shortcuts above, so they inherit that path's sleep gate too - reproducing
// handleAudioPost()'s action=dollar branch faithfully rather than picking
// the more permissive gate because it seems more sensible for a volume
// change.
static void consoleExecuteSoundVolumeUp(uint32_t requestId, const char* operationName,
                                        const ConsoleArgs& args, ConsoleCommandSource source,
                                        const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$+");
}
static void consoleExecuteSoundVolumeDown(uint32_t requestId, const char* operationName,
                                          const ConsoleArgs& args, ConsoleCommandSource source,
                                          const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$-");
}
static void consoleExecuteSoundVolumePresetMid(uint32_t requestId, const char* operationName,
                                               const ConsoleArgs& args, ConsoleCommandSource source,
                                               const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$m");
}
static void consoleExecuteSoundVolumePresetMax(uint32_t requestId, const char* operationName,
                                               const ConsoleArgs& args, ConsoleCommandSource source,
                                               const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$f");
}
static void consoleExecuteSoundVolumePresetMin(uint32_t requestId, const char* operationName,
                                               const ConsoleArgs& args, ConsoleCommandSource source,
                                               const ConsoleRecordSink* sink) {
    consoleExecuteSoundDollarShortcut(requestId, operationName, args, source, sink, "$p");
}

// sound.action.dollar-command: cmd=<$-prefixed string> - the same
// audioQueueDollar() call handleAudioPost()'s action=dollar branch makes
// (src/web/api_audio.cpp), including its "must start with '$'"/"<=9 chars"
// validation (audioCmdQueue's dollar[10] union member ceiling, include/
// audio_task.h) and sleep gate. The registry declares no params schema for
// this row (docs/action-registry.yaml), so this hand-validates the single
// "cmd" key the same way rc.action.test-bindable's "token" and
// rc.action.toggle-debug's "enabled" already do (include/console_direct_
// action_aux_rc.h) rather than through consoleValidateArgsAgainstSchema(),
// whose registry-sourced ConsoleParamDescriptor table has no row for it. An
// empty cmd= value answers MISSING_ARGUMENT rather than OUT_OF_RANGE,
// matching rc.action.test-bindable's own "token == nullptr || token[0] ==
// '\0'" precedent for the same shape of "supplied but empty" input.
static void consoleExecuteSoundDollarCommand(uint32_t requestId, const char* operationName,
                                             const ConsoleArgs& args, ConsoleCommandSource source,
                                             const ConsoleRecordSink* sink) {
    const char* badKey = nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "cmd") != 0) {
            badKey = args.items[i].key;
            break;
        }
    }
    if (badKey != nullptr) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }
    const char* cmd = consoleArgsFind(args, "cmd");
    if (cmd == nullptr || cmd[0] == '\0') {
        consoleEmitArgFailure(requestId, operationName, "cmd", CONSOLE_REASON_MISSING_ARGUMENT, sink);
        return;
    }
    if (cmd[0] != '$') {
        consoleEmitArgFailure(requestId, operationName, "cmd", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }
    if (strlen(cmd) > 9) {
        consoleEmitArgFailure(requestId, operationName, "cmd", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    const bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    if (sleeping) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_BLOCKED,
                                CONSOLE_REASON_TEMPORARILY_UNAVAILABLE);
        }
        return;
    }

    if (!audioQueueDollar(cmd, consoleCommandSourceFor(source))) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// sound.action.track-stop: the same audioQueueTrackStop() call
// handleAudioPost()'s action=stop branch makes (src/web/api_audio.cpp) -
// that branch has no sleep gate (unlike play/dollar above), so none is
// reproduced here either.
static void consoleExecuteSoundTrackStop(uint32_t requestId, const char* operationName,
                                         const ConsoleArgs& args, ConsoleCommandSource source,
                                         const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    if (!audioQueueTrackStop(consoleCommandSourceFor(source))) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// sound.action.query-status: the same audioQueueQueryStatus() call
// handleAudioQueryPost() makes (src/web/api_audio.cpp) - an on-demand
// module status poll; the result becomes visible on the next sound.status.
// current query (~1.5 s later per the registry description), not on this
// one. No sleep gate there either.
static void consoleExecuteSoundQueryStatus(uint32_t requestId, const char* operationName,
                                           const ConsoleArgs& args, ConsoleCommandSource source,
                                           const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    if (!audioQueueQueryStatus(consoleCommandSourceFor(source))) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                                CONSOLE_REASON_QUEUE_FULL);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// sound.action.set-mood-map: quiet=/mid=/full=/awakeplus=<0..4095> - the
// same audioMoodMapApply() + audioMoodMapCommitApplied() sequence
// handleAudioMoodMapPost() runs (src/web/api_audio.cpp), reusing the ADR
// 0011 Apply Core and its Commit Step (include/api_audio.h) exactly as
// extracted for this purpose ("No Console operation reaches these yet ...
// the extraction stands ready for whichever ticket wires them" -
// audioMoodMapCommitApplied()'s own comment in include/api_audio.h).
// consoleArgsAsParamSource() (include/console_args.h) is the SAME
// ConfigParamSource adapter #226 already established for Console-sourced
// Apply Core calls, reused verbatim rather than a second bridge. All four
// fields are required with range 0-4095 in the registry schema - exactly
// MOOD_CATEGORY_MASK_MAX (include/mood_sound_mapping.h) - so the apply
// core's own hasError branch below is unreachable in practice for a
// Console-originated call once schema validation has already passed; still
// handled explicitly rather than assumed away.
static void consoleExecuteSoundSetMoodMap(uint32_t requestId, const char* operationName,
                                          const ConsoleArgs& args, ConsoleCommandSource source,
                                          const ConsoleRecordSink* sink) {
    (void)source;  // audioMoodMapCommitApplied() has no CommandSource parameter - the REST
                   // handler it mirrors does not attribute this NVS write to a source either.
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    AudioMoodMapApplyResult result;
    audioMoodMapApply(consoleArgsAsParamSource(args), &result);
    if (result.error.hasError) {
        // Unreachable after schema validation above (see header comment) -
        // still a real status=err answer, not swallowed, matching every
        // other apply-core error path in this module.
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID,
                                CONSOLE_REASON_OUT_OF_RANGE);
        }
        return;
    }

    AudioMoodMapCommitOutcome commit = audioMoodMapCommitApplied(result);
    if (!commit.ok) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                CONSOLE_REASON_NONE);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED, CONSOLE_REASON_NONE);
    }
}

// sound.action.set-category-range: lo_key=/hi_key=<string> lo=/hi=<uint16> -
// the same audioCategoryRangeApply() + audioCategoryRangeCommitApplied()
// sequence handleAudioCategoryRangePost() runs (src/web/api_audio.cpp),
// reusing the ADR 0011 Apply Core and its Commit Step (include/api_audio.h)
// exactly as extracted for this purpose. The registry declares no range on
// lo/hi (docs/action-registry.yaml), so the apply core's own 0-999/lo<=hi
// and category-key-pair validation is the real gate here, not the schema -
// its failure (any of: bad key pair, out-of-range value, lo>hi) is answered
// as a single undifferentiated "invalid", the same OUT_OF_RANGE catch-all
// consoleWriteScalarConfigField() already uses for any Apply Core rejection
// (src/console/console_module.cpp), since there is no one attributable
// argument key for a multi-field range check.
//
// bank/page/clear_binding (REST's optional CHIRP-binding extension to this
// same route) are deliberately NOT in this row's registry schema, so
// consoleValidateArgsAgainstSchema() below already refuses them as unknown
// arguments before the apply core ever sees them - Console exposes only the
// plain lo/hi form, a subset of REST's accepted params, never a superset
// ("no widening").
static void consoleExecuteSoundSetCategoryRange(uint32_t requestId, const char* operationName,
                                                const ConsoleArgs& args, ConsoleCommandSource source,
                                                const ConsoleRecordSink* sink) {
    (void)source;  // audioCategoryRangeCommitApplied() has no CommandSource parameter -
                   // the REST handler it mirrors does not attribute this NVS write either.
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(consoleArgsAsParamSource(args), consoleAudioCatalogSupported(), &snap,
                            &result);
    if (result.error.hasError) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INVALID,
                                CONSOLE_REASON_OUT_OF_RANGE);
        }
        return;
    }

    AudioCategoryRangeCommitOutcome commit = audioCategoryRangeCommitApplied(&snap, result);
    if (!commit.ok) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_INTERNAL_ERROR,
                                CONSOLE_REASON_NONE);
        }
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED, CONSOLE_REASON_NONE);
    }
}

static const ConsoleDirectActionExecutorEntry g_soundDirectActionExecutors[] = {
    {"sound.action.play-track", consoleExecuteSoundPlayTrack},
    {"sound.action.set-volume", consoleExecuteSoundSetVolume},
    {"sound.action.play-track-scream", consoleExecuteSoundPlayTrackScream},
    {"sound.action.play-track-faint", consoleExecuteSoundPlayTrackFaint},
    {"sound.action.play-track-leia", consoleExecuteSoundPlayTrackLeia},
    {"sound.action.play-track-cantina-short", consoleExecuteSoundPlayTrackCantinaShort},
    {"sound.action.play-track-cantina-long", consoleExecuteSoundPlayTrackCantinaLong},
    {"sound.action.play-track-sw-theme", consoleExecuteSoundPlayTrackSwTheme},
    {"sound.action.play-track-imperial-march", consoleExecuteSoundPlayTrackImperialMarch},
    {"sound.action.play-track-startup", consoleExecuteSoundPlayTrackStartup},
    {"sound.action.play-track-disco", consoleExecuteSoundPlayTrackDisco},
    {"sound.action.quiet", consoleExecuteSoundQuiet},
    {"sound.action.random-on", consoleExecuteSoundRandomOn},
    {"sound.action.random-off", consoleExecuteSoundRandomOff},
    {"sound.action.volume-up", consoleExecuteSoundVolumeUp},
    {"sound.action.volume-down", consoleExecuteSoundVolumeDown},
    {"sound.action.volume-preset-mid", consoleExecuteSoundVolumePresetMid},
    {"sound.action.volume-preset-max", consoleExecuteSoundVolumePresetMax},
    {"sound.action.volume-preset-min", consoleExecuteSoundVolumePresetMin},
    {"sound.action.dollar-command", consoleExecuteSoundDollarCommand},
    {"sound.action.track-stop", consoleExecuteSoundTrackStop},
    {"sound.action.query-status", consoleExecuteSoundQueryStatus},
    {"sound.action.set-mood-map", consoleExecuteSoundSetMoodMap},
    {"sound.action.set-category-range", consoleExecuteSoundSetCategoryRange},
};
static const size_t kSoundDirectActionExecutorCount =
    sizeof(g_soundDirectActionExecutors) / sizeof(g_soundDirectActionExecutors[0]);
