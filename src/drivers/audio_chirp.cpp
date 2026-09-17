// =============================================================================
// src/drivers/audio_chirp.cpp
//
// AudioDriver implementation for the CHIRP Audio Trigger board.
//
// TX commands are sent over software UART on PIN_AUDIO_TX at 9600 baud.
// RX responses are read from UART_PORT_AUDIO on PIN_AUDIO_RX for manifest,
// catalog, and status queries.
//
// Written for the artoo-esp32 posture, where UART_PORT_AUDIO IS the dome link's
// controller and there is no spare TX, so this driver carries its own
// domeUartOwnedBy(DOME_UART_DOME) guards. No P4 environment selects this
// backend, so those guards have never run on a board with
// PA_CAP_DEDICATED_AUDIO_UART and are left as they are deliberately: adding a
// capability branch that no build compiles would ship untested code (#254).
//
// CHIRP must be pre-configured to 9600 baud via CHIRP.INI on the SD card root:
//   #BAUD_RATE 9600
//
// Commands used:
//   playTrack(n)        -> "PLAY:n,1,A\n"  (Bank 1, Page A)
//   playTrackBanked()   -> "PLAY:n,bank,page\n"
//   stop()              -> "STOP\n"
//   setVolume(v)        -> "VOL:N\n" where N = v * 99 / 30
//   begin() bootstrap   -> optional "GMAN\n" for bank summary when UART2 RX is available
//   refreshCatalog()    -> "GMAN\n" + per-entry "GNME:bank,page,index\n"
//   queryModuleState()  -> "STAT:0\n", "STAT:1\n", "STAT:2\n" for stream activity
// =============================================================================

#include "audio_chirp.h"
#include "dome_link.h"

#include <Arduino.h>
#include <ctype.h>   // isalpha, isdigit, toupper
#include <stdio.h>   // snprintf, sscanf
#include <stdlib.h>  // strtoul
#include <string.h>  // strcmp, strncmp, strstr, strncpy

#include "audio_soft_uart_tx.h"  // shared soft-UART bit-bang primitives
#include "config.h"
#include "logging.h"

static HardwareSerial s_chirpSerial(UART_PORT_AUDIO);
static const char* TAG = "ChirpDrv";
static uint32_t s_lastNoRspDiagMs = 0;
static constexpr uint32_t CHIRP_GNME_WAIT_MS = 450u;
static constexpr uint32_t CHIRP_GNME_READLINE_MS = 120u;
static constexpr uint8_t CHIRP_RX_DRAIN_YIELD_BYTES = 32u;
// LIST is a whole console dump -- roughly 25 lines, ~1 s of it at 9600 baud --
// and all of it has to be consumed here, so the budget is per-dump rather than
// per-line. Two silent read windows in a row mean the module has stopped
// sending: LIST is written in one pass, so there are no 100 ms gaps inside it.
static constexpr uint32_t CHIRP_LIST_REPLY_MS = 1500u;
static constexpr uint32_t CHIRP_LIST_READLINE_MS = 100u;
static constexpr uint8_t CHIRP_LIST_QUIET_WINDOWS = 2u;
// The module's default stream count (CHIRP config.h DEFAULT_MAX_STREAMS 3), and
// this droid's. #MAX_STREAMS can be set 1-10 in CHIRP.INI and no command
// reports the value, so asking about three is asking about the default rather
// than discovering a configuration.
static constexpr uint8_t CHIRP_STAT_STREAM_COUNT = 3u;
// Per stream, so a full snapshot is bounded by three of these. handleStat()
// answers synchronously and prints straight to the UART, so the wait is one
// module loop pass plus the line itself: "STAT:playing," + a 63-character path
// + ",99" is 79 bytes, ~82 ms at 9600 baud.
static constexpr uint32_t CHIRP_STAT_REPLY_MS = 200u;
// streams[n].filename is char[64] in the module, so 79 characters is the
// longest reply it can print and a filled buffer means something else arrived.
static constexpr uint8_t CHIRP_STAT_LINE_MAX = 96u;
static constexpr size_t CHIRP_STAT_PATH_MAX = 64u;

// Production IO adapters
static void chirpWriteByte(uint8_t b)    { softUartTxByte(b); }
static int  chirpRxAvailable()           { return s_chirpSerial.available(); }
static int  chirpRxRead()                { return s_chirpSerial.read(); }
static void chirpDelayMs(uint32_t ms)    { vTaskDelay(pdMS_TO_TICKS(ms)); }
static uint32_t chirpMillisNow()         { return (uint32_t)millis(); }

static void configureChirpRx() {
    s_chirpSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, -1);
}

static const AudioSerialIO kChirpProductionIO {
    chirpWriteByte, chirpRxAvailable, chirpRxRead, chirpDelayMs, chirpMillisNow,
};

