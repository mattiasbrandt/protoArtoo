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
                uint16_t lo = 0;
                uint16_t hi = 0;
                switch (input.target) {
                    case SOUND_ACTION_RANDOM_GENERAL:
                        lo = input.categories.gen_lo;
                        hi = input.categories.gen_hi;
                        break;
                    case SOUND_ACTION_RANDOM_CHATTY:
                        lo = input.categories.chat_lo;
                        hi = input.categories.chat_hi;
                        break;
                    case SOUND_ACTION_RANDOM_HAPPY:
                        lo = input.categories.hap_lo;
                        hi = input.categories.hap_hi;
                        break;
                    case SOUND_ACTION_RANDOM_PROCESSING:
                        lo = input.categories.proc_lo;
                        hi = input.categories.proc_hi;
                        break;
                    case SOUND_ACTION_RANDOM_SAD:
                        lo = input.categories.sad_lo;
                        hi = input.categories.sad_hi;
                        break;
                    case SOUND_ACTION_RANDOM_SENTIMENTAL:
                        lo = input.categories.sent_lo;
                        hi = input.categories.sent_hi;
                        break;
                    case SOUND_ACTION_RANDOM_HUMMING:
                        lo = input.categories.hum_lo;
                        hi = input.categories.hum_hi;
                        break;
                    case SOUND_ACTION_RANDOM_SCREAM:
                        lo = input.categories.scrm_lo;
                        hi = input.categories.scrm_hi;
                        break;
                    case SOUND_ACTION_RANDOM_SURPRISED:
                        lo = input.categories.ooh_lo;
                        hi = input.categories.ooh_hi;
                        break;
                    case SOUND_ACTION_RANDOM_ALERT:
                        lo = input.categories.alrm_lo;
                        hi = input.categories.alrm_hi;
                        break;
                    case SOUND_ACTION_RANDOM_SNARKY:
                        lo = input.categories.snarky_lo;
                        hi = input.categories.snarky_hi;
                        break;
                    case SOUND_ACTION_RANDOM_WHISTLE:
                        lo = input.categories.whis_lo;
                        hi = input.categories.whis_hi;
                        break;
                    default:
                        break;
                }
                uint16_t track = 0;
                if (selectRandomTrackInRange(lo, hi, input.randomSeed, &track)) {
                    res.audioTrack = track;
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
