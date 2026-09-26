// =============================================================================
// test/test_native/test_config_settings/test_config_settings.cpp
//
// Each Setting, declared once (include/config_settings.h; ADR 0068, amended
// 2026-09-26). What only the declarations can promise, asked of every one of
// them rather than a hand list:
//
//   - a non-default value of every Setting saved through the store loads back
//     equal, under the NVS key a controller already has it under - a stored
//     Configuration must load as it is across this change and every later one;
//   - every Setting refuses a value it does not take with its field, reason and
//     accepts, so no door can take what another refuses;
//   - the four ranges the 2026-09-26 defect pass found drifted between two
//     tables hold at both edges, at the one place they now live.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstring>

#include <Preferences.h>

#include "config_settings.h"
#include "config_store.h"
#include "dome_math.h"
#include "../../../test/stubs/config/setting_samples.h"

void setUp() {
    // The NVS double keeps its keys for the whole binary, as flash does; every
    // test here starts from an erased partition.
    Preferences::eraseFlash();
}

void tearDown() {}

namespace {

// The NVS key each Setting was stored under before its declaration existed, and
// the type it was stored as (src/config_serializer.cpp at 464cd119). A key string that changed would
// strand every stored value on a controller in the field, so this is the one
// place a key is written twice: as the pin that says it did not move.
struct KeyPin {
    const char* form;
    const char* key;
    // The type it is stored as: taken from the member, so widening a struct
    // field would silently change it and every droid would fall back to its
    // defaults (a Preferences read of the wrong type finds nothing).
    SettingStorage storage;
};

const KeyPin kStoredKeys[] = {
    {"speedLimitMax", "spd_max", SettingStorage::I16},       {"speedPresetSlow", "spd_pre_s", SettingStorage::I16},
    {"speedPresetNormal", "spd_pre_n", SettingStorage::I16}, {"speedPresetTurbo", "spd_pre_t", SettingStorage::I16},
    {"webDriveTimeoutMs", "web_tmo", SettingStorage::U32},   {"sbusTimeoutMs", "sbus_tmo", SettingStorage::U32},
    {"stationary", "op_mode", SettingStorage::Bool},          {"rcInputMode", "rc_mode", SettingStorage::U8},
    {"rcMember", "rc_member", SettingStorage::U8},          {"sbusRecvCh2", "sbus_recv_ch2", SettingStorage::Bool},
    {"enableDomeEsc", "en_dome_esc", SettingStorage::Bool},   {"enableRcCh1", "en_rc_ch1", SettingStorage::Bool},
    {"enableRcCh2", "en_rc_ch2", SettingStorage::Bool},       {"enableRcCh3", "en_rc_ch3", SettingStorage::Bool},
    {"enableRcCh4", "en_rc_ch4", SettingStorage::Bool},       {"enableRcCh5", "en_rc_ch5", SettingStorage::Bool},
    {"enableRcCh6", "en_rc_ch6", SettingStorage::Bool},       {"enableDrive", "en_drive", SettingStorage::Bool},
    {"enableAudio", "en_audio", SettingStorage::Bool},        {"soundMember", "snd_member", SettingStorage::U8},
    {"enableProtoR2link", "en_r2link", SettingStorage::Bool}, {"enableArm1", "en_arm1", SettingStorage::Bool},
    {"enableArm2", "en_arm2", SettingStorage::Bool},          {"enableAux1", "en_aux1", SettingStorage::Bool},
    {"enableAux2", "en_aux2", SettingStorage::Bool},          {"enableAux3", "en_aux3", SettingStorage::Bool},
    {"domeEscNeutralUs", "dome_neu", SettingStorage::U16},   {"domeEscMinPulseUs", "dome_minp", SettingStorage::U16},
    {"domeEscMaxPulseUs", "dome_maxp", SettingStorage::U16}, {"domeEscSpeedLimitPct", "dome_pct", SettingStorage::U8},
    {"domeEscRndEnable", "dome_rnd_en", SettingStorage::Bool}, {"domeEscRndSpeedPct", "dome_rnd_spd", SettingStorage::U8},
    {"domeEscRndPauseMin", "dome_rnd_pmin", SettingStorage::U8}, {"domeEscRndPauseMax", "dome_rnd_pmax", SettingStorage::U8},
    {"domeEscRndMoveMs", "dome_rnd_ms", SettingStorage::U16}, {"protoR2linkWifiPeerIp", "dome_wip", SettingStorage::Text},
    {"logLevel", "log_level", SettingStorage::U8},
    // The audio Settings (#431 addendum), by the key their door takes.
    {"volume", "aud_vol", SettingStorage::U8}, {"scream", "snd_scream", SettingStorage::U16}, {"faint", "snd_faint", SettingStorage::U16},
    {"leia", "snd_leia", SettingStorage::U16}, {"cantina_s", "snd_cantina_s", SettingStorage::U16}, {"sw_theme", "snd_sw", SettingStorage::U16},
    {"imp_march", "snd_march", SettingStorage::U16}, {"cantina_l", "snd_cantina_l", SettingStorage::U16}, {"startup", "snd_startup", SettingStorage::U16},
    {"doodoo", "snd_doodoo", SettingStorage::U16}, {"failure", "snd_failure", SettingStorage::U16}, {"disco", "snd_disco", SettingStorage::U16},
    {"mahna", "snd_mahna", SettingStorage::U16}, {"inlove", "snd_inlove", SettingStorage::U16}, {"macho", "snd_macho", SettingStorage::U16},
    {"gangnam", "snd_gangnam", SettingStorage::U16}, {"uptown", "snd_uptown", SettingStorage::U16}, {"celebr", "snd_celebr", SettingStorage::U16},
    {"stayin", "snd_stayin", SettingStorage::U16}, {"harlem", "snd_harlem", SettingStorage::U16}, {"pbjtime", "snd_pbjtime", SettingStorage::U16},
    {"sys_boot", "snd_sys_boot", SettingStorage::U16}, {"sys_mode_n", "snd_sys_mode_n", SettingStorage::U16},
    {"sys_mode_s", "snd_sys_mode_s", SettingStorage::U16}, {"sys_mode_t", "snd_sys_mode_t", SettingStorage::U16},
    {"sys_drv_on", "snd_sys_drv_on", SettingStorage::U16}, {"sys_dome_on", "snd_sys_dome_on", SettingStorage::U16},
    {"sys_net_down", "snd_sys_netdown", SettingStorage::U16}, {"rand_min", "snd_rand_min", SettingStorage::U16},
    {"rand_max", "snd_rand_max", SettingStorage::U16}, {"snd_int_quiet", "snd_int_quiet", SettingStorage::U16},
    {"snd_int_mid", "snd_int_mid", SettingStorage::U16}, {"snd_int_full", "snd_int_full", SettingStorage::U16},
    {"snd_int_awake", "snd_int_awake", SettingStorage::U16}, {"quiet", "snd_moodcat_q", SettingStorage::U16}, {"mid", "snd_moodcat_m", SettingStorage::U16},
    {"full", "snd_moodcat_f", SettingStorage::U16}, {"awakeplus", "snd_moodcat_a", SettingStorage::U16},
    {"snd_cat_gen_lo", "snd_cat_gen_lo", SettingStorage::U16}, {"snd_cat_gen_hi", "snd_cat_gen_hi", SettingStorage::U16},
    {"snd_cat_chat_lo", "snd_cat_chat_lo", SettingStorage::U16}, {"snd_cat_chat_hi", "snd_cat_chat_hi", SettingStorage::U16},
    {"snd_cat_hap_lo", "snd_cat_hap_lo", SettingStorage::U16}, {"snd_cat_hap_hi", "snd_cat_hap_hi", SettingStorage::U16},
    {"snd_cat_proc_lo", "snd_cat_proc_lo", SettingStorage::U16}, {"snd_cat_proc_hi", "snd_cat_proc_hi", SettingStorage::U16},
    {"snd_cat_sad_lo", "snd_cat_sad_lo", SettingStorage::U16}, {"snd_cat_sad_hi", "snd_cat_sad_hi", SettingStorage::U16},
    {"snd_cat_sent_lo", "snd_cat_sent_lo", SettingStorage::U16}, {"snd_cat_sent_hi", "snd_cat_sent_hi", SettingStorage::U16},
    {"snd_cat_hum_lo", "snd_cat_hum_lo", SettingStorage::U16}, {"snd_cat_hum_hi", "snd_cat_hum_hi", SettingStorage::U16},
    {"snd_cat_scrm_lo", "snd_cat_scrm_lo", SettingStorage::U16}, {"snd_cat_scrm_hi", "snd_cat_scrm_hi", SettingStorage::U16},
    {"snd_cat_ooh_lo", "snd_cat_ooh_lo", SettingStorage::U16}, {"snd_cat_ooh_hi", "snd_cat_ooh_hi", SettingStorage::U16},
    {"snd_cat_alrm_lo", "snd_cat_alrm_lo", SettingStorage::U16}, {"snd_cat_alrm_hi", "snd_cat_alrm_hi", SettingStorage::U16},
    {"snd_cat_snrk_lo", "snd_cat_snrk_lo", SettingStorage::U16}, {"snd_cat_snrk_hi", "snd_cat_snrk_hi", SettingStorage::U16},
    {"snd_cat_whis_lo", "snd_cat_whis_lo", SettingStorage::U16}, {"snd_cat_whis_hi", "snd_cat_whis_hi", SettingStorage::U16},
};

// Every declared Setting, the droid's and the audio ones, in one list.
size_t everySettingCount() { return configSettingCount() + audioSettingCount(); }

const ConfigSetting& everySettingAt(size_t index) {
    return index < configSettingCount() ? configSettingAt(index)
                                        : audioSettingAt(index - configSettingCount());
}

const KeyPin* storedPinOf(const char* form) {
    for (const KeyPin& pin : kStoredKeys) {
        if (strcmp(pin.form, form) == 0) {
            return &pin;
        }
    }
    return nullptr;
}

// Every Setting, the droid's and the audio ones, moved off the defaults,
// through its own check. The one rule
// across Settings a store holds to - the dome pulses in order - is put right
// by hand afterwards, the way configApply() judges it beside its loop.
ConfigSnapshot everySettingMovedOffTheDefaults() {
    ConfigSnapshot defaults = {};
    configSnapshotDefaults(&defaults);
    ConfigSnapshot moved = defaults;
    for (size_t i = 0; i < everySettingCount(); ++i) {
        const ConfigSetting& setting = everySettingAt(i);
        char text[24] = {};
        TEST_ASSERT_TRUE_MESSAGE(settingOtherText(setting, defaults, i, text, sizeof(text)),
                                 setting.form);
        ApplyRefusal refusal;
        char sentence[CONFIG_SETTING_SENTENCE_MAX] = {};
        TEST_ASSERT_TRUE_MESSAGE(
            configSettingApply(setting, text, &moved, &refusal, sentence, sizeof(sentence)),
            setting.form);
    }
    DomeConfig& dome = moved.dome;
    uint16_t pulses[3] = {dome.dome_min_pulse_us, dome.dome_neutral_us, dome.dome_max_pulse_us};
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            if (pulses[b] < pulses[a]) {
                const uint16_t t = pulses[a];
                pulses[a] = pulses[b];
                pulses[b] = t;
            }
        }
    }
    dome.dome_min_pulse_us = pulses[0];
    dome.dome_neutral_us = pulses[1];
    dome.dome_max_pulse_us = pulses[2];
    TEST_ASSERT_TRUE(domePulsesInOrder(dome.dome_min_pulse_us, dome.dome_neutral_us,
                                       dome.dome_max_pulse_us));
    return moved;
}

