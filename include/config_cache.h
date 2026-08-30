// =============================================================================
// include/config_cache.h
//
// Runtime config cache  --  accessor layer for live config state.
//
// Design:
// - configCacheRead/Apply manage the live config cache (mutex-protected).
// - Domain-specific accessors (configCacheReadDome, etc.) allow tasks to read
//   only the config slice they need, reducing contention.
// - Active WiFi settings (configCacheSetActiveWifi) represent the WiFi posture
//   actually applied at boot, distinct from persisted Device WiFi Settings.
// - Audio accessors (configAudioGetTrackByKey, etc.) bridge runtime queries
//   to the audio config schema.
// =============================================================================
#pragma once

#include "config_store.h"  // For type definitions (ConfigSnapshot, DomeConfig, etc.)
#include "rc_input_active_config.h"
#include "wifi_boot_decision.h"  // For WifiBootPosture (#189)

// =============================================================================
// Cache read-write (live runtime state)
// =============================================================================

// configCacheRead: Fill a ConfigSnapshot from the live config cache.
// This uses configCacheMux, not robotStateMux. Runtime tasks should copy the
// domain they need into stack locals, then release the cache lock before doing work.
void configCacheRead(ConfigSnapshot* out);
void configCacheReadDome(DomeConfig* out);
bool configCacheDomeEnabled();
void configCacheReadServo(ServoConfig* out);
bool configCacheServoAnyEnabled();
void configCacheReadWifi(WifiConfig* out);

// configCacheApply: Replace the live config cache with a full snapshot.
// Marks RobotState.rcConfigDirty so RcInputTask rebuilds cached mapping config.
void configCacheApply(const ConfigSnapshot& snap);

// Project the boot SystemConfig into the immutable RC settings that actually
// govern decoder startup, dispatch gates, and RC reporting. main calls this
// once after NVS load so saved staged changes cannot partially apply at runtime
// (ADR 0027).
RcInputActiveConfig rcInputActiveConfigFromSystem(const SystemConfig& system);

// Publish/read the RC configuration applied at boot. main publishes once;
// RcInputTask and RC diagnostics/status readers consume copies under the config
// cache lock so their reported state matches the running hardware paths.
void configCacheSetActiveRcInput(const RcInputActiveConfig& cfg);
void configCacheReadActiveRcInput(RcInputActiveConfig* out);

// =============================================================================
// Active WiFi state (reflects the bootstrap-time decision, not persisted settings)
// =============================================================================

// configCacheSetActiveWifi / configCacheReadActiveWifi: the Device WiFi
// Settings snapshot actually applied to WiFi hardware at the last boot or
// restart, as opposed to configCacheReadWifi() which reflects the latest
// persisted (possibly not-yet-applied) settings. The web read surface
// compares the two via wifiConfigsDiffer() to report active-vs-pending state
// for a Staged Network Switch (ADR 0015). Set once by the WiFi bootstrap
// shell after it decides and enters a boot posture.
void configCacheSetActiveWifi(const WifiConfig& cfg);
void configCacheReadActiveWifi(WifiConfig* out);

// configCacheSetActiveWifiRecovery / configCacheReadActiveWifiRecovery: was
// Network Recovery Mode (ADR 0015) the posture actually entered at the last
// boot? Recovery temporarily exposes WiFi Provisioning without
// touching Device WiFi Settings, so this flag  --  not activeWifiConfig  --  is
// the read surface's source of truth for "the controller is in recovery
// right now." Set once by the WiFi bootstrap shell alongside
// configCacheSetActiveWifi().
void configCacheSetActiveWifiRecovery(bool recovering);
bool configCacheReadActiveWifiRecovery();

// configCacheSetActiveWifiBootPosture / configCacheReadActiveWifiBootPosture:
// the full four-way WifiBootPosture actually decided and applied at the last
// WiFi bootstrap (#189). Distinct from configCacheReadActiveWifiRecovery(),
// which only answers "was NETWORK_RECOVERY the posture" -- this is needed by
// the Hosted backend's post-recovery rejoin guard (src/web/web_network_manager_hosted.cpp):
// only CLIENT_MODE's rejoin bypasses WiFi.begin() and forces WIFI_MODE_STA over
// raw esp_wifi_*; PROVISIONING/STANDALONE_AP_MODE/NETWORK_RECOVERY must be left
// alone rather than forced into STA after a C6 transport recovery. Set once by
// the WiFi bootstrap shell alongside configCacheSetActiveWifi()/
// configCacheSetActiveWifiRecovery().
void configCacheSetActiveWifiBootPosture(WifiBootPosture posture);
WifiBootPosture configCacheReadActiveWifiBootPosture();

// =============================================================================
// Active component output toggles (staged at reboot per ADR 0027)
// =============================================================================

// configCacheSetActiveDomeEnabled / configCacheReadActiveDomeEnabled: boot-effective
// dome output posture per ADR 0027 staged-at-reboot; saved toggle changes take
// effect next boot. Tasks read their toggles once at startup, never per iteration.
void configCacheSetActiveDomeEnabled(bool enabled);
bool configCacheReadActiveDomeEnabled();

// configCacheSetActiveAudioEnabled / configCacheReadActiveAudioEnabled: boot-effective
// audio output posture per ADR 0027 staged-at-reboot; saved toggle changes take
// effect next boot. Tasks read their toggles once at startup, never per iteration.
void configCacheSetActiveAudioEnabled(bool enabled);
bool configCacheReadActiveAudioEnabled();

// =============================================================================
// Log level accessor (lightweight, used by logging.h)
// =============================================================================

uint8_t configCurrentLogLevel();

// =============================================================================
// mDNS hostname resolution (config-driven)
// =============================================================================

// configResolvedMdnsHostname: Derive the mDNS hostname from SystemConfig
// (static version, used by tests and one-shot resolvers).
void configResolvedMdnsHostname(const SystemConfig& system, char* out, size_t outSize);

// configCacheResolvedMdnsHostname: Derive the mDNS hostname from the live cache.
void configCacheResolvedMdnsHostname(char* out, size_t outSize);

// =============================================================================
// Audio config accessors
// =============================================================================

// configAudioGetTrackByKey: Retrieve an audio track index by its symbolic key.
// Returns false if key is not found; true + *out = track index on success.
bool configAudioGetTrackByKey(const AudioConfig& config, const char* key, uint16_t* out);

// configAudioSetTrackByKey: Set an audio track index by its symbolic key.
// Returns false if key is not found; true on success.
bool configAudioSetTrackByKey(AudioConfig* config, const char* key, uint16_t value);

// configAudioCategoryCompanionKey: Given an audio key (e.g., "snd_rand_min"),
// return the companion category boundary key ("snd_rand_max"), or nullptr if none.
const char* configAudioCategoryCompanionKey(const char* key);

// configUpdateAudioMoodMasks: Atomically update mood category bounds in NVS.
// Used by the audio config API to set quiet/mid/full/awakeplus category boundaries.
// Caller opens Preferences with begin() before calling.
bool configUpdateAudioMoodMasks(Preferences& prefs, uint16_t quiet, uint16_t mid, uint16_t full,
                                uint16_t awakeplus);
