// =============================================================================
// include/web_webp.h
//
// MIME decision for Component Picker photographs (#316).
//
// PsychicHttp's table (PsychicFileResponse.cpp:107-133) knows .png .gif .jpg
// .ico .svg and falls back to text/plain for everything else. .webp is not on
// that list, and PsychicStaticFileHandler exposes no content-type hook, so a
// photograph served through serveStatic() would reach the browser as a text
// file. The owned handler in src/web/web_request_psychic.cpp consults these
// helpers and answers image/webp instead.
//
// A page names one path whichever asset set it was built with (ADR 0065):
// /<id>.webp, where <id> is the Component Registry token. These are products,
// not droid Parts -- no part_ prefix.
// =============================================================================
#pragma once

#include <stddef.h>
#include <string.h>

inline const char* webWebpContentType() {
    return "image/webp";
}

// True when uri is exactly /<registry-id>.webp. The id is the token
// include/component_registry.inc declares: lowercase letters, digits and
// underscore, starting with a letter. Anything else -- a query string, a
// second slash, a '..', an uppercase letter -- is rejected, so this cannot
// be talked into opening a different file.
inline bool webPathIsProductPhoto(const char* uri) {
    if (uri == nullptr) {
        return false;
    }
    static const char kSuffix[] = ".webp";
    const size_t suffixLen = sizeof(kSuffix) - 1;
    const size_t len = strlen(uri);
    if (len < 1 + 1 + suffixLen) {
        return false;
    }
    if (uri[0] != '/') {
        return false;
    }
    if (strcmp(uri + (len - suffixLen), kSuffix) != 0) {
        return false;
    }
    const char* id = uri + 1;
    const char* idEnd = uri + (len - suffixLen);
    if (id == idEnd) {
        return false;
    }
    if (*id < 'a' || *id > 'z') {
        return false;
    }
    for (const char* p = id; p < idEnd; ++p) {
        const bool ok =
            (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// The type the owned handler will send, or nullptr if this path is not ours
// (and serveStatic() may still claim it). The ticket's test is this returning
// image/webp for a .webp photograph rather than the table's text/plain.
inline const char* webProductPhotoContentType(const char* uri) {
    return webPathIsProductPhoto(uri) ? webWebpContentType() : nullptr;
}
