// =============================================================================
// src/config_serializer.cpp
//
// Pure config serialization implementation.
// No logging, no FreeRTOS, no task-level calls  --  uses Arduino String for NVS string values.
// =============================================================================

#include "config_serializer.h"

#include "api_helpers.h"
#include "audio_dollar_parser.h"
#include "config.h"
#include "config_settings.h"  // every Setting's NVS key, its check and its default
#include "dome_math.h"  // domePulsesInOrder()
#include "rc_mapping.h"
#include "board_outputs.h"            // which Output a retired aux_led_pin slot named
#include "servo_legacy_field_sets.h"  // the NVS keys the fixed sets left behind

#include <cstdlib>
#include <cstring>

namespace {

// NVS has no float primitive; store floats as raw IEEE 754 bits via uint32 to avoid
// text-roundtrip precision loss and platform endianness ambiguity.
float floatFromBits(uint32_t value) {
    float result = 0.0f;
    memcpy(&result, &value, sizeof(result));
    return result;
}

uint32_t floatToBits(float value) {
    uint32_t result = 0;
    memcpy(&result, &value, sizeof(result));
    return result;
}

// Addressed Servo Output row keys. "so_cnt" holds the row count; each row gets
// "soNN", two digits so an NVS dump reads in row order and four characters
// clear of the 15-character Preferences key ceiling.
constexpr char SERVO_OUTPUT_COUNT_KEY[] = "so_cnt";

// Droid Build keys (ADR 0047). Five records, each twelve characters and so
// three clear of the 15-character Preferences ceiling, named so an NVS dump
// reads as the answer it is: which design each half was built from, at which
// variant, and which Parts are on the droid.
constexpr char DROID_BUILD_DOME_DESIGN_KEY[] = "dbuild_domed";
constexpr char DROID_BUILD_DOME_VARIANT_KEY[] = "dbuild_domev";
constexpr char DROID_BUILD_BODY_DESIGN_KEY[] = "dbuild_bodyd";
constexpr char DROID_BUILD_BODY_VARIANT_KEY[] = "dbuild_bodyv";
constexpr char DROID_BUILD_FITTED_KEY[] = "dbuild_parts";

// Guided Setup keys (#351). Two records: where the run stands, and which of its
// steps have been on screen. "gsetup_visited" is fourteen characters and so one
// clear of the 15-character Preferences ceiling; both read in an NVS dump as the
// answers they are.
constexpr char GUIDED_SETUP_RUN_KEY[] = "gsetup_run";
constexpr char GUIDED_SETUP_VISITED_KEY[] = "gsetup_visited";
// Whether the builder pressed Done on the ended run's summary (#371). Absent
// reads as not done, so a controller that ended its run before this key existed
// shows the summary once.
constexpr char GUIDED_SETUP_SUMMARY_DONE_KEY[] = "gsetup_done";
static_assert(sizeof(GUIDED_SETUP_SUMMARY_DONE_KEY) - 1 <= 15,
              "an NVS key longer than 15 characters is refused by Preferences");
static_assert(sizeof(GUIDED_SETUP_RUN_KEY) - 1 <= 15,
              "an NVS key longer than 15 characters is refused by Preferences");
static_assert(sizeof(GUIDED_SETUP_VISITED_KEY) - 1 <= 15,
              "an NVS key longer than 15 characters is refused by Preferences");

// -----------------------------------------------------------------------------
// readDroidDesignChoice()
// One half of a stored Droid Build, or the default when what is stored is not
// an answer this image's catalog can name. Returns true when it had to repair.
//
// Both fields move together: a design and the variant of that design are one
// answer, and keeping a stored variant beside a defaulted design would produce
// a pairing neither the builder nor the catalog ever stated.
// -----------------------------------------------------------------------------
bool readDroidDesignChoice(const ConfigReader& r, const char* designKey,
                           const char* variantKey, DroidDesignChoice* out) {
    const String design = r.readStr(designKey, out->design);
    const String variant = r.readStr(variantKey, out->variant);
    DroidDesignChoice stored = {};
    if (!droidDesignChoiceSet(&stored, design.c_str(), variant.c_str()) ||
        !droidDesignChoiceIsKnown(stored)) {
        return true;  // *out is left holding the default it arrived with
    }
    *out = stored;
    return false;
}

void servoOutputRowKey(uint8_t index, char* buf, size_t bufSize) {
    snprintf(buf, bufSize, "so%02u", (unsigned)index);
}

// -----------------------------------------------------------------------------
// adoptLegacyFixedServoKeys()
// A builder's calibration, read once off the keys the five fixed field sets
// left behind (#286, #345, ADR 0041).
//
// The fields are gone; the stored keys are not, on any controller that has not
// saved a row yet, and dropping a builder's calibration on the floor is the one
// thing #286 refuses. So this reads them and nothing writes them. Which Output
// Address each set was about is include/servo_legacy_field_sets.h's to say --
// the names carry it in their spelling and nowhere else.
//
// The row's own values are the read fallback, deliberately. A key that is not
// there leaves the row exactly as it stood, so a fresh controller adopts
// nothing and reports nothing, without that resting on two default tables
// happening to agree.
//
// Returns the repair mask (0 when no set is addressed to this row, which is
// what an expander's row gets -- untouched, and reported as nothing).
// -----------------------------------------------------------------------------
uint16_t adoptLegacyFixedServoKeys(const ConfigReader& r, ServoOutputRow* row) {
    if (row == nullptr || row->driver != SERVO_DRIVER_LEDC) {
        return 0;
    }
    for (size_t i = 0; i < SERVO_LEGACY_FIELD_SET_COUNT; ++i) {
        const ServoLegacyFieldSet& set = SERVO_LEGACY_FIELD_SETS[i];
        if (row->channel != set.channel) {
            continue;
        }
        const uint16_t openUs = r.readU16(set.nvsOpenKey, row->open_us);
        const uint16_t closeUs = r.readU16(set.nvsCloseKey, row->close_us);
        const ServoComponentType component =
            (ServoComponentType)r.readU8(set.nvsTypeKey, (uint8_t)row->component);
        return servoOutputAdoptFixedPair(row, openUs, closeUs, component);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// adoptRetiredAuxLedKeys()
// The one lit wire a controller stored before #413, read onto the row it was
// always about.
//
// Before ADR 0067 a droid had exactly one body light: `aux_led_pin` named which
// of the light-capable Outputs carried it -- 1, 2 or 3, counting those Outputs
// in include/board_outputs.h's own order -- and `aux_led_count` said how many
// LEDs were on it. Both are now the row's: a wire carries a Light Type when its
// `component` names one, and its LEDs are that row's `led_count`.
//
// ONLY ONTO A ROW WITH NO ANSWER OF ITS OWN. `unanswered` has bit i set for a
// row whose record is absent or has the thirteen-field shape stored before
// #413; neither can hold a Light Type answer, so the keys are the only answer
// there is. A fourteen-field record was written by this firmware and already
// carries whatever the builder set -- an LED count, or a servo put back on
// the wire -- and the keys can still be in NVS beside it: they are removed
// only by a save that landed whole, so one failed row write keeps them. Adopting
// over that record would undo the builder's answer on every boot until a good
// save made the undo permanent, and a servo put back would be dead, since LEDC
// stays off a pin whose row names a Light Type (#417). A gate on "no record"
// alone would be too narrow the other way: epic-lineage controllers stored
// thirteen-field rows beside `aux_led_pin` before #413, and would lose the
// strip.
//
// The routed wire wins over the stored type, which is the rule the browser used
// to apply on the way in (data/output_settings.js before #413): a controller
// that was really lighting that wire had a light on it whatever its type field
// said, and reading it as a servo would put a PWM signal on a strip.
//
// Returns the row index it adopted onto, or SERVO_OUTPUT_ROW_MAX for a
// controller with nothing to adopt. *litOutput is the Output's index in
// BOARD_OUTPUTS, set only on an adoption.
// -----------------------------------------------------------------------------
uint8_t adoptRetiredAuxLedKeys(const ConfigReader& r, ServoOutputTable* table,
                               uint32_t unanswered, uint8_t* litOutput) {
    if (table == nullptr || litOutput == nullptr) {
        return SERVO_OUTPUT_ROW_MAX;
    }
    const uint8_t slot = r.readU8(NVS_KEY_RETIRED_AUX_LED_PIN, 0);
    if (slot == 0) {
        return SERVO_OUTPUT_ROW_MAX;  // disabled, or a key that is not there
    }

    uint8_t seen = 0;
    size_t lit = BOARD_OUTPUT_COUNT;
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (!BOARD_OUTPUTS[i].lightCapable) {
            continue;
        }
        if (++seen == slot) {
            lit = i;
            break;
        }
    }
    if (lit >= BOARD_OUTPUT_COUNT) {
        return SERVO_OUTPUT_ROW_MAX;  // a slot number this board never had
    }

    const uint8_t index =
        servoOutputTableFindByAddress(*table, SERVO_DRIVER_LEDC, BOARD_OUTPUTS[lit].channel);
    if (index >= SERVO_OUTPUT_ROW_MAX || (unanswered & ((uint32_t)1u << index)) == 0) {
        return SERVO_OUTPUT_ROW_MAX;
    }

    ServoOutputRow* row = &table->rows[index];
    row->component = SERVO_COMP_RGB;
    row->led_count = r.readU8(NVS_KEY_RETIRED_AUX_LED_COUNT, SERVO_LIGHT_LEDS_DEFAULT);
    *litOutput = (uint8_t)lit;
    return index;
}

// -----------------------------------------------------------------------------
// findLegacyNarrowing()
// Which rows still stand exactly where the band put `main`'s pair, and what
// that pair was (#417, include/servo_legacy_field_sets.h ServoLegacyNarrowing).
//
// One rule answers both boots that matter. On the first, the row was just
// adopted from the keys. On a later one the row is stored, because a save
// wrote it, but the keys were kept for it -- configSaveServoOutputs() keeps a
// narrowed set's keys until the builder saves that Output. Either way the
// question is the same: adopting the keys onto this row again changes nothing
// on it, and the band moved an end on the way. A row the builder has since
// saved differently fails the first half; keys the band did not move fail the
// second, and are removed by the next save like any other.
//
// Both keys must be there. `main` wrote the pair together and clamped each to
// 500..2500, so 0 reads as absent rather than as a width. And a row carrying a
// Light Type is skipped: a light is driven by no pulse width, so there is no
// number of the builder's on it to lose. That is the wire `main` lit, whose
// set can hold anything - `main` stored a type of `rgb` there, which takes the
// same band as a servo.
// -----------------------------------------------------------------------------
void findLegacyNarrowing(const ConfigReader& r, const ServoOutputTable& table,
                         ServoLegacyNarrowing* out) {
    *out = {};
    for (size_t i = 0; i < SERVO_LEGACY_FIELD_SET_COUNT; ++i) {
        const ServoLegacyFieldSet& set = SERVO_LEGACY_FIELD_SETS[i];
        const uint16_t openUs = r.readU16(set.nvsOpenKey, 0);
        const uint16_t closeUs = r.readU16(set.nvsCloseKey, 0);
        if (openUs == 0 || closeUs == 0) {
            continue;
        }
        const uint8_t index = servoOutputTableFindByAddress(table, SERVO_DRIVER_LEDC, set.channel);
        if (index >= SERVO_OUTPUT_ROW_MAX) {
            continue;
        }
        const ServoOutputRow& row = table.rows[index];
        if (row.component == SERVO_COMP_RGB) {
            continue;
        }
        ServoOutputRow again = row;
        const uint16_t moved = adoptLegacyFixedServoKeys(r, &again);
        const bool bandMovedAnEnd = (moved & (SERVO_FIELD_OPEN | SERVO_FIELD_CLOSE)) != 0;
        const bool stillTheAdoption = again.open_us == row.open_us &&
                                      again.close_us == row.close_us &&
                                      again.component == row.component;
        if (bandMovedAnEnd && stillTheAdoption) {
            out->sets |= (uint8_t)(1u << i);
            out->openUs[i] = openUs;
            out->closeUs[i] = closeUs;
        }
    }
}

// Forward declarations of deserialize/serialize helpers
void deserializeDrive(const ConfigReader& r, DriveConfig* out, const DriveConfig& def);
void deserializeAudio(const ConfigReader& r, AudioConfig* out, const AudioConfig& def);
void deserializeDome(const ConfigReader& r, DomeConfig* out, const DomeConfig& def);
void deserializeSystem(const ConfigReader& r, SystemConfig* out, const SystemConfig& def);
void deserializeWifi(const ConfigReader& r, WifiConfig* out, const WifiConfig& def);

void deserializeDrive(const ConfigReader& r, DriveConfig* out, const DriveConfig& def) {
    *out = def;
    // Every Setting read under its key and held to what its door takes - the
    // speeds to the project's absolute drive cap (SPEED_LIMIT_MAX), which no
    // drive backend can raise, and the timeouts to their windows - so a corrupt
    // NVS value never reaches DriveTask.
    configSettingsRead(SettingSection::Drive, r, out);
    // Which preset is active is derived from the speed limit, not a Setting.
    out->speedPresetActive =
        normalizeSpeedPresetId(r.readU8("spd_pre_a", (uint8_t)def.speedPresetActive));
}

void deserializeAudio(const ConfigReader& r, AudioConfig* out, const AudioConfig& def) {
    *out = def;
    // Every audio Setting under its key. The volume is held to the DFPlayer
    // Mini's 0..30 and a mood mask to its twelve bits (its upper nibble once
    // carried category flags); a track, an interval and a category bound are
    // read as stored, since a track can hold a CHIRP catalog index past 999
    // (repairOnLoad, include/config_settings.h).
    configSettingsRead(SettingSection::Audio, r, out);
    // Not a Setting: nothing writes it after its default (config_settings.h).
    out->snd_happy = r.readU16("snd_happy", def.snd_happy);
}

void deserializeDome(const ConfigReader& r, DomeConfig* out, const DomeConfig& def) {
    *out = def;
    out->dome_min_speed = floatFromBits(r.readU32("dome_min", floatToBits(def.dome_min_speed)));
    out->dome_max_speed = floatFromBits(r.readU32("dome_max", floatToBits(def.dome_max_speed)));
    // Every Setting, each held to what its door takes: the three ESC pulse
    // widths to 1000..2000 on their own, the order between them below.
    configSettingsRead(SettingSection::Dome, r, out);
    // A set stored out of order - before the config door refused one (#417) -
    // cannot put a stop on the ESC, so all three take the defaults rather than
    // one being picked to move: which of them is wrong is not something the
    // stored numbers can say. configLoad() warns (configDomePulsesStoredOutOfOrder()).
    if (!domePulsesInOrder(out->dome_min_pulse_us, out->dome_neutral_us,
                           out->dome_max_pulse_us)) {
        out->dome_neutral_us = def.dome_neutral_us;
        out->dome_min_pulse_us = def.dome_min_pulse_us;
        out->dome_max_pulse_us = def.dome_max_pulse_us;
    }

    if (out->dome_min_speed < 0.0f)
        out->dome_min_speed = 0.0f;
    if (out->dome_max_speed > 1.0f)
        out->dome_max_speed = 1.0f;
}

// Parse a stored RC analog binding. Starts from def so fields absent from the encoded
// string keep their default values. Falls back to def if the string is missing, parse
// fails, or validation rejects the result (e.g. unknown source enum).
RcBindingConfig loadRcBinding(const ConfigReader& r, const char* key, RcBindingConfig def) {
    String str = r.readStr(key, "");
    if (str.length() > 0) {
        RcBindingConfig parsed = def;
        parseRcBindingConfig(str.c_str(), &parsed);
        if (rcBindingIsValid(parsed)) return parsed;
    }
    return def;
}

// Same pattern for RC trigger bindings.
RcTriggerBinding loadRcTrigger(const ConfigReader& r, const char* key, RcTriggerBinding def) {
    String str = r.readStr(key, "");
    if (str.length() > 0) {
        RcTriggerBinding parsed = def;
        parseRcTriggerBinding(str.c_str(), &parsed);
        if (rcTriggerBindingIsValid(parsed)) return parsed;
    }
    return def;
}

void deserializeSystem(const ConfigReader& r, SystemConfig* out, const SystemConfig& def) {
    *out = def;

    {
        // Block scope: destroys droidName String before the larger field block below,
        // keeping the peak frame smaller. normalizeDroidName rejects uppercase  --  fall
        // back to DROID_NAME_DEFAULT if stored name is invalid.
        String droidName = r.readStr("droid_name", DROID_NAME_DEFAULT);
        char normalizedName[DROID_NAME_MAX_LEN + 1] = {};
        if (!normalizeDroidName(droidName.c_str(), normalizedName, sizeof(normalizedName))) {
            snprintf(normalizedName, sizeof(normalizedName), "%s", DROID_NAME_DEFAULT);
        }
        snprintf(out->droid_name, sizeof(out->droid_name), "%s", normalizedName);
    }

    out->mdns_use_name        = r.readBool("mdns_use_name",  def.mdns_use_name);
    // Every Setting under its key, held to what its door takes. The Component
    // Members are read as stored: whether this image can still drive the stored
    // product is componentResolveMember()'s question, not the serializer's, so
    // a member cut from one image and restored in the next survives the round
    // trip. An RC receiver mode this image has no word for reads as the
    // default.
    configSettingsRead(SettingSection::System, r, out);

    out->rc_pwm_drive_speed  = loadRcBinding(r, "rcp_drv", def.rc_pwm_drive_speed);
    out->rc_pwm_drive_steer  = loadRcBinding(r, "rcp_str", def.rc_pwm_drive_steer);
    out->rc_pwm_dome_speed   = loadRcBinding(r, "rcp_dom", def.rc_pwm_dome_speed);
    out->rc_pwm_arm1         = loadRcBinding(r, "rcp_a1",  def.rc_pwm_arm1);
    out->rc_pwm_arm2         = loadRcBinding(r, "rcp_a2",  def.rc_pwm_arm2);
    out->rc_pwm_audio        = loadRcBinding(r, "rcp_aud", def.rc_pwm_audio);
    out->rc_sbus_drive_speed = loadRcBinding(r, "rcs_drv", def.rc_sbus_drive_speed);
    out->rc_sbus_drive_steer = loadRcBinding(r, "rcs_str", def.rc_sbus_drive_steer);
    out->rc_sbus_dome_speed  = loadRcBinding(r, "rcs_dom", def.rc_sbus_dome_speed);
    out->rc_sbus_arm1        = loadRcBinding(r, "rcs_a1",  def.rc_sbus_arm1);
    out->rc_sbus_arm2        = loadRcBinding(r, "rcs_a2",  def.rc_sbus_arm2);
    out->rc_sbus_audio       = loadRcBinding(r, "rcs_aud", def.rc_sbus_audio);

    out->rc_arm1   = loadRcTrigger(r, "rc_arm1",  def.rc_arm1);
    out->rc_arm2   = loadRcTrigger(r, "rc_arm2",  def.rc_arm2);
    out->rc_aux1   = loadRcTrigger(r, "rc_aux1",  def.rc_aux1);
    out->rc_aux2   = loadRcTrigger(r, "rc_aux2",  def.rc_aux2);
    out->rc_aux3   = loadRcTrigger(r, "rc_aux3",  def.rc_aux3);
    out->rc_audio  = loadRcTrigger(r, "rc_aud", def.rc_audio);
    out->rc_opmode = loadRcTrigger(r, "rc_opmode",def.rc_opmode);
    out->rc_free0  = loadRcTrigger(r, "rc_free0", def.rc_free0);
    out->rc_free1  = loadRcTrigger(r, "rc_free1", def.rc_free1);
    out->rc_free2  = loadRcTrigger(r, "rc_free2", def.rc_free2);
    out->rc_free3  = loadRcTrigger(r, "rc_free3", def.rc_free3);
}

void deserializeWifi(const ConfigReader& r, WifiConfig* out, const WifiConfig& def) {
    *out = def;
    out->provisioned = r.readBool("wifi_prov", def.provisioned);

    uint8_t modeRaw = r.readU8("wifi_mode", (uint8_t)def.mode);
    out->mode = (modeRaw <= (uint8_t)WifiMode::STANDALONE_AP) ? (WifiMode)modeRaw : def.mode;

    // Reject overlong/invalid stored strings before copying into fixed-size buffers.
    String staSsid = r.readStr("wifi_sta_ssid", def.sta_ssid);
    if (staSsid.length() > WIFI_SSID_MAX_LEN) {
        staSsid = String(def.sta_ssid);
    }
    snprintf(out->sta_ssid, sizeof(out->sta_ssid), "%s", staSsid.c_str());

    String staPassword = r.readStr("wifi_sta_pw", def.sta_password);
    if (staPassword.length() > WIFI_PASSWORD_MAX_LEN) {
        staPassword = String(def.sta_password);
    }
    snprintf(out->sta_password, sizeof(out->sta_password), "%s", staPassword.c_str());

    // AP SSID must stay non-empty  --  an empty SSID would make Standalone AP Mode unusable.
    String apSsid = r.readStr("wifi_ap_ssid", def.ap_ssid);
    if (apSsid.length() == 0 || apSsid.length() > WIFI_SSID_MAX_LEN) {
        apSsid = String(def.ap_ssid);
    }
    snprintf(out->ap_ssid, sizeof(out->ap_ssid), "%s", apSsid.c_str());

    String apPassword = r.readStr("wifi_ap_pw", def.ap_password);
    if (apPassword.length() > WIFI_PASSWORD_MAX_LEN) {
        apPassword = String(def.ap_password);
    }
    snprintf(out->ap_password, sizeof(out->ap_password), "%s", apPassword.c_str());
}

}  // namespace

// =============================================================================
// Shared defaults  --  ConfigSnapshot is 916 bytes (static_assert in
// config_store.h), more than a stack local should cost the loopTask that runs
// setup(), whose stack is sized against a measured chain (platformio.ini).
// Static BSS allocation; populated once on first use.
// =============================================================================

static ConfigSnapshot s_defaults;
static bool s_defaults_initialised = false;

static const ConfigSnapshot& getDefaults() {
    if (!s_defaults_initialised) {
        configSnapshotDefaults(&s_defaults);
        s_defaults_initialised = true;
    }
    return s_defaults;
}

// =============================================================================
// Public API: Pure serializer functions
// =============================================================================

bool configDeserialize(const ConfigReader& reader, ConfigSnapshot* out) {
    if (out == nullptr) {
        return false;
    }
    const ConfigSnapshot& defaults = getDefaults();
    deserializeDrive(reader, &out->drive, defaults.drive);
    deserializeAudio(reader, &out->audio, defaults.audio);
    deserializeDome(reader, &out->dome, defaults.dome);
    deserializeSystem(reader, &out->system, defaults.system);
    deserializeWifi(reader, &out->wifi, defaults.wifi);
    return true;
}

bool configSerialize(const ConfigSnapshot& snap, ConfigWriter& writer) {
    bool ok = true;
    ok = configSerializeDrive(snap.drive, writer) && ok;
    ok = configSerializeAudio(snap.audio, writer) && ok;
    ok = configSerializeDome(snap.dome, writer) && ok;
    ok = configSerializeSystem(snap.system, writer) && ok;
    ok = configSerializeWifi(snap.wifi, writer) && ok;
    ok = writer.writeSchemaVersion(CONFIG_SCHEMA_VERSION) && ok;
    return ok;
}

bool configSerializeDrive(const DriveConfig& cfg, ConfigWriter& w) {
    bool ok = configSettingsWrite(SettingSection::Drive, &cfg, w);
    ok = w.writeU8("spd_pre_a", (uint8_t)cfg.speedPresetActive) && ok;
    return ok;
}

bool configSerializeAudio(const AudioConfig& cfg, ConfigWriter& w) {
    // snd_happy is read and never written, as it has always been: it is not a
    // Setting, and a write would add a key no controller stores today.
    return configSettingsWrite(SettingSection::Audio, &cfg, w);
}

bool configSerializeDome(const DomeConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeU32("dome_min", floatToBits(cfg.dome_min_speed)) && ok;
    ok = w.writeU32("dome_max", floatToBits(cfg.dome_max_speed)) && ok;
    ok = configSettingsWrite(SettingSection::Dome, &cfg, w) && ok;
    return ok;
}

bool configSerializeSystem(const SystemConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeStr("droid_name", cfg.droid_name) && ok;
    ok = w.writeBool("mdns_use_name", cfg.mdns_use_name) && ok;
    ok = configSettingsWrite(SettingSection::System, &cfg, w) && ok;

    // RC bindings  --  format and write as strings
    char encoded[48] = {};
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_drive_speed)) {
        ok = w.writeStr("rcp_drv", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_drive_steer)) {
        ok = w.writeStr("rcp_str", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_dome_speed)) {
        ok = w.writeStr("rcp_dom", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_arm1)) {
        ok = w.writeStr("rcp_a1", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_arm2)) {
        ok = w.writeStr("rcp_a2", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_audio)) {
        ok = w.writeStr("rcp_aud", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_drive_speed)) {
        ok = w.writeStr("rcs_drv", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_drive_steer)) {
        ok = w.writeStr("rcs_str", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_dome_speed)) {
        ok = w.writeStr("rcs_dom", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_arm1)) {
        ok = w.writeStr("rcs_a1", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_arm2)) {
        ok = w.writeStr("rcs_a2", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_audio)) {
        ok = w.writeStr("rcs_aud", encoded) && ok;
    }

    // RC trigger bindings
    char triggerEncoded[64] = {};
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_arm1)) {
        ok = w.writeStr("rc_arm1", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_arm2)) {
        ok = w.writeStr("rc_arm2", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_aux1)) {
        ok = w.writeStr("rc_aux1", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_aux2)) {
        ok = w.writeStr("rc_aux2", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_aux3)) {
        ok = w.writeStr("rc_aux3", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_audio)) {
        ok = w.writeStr("rc_aud", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_opmode)) {
        ok = w.writeStr("rc_opmode", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free0)) {
        ok = w.writeStr("rc_free0", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free1)) {
        ok = w.writeStr("rc_free1", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free2)) {
        ok = w.writeStr("rc_free2", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free3)) {
        ok = w.writeStr("rc_free3", triggerEncoded) && ok;
    }

    return ok;
}

