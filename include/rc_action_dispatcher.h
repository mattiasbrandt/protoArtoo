// =============================================================================
// include/rc_action_dispatcher.h
//
// RcActionDispatcher  --  pure action dispatch logic for RC trigger bindings.
// Input: RcActionPayload (snapshot of all needed state + target + seed).
// Output: RcActionResult (all intents, no side effects).
// No robotState, no FreeRTOS, no queues  --  safe for native unit tests.
// =============================================================================
#pragma once

#include <stdint.h>

#include "drive_speed_preset.h"
#include "rc_mapping.h"

struct RcAudioCategorySnapshot {
    uint16_t gen_lo, gen_hi;
    uint16_t chat_lo, chat_hi;
    uint16_t hap_lo, hap_hi;
    uint16_t proc_lo, proc_hi;
    uint16_t sad_lo, sad_hi;
    uint16_t sent_lo, sent_hi;
    uint16_t hum_lo, hum_hi;
    uint16_t scrm_lo, scrm_hi;
    uint16_t ooh_lo, ooh_hi;
    uint16_t alrm_lo, alrm_hi;
    uint16_t snarky_lo, snarky_hi;
    uint16_t whis_lo, whis_hi;
};

struct RcActionPayload {
    RobotActionId target;
    const char* bindingPayload;  // may be nullptr; not owned, must outlive call
    bool pressed;
    uint32_t randomSeed;
    RcAudioCategorySnapshot categories;
    bool estopActive;
    bool currentSleepMode;
    SpeedPresetId currentSpeedPreset;
};

struct RcActionResult {
    uint16_t audioTrack;        // 0 = none; track to play via audioQueuePlayTrack
    char audioDollarCmd[32];    // [0]=='\0' = none; command for audioQueueDollar

    int8_t servoIndex;          // -1 = no servo action; 0..4 = arm/aux index
    bool servoOpen;             // true=OPEN, false=CLOSE; ignored when servoIsSequence
    bool servoIsSequence;       // true = use servoSequenceId, not servoIndex/servoOpen
    uint8_t servoSequenceId;    // sequence ID (e.g. 30-36) when servoIsSequence

    char domeTxCmd[20];         // [0]=='\0' = none; command for domeQueueTx
    char marcduinoCmd[20];      // [0]=='\0' = none; command for parseMarcduinoCommand

    bool triggerEstop;
    bool setSleep;
    bool newSleepMode;
    bool setStationary;
    bool newStationaryMode;
    bool setSpeedPreset;
    SpeedPresetId newSpeedPreset;
};

RcActionResult rcDispatchAction(const RcActionPayload& input);
