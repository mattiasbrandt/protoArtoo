// =============================================================================
// src/rc_action_dispatcher.cpp
//
// Pure implementation of RC action dispatch logic.
// No robotState, no FreeRTOS, no queues.
// =============================================================================

#include "rc_action_dispatcher.h"

#include <cstdio>
#include <cstring>

#include "drive_speed_preset.h"
#include "marcduino_helpers.h"
#include "rc_mapping.h"
#include "rc_audio_category_table.h"

// =============================================================================
// Audio Category Table
// =============================================================================
// Maps each audio action to its category lo/hi member pointers.
// Defined here and declared in rc_audio_category_table.h for testing.

const RcAudioCategoryEntry rcAudioCategoryTable[] = {
    {SOUND_ACTION_RANDOM_GENERAL, &RcAudioCategorySnapshot::gen_lo,
     &RcAudioCategorySnapshot::gen_hi},
    {SOUND_ACTION_RANDOM_CHATTY, &RcAudioCategorySnapshot::chat_lo,
     &RcAudioCategorySnapshot::chat_hi},
    {SOUND_ACTION_RANDOM_HAPPY, &RcAudioCategorySnapshot::hap_lo,
     &RcAudioCategorySnapshot::hap_hi},
    {SOUND_ACTION_RANDOM_PROCESSING, &RcAudioCategorySnapshot::proc_lo,
     &RcAudioCategorySnapshot::proc_hi},
    {SOUND_ACTION_RANDOM_SAD, &RcAudioCategorySnapshot::sad_lo,
     &RcAudioCategorySnapshot::sad_hi},
    {SOUND_ACTION_RANDOM_SENTIMENTAL, &RcAudioCategorySnapshot::sent_lo,
     &RcAudioCategorySnapshot::sent_hi},
    {SOUND_ACTION_RANDOM_HUMMING, &RcAudioCategorySnapshot::hum_lo,
     &RcAudioCategorySnapshot::hum_hi},
    {SOUND_ACTION_RANDOM_SCREAM, &RcAudioCategorySnapshot::scrm_lo,
     &RcAudioCategorySnapshot::scrm_hi},
    {SOUND_ACTION_RANDOM_SURPRISED, &RcAudioCategorySnapshot::ooh_lo,
     &RcAudioCategorySnapshot::ooh_hi},
    {SOUND_ACTION_RANDOM_ALERT, &RcAudioCategorySnapshot::alrm_lo,
     &RcAudioCategorySnapshot::alrm_hi},
    {SOUND_ACTION_RANDOM_SNARKY, &RcAudioCategorySnapshot::snarky_lo,
     &RcAudioCategorySnapshot::snarky_hi},
    {SOUND_ACTION_RANDOM_WHISTLE, &RcAudioCategorySnapshot::whis_lo,
     &RcAudioCategorySnapshot::whis_hi},
};

const size_t rcAudioCategoryTableSize = sizeof(rcAudioCategoryTable) / sizeof(rcAudioCategoryTable[0]);

