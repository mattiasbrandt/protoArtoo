// =============================================================================
// test/test_native/test_audio_dollar/test_audio_dollar.cpp
//
// Native unit tests for parseAudioDollar().
// Covers: numeric tracks, named shortcuts, playback control, volume commands,
//         edge cases, and custom AudioNamedTracks overrides.
// =============================================================================

#include <string.h>
#include <unity.h>

#include "audio_dollar_parser.h"

void setUp() {}
void tearDown() {}

// -----------------------------------------------------------------------------
// Numeric track commands
// -----------------------------------------------------------------------------

void test_numeric_track_001() {
    AudioAction a = parseAudioDollar("$001");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(1, a.track);
}

void test_numeric_track_100() {
    AudioAction a = parseAudioDollar("$100");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(100, a.track);
}

void test_numeric_track_65535() {
    AudioAction a = parseAudioDollar("$65535");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(65535, a.track);
}

void test_numeric_track_zero_is_none() {
    // $0 is invalid — track 0 has no meaning
    AudioAction a = parseAudioDollar("$0");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, a.type);
}

// -----------------------------------------------------------------------------
// Named sound shortcuts — default tracks
// -----------------------------------------------------------------------------

void test_scream() {
    AudioAction a = parseAudioDollar("$S");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_SCREAM, a.track);
}

void test_faint() {
    AudioAction a = parseAudioDollar("$F");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_FAINT, a.track);
}

void test_leia() {
    AudioAction a = parseAudioDollar("$L");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_LEIA, a.track);
}

void test_cantina_short() {
    AudioAction a = parseAudioDollar("$c");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_CANTINA_S, a.track);
}

void test_cantina_long() {
    AudioAction a = parseAudioDollar("$C");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_CANTINA_L, a.track);
}

void test_sw_theme() {
    AudioAction a = parseAudioDollar("$W");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_SW_THEME, a.track);
}

void test_imp_march() {
    AudioAction a = parseAudioDollar("$M");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_IMP_MARCH, a.track);
}

void test_startup() {
    AudioAction a = parseAudioDollar("$B");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, a.type);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_STARTUP, a.track);
}

void test_disco_default_is_none_until_configured() {
    AudioAction a = parseAudioDollar("$D");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, a.type);
}

// -----------------------------------------------------------------------------
// Playback control
// -----------------------------------------------------------------------------

void test_random_on() {
    AudioAction a = parseAudioDollar("$R");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_RANDOM_ON, a.type);
}

void test_random_off() {
    AudioAction a = parseAudioDollar("$O");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_RANDOM_OFF, a.type);
}

void test_stop() {
    AudioAction a = parseAudioDollar("$s");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_STOP, a.type);
}

// -----------------------------------------------------------------------------
// Volume commands
// -----------------------------------------------------------------------------

void test_volume_up() {
    AudioAction a = parseAudioDollar("$+");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_VOLUME_UP, a.type);
}

void test_volume_down() {
    AudioAction a = parseAudioDollar("$-");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_VOLUME_DOWN, a.type);
}

void test_volume_mid() {
    AudioAction a = parseAudioDollar("$m");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_VOLUME_SET, a.type);
    TEST_ASSERT_EQUAL_UINT8(AUDIO_VOLUME_MID, a.volume);
}

void test_volume_max() {
    AudioAction a = parseAudioDollar("$f");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_VOLUME_SET, a.type);
    TEST_ASSERT_EQUAL_UINT8(AUDIO_VOLUME_MAX, a.volume);
}

void test_volume_min() {
    AudioAction a = parseAudioDollar("$p");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_VOLUME_SET, a.type);
    TEST_ASSERT_EQUAL_UINT8(AUDIO_VOLUME_MIN, a.volume);
}

// -----------------------------------------------------------------------------
// Edge cases
// -----------------------------------------------------------------------------

void test_null_is_none() {
    AudioAction a = parseAudioDollar(nullptr);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, a.type);
}

void test_empty_string_is_none() {
    AudioAction a = parseAudioDollar("");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, a.type);
}

void test_bare_dollar_is_none() {
    AudioAction a = parseAudioDollar("$");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, a.type);
}

void test_no_dollar_prefix_is_none() {
    // Must start with '$'
    AudioAction a = parseAudioDollar("R");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, a.type);
}

void test_unknown_command_is_none() {
    AudioAction a = parseAudioDollar("$Z");
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, a.type);
}

// -----------------------------------------------------------------------------
// Custom AudioNamedTracks override
// -----------------------------------------------------------------------------

