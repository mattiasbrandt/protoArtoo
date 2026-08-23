// FireBeetle 2 production pins — complete inventory.
//
// This file enumerates all GPIO pins that the FireBeetle 2 board and its
// DFR1237 shield bring into the firmware's peripheral set. Pins are grouped
// by assignment status: the first 14 are newly required by #190 Slice 2; the
// remaining 6 are assigned by prior work and listed here so that coherence
// checks can verify against the complete pin set.
//
// This file is intentionally an unguarded X-macro inventory. Define
// PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic) before including it. config.h
// expands the rows into compile-time guards; tests expand the same production
// inventory through independent positive and isolated-negative probes.

#ifndef PA_FIREBEETLE_REQUIRED_PIN
  #error "Define PA_FIREBEETLE_REQUIRED_PIN before including this inventory"
#endif

// Newly assigned in Slice 2 (#190) — 14 pins, safety-critical SBUS pair first
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

// Already assigned in prior slices — 6 pins, included for coherence verification
PA_FIREBEETLE_REQUIRED_PIN(PIN_I2C_SCL, "firebeetle2: PIN_I2C_SCL is unassigned - see #185")
PA_FIREBEETLE_REQUIRED_PIN(PIN_I2C_SDA, "firebeetle2: PIN_I2C_SDA is unassigned - see #185")
PA_FIREBEETLE_REQUIRED_PIN(PIN_HOVERBOARD_TX, "firebeetle2: PIN_HOVERBOARD_TX is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_HOVERBOARD_RX, "firebeetle2: PIN_HOVERBOARD_RX is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_DOME_TX, "firebeetle2: PIN_DOME_TX is unassigned - see #190")
PA_FIREBEETLE_REQUIRED_PIN(PIN_DOME_RX, "firebeetle2: PIN_DOME_RX is unassigned - see #190")
