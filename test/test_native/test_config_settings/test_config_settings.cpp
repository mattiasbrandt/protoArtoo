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

// The NVS key each Setting was stored under before its declaration existed
// (src/config_serializer.cpp at 464cd119). A key string that changed would
// strand every stored value on a controller in the field, so this is the one
// place a key is written twice: as the pin that says it did not move.
struct KeyPin {
    const char* form;
    const char* key;
};

const KeyPin kStoredKeys[] = {
    {"speedLimitMax", "spd_max"},       {"speedPresetSlow", "spd_pre_s"},
    {"speedPresetNormal", "spd_pre_n"}, {"speedPresetTurbo", "spd_pre_t"},
    {"webDriveTimeoutMs", "web_tmo"},   {"sbusTimeoutMs", "sbus_tmo"},
    {"stationary", "op_mode"},          {"rcInputMode", "rc_mode"},
    {"rcMember", "rc_member"},          {"sbusRecvCh2", "sbus_recv_ch2"},
    {"enableDomeEsc", "en_dome_esc"},   {"enableRcCh1", "en_rc_ch1"},
    {"enableRcCh2", "en_rc_ch2"},       {"enableRcCh3", "en_rc_ch3"},
    {"enableRcCh4", "en_rc_ch4"},       {"enableRcCh5", "en_rc_ch5"},
    {"enableRcCh6", "en_rc_ch6"},       {"enableDrive", "en_drive"},
    {"enableAudio", "en_audio"},        {"soundMember", "snd_member"},
    {"enableProtoR2link", "en_r2link"}, {"enableArm1", "en_arm1"},
    {"enableArm2", "en_arm2"},          {"enableAux1", "en_aux1"},
    {"enableAux2", "en_aux2"},          {"enableAux3", "en_aux3"},
    {"domeEscNeutralUs", "dome_neu"},   {"domeEscMinPulseUs", "dome_minp"},
    {"domeEscMaxPulseUs", "dome_maxp"}, {"domeEscSpeedLimitPct", "dome_pct"},
    {"domeEscRndEnable", "dome_rnd_en"}, {"domeEscRndSpeedPct", "dome_rnd_spd"},
    {"domeEscRndPauseMin", "dome_rnd_pmin"}, {"domeEscRndPauseMax", "dome_rnd_pmax"},
    {"domeEscRndMoveMs", "dome_rnd_ms"}, {"protoR2linkWifiPeerIp", "dome_wip"},
    {"logLevel", "log_level"},
    // The audio Settings (#431 addendum), by the key their door takes.
    {"volume", "aud_vol"}, {"scream", "snd_scream"}, {"faint", "snd_faint"},
    {"leia", "snd_leia"}, {"cantina_s", "snd_cantina_s"}, {"sw_theme", "snd_sw"},
    {"imp_march", "snd_march"}, {"cantina_l", "snd_cantina_l"}, {"startup", "snd_startup"},
    {"doodoo", "snd_doodoo"}, {"failure", "snd_failure"}, {"disco", "snd_disco"},
    {"mahna", "snd_mahna"}, {"inlove", "snd_inlove"}, {"macho", "snd_macho"},
    {"gangnam", "snd_gangnam"}, {"uptown", "snd_uptown"}, {"celebr", "snd_celebr"},
    {"stayin", "snd_stayin"}, {"harlem", "snd_harlem"}, {"pbjtime", "snd_pbjtime"},
    {"sys_boot", "snd_sys_boot"}, {"sys_mode_n", "snd_sys_mode_n"},
    {"sys_mode_s", "snd_sys_mode_s"}, {"sys_mode_t", "snd_sys_mode_t"},
    {"sys_drv_on", "snd_sys_drv_on"}, {"sys_dome_on", "snd_sys_dome_on"},
    {"sys_net_down", "snd_sys_netdown"}, {"rand_min", "snd_rand_min"},
    {"rand_max", "snd_rand_max"}, {"snd_int_quiet", "snd_int_quiet"},
    {"snd_int_mid", "snd_int_mid"}, {"snd_int_full", "snd_int_full"},
    {"snd_int_awake", "snd_int_awake"}, {"quiet", "snd_moodcat_q"}, {"mid", "snd_moodcat_m"},
    {"full", "snd_moodcat_f"}, {"awakeplus", "snd_moodcat_a"},
    {"snd_cat_gen_lo", "snd_cat_gen_lo"}, {"snd_cat_gen_hi", "snd_cat_gen_hi"},
    {"snd_cat_chat_lo", "snd_cat_chat_lo"}, {"snd_cat_chat_hi", "snd_cat_chat_hi"},
    {"snd_cat_hap_lo", "snd_cat_hap_lo"}, {"snd_cat_hap_hi", "snd_cat_hap_hi"},
    {"snd_cat_proc_lo", "snd_cat_proc_lo"}, {"snd_cat_proc_hi", "snd_cat_proc_hi"},
    {"snd_cat_sad_lo", "snd_cat_sad_lo"}, {"snd_cat_sad_hi", "snd_cat_sad_hi"},
    {"snd_cat_sent_lo", "snd_cat_sent_lo"}, {"snd_cat_sent_hi", "snd_cat_sent_hi"},
    {"snd_cat_hum_lo", "snd_cat_hum_lo"}, {"snd_cat_hum_hi", "snd_cat_hum_hi"},
    {"snd_cat_scrm_lo", "snd_cat_scrm_lo"}, {"snd_cat_scrm_hi", "snd_cat_scrm_hi"},
    {"snd_cat_ooh_lo", "snd_cat_ooh_lo"}, {"snd_cat_ooh_hi", "snd_cat_ooh_hi"},
    {"snd_cat_alrm_lo", "snd_cat_alrm_lo"}, {"snd_cat_alrm_hi", "snd_cat_alrm_hi"},
    {"snd_cat_snrk_lo", "snd_cat_snrk_lo"}, {"snd_cat_snrk_hi", "snd_cat_snrk_hi"},
    {"snd_cat_whis_lo", "snd_cat_whis_lo"}, {"snd_cat_whis_hi", "snd_cat_whis_hi"},
};

// Every declared Setting, the droid's and the audio ones, in one list.
size_t everySettingCount() { return configSettingCount() + audioSettingCount(); }

const ConfigSetting& everySettingAt(size_t index) {
    return index < configSettingCount() ? configSettingAt(index)
                                        : audioSettingAt(index - configSettingCount());
}

const char* storedKeyOf(const char* form) {
    for (const KeyPin& pin : kStoredKeys) {
        if (strcmp(pin.form, form) == 0) {
            return pin.key;
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
        const char* pinned = storedKeyOf(setting.form);
        TEST_ASSERT_NOT_NULL_MESSAGE(pinned, setting.form);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(pinned, setting.nvsKey, setting.form);
        TEST_ASSERT_TRUE_MESSAGE(prefs.isKey(pinned), setting.form);
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_setting_saved_through_the_store_loads_back_under_its_key);
    RUN_TEST(test_every_setting_refuses_a_value_it_does_not_take_with_field_reason_and_accepts);
    RUN_TEST(test_the_ranges_that_drifted_hold_at_both_edges);
    RUN_TEST(test_a_banked_track_index_loads_back_as_stored);
    return UNITY_END();
}