// Read one \r\n-terminated ASCII line via m_io. Stops at '\n', '\r' discarded.
// Yields Core 0 while waiting to keep WiFi/web tasks responsive.
uint8_t AudioDriverChirp::readLine(char* buf, uint8_t maxLen, uint32_t timeoutMs) {
    if (maxLen == 0) { return 0; }
    uint32_t start = m_io.millisNow();
    uint8_t pos = 0;
    while ((uint32_t)(m_io.millisNow() - start) < timeoutMs && pos < maxLen - 1u) {
        if (m_io.rxAvailable()) {
            char c = (char)m_io.rxRead();
            if (c == '\n') { break; }
            if (c != '\r') { buf[pos++] = c; }
        } else {
            m_io.delayMs(1);
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

// What a NAME reply that carried no page field reports as its page. The module
// omits the page in EVERY Bank 1 reply -- handleGnme() answers
// "NAME:1,,<index>,<basename>.wav" and ignores the page it was asked for when
// bank == 1 (CHIRP serial_commands.cpp) -- so this says "the reply named no
// page" and lets the catalog walk associate it with the bank being walked.
// Inventing 'A' here is what made the walk reject every name on a card whose
// Bank 1 page is B, at 450 ms of dead time per sound.
static constexpr char CHIRP_PAGE_OMITTED = '\0';

// "INVALID" is handleGnme()'s out-of-range answer, not a track called INVALID:
// it prints "NAME:%d,%c,%d,INVALID" when getSDFile() finds nothing at that
// index. Treat it as a missing name so the entry falls back to index_N.
static bool isMissingNameToken(const char* name) {
    return strcmp(name, "INVALID") == 0;
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
        if (bankEmptyPage <= 255u && indexEmptyPage <= 65535u && emptyPageName[0] != '\0' &&
            !isMissingNameToken(emptyPageName)) {
            *bankOut = (uint8_t)bankEmptyPage;
            *pageOut = CHIRP_PAGE_OMITTED;
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

    char pageVal = CHIRP_PAGE_OMITTED;
    if (*p == ',') {
        // Bank 1 NAME frames carry no page field: NAME:1,,<index>,<name>. This
        // branch only sees that form when the name was too long for the sscanf
        // above; either way the page stays unnamed rather than becoming 'A'.
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
    if (isMissingNameToken(nameOut)) {
        return false;
    }

    *bankOut = (uint8_t)bankVal;
    *pageOut = pageVal;
    *indexOut = (uint16_t)indexVal;
    return true;
}

// Parse GMAN output and cache all BANK lines.
// Returns true when any valid GMAN frame line was observed.
// Bank summary storage only (~2.3 KB). Needed by the bank/link path
// (loadManifestBanks), which runs at boot and is cheap to keep allocated, so it
// is held once allocated (no alloc/free churn on repeated link checks).
bool AudioDriverChirp::ensureBankStorage() {
    if (m_catalogBanks == nullptr) {
        m_catalogBanks = new (std::nothrow) AudioCatalogBank[AUDIO_CATALOG_MAX_BANKS];
    }
    return m_catalogBanks != nullptr;
}

// Per-track entry storage (~15.6 KB). Allocated ONLY when the full catalog is
// actually being filled (refreshCatalog, an explicit UI action with a live
// module). Keeping this out of the boot/link path is the heap-exhaustion fix:
// the bank/link path no longer transiently allocates 15.6 KB on a tight heap.
bool AudioDriverChirp::ensureEntryStorage(uint16_t needed) {
    if (needed == 0) needed = 1;
    if (needed > AUDIO_CATALOG_MAX_ENTRIES) needed = AUDIO_CATALOG_MAX_ENTRIES;
    // Reuse an existing allocation that already fits (a re-refresh of the same
    // module). Only (re)allocate to grow  --  sized to the module's actual track
    // count, not the fixed 300-entry worst case (~15.6 KB). Callers set
    // m_catalogCount = 0 before this, so a concurrent reader sees no entries
    // while the array is swapped.
    if (m_catalog != nullptr && m_catalogCapacity >= needed) {
        return true;
    }
    delete[] m_catalog;
    m_catalog = new (std::nothrow) AudioCatalogEntry[needed];
    m_catalogCapacity = (m_catalog != nullptr) ? needed : 0;
    return m_catalog != nullptr;
}

// -----------------------------------------------------------------------------
// queryBank1CountFromList()
// Bank 1's sound count, read back from the module's LIST dump.
//
// GMAN's reply is queued (CHIRP serial_commands.cpp sendSerialResponse ->
// serial_queue.cpp queueSerial2Message) into a ring of SERIAL2_QUEUE_SIZE 16
// slots -- 15 usable -- that drops the OLDEST message when it is full.
// handleGman() enqueues sdBankCount + 4 lines back to back, so a card with 13
// Bank 2-6 directories enqueues 17 and loses the first two: MDAT and BANK:1.
// BANK:1 is the only line that carries Bank 1's count, which is what the Sound
// page shows as Total tracks.
//
// LIST is the way back in: handleList() writes it with serial.println() and
// serial.printf() straight to the UART, bypassing that queue entirely, and its
// Bank 1 section opens with "Sounds: <n>". Only that one line is read. LIST is
// a console convenience rather than a second manifest protocol -- its headings
// are free to change, and it prints neither Bank 1's directory nor its active
// page -- so nothing else here is parsed out of it.
// -----------------------------------------------------------------------------
uint16_t AudioDriverChirp::queryBank1CountFromList() {
    sendCommand("LIST");
    cooperativeCatalogYield();

    uint16_t count = 0;
    uint8_t quietWindows = 0;
    char line[96];
    const uint32_t startMs = m_io.millisNow();

    while ((uint32_t)(m_io.millisNow() - startMs) < CHIRP_LIST_REPLY_MS) {
        uint8_t n = readLine(line, (uint8_t)sizeof(line), CHIRP_LIST_READLINE_MS);
        cooperativeCatalogYield();
        if (n == 0) {
            // n == 0 is either an empty line -- handleList() opens with
            // println("\n=== Bank 1 (Flash) ===\n"), so the first line of the
            // dump is empty -- or a window in which nothing arrived. Only the
            // second ends the dump, so check the reader rather than the line.
            if (m_io.rxAvailable() == 0 && ++quietWindows >= CHIRP_LIST_QUIET_WINDOWS) {
                break;
            }
            continue;
        }
        quietWindows = 0;

        if (count == 0 && strncmp(line, "Sounds:", 7) == 0) {
            char* end = nullptr;
            unsigned long parsed = strtoul(line + 7, &end, 10);
            if (end != line + 7 && parsed <= 65535u) {
                count = (uint16_t)parsed;
            }
        }
        // Keep reading past the count. The rest of the dump -- up to ten Bank 1
        // names and one line per SD bank -- is already on its way, and leaving
        // it in the buffer would spend the first GNME reply windows on it and
        // cost those entries their names.
    }

    return count;
}

bool AudioDriverChirp::loadManifestBanks(uint32_t timeoutMs, bool keepTotalTracks) {
    if (!ensureBankStorage()) {
        return false;
    }
    uint8_t drainedBytes = 0;
    while (m_io.rxAvailable()) {
        (void)m_io.rxRead();
        if (++drainedBytes >= CHIRP_RX_DRAIN_YIELD_BYTES) {
            cooperativeCatalogYield();
            drainedBytes = 0;
        }
    }

    sendCommand("GMAN");
    cooperativeCatalogYield();

    uint32_t startMs = m_io.millisNow();
    bool gotValidGmanLine = false;
    uint32_t rxBytes = 0;
    uint16_t bank1Count = keepTotalTracks ? m_totalTracks : 0;
    uint8_t catalogBankCount = 0;
    uint16_t droppedBankLines = 0;
    bool sawBank1 = false;
    bool sawMend = false;
    bool sawMdat = false;
    uint16_t declaredBankCount = 0;  // MDAT's count: Bank 1 plus every SD bank
    // Value-initialize catalog banks to respect declared defaults (page = 'A', etc.)
    for (uint8_t i = 0; i < AUDIO_CATALOG_MAX_BANKS; ++i) {
        m_catalogBanks[i] = AudioCatalogBank{};
    }
    char line[96];

    while ((uint32_t)(m_io.millisNow() - startMs) < timeoutMs) {
        uint8_t n = readLine(line, (uint8_t)sizeof(line), 80u);
        if (n == 0) {
            continue;
        }
        rxBytes += n;
        cooperativeCatalogYield();

        bool isGmanLine = (strncmp(line, "MDAT:", 5) == 0) || (strncmp(line, "BANK:", 5) == 0) ||
                          (strncmp(line, "MSUM:", 5) == 0) || (strcmp(line, "MEND") == 0);
        if (!isGmanLine) {
            continue;
        }
        gotValidGmanLine = true;

        if (strncmp(line, "MDAT:", 5) == 0) {
            // MDAT:<sdBankCount + 1> -- how many BANK rows handleGman() is
            // about to send, counting Bank 1. It is also the FIRST line it
            // enqueues, so it is the first one the module's reply queue drops:
            // seeing it at all is what lets the completeness check below be
            // more than a guess.
            char* mdatEnd = nullptr;
            unsigned long declared = strtoul(line + 5, &mdatEnd, 10);
            if (mdatEnd != line + 5 && declared <= 255u) {
                sawMdat = true;
                declaredBankCount = (uint16_t)declared;
            }
        }

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
                    sawBank1 = true;
                }
            }
        }

        if (strcmp(line, "MEND") == 0) {
            sawMend = true;
            break;
        }
    }

    if (!gotValidGmanLine) {
        if (rxBytes == 0) {
            PA_LOG_WARN(TAG,
                        "No CHIRP RX bytes during GMAN query. Verify CHIRP TX -> PIN_AUDIO_RX, common GND, and baud=9600.");
        } else {
            PA_LOG_WARN(TAG,
                        "CHIRP RX activity seen (%u bytes) but no valid GMAN frame.",
                        (unsigned)rxBytes);
        }
        // No usable bank summary (no module, contested UART2, or garbled RX).
        // The bank array (~2.3 KB) is small and held to avoid alloc/free churn on
        // repeated link checks; the large entry array is never touched here.
        return false;
    }
    if (droppedBankLines > 0) {
        PA_LOG_WARN(TAG,
                    "GMAN reported more banks/pages than supported (max=%u). Ignoring %u extra BANK lines.",
                    (unsigned)AUDIO_CATALOG_MAX_BANKS, (unsigned)droppedBankLines);
    }

    // The manifest answered, but without the one line that carries Bank 1's
    // count -- the second casualty of the module's 15-slot reply queue on a
    // card with 13 or more Bank 2-6 directories. Left alone, Total tracks
    // reads 0 and the catalog walk never asks Bank 1 for a single name, on a
    // card and a link that are both healthy. LIST can still answer, so ask it.
    // Only on this path: it costs up to CHIRP_LIST_REPLY_MS, including at boot.
    bool bank1Recovered = false;
    if (!sawBank1) {
        const uint16_t recovered = queryBank1CountFromList();
        if (recovered > 0 && catalogBankCount < AUDIO_CATALOG_MAX_BANKS) {
            // Insert at the front so the bank order stays 1,2,3... for the
            // catalog walk and for the Sound page's bank tabs.
            for (uint8_t i = catalogBankCount; i > 0; --i) {
                m_catalogBanks[i] = m_catalogBanks[i - 1];
            }
            m_catalogBanks[0] = AudioCatalogBank{};
            m_catalogBanks[0].bank = 1;
            m_catalogBanks[0].count = recovered;
            // dirName stays empty and page keeps its declared 'A' default as a
            // WIRE PLACEHOLDER, not as an observation: LIST prints neither Bank
            // 1's directory nor its active page, and handleGnme() and
            // handlePlay() both ignore the page they are given when bank == 1
            // (CHIRP serial_commands.cpp), so 'A' asks the right question of
            // the module without claiming the card's active page is A. A
            // GNME:1,%c,%u built from a NUL page would truncate at the page,
            // which is why the placeholder is a letter. Never present it as an
            // observed page; the empty dirName is what says "not observed".
            ++catalogBankCount;
            bank1Count = recovered;
            bank1Recovered = true;
        }
    }

    // Complete means every bank the module said it would send arrived. A
    // recovered Bank 1 count is not that proof: MDAT went missing before
    // BANK:1 did, so on a truncated reply there is no declared total left to
    // check the rows against, and a dropped SD row would look identical.
    m_manifestComplete = sawMend && sawMdat && (catalogBankCount == declaredBankCount);
    if (!m_manifestComplete) {
        PA_LOG_WARN(TAG,
                    "GMAN manifest incomplete: %u BANK rows, MDAT %s, MEND %s, Bank 1 count %s. "
                    "The module's 15-slot reply queue drops its oldest line, so a card with 13 or "
                    "more Bank 2-6 directories loses MDAT and BANK:1.",
                    (unsigned)catalogBankCount,
                    sawMdat ? "seen" : "lost", sawMend ? "seen" : "lost",
                    sawBank1 ? "from BANK:1"
                             : (bank1Recovered ? "recovered from LIST" : "unavailable"));
    }

    m_catalogBankCount = catalogBankCount;
    m_totalTracks = bank1Count;
    return true;
}

// -----------------------------------------------------------------------------
// begin()
// Configures CHIRP TX and applies boot volume. RX catalog discovery is
// opportunistic because UART2 is shared with the higher-priority DomeLink.
// -----------------------------------------------------------------------------
bool AudioDriverChirp::begin(uint8_t vol) {
    if (!m_io.writeByte) { m_io = kChirpProductionIO; }

    // Hardware TX init  --  no-op in native test builds.
    softUartTxBegin();

    // Reset to known sentinel values before optional RX work so cached state
    // is unambiguously "uninitialized" rather than defaulting to "idle" (0x00).
    m_totalTracks = 0;
    m_linkOk = false;
    m_playState = 0xFF;
    m_currentTrack = 0;
    m_catalogReady = false;
    m_catalogCount = 0;
    m_catalogBankCount = 0;

    // CHIRP boots, mounts SD, and optionally syncs Bank 1 to flash; 2 s covers
    // most cases. First boot after SD card change may need more time.
    m_io.delayMs(2000);

    // Apply NVS-configured boot volume before any playback.
    setVolume(vol);

    if (domeUartAcquire(DOME_UART_AUDIO)) {
        configureChirpRx();
        m_linkOk = loadManifestBanks(1500u, false);
        domeUartRelease(DOME_UART_AUDIO);
    } else {
        PA_LOG_INFO(TAG,
                    "CHIRP RX catalog discovery skipped: DomeLink is using UART2; playback commands remain available");
    }

    PA_LOG_INFO(TAG, "init - vol=%u Bank1 sounds=%u banks=%u link=%s", (unsigned)vol,
                (unsigned)m_totalTracks, (unsigned)m_catalogBankCount,
                m_linkOk ? "OK" : "no response");
    return true;
}

// -----------------------------------------------------------------------------
// sendCommand()
// Transmit a null-terminated ASCII string followed by '\n'.
// -----------------------------------------------------------------------------
void AudioDriverChirp::sendCommand(const char* cmd) {
    for (const char* p = cmd; p && *p; ++p) { m_io.writeByte((uint8_t)*p); }
    m_io.writeByte('\n');
}

void AudioDriverChirp::cooperativeCatalogYield() {
    if (m_io.delayMs != nullptr) {
        m_io.delayMs(1);
    }
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
    // A bare STOP stops every stream (handleStop()'s empty-argument path), so
    // idle and "no current track" are observations of what we just did, not
    // optimism. Leaving the old index behind is what kept the Sound page naming
    // a sound that had already stopped.
    m_currentTrack = 0;
    m_playState = 0x00;
}

// -----------------------------------------------------------------------------
// setVolume()
// Set global volume. vol is 0-30 (clamped by AudioTask before this call).
// Scales to CHIRP native range 0-99: N = (vol * CHIRP_VOL_MAX) / 30.
// Format: "VOL:N\n"
// -----------------------------------------------------------------------------
void AudioDriverChirp::setVolume(uint8_t vol) {
    uint8_t chirpVol = (uint8_t)((uint16_t)vol * CHIRP_VOL_MAX / 30u);
    char cmd[10];
    snprintf(cmd, sizeof(cmd), "VOL:%u", (unsigned)chirpVol);
    sendCommand(cmd);
}

bool AudioDriverChirp::refreshCatalog() {
    if (domeUartOwnedBy(DOME_UART_DOME)) {
        return false;
    }
    configureChirpRx();

    if (!loadManifestBanks(2500u, false)) {
        m_catalogReady = false;
        return false;
    }

    // Size the entry array to the module's ACTUAL track count (sum of bank
    // counts), not the fixed 300-entry worst case  --  a typical module has far
    // fewer, so this allocates only what it needs (issue #8 heap hardening).
    uint32_t total = 0;
    for (uint8_t i = 0; i < m_catalogBankCount; ++i) {
        total += m_catalogBanks[i].count;
    }
    // Mark empty before (re)allocating so a concurrent /api/audio/catalog reader
    // sees no entries while the array is swapped.
    m_catalogCount = 0;
    m_catalogReady = false;
    if (!ensureEntryStorage((uint16_t)(total > AUDIO_CATALOG_MAX_ENTRIES
                                           ? AUDIO_CATALOG_MAX_ENTRIES : total))) {
        PA_LOG_WARN(TAG, "catalog entry storage alloc failed (%u entries); refresh skipped",
                    (unsigned)total);
        return false;
    }

    uint16_t missingNameCount = 0;

    for (uint8_t bankIdx = 0; bankIdx < m_catalogBankCount; ++bankIdx) {
        const AudioCatalogBank& bank = m_catalogBanks[bankIdx];
        for (uint16_t soundIndex = 1; soundIndex <= bank.count; ++soundIndex) {
            if (m_catalogCount >= m_catalogCapacity) {
                PA_LOG_WARN(TAG, "catalog entry cap reached (%u)",
                            (unsigned)m_catalogCapacity);
                m_catalogReady = true;
                return true;
            }

            char cmd[28];
            snprintf(cmd, sizeof(cmd), "GNME:%u,%c,%u", (unsigned)bank.bank, bank.page,
                     (unsigned)soundIndex);
            sendCommand(cmd);
            cooperativeCatalogYield();

            bool gotName = false;
            uint32_t startMs = m_io.millisNow();
            char line[112];

            while ((uint32_t)(m_io.millisNow() - startMs) < CHIRP_GNME_WAIT_MS) {
                uint8_t n = readLine(line, (uint8_t)sizeof(line), CHIRP_GNME_READLINE_MS);
                if (n == 0) {
                    continue;
                }
                cooperativeCatalogYield();

                uint8_t respBank = 0;
                char respPage = 'A';
                uint16_t respIndex = 0;
                char fileName[sizeof(m_catalog[0].name)] = {0};
                if (!parseNameLine(line, &respBank, &respPage, &respIndex, fileName,
                                   sizeof(fileName))) {
                    continue;
                }
                // A reply that named no page belongs to the bank we asked
                // about, and only bank 1 ever omits it: a genuinely pageless SD
                // directory has numeric page 0, which handleGnme() prints as a
                // stray comma ("NAME:2,,,3,file") that parseNameLine() rejects
                // outright. That form is addressed by page 0 in the module and
                // by no page here, so tolerating it would not make the sound
                // playable -- it stays rejected.
                const bool pageMatches = (respPage == bank.page) ||
                                         (respPage == CHIRP_PAGE_OMITTED && bank.bank == 1);
                if (respBank != bank.bank || !pageMatches || respIndex != soundIndex) {
                    continue;
                }

                AudioCatalogEntry& entry = m_catalog[m_catalogCount++];
                entry.bank = respBank;
                // The bank's own page, not the reply's: an omitted page is the
                // page we were walking, whatever letter that is.
                entry.page = bank.page;
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
            cooperativeCatalogYield();
        }
    }

    m_catalogReady = true;
    PA_LOG_INFO(TAG, "catalog refresh done: banks=%u entries=%u missing_names=%u manifest=%s",
                (unsigned)m_catalogBankCount, (unsigned)m_catalogCount,
                (unsigned)missingNameCount,
                m_manifestComplete ? "complete" : "INCOMPLETE");
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
// Status snapshot
// -----------------------------------------------------------------------------

// Case-insensitive whole-string equality. Written out rather than calling
// strcasecmp(), which is POSIX <strings.h> -- the same rule
// src/console/console_module.cpp states for its own key checks. Card names come
// off FAT, where case is not identity, and the module's own Bank 1 grouping
// compares with strcasecmp() (CHIRP file_management.cpp scanBank1).
static bool equalsIgnoringCase(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

// A name with its last extension removed. Bank 1 catalog names carry the
// module's forced ".wav" whatever the file on the card really is: handleGnme()
// prints "%s.wav" over a basename whose real extension was already stripped
// (finding 13.4), so this droid's "general01.wav" is a .mp3 on the card.
static void nameWithoutExtension(const char* name, char* out, size_t outLen) {
    size_t len = strlen(name);
    const char* dot = strrchr(name, '.');
    if (dot != nullptr && dot != name) {
        len = (size_t)(dot - name);
    }
    if (len >= outLen) {
        len = outLen - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';
}

// The Bank 1 variant group a file belongs to, derived the way the module
// derives it in scanBank1() (CHIRP file_management.cpp): the text before the
// first '_' that is followed by a digit marks a variant set, otherwise the name
// without its extension. "general01_02.mp3" and "general01.mp3" both fold to
// "general01", which is what handleGnme() reports as "general01.wav".
//
// The module stores that basename in char[16], so a group longer than 15
// characters is truncated on its side and will not compare equal here. That
// leaves the playing sound unidentified, which is the honest answer; guessing
// by prefix would risk naming the wrong sound.
static void bank1GroupFromFileName(const char* file, char* out, size_t outLen) {
    const char* underscore = strchr(file, '_');
    if (underscore != nullptr && isdigit((unsigned char)underscore[1])) {
        size_t len = (size_t)(underscore - file);
        if (len >= outLen) {
            len = outLen - 1;
        }
        memcpy(out, file, len);
        out[len] = '\0';
        return;
    }
    nameWithoutExtension(file, out, outLen);
}

// One direct STAT reply: "STAT:playing,<path>,<volume 0-99>" or "STAT:idle,,0",
// printed straight to the UART by handleStat() rather than queued.
//
// Everything else returns false, including the queued "S:<n>,ply,<vol>"
// notification: handlePlay() enqueues that BEFORE startStream(), so it survives
// a play that then failed on a missing file, and the previous parser read it as
// status and made playback sticky inside a poll.
static bool parseChirpStatReply(const char* line, bool* playingOut, char* fileOut,
                                size_t fileLen) {
    if (line == nullptr || playingOut == nullptr || fileOut == nullptr || fileLen == 0) {
        return false;
    }
    if (strncmp(line, "STAT:", 5) != 0) {
        return false;
    }
    const char* state = line + 5;

    if (strncmp(state, "idle,", 5) == 0) {
        *playingOut = false;
        fileOut[0] = '\0';
        return true;
    }
    if (strncmp(state, "playing,", 8) != 0) {
        return false;
    }

    *playingOut = true;
    fileOut[0] = '\0';
    const char* path = state + 8;
    // The volume is the last field, so split on the LAST comma: a card filename
    // may contain one of its own.
    const char* volumeComma = strrchr(path, ',');
    size_t len = (volumeComma != nullptr) ? (size_t)(volumeComma - path) : strlen(path);
    if (len == 0 || len >= fileLen) {
        // Nothing, or more than the module can hold: an observation this long
        // did not come off a char[64] filename intact, so it identifies
        // nothing. The reply still proves the stream is playing.
        return true;
    }
    memcpy(fileOut, path, len);
    fileOut[len] = '\0';
    return true;
}

// -----------------------------------------------------------------------------
// catalogIndexForPath()
// The catalog entry a reported playback path identifies, or 0 for none.
//
// handlePlay() builds the path STAT reports: "/flash/<file>" when the module
// syncs Bank 1 to flash, "/<bank1Dir>/<file>" when it plays Bank 1 off the
// card, and "/<bankDir>/<file>" for banks 2-6 (CHIRP serial_commands.cpp;
// startStream() copies it into streams[n].filename). The catalog holds leaf
// names, so the directory selects the bank and the leaf selects the entry --
// which is also why two banks holding the same filename cannot be confused.
//
// A Bank 1 row recovered from LIST has no directory name, so only its
// "/flash/..." form can be identified; that is a consequence of the module
// never printing the directory, not a guess worth making.
// -----------------------------------------------------------------------------
uint16_t AudioDriverChirp::catalogIndexForPath(const char* path) const {
    if (path == nullptr || path[0] != '/' || m_catalog == nullptr || m_catalogCount == 0 ||
        m_catalogBanks == nullptr) {
        return 0;
    }

    const char* dirStart = path + 1;
    const char* slash = strchr(dirStart, '/');
    if (slash == nullptr || slash[1] == '\0') {
        return 0;  // not the "/<dir>/<leaf>" shape handlePlay() builds
    }
    const size_t dirLen = (size_t)(slash - dirStart);
    const char* leaf = slash + 1;

    char dirName[sizeof(m_catalogBanks[0].dirName)];
    if (dirLen == 0 || dirLen >= sizeof(dirName)) {
        return 0;  // no bank row could name a directory this long
    }
    memcpy(dirName, dirStart, dirLen);
    dirName[dirLen] = '\0';

    uint8_t wantBank = 0;
    char wantPage = 'A';
    if (equalsIgnoringCase(dirName, "flash")) {
        wantBank = 1;  // the module's flash mirror of the active Bank 1 page
    } else {
        for (uint8_t i = 0; i < m_catalogBankCount; ++i) {
            const AudioCatalogBank& bank = m_catalogBanks[i];
            if (bank.dirName[0] != '\0' && equalsIgnoringCase(bank.dirName, dirName)) {
                wantBank = bank.bank;
                wantPage = bank.page;
                break;
            }
        }
    }
    if (wantBank == 0) {
        return 0;
    }

    char wanted[sizeof(m_catalog[0].name)];
    if (wantBank == 1) {
        bank1GroupFromFileName(leaf, wanted, sizeof(wanted));
    } else {
        strncpy(wanted, leaf, sizeof(wanted) - 1);
        wanted[sizeof(wanted) - 1] = '\0';
    }

    uint16_t index = 0;
    uint8_t matches = 0;
    for (uint16_t i = 0; i < m_catalogCount; ++i) {
        const AudioCatalogEntry& entry = m_catalog[i];
        if (entry.bank != wantBank) {
            continue;
        }
        // Only one Bank 1 page is active at a time, so its page is not part of
        // the address here.
        if (wantBank != 1 && entry.page != wantPage) {
            continue;
        }
        bool same;
        if (wantBank == 1) {
            char group[sizeof(entry.name)];
            nameWithoutExtension(entry.name, group, sizeof(group));
            same = equalsIgnoringCase(group, wanted);
        } else {
            same = equalsIgnoringCase(entry.name, wanted);
        }
        if (same) {
            ++matches;
            index = entry.index;
        }
    }

    // More than one candidate is not an identification.
    return (matches == 1) ? index : 0;
}

bool AudioDriverChirp::queryModuleState(AudioModuleState& out) {
    out.linkOk = false;
    out.playState = 0xFF;
    out.device = 0x03;          // CHIRP: Bank 1 on flash, Banks 2-6 on SD
    out.totalTracks = m_totalTracks;
    out.currentTrack = 0;
    out.missingTrack = 0;

    // Guard: DomeLink has priority on UART2; return cached state unchanged.
    if (domeUartOwnedBy(DOME_UART_DOME)) {
        out.linkOk = m_linkOk;
        out.playState = m_playState;
        out.currentTrack = m_currentTrack;
        return out.linkOk;
    }

    configureChirpRx();

    while (m_io.rxAvailable()) { (void)m_io.rxRead(); }

    // Every default stream, asked one at a time. Stream 0 going idle while 1 or
    // 2 keep playing is the live lie: a beep that ended under a running piece
    // of music used to report the whole module idle.
    //
    // STAT replies carry no stream number, so this counts them rather than
    // attributing them: the module answers each query exactly once, so three
    // valid idle replies mean three streams answered idle, whichever query each
    // one belongs to. A stream that does not answer is NOT an idle stream.
    uint8_t playingReplies = 0;
    uint8_t idleReplies = 0;
    uint32_t rxBytes = 0;
    uint16_t unparsableLines = 0;
    char playingPath[CHIRP_STAT_PATH_MAX] = {0};

    for (uint8_t stream = 0; stream < CHIRP_STAT_STREAM_COUNT; ++stream) {
        char cmd[12];
        snprintf(cmd, sizeof(cmd), "STAT:%u", (unsigned)stream);
        sendCommand(cmd);

        const uint32_t startMs = m_io.millisNow();
        char line[CHIRP_STAT_LINE_MAX];
        while (true) {
            const uint32_t elapsed = (uint32_t)(m_io.millisNow() - startMs);
            if (elapsed >= CHIRP_STAT_REPLY_MS) {
                break;  // this stream did not answer within its own deadline
            }
            // Wait for the whole line rather than a fixed slice of it: a
            // 63-character path needs ~80 ms to arrive at 9600 baud, and half a
            // path left in the buffer poisons the next stream's reply.
            // readLine() returns as soon as it sees '\n'.
            uint8_t n = readLine(line, (uint8_t)sizeof(line), CHIRP_STAT_REPLY_MS - elapsed);
            if (n == 0) {
                continue;
            }
            rxBytes += n;
            if (n >= (uint8_t)(sizeof(line) - 1u)) {
                ++unparsableLines;  // filled the buffer with no terminator
                continue;
            }

            bool playing = false;
            char file[CHIRP_STAT_PATH_MAX] = {0};
            if (!parseChirpStatReply(line, &playing, file, sizeof(file))) {
                ++unparsableLines;
                continue;  // a queued S:/PACK: line or an ERR: answer, not status
            }
            if (playing) {
                ++playingReplies;
                if (playingPath[0] == '\0') {
                    strncpy(playingPath, file, sizeof(playingPath) - 1);
                    playingPath[sizeof(playingPath) - 1] = '\0';
                }
            } else {
                ++idleReplies;
            }
            break;
        }
    }

    if (playingReplies > 0) {
        out.playState = 0x01;
        out.linkOk = true;
        // Unidentified while playing is honest; a stale echo is not.
        out.currentTrack = catalogIndexForPath(playingPath);
    } else if (idleReplies >= CHIRP_STAT_STREAM_COUNT) {
        out.playState = 0x00;
        out.linkOk = true;
        out.currentTrack = 0;
    } else if (idleReplies > 0) {
        // Some streams answered idle and at least one did not, so silence is
        // not established. The module is clearly alive; its play state and
        // current track stay as they were rather than reporting an idle nobody
        // observed.
        out.linkOk = true;
        out.playState = m_playState;
        out.currentTrack = m_currentTrack;
    }

    m_linkOk = out.linkOk;
    if (out.playState != 0xFF) {
        m_playState = out.playState;
        m_currentTrack = out.currentTrack;
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
    out.device = 0x03;          // CHIRP: Bank 1 flash-backed, Banks 2-6 SD
    out.totalTracks = m_totalTracks;
    out.currentTrack = m_currentTrack;  // last OBSERVED playback, cleared by stop()
    out.missingTrack = 0;
}

// -----------------------------------------------------------------------------
// classifyRxStatus()
// CHIRP RX shares UART2 with DomeLink. When DomeLink owns the bus, a false
// linkOk is BLOCKED rather than NO_RESPONSE.
// -----------------------------------------------------------------------------
AudioRxStatus AudioDriverChirp::classifyRxStatus(bool linkOk) const {
    if (linkOk) {
        return AUDIO_RX_AVAILABLE;
    }
    if (domeUartOwnedBy(DOME_UART_DOME)) {
        return AUDIO_RX_BLOCKED_BY_DOME_UART;
    }
    return AUDIO_RX_NO_RESPONSE;
}
