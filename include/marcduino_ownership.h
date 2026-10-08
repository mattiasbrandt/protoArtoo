// =============================================================================
// include/marcduino_ownership.h
//
// Command Ownership (GLOSSARY.md, ADR 0055): whether the body answers a ':' or
// '#' Marcduino line itself or hands it on to the Dome Controller.
//
// The body owns a line when its own code resolves it to something it models,
// and forwards everything else (operator decision on #449, 2026-09-30):
//
//   :OPnn :CLnn :OFnn  an Output marcduino_panel_to_arm_id() maps (1-5, 0/99)
//   :MVnn              an Output marcduino_panel_to_arm_id_mv() maps (1-5)
//   a malformed panel  a panel head whose number is not all digits: the body's,
//                      and refused (marcduino_panel_command_well_formed())
//   :SE10/11/13/14     a Mood (moodIdFromSeCommand(), include/mood.h)
//   :SE30-:SE36        a body routine (marcduino_sequence_id_valid())
//   :SE01-09, 15, 16   a full-droid sequence: the body runs its half from
//                      marcduino_full_droid_body_actions() AND the line goes
//                      to the dome, the way the RC droid_seq_* tokens already
//                      send it (src/rc_action_dispatcher.cpp)
//   #APSL #APWU #PAHB  marcduino_is_body_hash_command()
//
// There is no list of forwarded tokens here and there must not be one: each
// answer is asked of the helper the body handler itself runs, so a number the
// body learns to drive stops being forwarded with nothing else edited, and a
// number it does not know reaches the dome instead of vanishing. ADR 0055
// rejected a hand-kept routing table for exactly that drift.
//
// This is the wire layer, deciding who a builder's line is for. It never runs
// on a line the dome sent the body: parseMarcduinoCommand() is also the dome
// RX handler (src/tasks/dome_link.cpp), and a forward decided in there would
// send a dome line straight back to the dome.
//
// Pure: no Arduino, no FreeRTOS, no queues. The executor that acts on this
// answer is include/marcduino_router.h.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdlib.h>  // atoi

#include "marcduino_helpers.h"
#include "mood.h"  // moodIdFromSeCommand()

enum class MarcduinoOwner : uint8_t {
    Dome,         // nothing the body models: forwarded verbatim, the dome's to report
    Body,         // the body answers it, and nothing is forwarded
    BodyAndDome,  // a full-droid sequence: the body half here, the line forwarded too
};

inline MarcduinoOwner marcduinoCommandOwner(const char* line) {
    if (line == nullptr) {
        return MarcduinoOwner::Dome;
    }
    if (line[0] == '#') {
        return marcduino_is_body_hash_command(line) ? MarcduinoOwner::Body
                                                    : MarcduinoOwner::Dome;
    }
    if (line[0] != ':') {
        return MarcduinoOwner::Dome;
    }
    if (line[1] == 'S' && line[2] == 'E') {
        if (moodIdFromSeCommand(line) != 0) {
            return MarcduinoOwner::Body;
        }
        // atoi(), as handleSequenceCommand() reads it (src/drivers/
        // dome_rx_parser.cpp), so the two agree on every spelling.
        const int seqId = atoi(line + 3);
        if (marcduino_sequence_id_valid(seqId)) {
            return MarcduinoOwner::Body;
        }
        const FullDroidBodyAction half = marcduino_full_droid_body_actions(seqId);
        if (half.audioDollarCmd != nullptr || half.bodySeqId >= 0) {
            return MarcduinoOwner::BodyAndDome;
        }
        return MarcduinoOwner::Dome;
    }
    if (marcduino_is_panel_command(line)) {
        // A panel number made of anything but digits is the body's to refuse,
        // never the dome's to guess at and never the broadcast atoi() would
        // make of it (marcduino_panel_command_well_formed()).
        if (!marcduino_panel_command_well_formed(line)) {
            return MarcduinoOwner::Body;
        }
        return marcduino_panel_command_arm_id(line) != 254 ? MarcduinoOwner::Body
                                                           : MarcduinoOwner::Dome;
    }
    return MarcduinoOwner::Dome;
}

// -----------------------------------------------------------------------------
// MarcduinoRouteOutcome - what happened to one routed line.
//
// Three kinds of answer that never share a value: the body did it, the body
// handed it on, and no. Forwarded claims nothing about the dome - the dialect
// has no reply channel - only that the line was queued for protoR2link.
// -----------------------------------------------------------------------------
enum class MarcduinoRouteOutcome : uint8_t {
    Applied,         // the body owns it and acted
    Forwarded,       // queued for the dome verbatim (a full-droid line: body half ran too)
    DomeLinkDown,    // forwarded, but protoR2link is not connected, so nothing was queued
    DomeQueueFull,   // forwarded, but the dome TX queue refused it
    BlockedByEstop,  // the body owns it and refused: estop is latched
    OutputUndriven,  // the body owns it, and nothing drives the Output it names this boot
    QueueFull,       // the body owns it, and the queue that runs it refused
    NotRun,          // the body owns the line but cannot run it (a malformed panel
                     // number, a Mood through a path that may not apply one)
    LineTooLong,     // longer than the dome TX buffer holds: refused before anything ran
};
