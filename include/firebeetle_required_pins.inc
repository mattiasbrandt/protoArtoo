// Required FireBeetle pins consumed by production firmware.
//
// This file is intentionally an unguarded X-macro inventory. Define
// PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic) before including it. config.h
// expands the rows into compile-time guards; tests expand the same production
// inventory through independent positive and isolated-negative probes.

#ifndef PA_FIREBEETLE_REQUIRED_PIN
  #error "Define PA_FIREBEETLE_REQUIRED_PIN before including this inventory"
#endif

PA_FIREBEETLE_REQUIRED_PIN(PIN_SBUS1_RX, "firebeetle2: PIN_SBUS1_RX is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_SBUS2_RX, "firebeetle2: PIN_SBUS2_RX is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_RC_CH3, "firebeetle2: PIN_RC_CH3 is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_RC_CH4, "firebeetle2: PIN_RC_CH4 is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_RC_CH5, "firebeetle2: PIN_RC_CH5 is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_RC_CH6, "firebeetle2: PIN_RC_CH6 is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_AUDIO_TX, "firebeetle2: PIN_AUDIO_TX is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_AUDIO_RX, "firebeetle2: PIN_AUDIO_RX is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_ARM1_SERVO, "firebeetle2: PIN_ARM1_SERVO is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_ARM2_SERVO, "firebeetle2: PIN_ARM2_SERVO is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_ARM3_SERVO, "firebeetle2: PIN_ARM3_SERVO is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_ARM4_SERVO, "firebeetle2: PIN_ARM4_SERVO is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_ARM5_SERVO, "firebeetle2: PIN_ARM5_SERVO is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_DOME_ESC, "firebeetle2: PIN_DOME_ESC is unassigned - see #190")