bool configSerializeWifi(const WifiConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeBool("wifi_prov", cfg.provisioned) && ok;
    ok = w.writeU8("wifi_mode", (uint8_t)cfg.mode) && ok;
    ok = w.writeStr("wifi_sta_ssid", cfg.sta_ssid) && ok;
    ok = w.writeStr("wifi_sta_pw", cfg.sta_password) && ok;
    ok = w.writeStr("wifi_ap_ssid", cfg.ap_ssid) && ok;
    ok = w.writeStr("wifi_ap_pw", cfg.ap_password) && ok;
    return ok;
}

// =============================================================================
// Domain-level deserializers  --  each loads only its own domain keys
// =============================================================================

void configDeserializeDrive(const ConfigReader& r, DriveConfig* out) {
    deserializeDrive(r, out, getDefaults().drive);
}

void configDeserializeAudio(const ConfigReader& r, AudioConfig* out) {
    deserializeAudio(r, out, getDefaults().audio);
}

void configDeserializeDome(const ConfigReader& r, DomeConfig* out) {
    deserializeDome(r, out, getDefaults().dome);
}

bool configDomePulsesStoredOutOfOrder(const ConfigReader& r) {
    DomeConfig stored = getDefaults().dome;
    configSettingsRead(SettingSection::Dome, r, &stored);
    return !domePulsesInOrder(stored.dome_min_pulse_us, stored.dome_neutral_us,
                              stored.dome_max_pulse_us);
}

