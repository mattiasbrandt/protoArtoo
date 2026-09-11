// =============================================================================
// src/web/api_identity_serializers.cpp
//
// Pure JSON serialization helpers for the identity endpoints: the compile-time
// Feature Availability manifest, and the Component Registry lineup.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_identity.h"

#include "component_registry.h"
#include "config.h"
#include "web_json_slice_writer.h"

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

// -----------------------------------------------------------------------------
// The Component Registry payload (ADR 0042 as amended 2026-09-09).
//
// Identity is the runtime source of the lineup. Every row travels, including a
// part nothing drives, carrying its own operator-visible name -- because
// `make ota` and `make uploadfs` are separate steps, so a controller whose
// firmware is newer than its web assets would otherwise report a part id with
// no name to put in front of a builder.
//
// It gets its own route rather than a key in GET /api/identity: that payload is
// bounded at IDENTITY_JSON_MAX_BYTES (512 B) with ~50 B of headroom, and the
// lineup is roughly 3 KB. Written by offset through JsonSliceWriter for the
// same reason GET /api/actions is, so no backend holds the body whole.
// -----------------------------------------------------------------------------

namespace {

// Snapshot of each family's active Component Member, pinned before the send
// starts. sendChunked() re-walks the whole body once per chunk, so a producer
// reads file-scope state rather than a live source mid-body
// (include/web_json_slice_writer.h). Indexed by ComponentCategoryId; 0 means
// "this family has no active member", which is no row's value.
uint8_t s_activeMembers[COMPONENT_CATEGORY_COUNT] = {};

const char* componentStatusToken(ComponentStatus status) {
    return status == COMPONENT_STATUS_SUPPORTED ? "supported" : "roadmap";
}

void appendCategoryJson(JsonSliceWriter& writer, const ComponentCategoryEntry& category) {
    char number[8];

    writer.append("{\"id\":");
    writer.appendJsonString(category.token);
    writer.append(",\"name\":");
    writer.appendJsonString(category.name);

    // How many members of this family the image can actually drive. More than
    // one is exactly the condition under which a Component Member setting
    // exists, so a picker reads this rather than deciding for itself.
    std::snprintf(number, sizeof(number), "%u",
                  (unsigned)componentCategorySelectableCount(category.id));
    writer.append(",\"selectable\":");
    writer.append(number);

    writer.append(",\"member_key\":");
    if (category.memberKey != nullptr) {
        writer.appendJsonString(category.memberKey);
    } else {
        writer.append("null");
    }

    // The member actually running since the last boot. null where the family
    // has no member setting, and null is a different answer from a member id:
    // one says there is nothing to choose, the other says what was chosen.
    const ComponentPartEntry* active = componentPartByValue(s_activeMembers[category.id]);
    writer.append(",\"active_member\":");
    if (active != nullptr && active->category == category.id) {
        writer.appendJsonString(active->id);
    } else {
        writer.append("null");
    }
    writer.append('}');
}

void appendPartJson(JsonSliceWriter& writer, const ComponentPartEntry& part) {
    char number[8];

    writer.append("{\"id\":");
    writer.appendJsonString(part.id);

    std::snprintf(number, sizeof(number), "%u", (unsigned)part.value);
    writer.append(",\"value\":");
    writer.append(number);

    writer.append(",\"name\":");
    writer.appendJsonString(part.name);
    writer.append(",\"category\":");
    writer.appendJsonString(componentCategory(part.category)->token);
    writer.append(",\"protocol\":");
    writer.appendJsonString(part.protocol);
    writer.append(",\"status\":");
    writer.appendJsonString(componentStatusToken(part.status));

    std::snprintf(number, sizeof(number), "%u", (unsigned)part.capabilities);
    writer.append(",\"capabilities\":");
    writer.append(number);

    // Whether this image carries a driver for it, which is a controller fact
    // and not the project fact `status` reports. A supported part that is not
    // included is the honest answer a builder gets when their module is not in
    // the image for their board.
    writer.append(",\"included\":");
    writer.append(part.included ? "true" : "false");

    // The Board Capability Gate this part requires, or null for universal --
    // the same meaning the action registry gives a null board_capability.
    writer.append(",\"board_capability\":");
    if (part.gate != nullptr) {
        writer.appendJsonString(part.gate);
    } else {
        writer.append("null");
    }
    writer.append('}');
}

}  // namespace

void componentRegistryJsonPinActiveMember(ComponentCategoryId category, uint8_t memberValue) {
    if ((size_t)category < COMPONENT_CATEGORY_COUNT) {
        s_activeMembers[category] = memberValue;
    }
}

size_t fillComponentRegistryJson(uint8_t* output, size_t capacity, size_t offset) {
    JsonSliceWriter writer(output, capacity, offset);

    writer.append("{\"categories\":[");
    for (size_t i = 0; i < COMPONENT_CATEGORY_TABLE_SIZE; ++i) {
        if (i > 0) {
            writer.append(',');
        }
        appendCategoryJson(writer, COMPONENT_CATEGORIES[i]);
    }

    writer.append("],\"parts\":[");
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        if (i > 0) {
            writer.append(',');
        }
        appendPartJson(writer, COMPONENT_PARTS[i]);
    }
    writer.append("]}");
    return writer.written();
}
