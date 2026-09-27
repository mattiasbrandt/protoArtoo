// =============================================================================
// include/web_webp.h
//
// MIME decision for the pictures a page requests as /<id>.webp (#316, #355).
//
// PsychicHttp's table (PsychicFileResponse.cpp:107-133) knows .png .gif .jpg
// .ico .svg and falls back to text/plain for everything else. .webp is not on
// that list, and PsychicStaticFileHandler exposes no content-type hook, so a
// picture served through serveStatic() would reach the browser as a text
// file. The owned handler in src/web/web_request_psychic.cpp consults these
// helpers and answers image/webp instead.
//
// A page names one path whichever asset set it was built with (ADR 0065):
// /<id>.webp. The id is a Component Registry token for a product photograph
// (data/product_art.js) or a Droid Build picture id (data/droid_parts.js
// "picture", e.g. mrbaddeley). The handler claims the shape, not a list of
// ids, so a picture added to an asset set answers image/webp without a route
// of its own -- a per-id list is what left /mrbaddeley.webp on text/plain.
// =============================================================================
#pragma once

#include <stddef.h>
#include <string.h>

inline const char* webWebpContentType() {
    return "image/webp";
}

// True when uri is exactly /<id>.webp, where <id> is lowercase letters,
// digits and underscore, starting with a letter -- the shape both the
// Component Registry tokens and the Droid Build picture ids take. Anything
// else -- a query string, a second slash, a '..', a '%', an uppercase letter
// -- is rejected, so this cannot be talked into opening a different file.
inline bool webPathIsWebpPicture(const char* uri) {
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

// Whether the picture handler claims a request, before it looks for the file.
// path is the request path with the query string already stripped
// (PsychicRequest::path()), matching how an endpoint matched before. Only a
// GET is claimed: any other method falls through to the routes and the
// not-found handler behind it, as it did when each picture had its own GET
// endpoint. A claimed path whose file is absent also falls through, so the
// answer for a missing picture stays the ordinary not-found.
inline bool webWebpPictureRequestClaimed(bool isGet, const char* path) {
    return isGet && webPathIsWebpPicture(path);
}
