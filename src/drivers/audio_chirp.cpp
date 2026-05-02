// =============================================================================
// src/drivers/audio_chirp.cpp
//
// AudioDriver implementation for the CHIRP Audio Trigger board.
//
// TX commands are sent over software UART on PIN_AUDIO_TX (GPIO 26) at 9600 baud.
// RX responses are read from HardwareSerial(2) on PIN_AUDIO_RX (GPIO 35) for
// manifest, catalog, and status queries.
//
// CHIRP must be pre-configured to 9600 baud via CHIRP.INI on the SD card root:
//   #BAUD_RATE 9600
//
// Commands used:
//   playTrack(n)        -> "PLAY:n,1,A\n"  (Bank 1, Page A)
//   playTrackBanked()   -> "PLAY:n,bank,page\n"
//   stop()              -> "STOP\n"
//   setVolume(v)        -> "VOL:N\n" where N = v * 99 / 30
//   begin() bootstrap   -> "GMAN\n" for bank summary
//   refreshCatalog()    -> "GMAN\n" + per-entry "GNME:bank,page,index\n"
//   queryModuleState()  -> "STAT:0\n" for stream activity
// =============================================================================

#include "audio_chirp.h"

#include <Arduino.h>
#include <ctype.h>   // isalpha, isdigit, toupper
#include <stdio.h>   // snprintf, sscanf
#include <stdlib.h>  // strtoul
#include <string.h>  // strcmp, strncmp, strstr, strncpy

#include "audio_soft_uart_tx.h"  // shared soft-UART bit-bang primitives
#include "config.h"
#include "logging.h"
#include "robot_state.h"

static HardwareSerial s_chirpSerial(2);
static const char* TAG = "ChirpDrv";
static uint32_t s_lastNoRspDiagMs = 0;
static constexpr uint32_t CHIRP_GNME_WAIT_MS = 450u;
static constexpr uint32_t CHIRP_GNME_READLINE_MS = 120u;

