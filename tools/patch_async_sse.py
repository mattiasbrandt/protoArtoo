from pathlib import Path


SSE_PREAMBLE_START = "#if defined(ESP32) || defined(LIBRETINY)\n"
SSE_PREAMBLE_END = "#include <ESPAsyncWebServer.h>\n"
SSE_PREAMBLE = """\
#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#ifdef LIBRETINY
#ifdef round
#undef round
#endif
#endif
#include <mutex>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 32
#endif
#define SSE_MIN_INFLIGH 2 * 1460   // allow 2 MSS packets
#ifndef SSE_MAX_INFLIGH
#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q
#endif
#elif defined(ESP8266)
#include <ESPAsyncTCP.h>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 8
#endif
#define SSE_MIN_INFLIGH 2 * 1460  // allow 2 MSS packets
#ifndef SSE_MAX_INFLIGH
#define SSE_MAX_INFLIGH 8 * 1024  // but no more than 8k, no need to blow it, since same data is kept in local Q
#endif
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 32
#endif
#define SSE_MIN_INFLIGH 2 * 1460   // allow 2 MSS packets
#ifndef SSE_MAX_INFLIGH
#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q
#endif
#endif

"""

STATIC_SEARCH_BEFORE = """\
  if (_tryGzipFirst) {
    if (_fs.exists(gzip)) {
      request->_tempFile = _fs.open(gzip, fs::FileOpenMode::read);
      gzipFound = FILE_IS_REAL(request->_tempFile);
    }
    if (!gzipFound) {
      if (_fs.exists(path)) {
        request->_tempFile = _fs.open(path, fs::FileOpenMode::read);
        fileFound = FILE_IS_REAL(request->_tempFile);
      }
    }
  } else {
    if (_fs.exists(path)) {
      request->_tempFile = _fs.open(path, fs::FileOpenMode::read);
      fileFound = FILE_IS_REAL(request->_tempFile);
    }
    if (!fileFound) {
      if (_fs.exists(gzip)) {
        request->_tempFile = _fs.open(gzip, fs::FileOpenMode::read);
        gzipFound = FILE_IS_REAL(request->_tempFile);
      }
    }
  }
"""

STATIC_SEARCH_AFTER = """\
  if (_tryGzipFirst) {
    request->_tempFile = _fs.open(gzip, fs::FileOpenMode::read);
    gzipFound = FILE_IS_REAL(request->_tempFile);
    if (!gzipFound) {
      request->_tempFile = _fs.open(path, fs::FileOpenMode::read);
      fileFound = FILE_IS_REAL(request->_tempFile);
    }
  } else {
    request->_tempFile = _fs.open(path, fs::FileOpenMode::read);
    fileFound = FILE_IS_REAL(request->_tempFile);
    if (!fileFound) {
      request->_tempFile = _fs.open(gzip, fs::FileOpenMode::read);
      gzipFound = FILE_IS_REAL(request->_tempFile);
    }
  }
"""


def patch_sse_header(text):
    if text.count(SSE_PREAMBLE_START) != 1 or text.count(SSE_PREAMBLE_END) != 1:
        raise RuntimeError(
            "ESPAsyncWebServer AsyncEventSource.h platform preamble changed; "
            "review tools/patch_async_sse.py"
        )

    start = text.index(SSE_PREAMBLE_START)
    end = text.index(SSE_PREAMBLE_END)
    if end <= start:
        raise RuntimeError("ESPAsyncWebServer AsyncEventSource.h preamble is malformed")

    return text[:start] + SSE_PREAMBLE + text[end:]


def patch_static_handler(text):
    if STATIC_SEARCH_AFTER in text:
        return text
    if text.count(STATIC_SEARCH_BEFORE) != 1:
        raise RuntimeError(
            "ESPAsyncWebServer WebHandlers.cpp static-file search changed; "
            "review tools/patch_async_sse.py"
        )
    return text.replace(STATIC_SEARCH_BEFORE, STATIC_SEARCH_AFTER)


def patch_file(path, patcher, description):
    if not path.exists():
        return

    text = path.read_text(encoding="utf-8")
    patched = patcher(text)
    if patched != text:
        path.write_text(patched, encoding="utf-8")
        print(f"[patch_async_sse.py] {description}")


def patch_async_webserver(env):
    libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    source_dir = libdeps_dir / env["PIOENV"] / "ESPAsyncWebServer" / "src"
    patch_file(
        source_dir / "AsyncEventSource.h",
        patch_sse_header,
        "made SSE_MAX_INFLIGH build-flag overridable",
    )
    patch_file(
        source_dir / "WebHandlers.cpp",
        patch_static_handler,
        "removed redundant filesystem exists() probes",
    )


try:
    Import("env")
except NameError:
    pass
else:
    patch_async_webserver(env)