RcActionResult rcDispatchAction(const RcActionPayload& input) {
    RcActionResult res = {};
    res.servoIndex = -1;

    switch (input.target) {
        case ROBOT_ACTION_NONE:
            break;

        case SERVO_ACTION_ARM1_TOGGLE:
        case SERVO_ACTION_ARM2_TOGGLE:
        case SERVO_ACTION_AUX1_TOGGLE:
        case SERVO_ACTION_AUX2_TOGGLE:
        case SERVO_ACTION_AUX3_TOGGLE: {
            res.servoIndex = (int8_t)((int)input.target - (int)SERVO_ACTION_ARM1_TOGGLE);
            res.servoOpen = input.pressed;
            res.servoIsSequence = false;
            break;
        }

        case DOME_ACTION_MARCDUINO_SEQ:
            if (input.pressed && rcPayloadValidForBodySequence(input.bindingPayload)) {
                snprintf(res.marcduinoCmd, sizeof(res.marcduinoCmd), ":SE%s",
                         input.bindingPayload);
            }
            break;

        case DOME_ACTION_MARCDUINO_CMD:
            if (input.pressed && rcPayloadValidForMarcduinoCommand(input.bindingPayload)) {
                strncpy(res.marcduinoCmd, input.bindingPayload, sizeof(res.marcduinoCmd) - 1);
                res.marcduinoCmd[sizeof(res.marcduinoCmd) - 1] = '\0';
            }
            break;

        case SOUND_ACTION_RANDOM_GENERAL:
        case SOUND_ACTION_RANDOM_CHATTY:
        case SOUND_ACTION_RANDOM_HAPPY:
        case SOUND_ACTION_RANDOM_PROCESSING:
        case SOUND_ACTION_RANDOM_SAD:
        case SOUND_ACTION_RANDOM_SENTIMENTAL:
        case SOUND_ACTION_RANDOM_HUMMING:
        case SOUND_ACTION_RANDOM_SCREAM:
        case SOUND_ACTION_RANDOM_SURPRISED:
        case SOUND_ACTION_RANDOM_ALERT:
        case SOUND_ACTION_RANDOM_SNARKY:
        case SOUND_ACTION_RANDOM_WHISTLE: {
            if (input.pressed) {
                // Lookup category entry for this action in the global table
                for (size_t i = 0; i < rcAudioCategoryTableSize; ++i) {
                    if (rcAudioCategoryTable[i].action == input.target) {
                        const RcAudioCategorySnapshot& cat = input.categories;
                        uint16_t lo = cat.*(rcAudioCategoryTable[i].lo);
                        uint16_t hi = cat.*(rcAudioCategoryTable[i].hi);
                        uint16_t track = 0;
                        if (selectRandomTrackInRange(lo, hi, input.randomSeed, &track)) {
                            res.audioTrack = track;
                        }
                        break;
                    }
                }
            }
            break;
        }

        case DROID_SEQ_SCREAM:
        case DROID_SEQ_WAVE:
        case DROID_SEQ_FAST_WAVE:
        case DROID_SEQ_OPEN_WAVE:
        case DROID_SEQ_BEEP_CANTINA:
        case DROID_SEQ_FAINT:
        case DROID_SEQ_CANTINA:
        case DROID_SEQ_LEIA:
        case DROID_SEQ_DISCO:
        case DROID_SEQ_SCREAMS:
        case DROID_SEQ_WIGGLE: {
            if (input.pressed) {
                int seqId = robotActionIdToDroidSeqId(input.target);
                if (seqId > 0) {
                    FullDroidBodyAction bodyAction = marcduino_full_droid_body_actions(seqId);

                    if (bodyAction.audioDollarCmd != nullptr) {
                        strncpy(res.audioDollarCmd, bodyAction.audioDollarCmd,
                                sizeof(res.audioDollarCmd) - 1);
                        res.audioDollarCmd[sizeof(res.audioDollarCmd) - 1] = '\0';
                    }

                    if (bodyAction.bodySeqId >= 30 && !input.estopActive) {
                        res.servoIsSequence = true;
                        res.servoSequenceId = (uint8_t)bodyAction.bodySeqId;
                        res.servoIndex = 0;  // dummy sentinel so caller sees servoIndex>=0
                    }

                    snprintf(res.domeTxCmd, sizeof(res.domeTxCmd), ":SE%02d", seqId);
                }
            }
            break;
        }

        case SYSTEM_ACTION_ESTOP:
            if (input.pressed) {
                res.triggerEstop = true;
            }
            break;

        case SYSTEM_ACTION_SLEEP_TOGGLE:
            if (input.pressed) {
                res.setSleep = true;
                res.newSleepMode = !input.currentSleepMode;
            }
            break;

        case SYSTEM_ACTION_OP_MODE:
            res.setStationary = true;
            res.newStationaryMode = input.pressed;
            break;

        case DRIVE_ACTION_SPEED_PRESET_CYCLE:
            if (input.pressed) {
                res.setSpeedPreset = true;
                res.newSpeedPreset = nextSpeedPreset(input.currentSpeedPreset);
            }
            break;

        case DOME_ACTION_SEQ:
            if (input.pressed && input.bindingPayload != nullptr &&
                input.bindingPayload[0] != '\0') {
                strncpy(res.domeTxCmd, input.bindingPayload, sizeof(res.domeTxCmd) - 1);
                res.domeTxCmd[sizeof(res.domeTxCmd) - 1] = '\0';
            }
            break;

        default:
            break;
    }

    return res;
}

bool rcActionResultHasEffect(const RcActionResult& result) {
    return result.audioTrack != 0 || result.audioDollarCmd[0] != '\0' || result.servoIndex >= 0 ||
           result.domeTxCmd[0] != '\0' || result.marcduinoCmd[0] != '\0' || result.triggerEstop ||
           result.setSleep || result.setStationary || result.setSpeedPreset;
}
