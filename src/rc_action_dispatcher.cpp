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
#include "rc_sound_category_table.h"

// =============================================================================
// Sound Category Table
// =============================================================================
// Maps each sound action to its category lo/hi member pointers.
// Defined here and declared in rc_sound_category_table.h for testing.

const RcSoundCategoryEntry rcSoundCategoryTable[] = {
    {SOUND_ACTION_RANDOM_GENERAL, &RcSoundCategorySnapshot::gen_lo,
     &RcSoundCategorySnapshot::gen_hi},
    {SOUND_ACTION_RANDOM_CHATTY, &RcSoundCategorySnapshot::chat_lo,
     &RcSoundCategorySnapshot::chat_hi},
    {SOUND_ACTION_RANDOM_HAPPY, &RcSoundCategorySnapshot::hap_lo,
     &RcSoundCategorySnapshot::hap_hi},
    {SOUND_ACTION_RANDOM_PROCESSING, &RcSoundCategorySnapshot::proc_lo,
     &RcSoundCategorySnapshot::proc_hi},
    {SOUND_ACTION_RANDOM_SAD, &RcSoundCategorySnapshot::sad_lo,
     &RcSoundCategorySnapshot::sad_hi},
    {SOUND_ACTION_RANDOM_SENTIMENTAL, &RcSoundCategorySnapshot::sent_lo,
     &RcSoundCategorySnapshot::sent_hi},
    {SOUND_ACTION_RANDOM_HUMMING, &RcSoundCategorySnapshot::hum_lo,
     &RcSoundCategorySnapshot::hum_hi},
    {SOUND_ACTION_RANDOM_SCREAM, &RcSoundCategorySnapshot::scrm_lo,
     &RcSoundCategorySnapshot::scrm_hi},
    {SOUND_ACTION_RANDOM_SURPRISED, &RcSoundCategorySnapshot::ooh_lo,
     &RcSoundCategorySnapshot::ooh_hi},
    {SOUND_ACTION_RANDOM_ALERT, &RcSoundCategorySnapshot::alrm_lo,
     &RcSoundCategorySnapshot::alrm_hi},
    {SOUND_ACTION_RANDOM_SNARKY, &RcSoundCategorySnapshot::snarky_lo,
     &RcSoundCategorySnapshot::snarky_hi},
    {SOUND_ACTION_RANDOM_WHISTLE, &RcSoundCategorySnapshot::whis_lo,
     &RcSoundCategorySnapshot::whis_hi},
};

const size_t rcSoundCategoryTableSize = sizeof(rcSoundCategoryTable) / sizeof(rcSoundCategoryTable[0]);

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
                for (size_t i = 0; i < rcSoundCategoryTableSize; ++i) {
                    if (rcSoundCategoryTable[i].action == input.target) {
                        const RcSoundCategorySnapshot& cat = input.categories;
                        uint16_t lo = cat.*(rcSoundCategoryTable[i].lo);
                        uint16_t hi = cat.*(rcSoundCategoryTable[i].hi);
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
