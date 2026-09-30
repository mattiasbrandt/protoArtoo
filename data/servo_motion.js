// =============================================================================
// data/servo_motion.js
//
// Auto-generated from docs/servo-motion.yaml by tools/generate_servo_motion.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 f58ec1c704908ab8ae93d86faf3f49b6de31a4b2275ade85efb2c266dd8d4e98
//
// The Servo Output motion model, as ServoTask runs it: the functions below
// are generated from the same declaration as include/servo_motion_model.h, so
// a move timed here is the move the droid plans (#287 specific 11, #439).
// Constants the firmware headers define are read from those headers when
// this is generated, and say where they came from.
// =============================================================================

(() => {
  "use strict";

  // Single precision, as the firmware computes: Math.fround() rounds a double to
  // the nearest float, which is exact for + - * / and for sqrt of a float.
  const f32 = Math.fround;
  const sqrtf = (x) => f32(Math.sqrt(x));
  // The C library's lroundf(): half away from zero. x is already a float, so
  // x + 0.5 is exact in a double and floor() does the rounding.
  const lroundf = (x) => (x < 0 ? -Math.floor(-x + 0.5) : Math.floor(x + 0.5));
  // C integer division truncates towards zero.
  const trunc = Math.trunc;

  const SERVO_THROW_MS_MIN = 20;  // include/servo_output_row.h
  const SEQ_CADENCE_FLOOR_MS = 450;  // include/sequence_bulk_centre.h
  const SERVO_EASE_NONE = 0;  // ServoEasing, include/servo_output_row.h
  const SERVO_EASE_SOFT = 1;  // ServoEasing, include/servo_output_row.h
  const SERVO_EASE_OVERSHOOT = 2;  // ServoEasing, include/servo_output_row.h
  const SERVO_EASE_COUNT = 3;  // ServoEasing, include/servo_output_row.h

  // -----------------------------------------------------------------------------
  // servoMotionOvershootAim()
  // Where an overshoot aims for a move from fromUs to toUs between the recorded
  // ends loUs..hiUs, or toUs itself when the move is not one to overshoot.
  //
  // A twelfth of the distance past the target, on a move longer than an eighth of
  // the travel -- a shorter one would read as a wobble rather than as weight --
  // and clamped to the recorded ends, which is the whole fence (ADR 0052). A
  // target already at or outside an end has no room past it, so it gets no aim
  // rather than one pointing back the way the move came, or one clamped onto the
  // nearer end past a target the calibration dial left outside the ends (#417).
  // -----------------------------------------------------------------------------
  function servoMotionOvershootAim(fromUs, toUs, loUs, hiUs) {
    if (toUs <= loUs || toUs >= hiUs) {
      return toUs;
    }
    const delta = toUs - fromUs;
    const distance = delta < 0 ? -delta : delta;
    if (hiUs <= loUs || distance <= trunc((hiUs - loUs) / 8)) {
      return toUs;
    }
    const over = trunc(distance / 12);
    let aim = toUs + (delta > 0 ? over : -over);
    if (aim < loUs) {
      aim = loUs;
    }
    if (aim > hiUs) {
      aim = hiUs;
    }
    if ((delta > 0 && aim <= toUs) || (delta < 0 && aim >= toUs)) {
      return toUs;
    }
    return aim & 0xFFFF;
  }

  // -----------------------------------------------------------------------------
  // servoMotionPlan()
  // Plan a move from fromUs to toUs by an Output's Motion Profile. The rate comes
  // from the span of the profile's recorded ends; the shape from its ease.
  //
  // The profile stores two times, not a rate: accelerate for the profile's own
  // ramp time, cruise at the speed that makes a full throw take exactly
  // throw_ms, and slow down the same way. A move shorter than two ramps never
  // reaches that speed, so it is a triangle -- the same acceleration, turned
  // round half way. A partial move therefore takes less time than a full throw,
  // but not proportionally less: the ramps at each end are paid whatever the
  // distance.
  //
  // An overshoot plans the way out, to the aim, with settleUs = toUs; the way back
  // is servoMotionSettleBack()'s. Every snap below goes straight to toUs, never
  // to an aim: a move with no ramp has nothing to overshoot with.
  //
  // It snaps -- durationMs 0 -- in four cases, each a statement about what the
  // model knows rather than a tuning choice:
  //   - the output is uncalibrated. Full throw is only defined once somebody has
  //     measured the ends, so there is no rate to derive and the move is a jump
  //     rather than a ramp (ADR 0052). Every row adopted from the fixed field sets
  //     is uncalibrated, so an existing droid moves exactly as it did until its
  //     builder calibrates an output;
  //   - the pair has no span, which is the same missing rate by another route;
  //   - there is nowhere to go;
  //   - the move would finish inside one 50 Hz ServoTask frame, which cannot
  //     resolve a ramp shorter than itself (SERVO_THROW_MS_MIN).
  // -----------------------------------------------------------------------------
  function servoMotionPlan(fromUs, toUs, profile, startMs) {
    const plan = { startMs: 0, fromUs: 0, toUs: 0, settleUs: 0, durationMs: 0, rampMs: 0, softStart: false };
    plan.startMs = startMs;
    plan.fromUs = fromUs;
    plan.toUs = toUs;
    plan.settleUs = toUs;
    const spanUs = profile.hiUs > profile.loUs ? (profile.hiUs - profile.loUs) & 0xFFFF : 0;
    if (!profile.calibrated || spanUs === 0 || fromUs === toUs || profile.throwMs < SERVO_THROW_MS_MIN) {
      return plan;
    }

    // Decided here, when the target is set: an overshoot changes where this
    // plan ends and nothing else about it.
    const aimUs = profile.easing === SERVO_EASE_OVERSHOOT ? servoMotionOvershootAim(fromUs, toUs, profile.loUs, profile.hiUs) : toUs;

    const fullThrowMs = f32(profile.throwMs);
    // A ramp longer than half the throw would leave no time to slow down, so
    // the profile's ramp is held to half -- a full throw is then a triangle and
    // still takes exactly throw_ms.
    let rampMs = f32(profile.accelMs === 0 ? 1 : profile.accelMs);
    if (rampMs > f32(fullThrowMs / 2.0)) {
      rampMs = f32(fullThrowMs / 2.0);
    }
    const cruiseUsPerMs = f32(f32(spanUs) / f32(fullThrowMs - rampMs));
    const accelUsPerMs2 = f32(cruiseUsPerMs / rampMs);

    const distanceUs = f32(aimUs > fromUs ? aimUs - fromUs : fromUs - aimUs);
    let durationMs = 0.0;
    let usedRampMs = 0.0;
    if (distanceUs >= f32(cruiseUsPerMs * rampMs)) {
      usedRampMs = rampMs;
      durationMs = f32(f32(distanceUs / cruiseUsPerMs) + rampMs);
    } else {
      usedRampMs = sqrtf(f32(distanceUs / accelUsPerMs2));
      durationMs = f32(2.0 * usedRampMs);
    }

    if (durationMs < f32(SERVO_THROW_MS_MIN)) {
      return plan;
    }
    plan.toUs = aimUs;
    plan.softStart = profile.easing === SERVO_EASE_SOFT;
    const roundedDuration = lroundf(durationMs);
    plan.durationMs = roundedDuration > 0xFFFF ? 0xFFFF : roundedDuration & 0xFFFF;
    let roundedRamp = lroundf(usedRampMs);
    if (roundedRamp < 1) {
      roundedRamp = 1;
    }
    if (roundedRamp > trunc(plan.durationMs / 2)) {
      roundedRamp = trunc(plan.durationMs / 2);
    }
    plan.rampMs = roundedRamp & 0xFFFF;
    return plan;
  }

  // -----------------------------------------------------------------------------
  // servoMotionSettles()
  // The second half of an overshoot. A plan that settles has ended at its aim
  // rather than its target; the way back is an ordinary move from the aim to the
  // target, shaped `none`, because it is the settle and not a new move with a
  // character of its own. ServoTask chains it when the first plan arrives.
  // -----------------------------------------------------------------------------
  function servoMotionSettles(ramp) {
    return ramp.settleUs !== ramp.toUs;
  }

  // -----------------------------------------------------------------------------
  // servoMotionSettleBack()
  // The way back from an overshoot's aim to its target: servoMotionPlan() again,
  // from where the first plan arrived, shaped `none`.
  // -----------------------------------------------------------------------------
  function servoMotionSettleBack(arrived, profile, nowMs) {
    const settle = { ...profile };
    settle.easing = SERVO_EASE_NONE;
    return servoMotionPlan(arrived.toUs, arrived.settleUs, settle, nowMs);
  }

  // -----------------------------------------------------------------------------
  // servoMotionArrivalMs()
  // How long after it is sent a move from fromUs to toUs comes to rest on toUs:
  // the plan, and on an overshoot the settle ServoTask chains after it. Zero is
  // a snap -- an Output nobody has measured jumps (ADR 0052), and how fast the
  // servo itself gets there is not something this model knows.
  //
  // This is the figure the Rehearsal times a body move with (#439): the
  // browser's answer is this function's, generated from this file, so it is
  // the answer ServoTask's own plan gives for the same row and the same move.
  // -----------------------------------------------------------------------------
  function servoMotionArrivalMs(fromUs, toUs, profile) {
    const out = servoMotionPlan(fromUs, toUs, profile, 0);
    if (!servoMotionSettles(out)) {
      return out.durationMs;
    }
    const back = servoMotionSettleBack(out, profile, out.durationMs);
    return (out.durationMs + back.durationMs) >>> 0;
  }

  window.ServoMotion = Object.freeze({
    source: "docs/servo-motion.yaml",
    generator: "tools/generate_servo_motion.py",
    sourceSha256: "f58ec1c704908ab8ae93d86faf3f49b6de31a4b2275ade85efb2c266dd8d4e98",
    SERVO_THROW_MS_MIN,
    SEQ_CADENCE_FLOOR_MS,
    ServoEasing: Object.freeze({ SERVO_EASE_NONE, SERVO_EASE_SOFT, SERVO_EASE_OVERSHOOT, SERVO_EASE_COUNT }),
    servoMotionOvershootAim,
    servoMotionPlan,
    servoMotionSettles,
    servoMotionSettleBack,
    servoMotionArrivalMs,
  });
})();