void configDeserializeSystem(const ConfigReader& r, SystemConfig* out) {
    deserializeSystem(r, out, getDefaults().system);
}

void configDeserializeWifi(const ConfigReader& r, WifiConfig* out) {
    deserializeWifi(r, out, getDefaults().wifi);
}

// =============================================================================
// Addressed Servo Output rows  --  see include/config_serializer.h
// =============================================================================

bool configSerializeServoOutputCount(uint8_t count, ConfigWriter& w) {
    return w.writeU8(SERVO_OUTPUT_COUNT_KEY, count);
}

bool configSerializeServoOutputRow(uint8_t index, const ServoOutputRow& row, ConfigWriter& w) {
    if (index >= SERVO_OUTPUT_ROW_MAX) {
        return false;
    }
    char key[8] = {};
    servoOutputRowKey(index, key, sizeof(key));
    char encoded[SERVO_OUTPUT_ROW_STR_MAX + 1] = {};
    if (!servoOutputRowFormat(encoded, sizeof(encoded), row)) {
        return false;
    }
    return w.writeStr(key, encoded);
}

void configDeserializeServoOutputs(const ConfigReader& r, ServoOutputTable* out,
                                   ServoOutputRepairReport* report,
                                   ServoLegacyNarrowing* narrowing) {
    if (out == nullptr) {
        return;
    }
    servoOutputTableDefaults(out);

    ServoOutputRepairReport local = {};

    const uint8_t storedCount = r.readU8(SERVO_OUTPUT_COUNT_KEY, out->count);
    if (storedCount > SERVO_OUTPUT_ROW_MAX) {
        local.countRepaired = true;  // keep the default count rather than the stored one
    } else {
        out->count = storedCount;
    }

    // Per-row masks are collected first because one rule cannot be decided a row
    // at a time: "a Part is driven by at most one Output" is a fact about the
    // whole table, so it runs once every row has been read.
    uint16_t rowMask[SERVO_OUTPUT_ROW_MAX] = {};
    // Rows no record answers for the Light Type: absent, or stored before #413.
    // See adoptRetiredAuxLedKeys().
    uint32_t unanswered = 0;

    // The bridge, crossed on first read (#286): a controller upgrading from
    // before ADR 0041 has five fixed key sets in NVS and no row records at all,
    // so a row nothing has written adopts whatever the set addressed to its
    // channel still holds. A stored row wins over it, because once a row exists
    // the row IS the output -- which is also what makes this idempotent and
    // marker-free: the bridge stops mattering for a row the moment that row is
    // saved, and configSaveServoOutputs() then removes the keys.
    //
    // Read through the same ConfigReader as everything else, so what crosses is
    // what is actually stored rather than what some caller happens to hold.
    for (uint8_t i = 0; i < out->count; ++i) {
        char key[8] = {};
        servoOutputRowKey(i, key, sizeof(key));
        const String stored = r.readStr(key, "");
        const ServoOutputRow fallback = out->rows[i];
        ServoOutputRow parsed = fallback;
        // An absent record is a device that has never written this row, not a
        // damaged one, so what it gets is the old form rather than a complaint.
        // A repair is still counted: the only thing an adoption can report is a
        // pulse width the component band had to move, and a builder's own number
        // changing under them is exactly what this project says out loud.
        bool oldShape = false;
        if (stored.length() == 0) {
            rowMask[i] = adoptLegacyFixedServoKeys(r, &parsed);
            unanswered |= (uint32_t)1u << i;
        } else {
            rowMask[i] = servoOutputRowParse(stored.c_str(), fallback, &parsed, &oldShape);
            if (oldShape) {
                unanswered |= (uint32_t)1u << i;
            }
        }
        out->rows[i] = parsed;
    }

    // The one lit wire a pre-#413 controller stored, onto the row it named. It
    // runs after the rows are read so it lands on the row as stored, and its
    // repair is reported like any other: normalising it can move a pulse width
    // into the band SERVO_COMP_RGB takes.
    uint8_t litOutput = 0;
    const uint8_t adopted = adoptRetiredAuxLedKeys(r, out, unanswered, &litOutput);
    if (adopted < SERVO_OUTPUT_ROW_MAX) {
        const ServoOutputRow before = out->rows[adopted];
        rowMask[adopted] |= servoOutputRowNormalise(&out->rows[adopted], before);
        local.litAdopted = true;
        local.litOutput = litOutput;
    }

    // After the light adoption, so the wire `main` lit is already a light and
    // findLegacyNarrowing() passes over it.
    if (narrowing != nullptr) {
        findLegacyNarrowing(r, *out, narrowing);
    }

    const uint32_t contested = servoOutputTableEnforcePartOwnership(out);
    for (uint8_t i = 0; i < out->count; ++i) {
        if ((contested & ((uint32_t)1u << i)) != 0) {
            rowMask[i] |= SERVO_FIELD_PARTS;
        }
    }

    for (uint8_t i = 0; i < out->count; ++i) {
        if (rowMask[i] == 0) {
            continue;
        }
        if (local.rowsRepaired == 0) {
            local.firstRow = i;
            local.firstRowMask = rowMask[i];
        }
        local.rowsRepaired++;
        for (uint8_t bit = 0; bit < SERVO_OUTPUT_FIELD_COUNT; ++bit) {
            if ((rowMask[i] & (uint16_t)(1u << bit)) != 0) {
                local.fieldsRepaired++;
            }
        }
    }

    if (report != nullptr) {
        *report = local;
    }
}

