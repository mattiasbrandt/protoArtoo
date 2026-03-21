#include "sbus_decoder.h"

#include <Arduino.h>

SbusDecoder::SbusDecoder()
    : _uart(nullptr), _data(), _newData(false), _frameIndex(0), _lastFrameMs(0) {
    memset(_frameBuffer, 0, sizeof(_frameBuffer));
}

bool SbusDecoder::begin(HardwareSerial* uart, int rxPin) {
    if (uart == nullptr) {
        return false;
    }
    _uart = uart;

    // Initialize UART with hardware inversion
    // SBUS: 100000 baud, 8 data bits, even parity, 2 stop bits, inverted
    _uart->begin(100000, SERIAL_8E2, rxPin, -1, true);

    _resetFrame();
    return true;
}

void SbusDecoder::end() {
    if (_uart != nullptr) {
        _uart->end();
        _uart = nullptr;
    }
    _newData = false;
    _lastFrameMs = 0;
    _resetFrame();
}

bool SbusDecoder::read() {
    if (_uart == nullptr || !_uart->available()) {
        return false;
    }

    _newData = false;

    unsigned long now = millis();
    if (_frameIndex > 0 && (now - _lastFrameMs) > 5) {
        _resetFrame();
    }

    // Read all available bytes
    while (_uart->available()) {
        uint8_t byte = _uart->read();

        // Look for frame start (0x0F)
        if (_frameIndex == 0 && byte != 0x0F) {
            continue;  // Not a frame start, skip
        }

        // Store byte in buffer
        if (_frameIndex < 25) {
            _frameBuffer[_frameIndex++] = byte;
            _lastFrameMs = now;
        }

        // Check if we have a complete frame
        if (_frameIndex >= 25) {
            // Validate frame: header, footer, and length
            if (_frameBuffer[0] == 0x0F && _frameBuffer[24] == 0x00) {
                _parseChannels(_frameBuffer);
                _newData = true;
            }
            _resetFrame();
        }
    }

    return _newData;
}

void SbusDecoder::_parseChannels(const uint8_t* frame) {
    // Parse 16 channels (11 bits each) from bytes 1-22
    // Channels are packed LSB first
    for (int ch = 0; ch < 16; ch++) {
        int bitIndex = ch * 11;
        int byteIndex = 1 + (bitIndex / 8);
        int bitOffset = bitIndex % 8;

        uint16_t value = frame[byteIndex] >> bitOffset;
        value |= ((uint16_t)frame[byteIndex + 1]) << (8 - bitOffset);
        if (bitOffset > 5) {
            value |= ((uint16_t)frame[byteIndex + 2]) << (16 - bitOffset);
        }
        _data.ch[ch] = value & 0x7FF;  // Mask to 11 bits
    }

    // Parse flags byte (byte 23)
    uint8_t flags = frame[23];
    _data.ch17 = (flags & 0x01) != 0;
    _data.ch18 = (flags & 0x02) != 0;
    _data.lost_frame = (flags & 0x04) != 0;
    _data.failsafe = (flags & 0x08) != 0;
}

void SbusDecoder::_resetFrame() {
    _frameIndex = 0;
    memset(_frameBuffer, 0, sizeof(_frameBuffer));
}