void assertSameSetting(const ConfigSetting& setting, const ConfigSnapshot& want,
                       const ConfigSnapshot& got) {
    char wantText[24] = {};
    char gotText[24] = {};
    configSettingFormat(setting, want, wantText, sizeof(wantText));
    configSettingFormat(setting, got, gotText, sizeof(gotText));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(wantText, gotText, setting.form);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(configSettingNumber(setting, want),
                                    configSettingNumber(setting, got), setting.form);
}

}  // namespace

// A non-default value of every declared Setting survives a save and a load
// through the store, and each is stored under the key a controller already
// holds it under.
void test_every_setting_saved_through_the_store_loads_back_under_its_key() {
    const ConfigSnapshot saved = everySettingMovedOffTheDefaults();

    Preferences prefs;
    prefs.begin("proto", false);
    TEST_ASSERT_TRUE(configSave(prefs, saved));

    for (size_t i = 0; i < everySettingCount(); ++i) {
        const ConfigSetting& setting = everySettingAt(i);
        const KeyPin* pinned = storedPinOf(setting.form);
        TEST_ASSERT_NOT_NULL_MESSAGE(pinned, setting.form);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(pinned->key, setting.nvsKey, setting.form);
        TEST_ASSERT_EQUAL_MESSAGE((int)pinned->storage, (int)setting.storage, setting.form);
        TEST_ASSERT_TRUE_MESSAGE(prefs.isKey(pinned->key), setting.form);
    }

    ConfigSnapshot loaded = {};
    TEST_ASSERT_TRUE(configLoad(prefs, &loaded));
    prefs.end();

    for (size_t i = 0; i < everySettingCount(); ++i) {
        assertSameSetting(everySettingAt(i), saved, loaded);
    }
}