// -----------------------------------------------------------------------------
// configSerializeDroidBuild()
// The Droid Build, written as the answer a builder gave.
//
// The Fitted Parts are joined on the heap rather than in a local char buffer on
// purpose: the joined list is DROID_FITTED_PARTS_STR_MAX bytes, and a frame
// that size would sit on the serial config-write path, whose task stack chain
// is a measured constant that one ConfigSnapshot-sized frame per nesting level
// already nearly overran once (include/config.h, include/config_store.h, #226).
// One bounded allocation on a Core 0 write path costs that chain nothing, and a
// failed one is reported rather than swallowed - the caller answers "not
// persisted" and the writer has touched nothing.
// -----------------------------------------------------------------------------
bool configSerializeDroidBuild(const DroidBuildConfig& cfg, ConfigWriter& w) {
    // The one step that can fail for a reason other than storage goes first, so
    // a controller that could not allocate has not had half an answer written
    // over the one it already held.
    char* fitted = (char*)malloc(DROID_FITTED_PARTS_STR_MAX + 1);
    if (fitted == nullptr) {
        return false;  // the caller reports a failed persist; nothing was written
    }
    size_t used = 0;
    for (size_t i = droidFittedPartsNextIndex(cfg.fitted, 0); i < DROID_PART_COUNT;
         i = droidFittedPartsNextIndex(cfg.fitted, i + 1)) {
        const int n = snprintf(fitted + used, DROID_FITTED_PARTS_STR_MAX + 1 - used, "%s%s",
                               used == 0 ? "" : ",", droidPartIdAt(i));
        if (n <= 0 || (size_t)n >= DROID_FITTED_PARTS_STR_MAX + 1 - used) {
            free(fitted);
            return false;  // the bound above is arithmetic, so this is unreachable
        }
        used += (size_t)n;
    }
    // A droid with nothing fitted is an answer, and writing it as an empty
    // string would read back as a controller nobody has answered yet.
    if (used == 0) {
        snprintf(fitted, DROID_FITTED_PARTS_STR_MAX + 1, "%s", DROID_FITTED_PARTS_NONE);
    }
    bool ok = w.writeStr(DROID_BUILD_DOME_DESIGN_KEY, cfg.dome.design);
    ok = w.writeStr(DROID_BUILD_DOME_VARIANT_KEY, cfg.dome.variant) && ok;
    ok = w.writeStr(DROID_BUILD_BODY_DESIGN_KEY, cfg.body.design) && ok;
    ok = w.writeStr(DROID_BUILD_BODY_VARIANT_KEY, cfg.body.variant) && ok;
    ok = w.writeStr(DROID_BUILD_FITTED_KEY, fitted) && ok;
    free(fitted);
    return ok;
}

