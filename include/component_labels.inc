// Board Component Label inventory (ADR 0033).
//
// This file is an unguarded X-macro manifest, intentionally included repeatedly.
// Define PA_COMPONENT_LABEL(board_name, component_name, label_text) before including it.
// The label_text is a short string (e.g., "S1", "GPIO 16/17") naming where the
// component is physically connected on that board. For an Output (enable_arm1 ..
// enable_aux3) the label IS the Output's name on screen, so every board declares
// one for each of its Outputs; any other entry may be absent where the board has
// no established label (CONTEXT.md "Board Component Label", ADR 0033 Amendment
// 2026-09-19).
//
// Component names correspond to registry entries:
//   enable_drive, enable_audio, enable_dome_esc, enable_protor2link,
//   enable_arm1, enable_arm2, enable_aux1, enable_aux2, enable_aux3,
//   enable_rc_ch1, enable_rc_ch2, enable_rc_ch3, enable_rc_ch4, enable_rc_ch5, enable_rc_ch6
//
// enable_arm1 / enable_arm2 name Outputs, not Parts: whether this controller
// drives the lead plugged into ARM1, never which piece of the droid that lead
// moves. The Droid Parts Catalog's `utilUp` / `utilLo` are the Part names, and
// what joins the two is the Part list on the Servo Output row addressed to that
// channel (ADR 0041, ADR 0050, #345). See include/ledc_pwm.h.

#ifndef PA_COMPONENT_LABEL
  #error "Define PA_COMPONENT_LABEL before including this inventory"
#endif

// artoo_esp32 board component labels (PCB silkscreen and GPIO legend)
PA_COMPONENT_LABEL(artoo_esp32, enable_drive, "S1")
PA_COMPONENT_LABEL(artoo_esp32, enable_audio, "S2")
PA_COMPONENT_LABEL(artoo_esp32, enable_dome_esc, "DOME")
PA_COMPONENT_LABEL(artoo_esp32, enable_protor2link, "S3")
PA_COMPONENT_LABEL(artoo_esp32, enable_arm1, "ARM1")
PA_COMPONENT_LABEL(artoo_esp32, enable_arm2, "ARM2")
// The Outputs behind enable_aux1..3 are printed ARM3 / ARM4 / ARM5 on the Artoo
// PCB (docs/pin_map.md "GPIO Assignment Summary (Artoo-esp32)", the traced board).
// Until #411 they read AUX1-AUX3 here: protoArtoo's word, on a board that
// prints another, and Wiring names every wire by what the board prints.
PA_COMPONENT_LABEL(artoo_esp32, enable_aux1, "ARM3")
PA_COMPONENT_LABEL(artoo_esp32, enable_aux2, "ARM4")
PA_COMPONENT_LABEL(artoo_esp32, enable_aux3, "ARM5")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch1, "CH1")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch2, "CH2")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch3, "CH3")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch4, "CH4")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch5, "CH5")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch6, "CH6")

// firebeetle2 board component labels, from docs/pin_map.md "FireBeetle 2: GPIO
// Assignment Summary". The DFR1237 prints a bare GPIO number beside each pin
// and nothing else, so every label here is `GPIO <n>` (CONTEXT.md "Board
// Component Label", ADR 0033 Amendment 2026-09-19) - a serial lane names both
// of its pins. The SPI block prints "28/SCK", "29/MO" and "30/MI"; the number
// is what a builder finds, so those read the same way as the main field.
PA_COMPONENT_LABEL(firebeetle2, enable_drive, "GPIO 20/21")
PA_COMPONENT_LABEL(firebeetle2, enable_audio, "GPIO 34/36")
PA_COMPONENT_LABEL(firebeetle2, enable_dome_esc, "GPIO 48")
PA_COMPONENT_LABEL(firebeetle2, enable_protor2link, "GPIO 22/23")
PA_COMPONENT_LABEL(firebeetle2, enable_arm1, "GPIO 49")
PA_COMPONENT_LABEL(firebeetle2, enable_arm2, "GPIO 50")
PA_COMPONENT_LABEL(firebeetle2, enable_aux1, "GPIO 4")
PA_COMPONENT_LABEL(firebeetle2, enable_aux2, "GPIO 5")
PA_COMPONENT_LABEL(firebeetle2, enable_aux3, "GPIO 51")
PA_COMPONENT_LABEL(firebeetle2, enable_rc_ch1, "GPIO 28")
PA_COMPONENT_LABEL(firebeetle2, enable_rc_ch2, "GPIO 29")
PA_COMPONENT_LABEL(firebeetle2, enable_rc_ch3, "GPIO 30")
PA_COMPONENT_LABEL(firebeetle2, enable_rc_ch4, "GPIO 31")
PA_COMPONENT_LABEL(firebeetle2, enable_rc_ch5, "GPIO 32")
PA_COMPONENT_LABEL(firebeetle2, enable_rc_ch6, "GPIO 33")