// Every declared Setting refuses a value it does not take - a number past its
// range, a word it has not got, something that is not a yes or a no, a part
// or an address that is not one - with the Setting named, the reason, and
// what it takes, and leaves the Configuration as it was.
void test_every_setting_refuses_a_value_it_does_not_take_with_field_reason_and_accepts() {
    ConfigSnapshot defaults = {};
    configSnapshotDefaults(&defaults);

    for (size_t i = 0; i < everySettingCount(); ++i) {
        const ConfigSetting& setting = everySettingAt(i);
        char bad[24] = {};
        char accepts[APPLY_REFUSAL_ACCEPTS_MAX] = {};
        switch (setting.rule) {
            case SettingRule::Range:
                snprintf(bad, sizeof(bad), "%ld", (long)setting.hi + 1);
                if (setting.words == nullptr) {
                    snprintf(accepts, sizeof(accepts), "%ld..%ld", (long)setting.lo,
                             (long)setting.hi);
                }
                break;
            case SettingRule::Words:
                snprintf(bad, sizeof(bad), "%s", "not_a_word");
                break;
            case SettingRule::Bool:
                snprintf(bad, sizeof(bad), "%s", "maybe");
                snprintf(accepts, sizeof(accepts), "%s", "true,false,1,0");
                break;
            case SettingRule::Member:
                snprintf(bad, sizeof(bad), "%s", "not_a_part");
                break;
            case SettingRule::Ipv4:
                snprintf(bad, sizeof(bad), "%s", "999.1.1.1");
                break;
            case SettingRule::Mask:
                snprintf(bad, sizeof(bad), "%ld", (long)setting.hi + 1);
                snprintf(accepts, sizeof(accepts), "%ld..%ld", (long)setting.lo, (long)setting.hi);
                break;
        }
        // A Setting that takes words is refused in its words, all of them.
        if (setting.words != nullptr) {
            size_t used = 0;
            for (uint8_t w = 0; w < setting.words->count; ++w) {
                used += (size_t)snprintf(accepts + used, sizeof(accepts) - used, "%s%s",
                                         w == 0 ? "" : ",",
                                         setting.words->nameOf((uint8_t)(setting.words->first + w)));
            }
        }

        ConfigSnapshot working = defaults;
        ApplyRefusal refusal;
        char sentence[CONFIG_SETTING_SENTENCE_MAX] = {};
        TEST_ASSERT_FALSE_MESSAGE(
            configSettingApply(setting, bad, &working, &refusal, sentence, sizeof(sentence)),
            setting.form);
        TEST_ASSERT_EQUAL_MESSAGE(ApplyRefusalReason::OutOfRange, refusal.reason, setting.form);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(setting.form, refusal.field, setting.form);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(accepts, refusal.accepts, setting.form);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(sentence, setting.form), setting.form);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&defaults, &working, sizeof(ConfigSnapshot), setting.form);
    }
}

