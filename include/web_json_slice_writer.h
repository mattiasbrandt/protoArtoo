// =============================================================================
// include/web_json_slice_writer.h
//
// Writes one window of a JSON body for WebRequest::sendChunked() (ADR 0021).
//
// sendChunked() asks for bytes by offset, so the producer is called once per
// chunk and each call re-walks the whole logical body, copying only the slice
// the backend asked for. That is what lets a payload larger than any buffer
// cost only one chunk buffer: nothing is ever assembled whole, on the stack or
// on the heap.
//
// The re-walk means the data a producer reads must be cheap to read and stable
// across the calls. Producers snapshot into file-scope state before starting
// the send rather than reaching for a live source mid-body -- see
// src/web/api_dome.cpp for that pattern and the single-server-task argument it
// rests on.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

class JsonSliceWriter {
   public:
    // output/capacity are the chunk buffer the backend handed over; offset is
    // the body byte the chunk starts at. Everything appended before that offset
    // is counted and discarded, so a producer writes the whole body every call
    // and this decides what actually lands.
    JsonSliceWriter(uint8_t* output, size_t capacity, size_t offset)
        : output_(output), capacity_(capacity), offset_(offset) {
    }

    void append(const char* text) {
        append(text, std::strlen(text));
    }

    void append(const char* text, size_t length) {
        if (written_ >= capacity_) {
            logicalOffset_ += length;
            return;
        }

        const size_t segmentEnd = logicalOffset_ + length;
        if (offset_ < segmentEnd) {
            const size_t start = offset_ > logicalOffset_ ? offset_ - logicalOffset_ : 0;
            const size_t count = std::min(length - start, capacity_ - written_);
            std::memcpy(output_ + written_, text + start, count);
            written_ += count;
        }
        logicalOffset_ = segmentEnd;
    }

    void append(char value) {
        append(&value, 1);
    }

    // Append an unsigned decimal, for the numeric fields that make up most of
    // these payloads. Saves every producer its own snprintf scratch buffer.
    void appendUint(unsigned long value) {
        char digits[21];
        std::snprintf(digits, sizeof(digits), "%lu", value);
        append(digits);
    }

    void appendJsonString(const char* value) {
        append('"');
        if (value != nullptr) {
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p != '\0';
                 ++p) {
                switch (*p) {
                    case '"':
                        append("\\\"");
                        break;
                    case '\\':
                        append("\\\\");
                        break;
                    case '\b':
                        append("\\b");
                        break;
                    case '\f':
                        append("\\f");
                        break;
                    case '\n':
                        append("\\n");
                        break;
                    case '\r':
                        append("\\r");
                        break;
                    case '\t':
                        append("\\t");
                        break;
                    default:
                        if (*p < 0x20) {
                            char escaped[7];
                            std::snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                            append(escaped);
                        } else {
                            append(reinterpret_cast<const char*>(p), 1);
                        }
                        break;
                }
            }
        }
        append('"');
    }

    // Bytes landed in this chunk. Returning 0 is how a filler ends the body,
    // which happens naturally once offset has passed the last byte.
    size_t written() const {
        return written_;
    }

   private:
    uint8_t* output_;
    size_t capacity_;
    size_t offset_;
    size_t logicalOffset_ = 0;
    size_t written_ = 0;
};