void configDeserializeDroidBuild(const ConfigReader& r, DroidBuildConfig* out,
                                 DroidBuildRepairReport* report) {
    if (out == nullptr) {
        return;
    }
    droidBuildDefaults(out);

    DroidBuildRepairReport local = {};
    local.domeRepaired = readDroidDesignChoice(r, DROID_BUILD_DOME_DESIGN_KEY,
                                               DROID_BUILD_DOME_VARIANT_KEY, &out->dome);
    local.bodyRepaired = readDroidDesignChoice(r, DROID_BUILD_BODY_DESIGN_KEY,
                                               DROID_BUILD_BODY_VARIANT_KEY, &out->body);

    // Three states, and the middle one is the whole reason this record is not
    // read as a plain string: absent means nobody has answered, so the
    // pre-selected design's complement stands; "-" means a builder said their
    // droid carries nothing yet, which is theirs to say and is kept.
    const String fitted = r.readStr(DROID_BUILD_FITTED_KEY, "");
    if (fitted.length() > 0) {
        if (strcmp(fitted.c_str(), DROID_FITTED_PARTS_NONE) == 0) {
            droidFittedPartsClear(&out->fitted);
        } else {
            local.partsDropped = droidFittedPartsParse(fitted.c_str(), &out->fitted);
        }
    }

    if (report != nullptr) {
        *report = local;
    }
}

