// =============================================================================
// src/drivers/audio_soft_uart.cpp
//
// DY-SV5W compatibility driver (bring-up mode)
//
// Why this exists:
//   DY-SV5W boards in the wild use at least two UART dialects depending on
//   firmware revision/vendor clone. To de-risk hardware bring-up we broadcast
//   commands in both known dialects.
//
// Transport:
//   HardwareSerial(2) remapped to S2 pins:
//     TX GPIO26 (PIN_AUDIO_TX)
//     RX GPIO35 (PIN_AUDIO_RX)
//   9600 8N1
//
// Dialect A (end marker):
//   0xAA [CMD] [LEN] [DATA...] 0xAB
//   Common opcodes: play=0x0D stop=0x03 vol=0x0A seldev=0x11
//
// Dialect B (checksum):
//   0xAA [CMD] [LEN] [DATA...] [CRC]
//   CRC=(sum all bytes incl 0xAA) & 0xFF
//   Common opcodes: play=0x06 stop=0x02 vol=0x13 seldev=0x10
//
// This dual-send path is temporary for hardware validation and can be reduced
// to a single dialect once confirmed on this exact module.
// =============================================================================

#include "audio_soft_uart.h"

#include <Arduino.h>

#include "config.h"

static HardwareSerial s_audioSerial(2);

static constexpr uint8_t FRAME_START = 0xAA;

// -----------------------------------------------------------------------------
// Frame helpers
// -----------------------------------------------------------------------------
static void sendFrameEndMarker(const uint8_t* body, uint8_t bodyLen) {
    s_audioSerial.write(FRAME_START);
    s_audioSerial.write(body, bodyLen);
    s_audioSerial.write((uint8_t)0xAB);
}

static void sendFrameChecksum(const uint8_t* body, uint8_t bodyLen) {
    uint8_t crc = FRAME_START;
    s_audioSerial.write(FRAME_START);
    for (uint8_t i = 0; i < bodyLen; i++) {
        s_audioSerial.write(body[i]);
        crc = (uint8_t)(crc + body[i]);
    }
    s_audioSerial.write(crc);
}

void AudioDriverSoftUart::sendFrame(const uint8_t* data, uint8_t len) {
    // Compatibility mode: send both dialects back-to-back.
    sendFrameEndMarker(data, len);
    delayMicroseconds(800);
    sendFrameChecksum(data, len);
}

// -----------------------------------------------------------------------------
// begin()
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::begin() {
    s_audioSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, PIN_AUDIO_TX);

    delay(1500);

    // Clear stale bytes
    while (s_audioSerial.available()) {
        (void)s_audioSerial.read();
    }

    // Select SD card using both known opcode variants
    uint8_t selA[] = {0x11, 0x01, 0x01};  // dialect A opcode
    uint8_t selB[] = {0x10, 0x01, 0x01};  // dialect B opcode
    sendFrame(selA, sizeof(selA));
    delay(20);
    sendFrame(selB, sizeof(selB));

    delay(200);
}

// -----------------------------------------------------------------------------
// playTrack()
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::playTrack(uint16_t track) {
    if (track == 0) {
        return;
    }
    uint8_t hi = static_cast<uint8_t>(track >> 8);
    uint8_t lo = static_cast<uint8_t>(track & 0xFF);

    // Known play opcodes across DY-SV5W firmware variants
    uint8_t playA[] = {0x0D, 0x02, hi, lo};
    uint8_t playB[] = {0x06, 0x02, hi, lo};

    sendFrame(playA, sizeof(playA));
    delay(20);
    sendFrame(playB, sizeof(playB));
}

// -----------------------------------------------------------------------------
// stop()
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::stop() {
    uint8_t stopA[] = {0x03, 0x00};
    uint8_t stopB[] = {0x02, 0x00};
    sendFrame(stopA, sizeof(stopA));
    delay(20);
    sendFrame(stopB, sizeof(stopB));
}

// -----------------------------------------------------------------------------
// setVolume()
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::setVolume(uint8_t vol) {
    // Known set-volume opcodes across DY-SV5W firmware variants
    uint8_t volA[] = {0x0A, 0x01, vol};
    uint8_t volB[] = {0x13, 0x01, vol};

    sendFrame(volA, sizeof(volA));
    delay(20);
    sendFrame(volB, sizeof(volB));
}