void test_custom_named_tracks() {
    AudioNamedTracks custom{};
    custom.scream    = 200;
    custom.leia      = 201;
    custom.imp_march = 202;

    AudioAction scream = parseAudioDollar("$S", custom);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, scream.type);
    TEST_ASSERT_EQUAL_UINT16(200, scream.track);

    AudioAction leia = parseAudioDollar("$L", custom);
    TEST_ASSERT_EQUAL_UINT16(201, leia.track);

    AudioAction march = parseAudioDollar("$M", custom);
    TEST_ASSERT_EQUAL_UINT16(202, march.track);

    // Unchanged field uses its own default
    AudioAction faint = parseAudioDollar("$F", custom);
    TEST_ASSERT_EQUAL_UINT16(AUDIO_TRACK_FAINT, faint.track);
}

void test_disco_plays_custom_named_track() {
    AudioNamedTracks custom{};
    custom.disco = 9;

    AudioAction disco = parseAudioDollar("$D", custom);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_PLAY_TRACK, disco.type);
    TEST_ASSERT_EQUAL_UINT16(9, disco.track);
}

void test_disco_zero_track_is_suppressed() {
    AudioNamedTracks custom{};
    custom.disco = 0;

    AudioAction disco = parseAudioDollar("$D", custom);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_NONE, disco.type);
}

// -----------------------------------------------------------------------------
// audioTrackNvsKey() — API key → NVS key mapping
// -----------------------------------------------------------------------------

void test_nvs_key_scream() {
    TEST_ASSERT_EQUAL_STRING("snd_scream", audioTrackNvsKey("scream"));
}
void test_nvs_key_faint() {
    TEST_ASSERT_EQUAL_STRING("snd_faint", audioTrackNvsKey("faint"));
}
void test_nvs_key_leia() {
    TEST_ASSERT_EQUAL_STRING("snd_leia", audioTrackNvsKey("leia"));
}
void test_nvs_key_cantina_s() {
    TEST_ASSERT_EQUAL_STRING("snd_cantina_s", audioTrackNvsKey("cantina_s"));
}
void test_nvs_key_sw_theme() {
    TEST_ASSERT_EQUAL_STRING("snd_sw", audioTrackNvsKey("sw_theme"));
}
void test_nvs_key_imp_march() {
    TEST_ASSERT_EQUAL_STRING("snd_march", audioTrackNvsKey("imp_march"));
}
void test_nvs_key_cantina_l() {
    TEST_ASSERT_EQUAL_STRING("snd_cantina_l", audioTrackNvsKey("cantina_l"));
}
void test_nvs_key_startup() {
    TEST_ASSERT_EQUAL_STRING("snd_startup", audioTrackNvsKey("startup"));
}
void test_nvs_key_rand_min() {
    TEST_ASSERT_EQUAL_STRING("snd_rand_min", audioTrackNvsKey("rand_min"));
}
void test_nvs_key_rand_max() {
    TEST_ASSERT_EQUAL_STRING("snd_rand_max", audioTrackNvsKey("rand_max"));
}
void test_nvs_key_snd_int_quiet() {
    TEST_ASSERT_EQUAL_STRING("snd_int_quiet", audioTrackNvsKey("snd_int_quiet"));
}
void test_nvs_key_snd_int_mid() {
    TEST_ASSERT_EQUAL_STRING("snd_int_mid", audioTrackNvsKey("snd_int_mid"));
}
void test_nvs_key_snd_int_full() {
    TEST_ASSERT_EQUAL_STRING("snd_int_full", audioTrackNvsKey("snd_int_full"));
}
void test_nvs_key_snd_int_awake() {
    TEST_ASSERT_EQUAL_STRING("snd_int_awake", audioTrackNvsKey("snd_int_awake"));
}
void test_nvs_key_doodoo() {
    TEST_ASSERT_EQUAL_STRING("snd_doodoo", audioTrackNvsKey("doodoo"));
}
void test_nvs_key_failure() {
    TEST_ASSERT_EQUAL_STRING("snd_failure", audioTrackNvsKey("failure"));
}
void test_nvs_key_disco() {
    TEST_ASSERT_EQUAL_STRING("snd_disco", audioTrackNvsKey("disco"));
}
void test_nvs_key_mahna() {
    TEST_ASSERT_EQUAL_STRING("snd_mahna", audioTrackNvsKey("mahna"));
}
void test_nvs_key_inlove() {
    TEST_ASSERT_EQUAL_STRING("snd_inlove", audioTrackNvsKey("inlove"));
}
void test_nvs_key_macho() {
    TEST_ASSERT_EQUAL_STRING("snd_macho", audioTrackNvsKey("macho"));
}
void test_nvs_key_gangnam() {
    TEST_ASSERT_EQUAL_STRING("snd_gangnam", audioTrackNvsKey("gangnam"));
}
void test_nvs_key_uptown() {
    TEST_ASSERT_EQUAL_STRING("snd_uptown", audioTrackNvsKey("uptown"));
}
void test_nvs_key_celebr() {
    TEST_ASSERT_EQUAL_STRING("snd_celebr", audioTrackNvsKey("celebr"));
}
void test_nvs_key_stayin() {
    TEST_ASSERT_EQUAL_STRING("snd_stayin", audioTrackNvsKey("stayin"));
}
void test_nvs_key_harlem() {
    TEST_ASSERT_EQUAL_STRING("snd_harlem", audioTrackNvsKey("harlem"));
}
void test_nvs_key_pbjtime() {
    TEST_ASSERT_EQUAL_STRING("snd_pbjtime", audioTrackNvsKey("pbjtime"));
}
void test_nvs_key_sys_boot() {
    TEST_ASSERT_EQUAL_STRING("snd_sys_boot", audioTrackNvsKey("sys_boot"));
}
void test_nvs_key_sys_mode_n() {
    TEST_ASSERT_EQUAL_STRING("snd_sys_mode_n", audioTrackNvsKey("sys_mode_n"));
}
void test_nvs_key_sys_mode_s() {
    TEST_ASSERT_EQUAL_STRING("snd_sys_mode_s", audioTrackNvsKey("sys_mode_s"));
}
void test_nvs_key_sys_mode_t() {
    TEST_ASSERT_EQUAL_STRING("snd_sys_mode_t", audioTrackNvsKey("sys_mode_t"));
}
void test_nvs_key_sys_drv_on() {
    TEST_ASSERT_EQUAL_STRING("snd_sys_drv_on", audioTrackNvsKey("sys_drv_on"));
}
void test_nvs_key_sys_dome_on() {
    TEST_ASSERT_EQUAL_STRING("snd_sys_dome_on", audioTrackNvsKey("sys_dome_on"));
}


