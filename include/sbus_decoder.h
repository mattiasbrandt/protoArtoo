// =============================================================================
// include/sbus_decoder.h
//
// UART-based SBUS decoder for ESP32 with hardware inversion support.
// Uses ESP32's built-in UART hardware with inverted signal support.
//
// Hardware: HOTRC SBUS-A receivers on GPIO 15 (drive) and GPIO 13 (dome).
// Protocol: 25-byte frame at 100 Hz. Channels 172–1811, center ~992.
//
// Frame format: [0x0F][22 data bytes][flags byte][0x00]
//   16 channels × 11 bits packed LSB-first into bytes 1–22.
//   Flags byte (index 23): bit0=CH17, bit1=CH18, bit2=lost_frame, bit3=failsafe.
//
// UART Configuration:
//   Baud: 100000 (100 kbaud)
//   Data bits: 8
//   Parity: Even
//   Stop bits: 2
//   Inversion: Hardware inverted (required for SBUS)
// =============================================================================
#pragma once
#include <HardwareSerial.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// SbusData — decoded SBUS frame
// -----------------------------------------------------------------------------
struct SbusData {
    uint16_t ch[16];
    bool ch17;
    bool ch18;
    bool lost_frame;
    bool failsafe;
};

class SbusDecoder {
   public:
    SbusDecoder();
    // WARNING — UART1 conflict with DriveTask.
    // Calling begin(&Serial1, ...) fully reconfigures UART1 (baud divisor, parity,
    // stop bits, inversion flag, GPIO matrix). DriveTask also claims Serial1 for
    // hoverboard drive (115200 8N1). The second begin() wins and silently breaks
    // the other task's UART configuration. SBUS1 on Serial1 and hoverboard on
    // Serial1 cannot coexist on the current PCB hardware.
    // Structural fix: RMT peripheral (Phase 5 T01 — tasks/phase5-tasks.md).
    bool begin(HardwareSerial* uart, int rxPin);
    bool read();
    void end();
    SbusData data() const {
        return _data;
    }
    bool failsafe() const {
        return _data.failsafe;
    }
    bool lostFrame() const {
        return _data.lost_frame;
    }
    bool isInitialized() const {
        return _uart != nullptr;
    }

   private:
    HardwareSerial* _uart;
    SbusData _data;
    bool _newData;
    uint8_t _frameBuffer[25];
    int _frameIndex;
    uint32_t _lastFrameMs;
    void _parseChannels(const uint8_t* frame);
    void _resetFrame();
};
