// =============================================================================
// include/console_direct_action_sound.h
//
// Controller Console direct-action executors - sound domain: play-track and
// set-volume. Split out of src/console/console_module.cpp by #257 so this
// domain's rows can be extended (the sound.action.* sweep is still open on
// the epic) without colliding with the other domains' files.
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

#include "console_direct_action_types.h"  // ConsoleDirectActionExecutorFn/Entry
#include "console_module.h"               // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"                 // ConsoleArgs, consoleArgsFind(), schema validation
#include "console_catalog.h"              // ConsoleCatalogEntry, consoleCatalogFindByName()
#include "robot_state.h"                  // robotState, robotStateMux
#include "audio_task.h"                   // audioQueuePlayTrack()
#include "api_audio.h"                    // AudioSetVolumeCommitOutcome, audioSetVolumeCommitApplied()

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

static const ConsoleDirectActionExecutorEntry g_soundDirectActionExecutors[] = {
    {"sound.action.play-track", consoleExecuteSoundPlayTrack},
    {"sound.action.set-volume", consoleExecuteSoundSetVolume},
};
static const size_t kSoundDirectActionExecutorCount =
    sizeof(g_soundDirectActionExecutors) / sizeof(g_soundDirectActionExecutors[0]);
