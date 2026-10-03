// =============================================================================
// data/servo_motion.js
//
// Auto-generated from docs/servo-motion.yaml by tools/generate_servo_motion.py
// DO NOT EDIT MANUALLY
//
// Source digest: sha256 af61b6095c761cc5eaeec447f35e8a81887042d1a927c55f7d09934b6f0fbeb9
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
  // servoMotionRampMs()
  // How long a move by this profile spends getting up to speed, and slowing
  // down: the profile's own ramp time, and at least a millisecond.
  // -----------------------------------------------------------------------------
  function servoMotionRampMs(profile) {
    const fullThrowMs = f32(profile.throwMs);
    // A ramp longer than half the throw would leave no time to slow down, so
    // the profile's ramp is held to half -- a full throw is then a triangle and
    // still takes exactly throw_ms.
    let rampMs = f32(profile.accelMs === 0 ? 1 : profile.accelMs);
    if (rampMs > f32(fullThrowMs / 2.0)) {
      rampMs = f32(fullThrowMs / 2.0);
    }
    return rampMs;
  }

  // -----------------------------------------------------------------------------
  // servoMotionCruiseUsPerMs()
  // The speed a move by this profile cruises at, in microseconds a
  // millisecond: the one that makes a full throw, ramps included, take exactly
  // throw_ms. rampMs is servoMotionRampMs(profile). The acceleration is this
  // over rampMs, so the profile's two times fix both limits a move keeps to.
  // -----------------------------------------------------------------------------
  function servoMotionCruiseUsPerMs(profile, rampMs) {
    const spanUs = profile.hiUs > profile.loUs ? (profile.hiUs - profile.loUs) & 0xFFFF : 0;
    return f32(f32(spanUs) / f32(f32(profile.throwMs) - rampMs));
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
    const plan = { startMs: 0, fromUs: 0, toUs: 0, settleUs: 0, durationMs: 0, rampMs: 0, softStart: false, restarts: false };
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

    const rampMs = servoMotionRampMs(profile);
    const cruiseUsPerMs = servoMotionCruiseUsPerMs(profile, rampMs);
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
  // servoMotionRetarget()
  // Plan a move to toUs for an Output that is at fromUs and going speedUsPerMs
  // (signed: positive towards the higher width) -- a new target replacing a
  // move part way through, whatever sent it (#442). It keeps to the profile's
  // two limits: it speeds up to the cruise speed and never past it, and slows
  // down at the profile's acceleration and never harder.
  //
  // At a speed of zero it is servoMotionPlan(), exactly. Every move from rest
  // -- and so every move a saved sequence makes from rest, and every time the
  // Rehearsal gives -- is the move it was.
  //
  // Going the way the target lies, with room to slow down before it: no stop.
  // A move from rest at the profile's acceleration is going speedUsPerMs at
  // one moment of its ramp, so the plan is that move, begun that long ago from
  // that far behind the Output, and the Output carries on along it
  // (ServoMotionRamp).
  //
  // A target behind the Output, or one too close to slow down for: the plan
  // slows it to a stop at the profile's acceleration and says so (`restarts`),
  // and the rest is planned from rest where it stopped, once it gets there
  // (servoMotionSettleBack()).
  //
  // The ease, on a move that is already moving:
  //   - `soft` shapes a start from rest and nothing else. Carrying on is not a
  //     start, so it is the plain ramp; the rest after a stop is one, so it is
  //     soft.
  //   - `overshoot` aims when a move from rest over the same distance would --
  //     from where the Output is to the target -- and settles back as it does
  //     from rest. The rest after a stop aims, or not, over its own distance,
  //     as every move from rest does.
  //
  // Two limits the profile cannot keep by itself, both reachable only when the
  // move in progress was faster than this profile -- a Gesture's own throw
  // (servoMotionOverride()), or the row changed under the move:
  //   - a speed over the cruise is taken as the cruise. That is the one step
  //     in speed this planner makes;
  //   - a stop never carries the Output past the recorded end it is heading
  //     for. Every point of a ramp lies between two widths already inside the
  //     component's band (ADR 0041) and a brake point past the end need not,
  //     so the stop is planned from the speed that ends it on the end.
  //
  // A moving plan too short to ramp is the move from rest instead, never a
  // snap: ServoTask writes a snap's target, not a brake point or an aim.
  // -----------------------------------------------------------------------------
  function servoMotionRetarget(fromUs, speedUsPerMs, toUs, profile, startMs) {
    const rest = servoMotionPlan(fromUs, toUs, profile, startMs);
    if (speedUsPerMs === 0.0 || !profile.calibrated || profile.hiUs <= profile.loUs || profile.throwMs < SERVO_THROW_MS_MIN) {
      return rest;
    }

    const rampMs = servoMotionRampMs(profile);
    const cruiseUsPerMs = servoMotionCruiseUsPerMs(profile, rampMs);
    const accelUsPerMs2 = f32(cruiseUsPerMs / rampMs);
    const rising = speedUsPerMs > 0.0;
    let speed = rising ? speedUsPerMs : -speedUsPerMs;
    if (speed > cruiseUsPerMs) {
      speed = cruiseUsPerMs;
    }
    // How long ago, to the millisecond, a move from rest would have set off to
    // be going this fast now, and how far behind the Output. The distance is
    // the one that whole millisecond gives, so the plan passes through the
    // Output now rather than half a millisecond either side of it.
    let sinceMs = lroundf(f32(speed / accelUsPerMs2));
    let behindUs = lroundf(f32(f32(f32(0.5 * accelUsPerMs2) * f32(sinceMs)) * f32(sinceMs)));

    const aimUs = profile.easing === SERVO_EASE_OVERSHOOT ? servoMotionOvershootAim(fromUs, toUs, profile.loUs, profile.hiUs) : toUs;
    // behindUs is also how far the Output needs to stop: the move from rest
    // turned round covers it again slowing down.
    const aheadUs = rising ? aimUs - fromUs : fromUs - aimUs;
    const carriesOn = aheadUs >= (behindUs | 0);
    if (!carriesOn) {
      // Room to stop in: up to the recorded end it is heading for. Past it
      // already, there is none, and the move is the one from rest.
      const roomUs = rising ? profile.hiUs - fromUs : fromUs - profile.loUs;
      if (roomUs < 1) {
        return rest;
      }
      if (roomUs < (behindUs | 0)) {
        // The fastest it can be going and still stop on the end, whole
        // milliseconds rounded down so the stop lands on it or short.
        sinceMs = lroundf(f32(sqrtf(f32(f32(2.0 * f32(roomUs)) / accelUsPerMs2)) - 0.5));
        behindUs = lroundf(f32(f32(f32(0.5 * accelUsPerMs2) * f32(sinceMs)) * f32(sinceMs)));
      }
    }
    // Slow enough to stop inside a millisecond is at rest.
    if (sinceMs < 1 || behindUs < 1) {
      return rest;
    }
    const originUs = rising ? fromUs - (behindUs | 0) : fromUs + (behindUs | 0);
    if (originUs < 0 || originUs > 0xFFFF) {
      return rest;
    }
    const stopUs = rising ? fromUs + (behindUs | 0) : fromUs - (behindUs | 0);

    // The ramp this is the tail of is the plain one: `soft` shapes a start
    // from rest, and neither carrying on nor stopping is one.
    const plain = { ...profile };
    plain.easing = SERVO_EASE_NONE;
    const plan = servoMotionPlan(originUs & 0xFFFF, carriesOn ? aimUs : stopUs & 0xFFFF, plain, (startMs - (sinceMs >>> 0)) >>> 0);
    if (plan.durationMs === 0) {
      return rest;
    }
    plan.settleUs = toUs;
    plan.restarts = !carriesOn;
    return plan;
  }

  // -----------------------------------------------------------------------------
  // servoMotionSettles()
  // The second half of an overshoot, or of a move that had to stop first. A
  // plan that settles has ended at its aim, or where it stopped, rather than
  // its target; ServoTask chains the move to the target
  // (servoMotionSettleBack()) when the first plan arrives.
  // -----------------------------------------------------------------------------
  function servoMotionSettles(ramp) {
    return ramp.settleUs !== ramp.toUs;
  }

  // -----------------------------------------------------------------------------
  // servoMotionSettleBack()
  // The move from where a plan that settles arrived to its target:
  // servoMotionPlan() again, from rest.
  //
  // From an overshoot's aim it is shaped `none`, because it is the settle and
  // not a new move with a character of its own. From a stop (`restarts`) it is
  // the rest of the move, and a start from rest like any other, so it has the
  // profile's own ease -- an overshoot of its own included.
  //
  // The profile is the caller's. ServoTask passes the row's as it reads it on
  // arrival, and for the rest after a stop puts back the Gesture's own throw
  // and ease that the stop was planned with (servoMotionOverride(), ADR 0049),
  // so the whole move keeps one pace. An overshoot's settle is the row's.
  // -----------------------------------------------------------------------------
  function servoMotionSettleBack(arrived, profile, nowMs) {
    const settle = { ...profile };
    if (!arrived.restarts) {
      settle.easing = SERVO_EASE_NONE;
    }
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
    sourceSha256: "af61b6095c761cc5eaeec447f35e8a81887042d1a927c55f7d09934b6f0fbeb9",
    SERVO_THROW_MS_MIN,
    SEQ_CADENCE_FLOOR_MS,
    ServoEasing: Object.freeze({ SERVO_EASE_NONE, SERVO_EASE_SOFT, SERVO_EASE_OVERSHOOT, SERVO_EASE_COUNT }),
    servoMotionOvershootAim,
    servoMotionRampMs,
    servoMotionCruiseUsPerMs,
    servoMotionPlan,
    servoMotionRetarget,
    servoMotionSettles,
    servoMotionSettleBack,
    servoMotionArrivalMs,
  });
})();
