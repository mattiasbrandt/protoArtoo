// Component Registry inventory (ADR 0042 as amended 2026-09-09; #303, #340).
//
// The single declaration of every Component Family and its members. Firmware
// tables, the identity manifest and the operator lineup all derive from these
// rows, so a product is added here and nowhere else. The content is #303's
// resolved lineup, verbatim: seven categories, twenty-one products, nine of
// them roadmap.
//
// This file is an unguarded X-macro manifest, intentionally included
// repeatedly. Define BOTH PA_COMPONENT_CATEGORY and PA_COMPONENT_PART before
// including it -- define the one you do not need as empty rather than leaving
// it out, so a missing definition stays a hard error instead of silently
// dropping half the registry. Adding #pragma once here breaks the mechanism:
// include/component_registry.h expands this file four times in one translation
// unit with different macro definitions, and #pragma once would make every
// expansion after the first a no-op.
//
// -----------------------------------------------------------------------------
// PA_COMPONENT_CATEGORY(enumerator, id, name, member_key)
// -----------------------------------------------------------------------------
//   enumerator  the C++ enumerator for this Component Family
//   id          the stable token the JSON payload reports it under
//   name        the operator-visible category name
//   member_key  the NVS key holding this family's Component Member, or nullptr
//               where the family has no member setting. A member setting
//               exists ONLY where identity reports more than one selectable
//               member (CONTEXT.md "Component Member"); the invariant is
//               asserted in test/test_native/test_component_registry, not
//               trusted to whoever edits this file.
//
// -----------------------------------------------------------------------------
// PA_COMPONENT_PART(value, id, name, category, protocol, status, capabilities,
//                   gate, included)
// -----------------------------------------------------------------------------
//   value         stable numeric id, unique across the whole registry. This is
//                 what a Component Member setting persists, so a value is never
//                 reused and never renumbered -- a renumber silently re-points
//                 every controller that already stored the old number. New rows
//                 take the next free value wherever they sit in the table.
//   id            the stable token the JSON payload reports it under
//   name          the operator-visible product name. It travels in the image on
//                 purpose: `make ota` and `make uploadfs` are separate steps, so
//                 a controller whose firmware is newer than its web assets must
//                 be able to name what it reports without help (ADR 0042).
//   category      a PA_COMPONENT_CATEGORY enumerator
//   protocol      the Component Protocol token firmware speaks to this product.
//                 One protocol commonly serves several products, and two rows
//                 sharing a token is the visible form of that -- `de_packet_serial`
//                 drives a Sabertooth under Foot Drive and a SyRen under Dome
//                 Rotation from one implementation (CONTEXT.md "Component
//                 Protocol"). `none` is the Body Controller's: it hosts
//                 protoArtoo rather than being driven by it.
//   status        COMPONENT_STATUS_SUPPORTED (implemented and drivable) or
//                 COMPONENT_STATUS_ROADMAP (intended, not built). Two values,
//                 and "might" is deliberately not one of them (#303).
//   capabilities  what this product can be asked, as the family's own bitmask.
//                 Declared per part, not per family and not per protocol: two
//                 products on one protocol can genuinely differ in what they
//                 can be asked (ADR 0042). Sound is the only family with a
//                 vocabulary today (AudioDriver::AUDIO_CAP_*); every other row
//                 declares 0, which says "nothing to ask", not "unknown".
//   gate          the Board Capability Gate this part requires, as the name
//                 string the payload reports, or nullptr where it needs none.
//                 nullptr is universal -- the same meaning ActionEntry gives it
//                 (include/action_registry.h). This is the row's board
//                 applicability: what a board can be WIRED for, which is a
//                 different question from what this image carries.
//   included      does this image carry a driver for this part, as a 0/1
//                 compile-time expression. Derived here rather than declared
//                 anywhere else, and it is what "more than one selectable
//                 member" counts. A roadmap row is always literally 0: drivers
//                 are carried only for supported parts, so a row present with
//                 no driver is the normal way an unbuilt part exists rather
//                 than a defect (ADR 0042 amended). A row naming a gate
//                 consults that gate here -- the pairing is checked by
//                 test/test_tools/test_component_registry_drift.py.

#ifndef PA_COMPONENT_CATEGORY
  #error "Define PA_COMPONENT_CATEGORY before including this inventory"
#endif
#ifndef PA_COMPONENT_PART
  #error "Define PA_COMPONENT_PART before including this inventory"
#endif

