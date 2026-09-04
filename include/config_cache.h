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

#include <stddef.h>

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

// configCacheSetStationary: write the one field the Commanded Mode setters
// mirror into the cache, by field.
//
// commandedSetStationary() (src/commanded_modes.cpp) keeps this in step with
// RobotState.stationary so the next config save persists the commanded mode
// instead of reverting it from a stale cache. It used to do that with a
// whole-snapshot round trip - read all 944 B of ConfigSnapshot out, set one
// bool, write all 944 B back through configCacheApply() - on the SBUS path
// (Core 1, once per frame while driving, src/tasks/rc_input.cpp), on the httpd
// task and on the Console alike. That also marked RobotState.rcConfigDirty on
// every toggle, making RcInputTask rebuild its cached mapping config for a
// field the RC processor config does not contain at all
// (include/rc_input_processor.h reads stationary only as stationaryLocked
// state). By field: no snapshot copy on the caller's stack, and no dirty
// flag (ADR 0011, 2026-09-04 amendment).
void configCacheSetStationary(bool stationary);

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

// configCacheSetActiveComponentToggles / configCacheReadActiveComponentToggle:
// the packed bitmask of every system.config.enable_* value actually running
// since the last boot, as opposed to configCacheRead()'s SystemConfig fields
// which reflect the latest persisted (possibly not-yet-applied) toggle write.
// This is the general form of configCacheSetActiveDomeEnabled/AudioEnabled
// above for all 15 Component Toggles at once - added for the Controller
// Console's "read shows saved vs active" answer (#226) rather than 13 more
// single-bool accessor pairs. Bit index matches
// include/console_config_fields.h's kComponentToggleFields[] order; that
// header owns the name<->index mapping, so this stays index-based and
// name-agnostic. Set once by setup() from the boot config snapshot,
// mirroring the two calls above.
void configCacheSetActiveComponentToggles(const SystemConfig& system);
bool configCacheReadActiveComponentToggle(size_t bitIndex);

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