void test_nvs_key_sound_categories() {
    TEST_ASSERT_EQUAL_STRING("snd_cat_gen_lo", audioTrackNvsKey("snd_cat_gen_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_gen_hi", audioTrackNvsKey("snd_cat_gen_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_chat_lo", audioTrackNvsKey("snd_cat_chat_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_chat_hi", audioTrackNvsKey("snd_cat_chat_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_hap_lo", audioTrackNvsKey("snd_cat_hap_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_hap_hi", audioTrackNvsKey("snd_cat_hap_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_proc_lo", audioTrackNvsKey("snd_cat_proc_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_proc_hi", audioTrackNvsKey("snd_cat_proc_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_sad_lo", audioTrackNvsKey("snd_cat_sad_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_sad_hi", audioTrackNvsKey("snd_cat_sad_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_sent_lo", audioTrackNvsKey("snd_cat_sent_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_sent_hi", audioTrackNvsKey("snd_cat_sent_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_hum_lo", audioTrackNvsKey("snd_cat_hum_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_hum_hi", audioTrackNvsKey("snd_cat_hum_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_scrm_lo", audioTrackNvsKey("snd_cat_scrm_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_scrm_hi", audioTrackNvsKey("snd_cat_scrm_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_ooh_lo", audioTrackNvsKey("snd_cat_ooh_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_ooh_hi", audioTrackNvsKey("snd_cat_ooh_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_alrm_lo", audioTrackNvsKey("snd_cat_alrm_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_alrm_hi", audioTrackNvsKey("snd_cat_alrm_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_snrk_lo", audioTrackNvsKey("snd_cat_snrk_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_snrk_hi", audioTrackNvsKey("snd_cat_snrk_hi"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_whis_lo", audioTrackNvsKey("snd_cat_whis_lo"));
    TEST_ASSERT_EQUAL_STRING("snd_cat_whis_hi", audioTrackNvsKey("snd_cat_whis_hi"));
}

void test_nvs_key_unknown_returns_null() {
    TEST_ASSERT_NULL(audioTrackNvsKey("bogus"));
}
void test_nvs_key_null_returns_null() {
    TEST_ASSERT_NULL(audioTrackNvsKey(nullptr));
}
void test_nvs_key_empty_returns_null() {
    TEST_ASSERT_NULL(audioTrackNvsKey(""));
}
void test_nvs_keys_are_15_chars_or_less() {
    // NVS key length limit is 15 chars (ESP-IDF constraint)
    const char* keys[] = {
        "scream","faint","leia","cantina_s","sw_theme",
        "imp_march","cantina_l","startup","doodoo","failure",
        "disco","mahna","inlove","macho","gangnam","uptown",
        "celebr","stayin","harlem","pbjtime",
        "sys_boot","sys_mode_n","sys_mode_s","sys_mode_t","sys_drv_on","sys_dome_on",
        "snd_cat_gen_lo","snd_cat_gen_hi","snd_cat_chat_lo","snd_cat_chat_hi",
        "snd_cat_hap_lo","snd_cat_hap_hi","snd_cat_proc_lo","snd_cat_proc_hi",
        "snd_cat_sad_lo","snd_cat_sad_hi","snd_cat_sent_lo","snd_cat_sent_hi",
        "snd_cat_hum_lo","snd_cat_hum_hi","snd_cat_scrm_lo","snd_cat_scrm_hi",
        "snd_cat_ooh_lo","snd_cat_ooh_hi","snd_cat_alrm_lo","snd_cat_alrm_hi",
        "snd_cat_snrk_lo","snd_cat_snrk_hi","snd_cat_whis_lo","snd_cat_whis_hi",
        "rand_min","rand_max","snd_int_quiet","snd_int_mid","snd_int_full","snd_int_awake"
    };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        const char* nvsKey = audioTrackNvsKey(keys[i]);
        TEST_ASSERT_NOT_NULL(nvsKey);
        TEST_ASSERT_TRUE_MESSAGE(strlen(nvsKey) <= 15,
            "NVS key exceeds 15-char ESP-IDF limit");
    }
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Numeric
    RUN_TEST(test_numeric_track_001);
    RUN_TEST(test_numeric_track_100);
    RUN_TEST(test_numeric_track_65535);
    RUN_TEST(test_numeric_track_zero_is_none);

    // Named shortcuts
    RUN_TEST(test_scream);
    RUN_TEST(test_faint);
    RUN_TEST(test_leia);
    RUN_TEST(test_cantina_short);
    RUN_TEST(test_cantina_long);
    RUN_TEST(test_sw_theme);
    RUN_TEST(test_imp_march);
    RUN_TEST(test_startup);

    RUN_TEST(test_disco_default_is_none_until_configured);
    // Playback control
    RUN_TEST(test_random_on);
    RUN_TEST(test_random_off);
    RUN_TEST(test_stop);

    // Volume
    RUN_TEST(test_volume_up);
    RUN_TEST(test_volume_down);
    RUN_TEST(test_volume_mid);
    RUN_TEST(test_volume_max);
    RUN_TEST(test_volume_min);

    // Edge cases
    RUN_TEST(test_null_is_none);
    RUN_TEST(test_empty_string_is_none);
    RUN_TEST(test_bare_dollar_is_none);
    RUN_TEST(test_no_dollar_prefix_is_none);
    RUN_TEST(test_unknown_command_is_none);

    // Custom named tracks
    RUN_TEST(test_custom_named_tracks);
    RUN_TEST(test_disco_plays_custom_named_track);
    RUN_TEST(test_disco_zero_track_is_suppressed);

    // audioTrackNvsKey
    RUN_TEST(test_nvs_key_scream);
    RUN_TEST(test_nvs_key_faint);
    RUN_TEST(test_nvs_key_leia);
    RUN_TEST(test_nvs_key_cantina_s);
    RUN_TEST(test_nvs_key_sw_theme);
    RUN_TEST(test_nvs_key_imp_march);
    RUN_TEST(test_nvs_key_cantina_l);
    RUN_TEST(test_nvs_key_startup);
    RUN_TEST(test_nvs_key_doodoo);
    RUN_TEST(test_nvs_key_failure);
    RUN_TEST(test_nvs_key_disco);
    RUN_TEST(test_nvs_key_mahna);
    RUN_TEST(test_nvs_key_inlove);
    RUN_TEST(test_nvs_key_macho);
    RUN_TEST(test_nvs_key_gangnam);
    RUN_TEST(test_nvs_key_uptown);
    RUN_TEST(test_nvs_key_celebr);
    RUN_TEST(test_nvs_key_stayin);
    RUN_TEST(test_nvs_key_harlem);
    RUN_TEST(test_nvs_key_pbjtime);
    RUN_TEST(test_nvs_key_sys_boot);
    RUN_TEST(test_nvs_key_sys_mode_n);
    RUN_TEST(test_nvs_key_sys_mode_s);
    RUN_TEST(test_nvs_key_sys_mode_t);
    RUN_TEST(test_nvs_key_sys_drv_on);
    RUN_TEST(test_nvs_key_sys_dome_on);
    RUN_TEST(test_nvs_key_sound_categories);
    RUN_TEST(test_nvs_key_rand_min);
    RUN_TEST(test_nvs_key_rand_max);
    RUN_TEST(test_nvs_key_snd_int_quiet);
    RUN_TEST(test_nvs_key_snd_int_mid);
    RUN_TEST(test_nvs_key_snd_int_full);
    RUN_TEST(test_nvs_key_snd_int_awake);
    RUN_TEST(test_nvs_key_unknown_returns_null);
    RUN_TEST(test_nvs_key_null_returns_null);
    RUN_TEST(test_nvs_key_empty_returns_null);
    RUN_TEST(test_nvs_keys_are_15_chars_or_less);

    return UNITY_END();
}