// The four ranges the 2026-09-26 defect pass found a second table disagreeing
// on (efe2273b): both edges taken, one past each refused, at the declaration
// every door now reads.
void test_the_ranges_that_drifted_hold_at_both_edges() {
    struct Bound {
        const char* form;
        long lo;
        long hi;
    };
    const Bound bounds[] = {
        {"logLevel", 1, 4},
        {"domeEscRndSpeedPct", 5, 100},
        {"domeEscRndPauseMin", 1, 120},
        {"domeEscRndPauseMax", 1, 120},
        {"domeEscRndMoveMs", 500, 10000},
    };
    for (const Bound& b : bounds) {
        const ConfigSetting* setting = configSettingByForm(b.form);
        TEST_ASSERT_NOT_NULL_MESSAGE(setting, b.form);
        const long cases[][2] = {{b.lo, 1}, {b.hi, 1}, {b.lo - 1, 0}, {b.hi + 1, 0}};
        for (const auto& c : cases) {
            ConfigSnapshot working = {};
            configSnapshotDefaults(&working);
            char text[16] = {};
            snprintf(text, sizeof(text), "%ld", c[0]);
            ApplyRefusal refusal;
            char sentence[CONFIG_SETTING_SENTENCE_MAX] = {};
            const bool taken =
                configSettingApply(*setting, text, &working, &refusal, sentence, sizeof(sentence));
            TEST_ASSERT_EQUAL_MESSAGE(c[1] != 0, taken, b.form);
            if (taken) {
                TEST_ASSERT_EQUAL_INT32_MESSAGE(c[0], configSettingNumber(*setting, working), b.form);
            } else {
                TEST_ASSERT_EQUAL_MESSAGE(ApplyRefusalReason::OutOfRange, refusal.reason, b.form);
            }
        }
    }
}