// -----------------------------------------------------------------------------
// The seven categories (#303, resolved by the operator 2026-09-07).
// -----------------------------------------------------------------------------
PA_COMPONENT_CATEGORY(COMPONENT_CATEGORY_BODY_CONTROLLER,       "body_controller",       "Body Controller",       nullptr)
PA_COMPONENT_CATEGORY(COMPONENT_CATEGORY_RADIO_CONTROLLER,      "radio_controller",      "Radio Controller",      nullptr)
PA_COMPONENT_CATEGORY(COMPONENT_CATEGORY_BODY_SERVO_CONTROLLER, "body_servo_controller", "Body servo controller", nullptr)
PA_COMPONENT_CATEGORY(COMPONENT_CATEGORY_DOME_ROTATION,         "dome_rotation",         "Dome Rotation",         nullptr)
PA_COMPONENT_CATEGORY(COMPONENT_CATEGORY_DOME_CONTROLLER,       "dome_controller",       "Dome Controller",       nullptr)
PA_COMPONENT_CATEGORY(COMPONENT_CATEGORY_FOOT_DRIVE,            "foot_drive",            "Foot Drive",            nullptr)
// Sound is the only family carrying more than one selectable member, so it is
// the only one with a Component Member setting. "snd_member" is its NVS key
// (include/config_store.h, ConfigKey::SOUND_MEMBER); 10 characters, clear of
// the 15-character Preferences ceiling.
PA_COMPONENT_CATEGORY(COMPONENT_CATEGORY_SOUND,                 "sound",                 "Sound",                 "snd_member")

// -----------------------------------------------------------------------------
// Body Controller -- the board hosting protoArtoo. "more to come"; the category
// is open (#303). Neither row is driven by protoArtoo, so neither declares a
// protocol: the running image IS the answer to which one is fitted, which is
// why `included` tests PA_BOARD rather than a capability.
// -----------------------------------------------------------------------------
PA_COMPONENT_PART( 1, "artoo_pcb",  "Artoo PCB (artoo.uk)",   COMPONENT_CATEGORY_BODY_CONTROLLER, "none", COMPONENT_STATUS_SUPPORTED, 0, nullptr, (PA_BOARD == PA_BOARD_ARTOO_ESP32))
PA_COMPONENT_PART( 2, "firebeetle2", "FireBeetle 2 (ESP32-P4)", COMPONENT_CATEGORY_BODY_CONTROLLER, "none", COMPONENT_STATUS_SUPPORTED, 0, nullptr, (PA_BOARD == PA_BOARD_FIREBEETLE2))

// -----------------------------------------------------------------------------
// Radio Controller -- the RC gear the droid is driven with. Three supported
// rows: Standard PWM, single SBUS and dual SBUS all ship today
// (docs/pin_map.md, src/tasks/rc_input.cpp), and single-or-dual is
// configuration rather than a second product.
// -----------------------------------------------------------------------------
PA_COMPONENT_PART( 3, "hotrc_ds650",         "HotRC DS-650",         COMPONENT_CATEGORY_RADIO_CONTROLLER, "sbus",          COMPONENT_STATUS_SUPPORTED, 0, nullptr, 1)
PA_COMPONENT_PART( 4, "rc_transmitter_pwm",  "RC Transmitter - PWM",  COMPONENT_CATEGORY_RADIO_CONTROLLER, "standard_pwm",  COMPONENT_STATUS_SUPPORTED, 0, nullptr, 1)
PA_COMPONENT_PART( 5, "rc_transmitter_sbus", "RC Transmitter - SBUS", COMPONENT_CATEGORY_RADIO_CONTROLLER, "sbus",          COMPONENT_STATUS_SUPPORTED, 0, nullptr, 1)
PA_COMPONENT_PART( 6, "rc_transmitter_elrs", "RC Transmitter - ELRS", COMPONENT_CATEGORY_RADIO_CONTROLLER, "crsf",          COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)
PA_COMPONENT_PART( 7, "xbox_controller",     "Xbox Controller",       COMPONENT_CATEGORY_RADIO_CONTROLLER, "bluetooth_hid", COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)

// -----------------------------------------------------------------------------
// Body servo controller -- what drives the body's servos.
// -----------------------------------------------------------------------------
PA_COMPONENT_PART( 8, "esp32_gpio_ledc", "ESP32 GPIO (LEDC)", COMPONENT_CATEGORY_BODY_SERVO_CONTROLLER, "ledc",             COMPONENT_STATUS_SUPPORTED, 0, nullptr, 1)
PA_COMPONENT_PART( 9, "pca9685",         "PCA9685",           COMPONENT_CATEGORY_BODY_SERVO_CONTROLLER, "i2c_pwm_expander", COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)
PA_COMPONENT_PART(10, "pololu_maestro",  "Pololu Maestro",    COMPONENT_CATEGORY_BODY_SERVO_CONTROLLER, "maestro_serial",   COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)

