// =============================================================================
// src/web/api_audio_serializers.cpp
//
// Pure JSON serialization helper for audio module status.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_audio.h"

#include <cstdio>

void formatAudioStatusJson(char* buf, size_t bufSize, const char* driverName, uint8_t capabilities,
                           bool linkOk, bool active, uint8_t playState, uint8_t device,
                           uint16_t totalTracks, uint16_t currentTrack, const char* rxStatus,
                           const char* rxDetail) {
    // playState labels per datasheet: 0=stop 1=playing 2=paused 0xFF=unknown
    const char* playSt = (playState == 0x00)   ? "stop"
                         : (playState == 0x01) ? "playing"
                         : (playState == 0x02) ? "paused"
                                               : "unknown";
    // device labels: 0=USB 1=SD/TF 2=FLASH 3=Flash+SD(CHIRP) 0xFF=none/unknown
    const char* devStr = (device == 0x00)   ? "USB"
                         : (device == 0x01) ? "SD/TF"
                         : (device == 0x02) ? "FLASH"
                         : (device == 0x03) ? "Flash+SD"
                         : (device == 0xFF) ? "none"
                                            : "unknown";
    snprintf(buf, bufSize,
             "{\"driver\":\"%s\",\"capabilities\":%u,\"link_ok\":%s,\"active\":%s,"
             "\"play_state\":\"%s\",\"device\":\"%s\","
             "\"total_tracks\":%u,\"current_track\":%u,"
             "\"rx_status\":\"%s\",\"rx_detail\":\"%s\"}",
             driverName, (unsigned)capabilities, linkOk ? "true" : "false",
             active ? "true" : "false", playSt, devStr, (unsigned)totalTracks,
             (unsigned)currentTrack, rxStatus, rxDetail);
}