// Read one \r\n-terminated ASCII line from s_chirpSerial into buf (null-terminated).
// Returns number of characters written (excluding null). Stops at '\n', '\r' discarded.
// Yields Core 0 while waiting to keep WiFi/web tasks responsive.
static uint8_t readLine(char* buf, uint8_t maxLen, uint32_t timeoutMs) {
    if (maxLen == 0) {
        return 0;
    }

    uint32_t start = millis();
    uint8_t pos = 0;
    while ((uint32_t)(millis() - start) < timeoutMs && pos < maxLen - 1u) {
        if (s_chirpSerial.available()) {
            char c = (char)s_chirpSerial.read();
            if (c == '\n') {
                break;
            }
            if (c != '\r') {
                buf[pos++] = c;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    buf[pos] = '\0';
    return pos;
}

static char normalizePage(char page) {
    if (page >= 'a' && page <= 'z') {
        return (char)(page - ('a' - 'A'));
    }
    if (page >= 'A' && page <= 'Z') {
        return page;
    }
    return 'A';
}

static char derivePageFromDirName(const char* dirName) {
    if (dirName == nullptr || dirName[0] == '\0') {
        return 'A';
    }
    uint8_t idx = 0;
    while (dirName[idx] != '\0' && isdigit((unsigned char)dirName[idx])) {
        ++idx;
    }
    if (dirName[idx] != '\0' && isalpha((unsigned char)dirName[idx])) {
        return (char)toupper((unsigned char)dirName[idx]);
    }
    return 'A';
}

static bool parseBankLine(const char* line, AudioCatalogBank* out) {
    if (line == nullptr || out == nullptr) {
        return false;
    }
    if (strncmp(line, "BANK:", 5) != 0) {
        return false;
    }

    const char* p = line + 5;
    char* end = nullptr;
    unsigned long bankVal = strtoul(p, &end, 10);
    if (end == p || *end != ',') {
        return false;
    }
    p = end + 1;

    const char* dirStart = p;
    while (*p != '\0' && *p != ',') {
        ++p;
    }
    if (p == dirStart || *p != ',') {
        return false;
    }

    size_t dirLen = (size_t)(p - dirStart);
    if (dirLen >= sizeof(out->dirName)) {
        dirLen = sizeof(out->dirName) - 1;
    }
    memcpy(out->dirName, dirStart, dirLen);
    out->dirName[dirLen] = '\0';
    p += 1;

    bool sawCount = false;
    unsigned long countVal = 0;
    while (*p != '\0') {
        unsigned long token = strtoul(p, &end, 10);
        if (end == p) {
            return false;
        }
        sawCount = true;
        countVal = token;
        if (*end == '\0') {
            break;
        }
        if (*end != ',') {
            return false;
        }
        p = end + 1;
    }
    if (!sawCount || bankVal == 0 || bankVal > 255u) {
        return false;
    }

    out->bank = (uint8_t)bankVal;
    out->page = derivePageFromDirName(out->dirName);
    out->count = (countVal > 65535u) ? 65535u : (uint16_t)countVal;
    return true;
}

static bool parseNameLine(const char* line, uint8_t* bankOut, char* pageOut, uint16_t* indexOut,
                          char* nameOut, size_t nameLen) {
    if (line == nullptr || bankOut == nullptr || pageOut == nullptr || indexOut == nullptr ||
        nameOut == nullptr || nameLen == 0) {
        return false;
    }

    const char* namePrefix = strstr(line, "NAME:");
    if (namePrefix == nullptr) {
        return false;
    }
    const char* p = namePrefix + 5;
    while (*p == ' ') {
        ++p;
    }
    char* end = nullptr;

    // Bank 1 responses use empty page field: NAME:1,,<index>,<name>
    // Handle this form first to avoid parser ambiguity.
    unsigned long bankEmptyPage = 0;
    unsigned long indexEmptyPage = 0;
    char emptyPageName[48] = {0};
    if (sscanf(namePrefix, "NAME:%lu,,%lu,%47[^\r\n]", &bankEmptyPage, &indexEmptyPage,
               emptyPageName) == 3) {
        if (bankEmptyPage <= 255u && indexEmptyPage <= 65535u && emptyPageName[0] != '\0') {
            *bankOut = (uint8_t)bankEmptyPage;
            *pageOut = 'A';
            *indexOut = (uint16_t)indexEmptyPage;
            strncpy(nameOut, emptyPageName, nameLen - 1);
            nameOut[nameLen - 1] = '\0';
            return true;
        }
    }

    unsigned long bankVal = strtoul(p, &end, 10);
    if (end == p || *end != ',') {
        return false;
    }
    p = end + 1;
    while (*p == ' ') {
        ++p;
    }

    char pageVal = 'A';
    if (*p == ',') {
        // Bank 1 NAME frames use empty page field: NAME:1,,<index>,<name>
        ++p;
    } else if (*p != '\0' && p[1] == ',' && isalpha((unsigned char)*p)) {
        pageVal = normalizePage(*p);
        p += 2;
    } else {
        return false;
    }
    while (*p == ' ') {
        ++p;
    }

    unsigned long indexVal = strtoul(p, &end, 10);
    if (end == p || *end != ',') {
        return false;
    }
    p = end + 1;
    while (*p == ' ') {
        ++p;
    }

    if (*p == '\0' || bankVal > 255u || indexVal > 65535u) {
        return false;
    }

    strncpy(nameOut, p, nameLen - 1);
    nameOut[nameLen - 1] = '\0';

    *bankOut = (uint8_t)bankVal;
    *pageOut = pageVal;
    *indexOut = (uint16_t)indexVal;
    return true;
}

// Parse GMAN output and cache all BANK lines.
// Returns true when any valid GMAN frame line was observed.
bool AudioDriverChirp::loadManifestBanks(uint32_t timeoutMs, bool keepTotalTracks) {
    while (s_chirpSerial.available()) {
        s_chirpSerial.read();
    }

    sendCommand("GMAN");

    uint32_t startMs = millis();
    bool gotValidGmanLine = false;
    uint32_t rxBytes = 0;
    uint16_t bank1Count = keepTotalTracks ? m_totalTracks : 0;
    uint8_t catalogBankCount = 0;
    uint16_t droppedBankLines = 0;
    memset(m_catalogBanks, 0, sizeof(m_catalogBanks));
    char line[96];

    while ((uint32_t)(millis() - startMs) < timeoutMs) {
        uint8_t n = readLine(line, (uint8_t)sizeof(line), 80u);
        if (n == 0) {
            continue;
        }
        rxBytes += n;

        bool isGmanLine = (strncmp(line, "MDAT:", 5) == 0) || (strncmp(line, "BANK:", 5) == 0) ||
                          (strncmp(line, "MSUM:", 5) == 0) || (strcmp(line, "MEND") == 0);
        if (!isGmanLine) {
            continue;
        }
        gotValidGmanLine = true;

        if (strncmp(line, "BANK:", 5) == 0) {
            AudioCatalogBank bank{};
            if (parseBankLine(line, &bank)) {
                if (catalogBankCount < AUDIO_CATALOG_MAX_BANKS) {
                    m_catalogBanks[catalogBankCount++] = bank;
                } else {
                    ++droppedBankLines;
                }
                if (bank.bank == 1) {
                    bank1Count = bank.count;
                }
            }
        }

        if (strcmp(line, "MEND") == 0) {
            break;
        }
    }

    if (!gotValidGmanLine) {
        if (rxBytes == 0) {
            PA_LOG_WARN(TAG,
                        "No CHIRP RX bytes during GMAN query. Verify CHIRP TX->S2 RX (GPIO35), common GND, and baud=9600.");
        } else {
            PA_LOG_WARN(TAG,
                        "CHIRP RX activity seen (%u bytes) but no valid GMAN frame.",
                        (unsigned)rxBytes);
        }
        return false;
    }
    if (droppedBankLines > 0) {
        PA_LOG_WARN(TAG,
                    "GMAN reported more banks/pages than supported (max=%u). Ignoring %u extra BANK lines.",
                    (unsigned)AUDIO_CATALOG_MAX_BANKS, (unsigned)droppedBankLines);
    }

    m_catalogBankCount = catalogBankCount;
    m_totalTracks = bank1Count;
    return true;
}

// -----------------------------------------------------------------------------
// begin()
// Configures CHIRP TX/RX, applies boot volume, then queries GMAN to cache
// bank descriptors and Bank 1 sound count.
// -----------------------------------------------------------------------------
void AudioDriverChirp::begin(uint8_t vol) {
    // same UART2 RX path as DY-SV5W; see T66 for dome-link contention handling.
    // SBUS2 is now RMT-based; UART2 is only contended by dome link.
    s_chirpSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, -1);
    softUartTxBegin();

    // CHIRP boots, mounts SD, and optionally syncs Bank 1 to flash; 2 s covers
    // most cases. First boot after SD card change may need more time.
    delay(2000);

    // Apply NVS-configured boot volume before any playback.
    setVolume(vol);

    m_totalTracks = 0;
    m_linkOk = false;
    m_playState = 0xFF;
    m_lastTrack = 0;
    m_catalogReady = false;
    m_catalogCount = 0;
    m_catalogBankCount = 0;

    m_linkOk = loadManifestBanks(1500u, false);

    PA_LOG_INFO(TAG, "init — vol=%u Bank1 sounds=%u banks=%u link=%s", (unsigned)vol,
                (unsigned)m_totalTracks, (unsigned)m_catalogBankCount,
                m_linkOk ? "OK" : "no response");
}

// -----------------------------------------------------------------------------
// sendCommand()
// Transmit a null-terminated ASCII string followed by '\n'.
// -----------------------------------------------------------------------------
void AudioDriverChirp::sendCommand(const char* cmd) {
    softUartTxString(cmd);
    softUartTxByte('\n');
}

// -----------------------------------------------------------------------------
// playTrack()
// Play a track by 1-based index in Bank 1, Page A on stream 0.
// -----------------------------------------------------------------------------
void AudioDriverChirp::playTrack(uint16_t track) {
    playTrackBanked(track, 1, 'A');
}

void AudioDriverChirp::playTrackBanked(uint16_t index, uint8_t bank, char page) {
    if (index == 0) {
        return;
    }
    if (bank == 0) {
        bank = 1;
    }
    page = normalizePage(page);
    m_lastTrack = index;  // cache for currentTrack reporting

    // Buffer sized for "PLAY:65535,255,Z" (16 chars) + null
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "PLAY:%u,%u,%c", (unsigned)index, (unsigned)bank, page);
    sendCommand(cmd);
}

// -----------------------------------------------------------------------------
// stop()
// Stop all active streams.
// Format: "STOP\n"
// -----------------------------------------------------------------------------
void AudioDriverChirp::stop() {
    sendCommand("STOP");
}

// -----------------------------------------------------------------------------
// setVolume()
// Set global volume. vol is 0–30 (clamped by AudioTask before this call).
// Scales to CHIRP native range 0–99: N = (vol * CHIRP_VOL_MAX) / 30.
// Format: "VOL:N\n"
// -----------------------------------------------------------------------------
void AudioDriverChirp::setVolume(uint8_t vol) {
    uint8_t chirpVol = (uint8_t)((uint16_t)vol * CHIRP_VOL_MAX / 30u);
    char cmd[10];
    snprintf(cmd, sizeof(cmd), "VOL:%u", (unsigned)chirpVol);
    sendCommand(cmd);
}

bool AudioDriverChirp::refreshCatalog() {
    bool uart2Contended;
    taskENTER_CRITICAL(&robotStateMux);
    uart2Contended = robotState.domeUartOwned;
    taskEXIT_CRITICAL(&robotStateMux);

    if (uart2Contended) {
        PA_LOG_WARN(TAG, "catalog refresh skipped — UART2 contended by dome link");
        return false;
    }

    if (!loadManifestBanks(2500u, false)) {
        m_catalogReady = false;
        return false;
    }

    m_catalogCount = 0;
    m_catalogReady = false;
    uint16_t missingNameCount = 0;

    for (uint8_t bankIdx = 0; bankIdx < m_catalogBankCount; ++bankIdx) {
        const AudioCatalogBank& bank = m_catalogBanks[bankIdx];
        for (uint16_t soundIndex = 1; soundIndex <= bank.count; ++soundIndex) {
            if (m_catalogCount >= AUDIO_CATALOG_MAX_ENTRIES) {
                PA_LOG_WARN(TAG, "catalog entry cap reached (%u)",
                            (unsigned)AUDIO_CATALOG_MAX_ENTRIES);
                m_catalogReady = true;
                return true;
            }

            char cmd[28];
            snprintf(cmd, sizeof(cmd), "GNME:%u,%c,%u", (unsigned)bank.bank, bank.page,
                     (unsigned)soundIndex);
            sendCommand(cmd);

            bool gotName = false;
            uint32_t startMs = millis();
            char line[112];

            while ((uint32_t)(millis() - startMs) < CHIRP_GNME_WAIT_MS) {
                uint8_t n = readLine(line, (uint8_t)sizeof(line), CHIRP_GNME_READLINE_MS);
                if (n == 0) {
                    continue;
                }

                uint8_t respBank = 0;
                char respPage = 'A';
                uint16_t respIndex = 0;
                char fileName[sizeof(m_catalog[0].name)] = {0};
                if (!parseNameLine(line, &respBank, &respPage, &respIndex, fileName,
                                   sizeof(fileName))) {
                    continue;
                }
                if (respBank != bank.bank || respPage != bank.page || respIndex != soundIndex) {
                    continue;
                }

                AudioCatalogEntry& entry = m_catalog[m_catalogCount++];
                entry.bank = respBank;
                entry.page = respPage;
                entry.index = respIndex;
                strncpy(entry.name, fileName, sizeof(entry.name) - 1);
                entry.name[sizeof(entry.name) - 1] = '\0';
                gotName = true;
                break;
            }

            if (!gotName) {
                AudioCatalogEntry& entry = m_catalog[m_catalogCount++];
                entry.bank = bank.bank;
                entry.page = bank.page;
                entry.index = soundIndex;
                snprintf(entry.name, sizeof(entry.name), "index_%u", (unsigned)soundIndex);
                ++missingNameCount;
            }
        }
    }

    m_catalogReady = true;
    PA_LOG_INFO(TAG, "catalog refresh complete: banks=%u entries=%u missing_names=%u",
                (unsigned)m_catalogBankCount, (unsigned)m_catalogCount,
                (unsigned)missingNameCount);
    return true;
}

uint16_t AudioDriverChirp::getCatalogEntryCount() const {
    return m_catalogCount;
}

const AudioCatalogEntry* AudioDriverChirp::getCatalogEntries() const {
    return m_catalog;
}

uint8_t AudioDriverChirp::getCatalogBankCount() const {
    return m_catalogBankCount;
}

const AudioCatalogBank* AudioDriverChirp::getCatalogBanks() const {
    return m_catalogBanks;
}

bool AudioDriverChirp::isCatalogReady() const {
    return m_catalogReady;
}

// -----------------------------------------------------------------------------
// queryModuleState()
// Query CHIRP stream status and map to AudioModuleState.
// Returns true if at least one valid status line is received.
// -----------------------------------------------------------------------------
static bool parseChirpStatusLine(const char* line, uint8_t* playStateOut) {
    if (line == nullptr || playStateOut == nullptr) {
        return false;
    }

    if (strncmp(line, "STAT:", 5) == 0) {
        const char* state = line + 5;
        if (strncmp(state, "playing,", 8) == 0) {
            *playStateOut = 0x01;
            return true;
        }
        if (strncmp(state, "idle,", 5) == 0) {
            *playStateOut = 0x00;
            return true;
        }
    }

    if (strncmp(line, "S:", 2) == 0) {
        if (strstr(line, ",ply,") != nullptr) {
            *playStateOut = 0x01;
            return true;
        }
        if (strstr(line, ",idle,") != nullptr) {
            *playStateOut = 0x00;
            return true;
        }
    }

    return false;
}

bool AudioDriverChirp::queryModuleState(AudioModuleState& out) {
    bool uart2Contended;
    taskENTER_CRITICAL(&robotStateMux);
    uart2Contended = robotState.domeUartOwned;
    taskEXIT_CRITICAL(&robotStateMux);
    if (uart2Contended) {
        PA_LOG_DEBUG(TAG, "UART2 contended — returning cached module state");
        getCachedState(out);
        return false;
    }

    out.linkOk = false;
    out.playState = 0xFF;
    out.device = 0x03;          // CHIRP: Bank 1 on flash, Banks 2–6 on SD
    out.totalTracks = m_totalTracks;
    out.currentTrack = m_lastTrack;   // last index sent via playTrack()

    while (s_chirpSerial.available()) {
        s_chirpSerial.read();
    }

    sendCommand("STAT:0");

    uint32_t startMs = millis();
    char line[64];
    uint32_t rxBytes = 0;
    uint16_t unparsableLines = 0;
    while ((uint32_t)(millis() - startMs) < 300u) {
        uint8_t n = readLine(line, (uint8_t)sizeof(line), 40u);
        if (n == 0) {
            continue;
        }
        rxBytes += n;
        uint8_t parsedPlayState = 0xFF;
        if (!parseChirpStatusLine(line, &parsedPlayState)) {
            ++unparsableLines;
            continue;
        }

        if (parsedPlayState == 0x01) {
            out.playState = 0x01;
            out.linkOk = true;
        } else if (parsedPlayState == 0x00) {
            if (out.playState != 0x01) {
                out.playState = 0x00;
            }
            out.linkOk = true;
        }
    }

    m_linkOk = out.linkOk;
    if (out.playState != 0xFF) {
        m_playState = out.playState;
    }

    if (!out.linkOk) {
        uint32_t now = millis();
        if ((uint32_t)(now - s_lastNoRspDiagMs) > 5000u) {
            if (rxBytes == 0) {
                PA_LOG_WARN(TAG,
                            "No CHIRP RX bytes during STAT query. Verify return path CHIRP TX->S2 RX and shared GND.");
            } else {
                PA_LOG_WARN(TAG,
                            "CHIRP RX activity seen (%u bytes) but no valid STAT line (%u unparsable lines).",
                            (unsigned)rxBytes, (unsigned)unparsableLines);
            }
            s_lastNoRspDiagMs = now;
        }
    }

    return out.linkOk;
}

// -----------------------------------------------------------------------------
// getCachedState()
// Returns the cached CHIRP state with no UART traffic.
// -----------------------------------------------------------------------------
void AudioDriverChirp::getCachedState(AudioModuleState& out) const {
    out.linkOk = m_linkOk;
    out.playState = m_playState;
    out.device = 0x03;          // CHIRP: Bank 1 flash-backed, Banks 2–6 SD
    out.totalTracks = m_totalTracks;
    out.currentTrack = m_lastTrack;   // last track index sent to playTrack()
}