// A sound action's track field can hold a CHIRP catalog index past the 999 its
// door takes as a plain track - that is a banked binding, stored by the tracks
// write path. A load must hand it back as stored: clamping it to its
// declaration's range would quietly unbind the sound.
void test_a_banked_track_index_loads_back_as_stored() {
    Preferences prefs;
    prefs.begin("proto", false);
    prefs.putUShort("snd_scream", 40000);
    ConfigSnapshot loaded = {};
    configLoad(prefs, &loaded);
    prefs.end();
    TEST_ASSERT_EQUAL_UINT16(40000, loaded.audio.snd_scream);
}

// The CHIRP catalog binding's parts are declared like any Setting's value
// (#431): each refuses what it does not take with its range, and a page
// letter is taken in either case.
void test_the_catalog_binding_parts_refuse_with_what_they_take() {
    const struct {
        const char* part;
        const char* bad;
        const char* accepts;
    } cases[] = {{"bank", "7", "1..6"}, {"page", "AB", "A..Z"}, {"index", "0", "1..65535"}};
    for (const auto& c : cases) {
        const ConfigSetting* setting = catalogBindingSetting(c.part);
        TEST_ASSERT_NOT_NULL_MESSAGE(setting, c.part);
        int32_t value = 0;
        ApplyRefusal refusal;
        TEST_ASSERT_FALSE_MESSAGE(configSettingCheck(*setting, c.bad, c.part, &value, &refusal,
                                                     nullptr, 0),
                                  c.part);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.accepts, refusal.accepts, c.part);
    }
    int32_t page = 0;
    ApplyRefusal refusal;
    TEST_ASSERT_TRUE(configSettingCheck(*catalogBindingSetting("page"), "c", "page", &page, &refusal,
                                        nullptr, 0));
    TEST_ASSERT_EQUAL_INT32('C', page);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_setting_saved_through_the_store_loads_back_under_its_key);
    RUN_TEST(test_every_setting_refuses_a_value_it_does_not_take_with_field_reason_and_accepts);
    RUN_TEST(test_the_ranges_that_drifted_hold_at_both_edges);
    RUN_TEST(test_a_banked_track_index_loads_back_as_stored);
    RUN_TEST(test_the_catalog_binding_parts_refuse_with_what_they_take);
    return UNITY_END();
}