// -----------------------------------------------------------------------------
// configSerializeGuidedSetup()
// Where the guided run stands, and which of its steps the builder has been
// shown.
//
// The visited list is written even when it is empty, as the sentinel: NVS keeps
// every key a save does not touch, and "nothing visited" written as an empty
// string would read back on the next cold boot as a controller guided Setup has
// never drawn on - which is the one thing this record has to be able to tell
// apart (include/guided_setup.h).
//
// No heap here, unlike the Droid Build above: this list is bounded at
// GUIDED_SETUP_VISITED_STR_MAX and is already held as the joined string, so
// there is nothing to build and nothing to free.
// -----------------------------------------------------------------------------
bool configSerializeGuidedSetup(const GuidedSetupConfig& cfg, ConfigWriter& w) {
    bool ok = w.writeU8(GUIDED_SETUP_RUN_KEY, (uint8_t)cfg.run);
    ok = w.writeStr(GUIDED_SETUP_VISITED_KEY, guidedSetupVisitedStored(cfg)) && ok;
    ok = w.writeBool(GUIDED_SETUP_SUMMARY_DONE_KEY, cfg.summaryDone) && ok;
    return ok;
}

void configDeserializeGuidedSetup(const ConfigReader& r, GuidedSetupConfig* out,
                                  GuidedSetupRepairReport* report) {
    if (out == nullptr) {
        return;
    }
    guidedSetupDefaults(out);

    GuidedSetupRepairReport local = {};

    const uint8_t storedRun = r.readU8(GUIDED_SETUP_RUN_KEY, (uint8_t)GUIDED_SETUP_NOT_RUN);
    out->run = guidedSetupRunFromStored(storedRun);
    local.runRepaired = ((uint8_t)out->run != storedRun);
    out->summaryDone = r.readBool(GUIDED_SETUP_SUMMARY_DONE_KEY, false);

    // Absent, sentinel, or a list - and the first of those is the one that
    // carries a fact nothing else can: guided Setup has never been drawn on this
    // controller. The writer never stores an empty string, so an empty read is
    // unambiguously "no record".
    const String visited = r.readStr(GUIDED_SETUP_VISITED_KEY, "");
    if (visited.length() > 0) {
        out->recorded = true;
        local.stepsDropped = guidedSetupVisitedSet(out, visited.c_str());
    }

    if (report != nullptr) {
        *report = local;
    }
}
