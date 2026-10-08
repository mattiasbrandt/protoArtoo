// =============================================================================
// include/servo_halt.h
//
// When a halt lets go of every Servo Output, and what it records as the reason
// (ADR 0043, #417).
//
// Estop and Sleep Mode each release every enabled Output on the frame they are
// entered. They are two edges, not one: a person may still move an Output while
// the droid is asleep (ServoTask refuses only a sequence's moves there), so an
// estop that latches while Sleep Mode is already on has to release whatever
// that person drove. Watching the single edge of `estop || sleep` missed it --
// the halt was already "on", so the estop found no edge and the pulse stayed on
// for the whole latch.
//
// On the edge only, never every frame: a latched estop refuses every command,
// so nothing can put a pulse back while it stands, and a release each frame
// would publish every Output at 50 Hz for nothing. An estop edge while asleep
// with nothing driven only re-labels why each Output is limp, which is true.
//
// Pure: no clock and no state of its own; robot_state.h is here only for
// ServoLimpReason. ServoTask reads both flags under robotStateMux and keeps the
// previous frame's; this header owns the rule, so it is under native test away
// from the 50 Hz loop that applies it -- the shape include/servo_hold.h set for
// the dial's bounds.
// =============================================================================
#pragma once

#include "robot_state.h"  // ServoLimpReason

struct ServoHaltFlags {
    bool estop;
    bool sleep;
};

struct ServoHaltEdge {
    bool release;            // release every enabled Output this frame
    ServoLimpReason reason;  // what the release records; read only when release is set
};

// -----------------------------------------------------------------------------
// servoHaltEdge()
// Whether this frame lets go of every Output: estop was entered, or Sleep Mode
// was. The estop is named whenever it stands, since it is the stronger halt and
// the one a builder must clear, so an estop and Sleep Mode entered together, or
// Sleep Mode entered under a latched estop, both read as the estop.
// -----------------------------------------------------------------------------
inline ServoHaltEdge servoHaltEdge(ServoHaltFlags prev, ServoHaltFlags now) {
    ServoHaltEdge edge = {};
    edge.release = (now.estop && !prev.estop) || (now.sleep && !prev.sleep);
    edge.reason = now.estop ? SERVO_LIMP_ESTOP : SERVO_LIMP_SLEEP;
    return edge;
}
