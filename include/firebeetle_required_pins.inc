// FireBeetle 2 production pins — complete inventory.
//
// This file enumerates all GPIO pins that the FireBeetle 2 board and its
// DFR1237 shield bring into the firmware's peripheral set. The pins are split
// into two groups: the 14 safety-critical and peripheral outputs that require
// explicit assignment by the firmware design (#190), and the 6 interface lanes
// already committed to standard transports by the board's bring-up.
//
// This file is intentionally an unguarded X-macro inventory. Define
// PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic) before including it to expand the
// rows into compile-time guards; tests expand the same production inventory
// through independent positive and isolated-negative probes.

#ifndef PA_FIREBEETLE_REQUIRED_PIN
  #error "Define PA_FIREBEETLE_REQUIRED_PIN before including this inventory"
#endif

// Safety-critical and peripheral outputs — 14 pins assigned by firmware design (#190)
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

// Standard interface lanes — 6 pins committed by board bring-up
PA_FIREBEETLE_REQUIRED_PIN(PIN_I2C_SCL, "firebeetle2: PIN_I2C_SCL is unassigned - see spec sheet §Exposed GPIO table")
PA_FIREBEETLE_REQUIRED_PIN(PIN_I2C_SDA, "firebeetle2: PIN_I2C_SDA is unassigned - see spec sheet §Exposed GPIO table")
PA_FIREBEETLE_REQUIRED_PIN(PIN_DRIVE_TX, "firebeetle2: PIN_DRIVE_TX is unassigned - see spec sheet §Recommended allocation")
PA_FIREBEETLE_REQUIRED_PIN(PIN_DRIVE_RX, "firebeetle2: PIN_DRIVE_RX is unassigned - see spec sheet §Recommended allocation")
PA_FIREBEETLE_REQUIRED_PIN(PIN_DOME_TX, "firebeetle2: PIN_DOME_TX is unassigned - see spec sheet §Recommended allocation")
PA_FIREBEETLE_REQUIRED_PIN(PIN_DOME_RX, "firebeetle2: PIN_DOME_RX is unassigned - see spec sheet §Recommended allocation")
