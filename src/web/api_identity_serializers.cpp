// =============================================================================
// src/web/api_identity_serializers.cpp
//
// Pure JSON serialization helper for identity endpoints.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_identity.h"

#include "config.h"

#include <cstdarg>
#include <cstdio>

namespace {

class IdentityJsonWriter {
public:
    IdentityJsonWriter(char* buffer, size_t capacity)
        : buffer_(buffer), capacity_(capacity), length_(0), valid_(buffer != nullptr && capacity > 0) {
        if (valid_) {
            buffer_[0] = '\0';
        }
    }

    void append(const char* format, ...) {
        if (!valid_) {
            return;
        }
        va_list args;
        va_start(args, format);
        const int written = vsnprintf(buffer_ + length_, capacity_ - length_, format, args);
        va_end(args);
        if (written < 0 || static_cast<size_t>(written) >= capacity_ - length_) {
            valid_ = false;
            buffer_[capacity_ - 1] = '\0';
            return;
        }
        length_ += static_cast<size_t>(written);
    }

    bool valid() const { return valid_; }

private:
    char* buffer_;
    size_t capacity_;
    size_t length_;
    bool valid_;
};

constexpr const char* boardVariantId() {
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
    return "artoo_esp32";
#elif PA_BOARD == PA_BOARD_FIREBEETLE2
    return "firebeetle2";
#else
    #error "PA_BOARD value not recognized by identity serializer"
#endif
}

}  // namespace

bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName) {
    if (buf == nullptr || bufSize == 0 || droidName == nullptr) {
        return false;
    }

    IdentityJsonWriter writer(buf, bufSize);
    writer.append("{\"droidName\":\"%s\",\"mdnsUseName\":%s,\"board\":\"%s\","
                  "\"board_capabilities\":{",
                  droidName, mdnsUseName ? "true" : "false", boardVariantId());

    bool first = true;
#define PA_BOARD_CAPABILITY(name)                                                \
    writer.append("%s\"%s\":%s", first ? "" : ",", #name, (name) ? "true" : "false"); \
    first = false;
#include "board_capabilities.inc"
#undef PA_BOARD_CAPABILITY

    // Board Lanes sit beside the Gates deliberately (CONTEXT.md "Board Lane"):
    // a Gate answers whether the board can support something, a Lane answers
    // where it is routed, and an operator surface needs both from one payload
    // rather than keeping its own copy of one board's wiring.
    writer.append("},\"board_lanes\":{");
    first = true;
#define PA_BOARD_LANE(name, uart_port, tx_pin, rx_pin)                                    \
    writer.append("%s\"%s\":{\"uart\":%u,\"tx\":%u,\"rx\":%u}", first ? "" : ",", #name,  \
                  (unsigned)(uart_port), (unsigned)(tx_pin), (unsigned)(rx_pin));         \
    first = false;
#include "board_lanes.inc"
#undef PA_BOARD_LANE

    writer.append("},\"build_flags\":{");
    first = true;
#define PA_BUILD_FLAG(name)                                                      \
    writer.append("%s\"%s\":%s", first ? "" : ",", #name, (name) ? "true" : "false"); \
    first = false;
#include "build_flags.inc"
#undef PA_BUILD_FLAG
    writer.append("}}");
    return writer.valid();
}
