#pragma once

#include <stddef.h>
#include <stdint.h>

inline bool domeLinkAppendUrlEncodedChar(char* out, size_t outSize, size_t* pos, char c) {
    if (out == nullptr || pos == nullptr || *pos >= outSize) {
        return false;
    }

    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
        if (*pos + 1 >= outSize) {
            return false;
        }
        out[(*pos)++] = c;
        out[*pos] = '\0';
        return true;
    }
    if (c == ' ') {
        if (*pos + 1 >= outSize) {
            return false;
        }
        out[(*pos)++] = '+';
        out[*pos] = '\0';
        return true;
    }

    static const char* kHex = "0123456789ABCDEF";
    if (*pos + 3 >= outSize) {
        return false;
    }
    out[(*pos)++] = '%';
    out[(*pos)++] = kHex[(uint8_t)c >> 4];
    out[(*pos)++] = kHex[(uint8_t)c & 0x0F];
    out[*pos] = '\0';
    return true;
}

inline bool domeLinkUrlEncodeInto(char* out, size_t outSize, const char* input, size_t* outLen) {
    if (out == nullptr || outSize == 0 || outLen == nullptr) {
        return false;
    }

    out[0] = '\0';
    *outLen = 0;
    if (input == nullptr) {
        return true;
    }

    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (!domeLinkAppendUrlEncodedChar(out, outSize, outLen, input[i])) {
            return false;
        }
    }
    return true;
}