// -----------------------------------------------------------------------------
// Dome Rotation -- what turns the dome.
// -----------------------------------------------------------------------------
PA_COMPONENT_PART(11, "isdt_esc70", "ISDT ESC70 (RC ESC)", COMPONENT_CATEGORY_DOME_ROTATION, "ledc",              COMPONENT_STATUS_SUPPORTED, 0, nullptr, 1)
PA_COMPONENT_PART(12, "syren10",    "SyRen 10",            COMPONENT_CATEGORY_DOME_ROTATION, "de_packet_serial",  COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)

// -----------------------------------------------------------------------------
// Dome Controller -- the dome's own board, panels and lighting, over protoR2link.
// Teeces' protocol is genuinely undecided and says so rather than guessing;
// #313 resolves it.
// -----------------------------------------------------------------------------
PA_COMPONENT_PART(13, "astropixels_plus", "AstroPixels Plus", COMPONENT_CATEGORY_DOME_CONTROLLER, "protor2link", COMPONENT_STATUS_SUPPORTED, 0, nullptr, 1)
PA_COMPONENT_PART(14, "teeces",           "Teeces",           COMPONENT_CATEGORY_DOME_CONTROLLER, "tbd",         COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)

// -----------------------------------------------------------------------------
// Foot Drive -- what drives the feet.
// -----------------------------------------------------------------------------
PA_COMPONENT_PART(15, "hoverboard",           "Hoverboard, hacked firmware", COMPONENT_CATEGORY_FOOT_DRIVE, "hoverboard_gen2_uart", COMPONENT_STATUS_SUPPORTED, 0, "PA_CAP_DRIVE_BACKEND_HOVERBOARD", PA_CAP_DRIVE_BACKEND_HOVERBOARD)
PA_COMPONENT_PART(16, "sabertooth_2x25",      "Sabertooth 2x25",             COMPONENT_CATEGORY_FOOT_DRIVE, "de_packet_serial",     COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)
PA_COMPONENT_PART(17, "flipsky_mini_v6_vesc", "Flipsky Mini V6 VESC",        COMPONENT_CATEGORY_FOOT_DRIVE, "vesc_uart",            COMPONENT_STATUS_ROADMAP,   0, nullptr, 0)

// -----------------------------------------------------------------------------
// Sound -- the sound module, and the only Component Family with more than one
// selectable member. All three supported modules bolt to PIN_AUDIO_TX
// (include/config.h, include/audio_chirp.h, include/audio_mp3trigger.h), so no
// board narrows the set and every image carries all three drivers.
//
// These capability words are the ONE declaration of what each module can be
// asked: the concrete drivers return them from capabilities() rather than
// restating them (include/audio_chirp.h, include/audio_dy_sv5w.h,
// include/audio_mp3trigger.h), so a row and its driver cannot drift apart.
//
// DFPlayer Mini declares 0 because nobody has read its protocol against this
// bitmask yet, not because it can be asked nothing. #305 declares its bits when
// it builds the driver; until then 0 is the honest word for "none declared".
// -----------------------------------------------------------------------------
PA_COMPONENT_PART(18, "dy_sv5w",       "DY-SV5W",             COMPONENT_CATEGORY_SOUND, "soft_uart_binary",  COMPONENT_STATUS_SUPPORTED,
                  AudioDriver::AUDIO_CAP_STATUS_QUERY | AudioDriver::AUDIO_CAP_DEVICE_TYPE |
                  AudioDriver::AUDIO_CAP_TRACK_COUNT | AudioDriver::AUDIO_CAP_CURRENT_TRACK,
                  nullptr, 1)
PA_COMPONENT_PART(19, "mp3_trigger",   "MP3 Trigger",         COMPONENT_CATEGORY_SOUND, "mp3trigger_serial", COMPONENT_STATUS_SUPPORTED,
                  AudioDriver::AUDIO_CAP_STATUS_QUERY | AudioDriver::AUDIO_CAP_TRACK_COUNT |
                  AudioDriver::AUDIO_CAP_CURRENT_TRACK,
                  nullptr, 1)
PA_COMPONENT_PART(20, "chirp",         "CHIRP Audio Trigger", COMPONENT_CATEGORY_SOUND, "chirp_ascii_uart",  COMPONENT_STATUS_SUPPORTED,
                  AudioDriver::AUDIO_CAP_STATUS_QUERY | AudioDriver::AUDIO_CAP_DEVICE_TYPE |
                  AudioDriver::AUDIO_CAP_TRACK_COUNT | AudioDriver::AUDIO_CAP_CURRENT_TRACK |
                  AudioDriver::AUDIO_CAP_QUERY_SAFE_PLAYING | AudioDriver::AUDIO_CAP_CATALOG,
                  nullptr, 1)
PA_COMPONENT_PART(21, "dfplayer_mini", "DFPlayer Mini",       COMPONENT_CATEGORY_SOUND, "dfplayer_serial",   COMPONENT_STATUS_ROADMAP, 0, nullptr, 0)
