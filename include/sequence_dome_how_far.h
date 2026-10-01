// =============================================================================
// include/sequence_dome_how_far.h
//
// How far a dome panel goes, as a fraction of that panel's own throw
// (ADR 0046, #438). A move says how far it goes on the dome as it already does
// on the body, and the dome resolves it: the fraction is sent, never a pulse.
//
// THE WIRE FORM is our fork's `:MV<tt><vvvv>` (AstroPixelsPlus
// MarcduinoPanel.h:23-60 and :143-152 at 61303ad): a two-digit panel target
// and a four-digit value. A value 0..180 is scaleToPos(servo, v / 180) - a
// point along THAT panel's own calibrated throw, from its open end (0) to its
// closed end (180) - so a recalibration changes the pulse and not the
// routine. A value 544..2500 is a raw pulse, which is `:SM` by another name:
// this header never writes one, and Protocol Check never lets a saved step
// carry `:MV` at all. A saved step says `:OP01` with a howFar; the `:MV` line
// exists only on the way to the dome.
//
// How far is measured the way a Body Step measures it (seqBodyTargetUs(),
// include/sequence_body_step.h): an open goes that far from closed towards
// open, and a close that far from open towards closed, so a half open and a
// half close are one place.
//
// The targets are the fork's panelTargetToMask(): ring panels by their own
// number, pies PP1/PP2/PP4/PP6 as 08/09/10/12, and the groups 00 (every panel),
// 14 (the pies) and 15 (the ring). PP3 and PP5 have no `:MV` target on the
// fork; a partial move of either is sent as the full `:OP`/`:CL` and the
// Rehearsal says so.
//
// Pure: used by the engine (the run) and the pose planner (the pose), so a
// pose sends what a run sends.
// =============================================================================

#pragma once

#include <stdint.h>
#include <string.h>

// The fork's `:MV` target for a Panel Intent target ("01", "P1", "15"), or
// nullptr when the fork has none.
inline const char* seqDomeMoveTarget(const char* target) {
    if (target == nullptr || strlen(target) != 2) return nullptr;
    if (target[0] == 'P') {
        switch (target[1]) {
            case '1': return "08";
            case '2': return "09";
            case '4': return "10";
            case '6': return "12";
            default:  return nullptr;
        }
    }
    static const char* const kNumeric[] = {"00", "01", "02", "03", "04", "07", "11", "13", "14", "15"};
    for (const char* t : kNumeric) {
        if (strcmp(t, target) == 0) return t;
    }
    return nullptr;
}

// `:OP<target>` or `:CL<target>` with howFar percent (1..99) as the `:MV` line
// the dome resolves, written into out (at least 10 bytes). False - send the
// command as written - when howFar is the whole throw, the command is not an
// open or a close of one target, or the fork has no `:MV` target for it.
inline bool seqDomeHowFarCommand(const char* cmd, uint8_t howFar, char* out, size_t outLen) {
    if (cmd == nullptr || out == nullptr || outLen < 10) return false;
    if (howFar == 0 || howFar >= 100 || cmd[0] != ':' || strlen(cmd) != 5) return false;
    const bool open = cmd[1] == 'O' && cmd[2] == 'P';
    const bool close = cmd[1] == 'C' && cmd[2] == 'L';
    if (!open && !close) return false;
    const char* target = seqDomeMoveTarget(cmd + 3);
    if (target == nullptr) return false;
    // Along the throw from the open end (0) to the closed end (180), rounded
    // half up: an open stops howFar short of closed, a close howFar short of
    // open.
    const uint32_t closedness = open ? (100u - howFar) : howFar;
    const uint32_t value = (180u * closedness + 50u) / 100u;
    out[0] = ':';
    out[1] = 'M';
    out[2] = 'V';
    out[3] = target[0];
    out[4] = target[1];
    out[5] = (char)('0' + (value / 1000u) % 10u);
    out[6] = (char)('0' + (value / 100u) % 10u);
    out[7] = (char)('0' + (value / 10u) % 10u);
    out[8] = (char)('0' + value % 10u);
    out[9] = '\0';
    return true;
}
