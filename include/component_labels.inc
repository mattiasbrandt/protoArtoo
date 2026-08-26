// Board Component Label inventory (ADR 0033).
//
// This file is an unguarded X-macro manifest, intentionally included repeatedly.
// Define PA_COMPONENT_LABEL(board_name, component_name, label_text) before including it.
// The label_text is a short string (e.g., "S1", "GPIO 16/17") naming where the
// component is physically connected on that board. Entries may be absent for a board
// where no established label exists.
//
// Component names correspond to registry entries:
//   enable_drive, enable_audio, enable_dome_esc, enable_protor2link,
//   enable_arm1, enable_arm2, enable_aux1, enable_aux2, enable_aux3,
//   enable_rc_ch1, enable_rc_ch2, enable_rc_ch3, enable_rc_ch4, enable_rc_ch5, enable_rc_ch6

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
PA_COMPONENT_LABEL(artoo_esp32, enable_aux1, "AUX1")
PA_COMPONENT_LABEL(artoo_esp32, enable_aux2, "AUX2")
PA_COMPONENT_LABEL(artoo_esp32, enable_aux3, "AUX3")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch1, "CH1")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch2, "CH2")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch3, "CH3")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch4, "CH4")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch5, "CH5")
PA_COMPONENT_LABEL(artoo_esp32, enable_rc_ch6, "CH6")

// firebeetle2 board component labels (labels absent where not yet established)
// firebeetle2 entries may be added as the board's pin plan documentation stabilizes
