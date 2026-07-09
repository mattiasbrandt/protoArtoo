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

# Fixes issue #21: under heap pressure (concurrent SSE + reload + audio-task
# allocation churn), plain `new` throws std::bad_alloc instead of returning
# nullptr, so the existing `if (!response)` check below never runs and the
# uncaught exception unwinds to std::terminate()/abort() (PANIC on core 1,
# task async_tcp), crashing the whole device instead of failing one request.
# `new (std::nothrow)` restores the null-return contract the surrounding code
# already assumes.
STATIC_ALLOC_INCLUDES_BEFORE = """\
#include <cstdio>
#include <utility>
"""

STATIC_ALLOC_INCLUDES_AFTER = """\
#include <cstdio>
#include <new>
#include <utility>
"""

STATIC_ALLOC_SEARCH_BEFORE = """\
  if (notModified) {
    request->_tempFile.close();
    response = new AsyncBasicResponse(304);  // Not modified
  } else {
    response = new AsyncFileResponse(request->_tempFile, filename, emptyString, false, _callback);
  }
"""

STATIC_ALLOC_SEARCH_AFTER = """\
  if (notModified) {
    request->_tempFile.close();
    response = new (std::nothrow) AsyncBasicResponse(304);  // Not modified
  } else {
    response = new (std::nothrow) AsyncFileResponse(request->_tempFile, filename, emptyString, false, _callback);
  }
"""

# Issue #21, round 2: the nothrow fix above stops that one allocation from
# crashing the device, but under the same heap pressure a *different*
# allocation elsewhere in the request/response call graph (observed:
# std::list<AsyncWebHeader> node allocation inside
# AsyncWebServerResponse::addHeader, called from
# AsyncStaticWebHandler::handleRequest) throws std::bad_alloc just the same,
# uncaught, and still aborts the whole device. Patching each allocation site
# one at a time is whack-a-mole -- there is no guarantee another one won't
# surface next. AsyncTCP_detail::handle_async_event is the single choke point
# every inbound/outbound event for every request passes through before
# reaching any handler code, so it is the right place to contain *any*
# uncaught exception (this one or a future one) to the one request/connection
# that triggered it, instead of the whole board.
ASYNC_EVENT_INCLUDES_BEFORE = """\
#include "AsyncTCP.h"
#include "AsyncTCPLogging.h"
#include "AsyncTCPSimpleIntrusiveList.h"
"""

ASYNC_EVENT_INCLUDES_AFTER = """\
#include "AsyncTCP.h"
#include "AsyncTCPLogging.h"
#include "AsyncTCPSimpleIntrusiveList.h"

#include <exception>
"""

# Issue #21, round 6: raw TCP accept runs on lwIP's tiT task before a
# connection becomes an AsyncClient or HTTP request, so neither request
# admission control nor handle_async_event's try/catch can protect it. The
# observed crash was already using `new (std::nothrow) AsyncClient(pcb)`, but
# this toolchain's nothrow new wraps throwing new and can still terminate if
# __cxa_allocate_exception itself cannot allocate a bad_alloc object. Guard the
# accept path before it enters any C++ allocation machinery.
TCP_ACCEPT_HEAP_GUARD_INCLUDES_BEFORE = """\
#include "AsyncTCP.h"
#include "AsyncTCPLogging.h"
#include "AsyncTCPSimpleIntrusiveList.h"

#include <exception>
"""

TCP_ACCEPT_HEAP_GUARD_INCLUDES_AFTER = """\
#include "AsyncTCP.h"
#include "AsyncTCPLogging.h"
#include "AsyncTCPSimpleIntrusiveList.h"

#if defined(ESP32)
#include <esp_heap_caps.h>
// Accept-guard telemetry, readable by the application (e.g. /api/status).
// The guard's own log_w is compiled out at CORE_DEBUG_LEVEL=0, so these
// counters are the only always-on evidence that accepts are being rejected.
// Written only from the lwIP task inside tcp_accept.
extern "C" {
volatile uint32_t g_asyncTcpAcceptRejectHeap = 0;
volatile uint32_t g_asyncTcpAcceptRejectRate = 0;
volatile uint32_t g_asyncTcpAcceptRejectLastMs = 0;
}
#endif
#include <exception>
"""

ASYNC_EVENT_DISPATCH_BEFORE = """\
void AsyncTCP_detail::handle_async_event(lwip_tcp_event_packet_t *e) {
  if (e->client == NULL) {
    // do nothing when arg is NULL
    // ets_printf("event arg == NULL: 0x%08x\\n", e->recv.pcb);
  } else if (e->event == LWIP_TCP_RECV) {
    // ets_printf("-R: 0x%08x\\n", e->recv.pcb);
    e->client->_recv(e->recv.pcb, e->recv.pb, e->recv.err);
    e->recv.pb = nullptr;  // given to client
  } else if (e->event == LWIP_TCP_FIN) {
    // ets_printf("-F: 0x%08x\\n", e->fin.pcb);
    e->client->_fin(e->fin.pcb, e->fin.err);
  } else if (e->event == LWIP_TCP_SENT) {
    // ets_printf("-S: 0x%08x\\n", e->sent.pcb);
    e->client->_sent(e->sent.pcb, e->sent.len);
  } else if (e->event == LWIP_TCP_POLL) {
    // ets_printf("-P: 0x%08x\\n", e->poll.pcb);
    e->client->_poll(e->poll.pcb);
  } else if (e->event == LWIP_TCP_ERROR) {
    // ets_printf("-E: 0x%08x %d\\n", e->client, e->error.err);
    e->client->_error(e->error.err);
  } else if (e->event == LWIP_TCP_CONNECTED) {
    // ets_printf("C: 0x%08x 0x%08x %d\\n", e->client, e->connected.pcb, e->connected.err);
    e->client->_connected(e->connected.pcb, e->connected.err);
  } else if (e->event == LWIP_TCP_ACCEPT) {
    // ets_printf("A: 0x%08x 0x%08x\\n", e->client, e->accept.client);
    e->accept.server->_accepted(e->client);
  } else if (e->event == LWIP_TCP_DNS) {
    // ets_printf("D: 0x%08x %s = %s\\n", e->client, e->dns.name, ipaddr_ntoa(&e->dns.addr));
    e->client->_dns_found(&e->dns.addr);
  }
  _free_event(e);
}
"""

ASYNC_EVENT_DISPATCH_AFTER = """\
void AsyncTCP_detail::handle_async_event(lwip_tcp_event_packet_t *e) {
  try {
    if (e->client == NULL) {
      // do nothing when arg is NULL
      // ets_printf("event arg == NULL: 0x%08x\\n", e->recv.pcb);
    } else if (e->event == LWIP_TCP_RECV) {
      // ets_printf("-R: 0x%08x\\n", e->recv.pcb);
      e->client->_recv(e->recv.pcb, e->recv.pb, e->recv.err);
      e->recv.pb = nullptr;  // given to client
    } else if (e->event == LWIP_TCP_FIN) {
      // ets_printf("-F: 0x%08x\\n", e->fin.pcb);
      e->client->_fin(e->fin.pcb, e->fin.err);
    } else if (e->event == LWIP_TCP_SENT) {
      // ets_printf("-S: 0x%08x\\n", e->sent.pcb);
      e->client->_sent(e->sent.pcb, e->sent.len);
    } else if (e->event == LWIP_TCP_POLL) {
      // ets_printf("-P: 0x%08x\\n", e->poll.pcb);
      e->client->_poll(e->poll.pcb);
    } else if (e->event == LWIP_TCP_ERROR) {
      // ets_printf("-E: 0x%08x %d\\n", e->client, e->error.err);
      e->client->_error(e->error.err);
    } else if (e->event == LWIP_TCP_CONNECTED) {
      // ets_printf("C: 0x%08x 0x%08x %d\\n", e->client, e->connected.pcb, e->connected.err);
      e->client->_connected(e->connected.pcb, e->connected.err);
    } else if (e->event == LWIP_TCP_ACCEPT) {
      // ets_printf("A: 0x%08x 0x%08x\\n", e->client, e->accept.client);
      e->accept.server->_accepted(e->client);
    } else if (e->event == LWIP_TCP_DNS) {
      // ets_printf("D: 0x%08x %s = %s\\n", e->client, e->dns.name, ipaddr_ntoa(&e->dns.addr));
      e->client->_dns_found(&e->dns.addr);
    }
  } catch (const std::exception &ex) {
    // issue #21: contain an uncaught allocation failure (or other exception)
    // from anywhere in request/response handling to this one event instead
    // of letting it unwind to std::terminate() and abort() the whole device.
    async_tcp_log_e("Uncaught exception in async event %d, dropping event: %s", (int)e->event, ex.what());
  } catch (...) {
    async_tcp_log_e("Uncaught non-standard exception in async event %d, dropping event", (int)e->event);
  }
  _free_event(e);
}
"""

TCP_ACCEPT_HEAP_GUARD_BEFORE = """\
  if (server->_connect_cb) {
    AsyncClient *c = new (std::nothrow) AsyncClient(pcb);
"""

TCP_ACCEPT_HEAP_GUARD_AFTER = """\
  if (server->_connect_cb) {
#if defined(ESP32)
#ifndef ASYNC_TCP_ACCEPT_MIN_LARGEST_FREE_BLOCK
#define ASYNC_TCP_ACCEPT_MIN_LARGEST_FREE_BLOCK 20000
#endif
#ifndef ASYNC_TCP_ACCEPT_BURST
#define ASYNC_TCP_ACCEPT_BURST 6
#endif
#ifndef ASYNC_TCP_ACCEPT_PER_SECOND
#define ASYNC_TCP_ACCEPT_PER_SECOND 8
#endif
#if ASYNC_TCP_ACCEPT_PER_SECOND > 0
    // Token-bucket accept pacing. Connections already accepted keep
    // allocating (request, response, headers) while the heap falls, so a
    // heap threshold alone cannot stop a dense burst from exhausting the
    // heap between samples. Bounding the admission rate caps how much
    // allocation pressure can pile up regardless of heap state. Static
    // state is safe: tcp_accept runs only on the lwIP task.
    {
      static uint32_t s_tokens_m = (uint32_t)ASYNC_TCP_ACCEPT_BURST * 1000u;
      static uint32_t s_last_ms = 0;
      const uint32_t now_ms = millis();
      const uint32_t cap_m = (uint32_t)ASYNC_TCP_ACCEPT_BURST * 1000u;
      const uint64_t refill_m = (uint64_t)s_tokens_m
          + (uint64_t)(uint32_t)(now_ms - s_last_ms) * (uint64_t)ASYNC_TCP_ACCEPT_PER_SECOND;
      s_last_ms = now_ms;
      s_tokens_m = refill_m > cap_m ? cap_m : (uint32_t)refill_m;
      if (s_tokens_m < 1000u) {
        g_asyncTcpAcceptRejectRate = g_asyncTcpAcceptRejectRate + 1u;
        g_asyncTcpAcceptRejectLastMs = now_ms;
        tcp_abort(pcb);
        return ERR_ABRT;
      }
      s_tokens_m -= 1000u;
    }
#endif
#if ASYNC_TCP_ACCEPT_MIN_LARGEST_FREE_BLOCK > 0
    const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largestBlock < (size_t)ASYNC_TCP_ACCEPT_MIN_LARGEST_FREE_BLOCK) {
      async_tcp_log_w(
        "_accept failed: largest free block %u < %u",
        (unsigned)largestBlock, (unsigned)ASYNC_TCP_ACCEPT_MIN_LARGEST_FREE_BLOCK
      );
      g_asyncTcpAcceptRejectHeap = g_asyncTcpAcceptRejectHeap + 1u;
      g_asyncTcpAcceptRejectLastMs = millis();
      tcp_abort(pcb);
      return ERR_ABRT;
    }
#endif
#endif

    AsyncClient *c = new (std::nothrow) AsyncClient(pcb);
"""

# Issue #21, round 3: a simple req->send(200, ...) route crashed in
# AsyncWebServerRequest::beginResponse when plain `new AsyncBasicResponse`
# failed. The exception machinery itself could not allocate the bad_alloc object,
# so the try/catch guard in AsyncTCP never got a chance to run. Make the
# response factories non-throwing and teach send(nullptr) to abort just the one
# request.
WEBREQUEST_ALLOC_INCLUDES_BEFORE = """\
#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
"""

WEBREQUEST_ALLOC_INCLUDES_AFTER = """\
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
"""

WEBREQUEST_FACTORY_ALLOC_BEFORE = """\
AsyncWebServerResponse *AsyncWebServerRequest::beginResponse(int code, const char *contentType, const char *content, AwsTemplateProcessor callback) {
  if (callback) {
    return new AsyncProgmemResponse(code, contentType, (const uint8_t *)content, strlen(content), callback);
  }
  return new AsyncBasicResponse(code, contentType, content);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(int code, const char *contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback) {
  return new AsyncProgmemResponse(code, contentType, content, len, callback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(FS &fs, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  if (fs.exists(path) || (!download && fs.exists(path + T__gz))) {
    return new AsyncFileResponse(fs, path, contentType, download, callback);
  }
  return NULL;
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(File content, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  if (content == true) {
    return new AsyncFileResponse(content, path, contentType, download, callback);
  }
  return NULL;
}

AsyncWebServerResponse *AsyncWebServerRequest::beginResponse(Stream &stream, const char *contentType, size_t len, AwsTemplateProcessor callback) {
  return new AsyncStreamResponse(stream, contentType, len, callback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(const char *contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback) {
  return new AsyncCallbackResponse(contentType, len, callback, templateCallback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginChunkedResponse(const char *contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback) {
  if (_version) {
    return new AsyncChunkedResponse(contentType, callback, templateCallback);
  }
  return new AsyncCallbackResponse(contentType, 0, callback, templateCallback);
}

AsyncResponseStream *AsyncWebServerRequest::beginResponseStream(const char *contentType, size_t bufferSize) {
  return new AsyncResponseStream(contentType, bufferSize);
}

AsyncWebServerResponse *AsyncWebServerRequest::beginResponse_P(int code, const String &contentType, PGM_P content, AwsTemplateProcessor callback) {
  return new AsyncProgmemResponse(code, contentType, (const uint8_t *)content, strlen_P(content), callback);
}
"""

WEBREQUEST_FACTORY_ALLOC_AFTER = """\
AsyncWebServerResponse *AsyncWebServerRequest::beginResponse(int code, const char *contentType, const char *content, AwsTemplateProcessor callback) {
  if (callback) {
    return new (std::nothrow) AsyncProgmemResponse(code, contentType, (const uint8_t *)content, strlen(content), callback);
  }
  return new (std::nothrow) AsyncBasicResponse(code, contentType, content);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(int code, const char *contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback) {
  return new (std::nothrow) AsyncProgmemResponse(code, contentType, content, len, callback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(FS &fs, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  if (fs.exists(path) || (!download && fs.exists(path + T__gz))) {
    return new (std::nothrow) AsyncFileResponse(fs, path, contentType, download, callback);
  }
  return NULL;
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(File content, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  if (content == true) {
    return new (std::nothrow) AsyncFileResponse(content, path, contentType, download, callback);
  }
  return NULL;
}

AsyncWebServerResponse *AsyncWebServerRequest::beginResponse(Stream &stream, const char *contentType, size_t len, AwsTemplateProcessor callback) {
  return new (std::nothrow) AsyncStreamResponse(stream, contentType, len, callback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(const char *contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback) {
  return new (std::nothrow) AsyncCallbackResponse(contentType, len, callback, templateCallback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginChunkedResponse(const char *contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback) {
  if (_version) {
    return new (std::nothrow) AsyncChunkedResponse(contentType, callback, templateCallback);
  }
  return new (std::nothrow) AsyncCallbackResponse(contentType, 0, callback, templateCallback);
}

AsyncResponseStream *AsyncWebServerRequest::beginResponseStream(const char *contentType, size_t bufferSize) {
  return new (std::nothrow) AsyncResponseStream(contentType, bufferSize);
}

AsyncWebServerResponse *AsyncWebServerRequest::beginResponse_P(int code, const String &contentType, PGM_P content, AwsTemplateProcessor callback) {
  return new (std::nothrow) AsyncProgmemResponse(code, contentType, (const uint8_t *)content, strlen_P(content), callback);
}
"""

WEBREQUEST_SEND_NULL_BEFORE = """\
void AsyncWebServerRequest::send(AsyncWebServerResponse *response) {
  // request is already sent on the wire ?
  if (_sent) {
    return;
  }

  // if we already had a response, delete it and replace it with the new one
"""

WEBREQUEST_SEND_NULL_AFTER = """\
void AsyncWebServerRequest::send(AsyncWebServerResponse *response) {
  // request is already sent on the wire ?
  if (_sent) {
    return;
  }

  if (response == nullptr) {
    async_ws_log_e("Failed to allocate response");
    abort();
    return;
  }

  // if we already had a response, delete it and replace it with the new one
"""

EVENTSOURCE_ALLOC_INCLUDES_BEFORE = """\
#include <algorithm>
#include <memory>
#include <utility>
"""

EVENTSOURCE_ALLOC_INCLUDES_AFTER = """\
#include <algorithm>
#include <memory>
#include <new>
#include <utility>
"""

EVENTSOURCE_RESPONSE_ALLOC_BEFORE = """\
void AsyncEventSource::handleRequest(AsyncWebServerRequest *request) {
  request->send(new AsyncEventSourceResponse(this));
}
"""

EVENTSOURCE_RESPONSE_ALLOC_AFTER = """\
void AsyncEventSource::handleRequest(AsyncWebServerRequest *request) {
  request->send(new (std::nothrow) AsyncEventSourceResponse(this));
}
"""

# Issue #21, round 4: ESPAsyncWebServer's static handler opens files inside
# canHandle(), before server.addMiddleware() runs. Under the 14x reload stress
# repro, this let already-admitted static asset work call into LittleFS while
# the largest free block had collapsed, and esp_littlefs aborted directly while
# allocating its FD tracking structure. Guard the static file open path at the
# actual _fs.open() seam; no C++ exception boundary can catch a raw abort().
STATIC_OPEN_GUARD_INCLUDES_BEFORE = """\
#include <cstdio>
#include <new>
#include <utility>
"""

STATIC_OPEN_GUARD_INCLUDES_AFTER = """\
#include <cstdio>
#ifdef ESP32
#include <esp_heap_caps.h>
#endif
#include <new>
#include <utility>
"""

STATIC_OPEN_GUARD_BEFORE = """\
  bool fileFound = false;
  bool gzipFound = false;

  String gzip = path + T__gz;
"""

STATIC_OPEN_GUARD_AFTER = """\
  bool fileFound = false;
  bool gzipFound = false;

#ifdef ESP32
#ifndef ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK
#define ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK 20000
#endif
#if ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK > 0
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largestBlock < (size_t)ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK) {
    async_ws_log_w(
      "Skipping static file open: largest free block %u < %u",
      (unsigned)largestBlock, (unsigned)ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK
    );
    request->abort();
    return false;
  }
#endif
#endif

  String gzip = path + T__gz;
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


def patch_static_handler_alloc(text):
    if STATIC_ALLOC_SEARCH_AFTER in text:
        return text
    if text.count(STATIC_ALLOC_SEARCH_BEFORE) != 1:
        raise RuntimeError(
            "ESPAsyncWebServer WebHandlers.cpp response-allocation site changed; "
            "review tools/patch_async_sse.py"
        )
    text = text.replace(STATIC_ALLOC_SEARCH_BEFORE, STATIC_ALLOC_SEARCH_AFTER)

    if STATIC_ALLOC_INCLUDES_AFTER not in text:
        if text.count(STATIC_ALLOC_INCLUDES_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebHandlers.cpp include block changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(STATIC_ALLOC_INCLUDES_BEFORE, STATIC_ALLOC_INCLUDES_AFTER)

    return text


def patch_static_handler_open_guard(text):
    if STATIC_OPEN_GUARD_AFTER not in text:
        if text.count(STATIC_OPEN_GUARD_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebHandlers.cpp static-file open block changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(STATIC_OPEN_GUARD_BEFORE, STATIC_OPEN_GUARD_AFTER)

    if STATIC_OPEN_GUARD_INCLUDES_AFTER not in text:
        if text.count(STATIC_OPEN_GUARD_INCLUDES_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebHandlers.cpp include block changed for static-file open guard; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(STATIC_OPEN_GUARD_INCLUDES_BEFORE, STATIC_OPEN_GUARD_INCLUDES_AFTER)

    return text


def patch_async_event_dispatch(text):
    if ASYNC_EVENT_DISPATCH_AFTER in text:
        return text
    if text.count(ASYNC_EVENT_DISPATCH_BEFORE) != 1:
        raise RuntimeError(
            "AsyncTCP.cpp handle_async_event dispatch changed; "
            "review tools/patch_async_sse.py"
        )
    text = text.replace(ASYNC_EVENT_DISPATCH_BEFORE, ASYNC_EVENT_DISPATCH_AFTER)

    if ASYNC_EVENT_INCLUDES_AFTER not in text:
        if text.count(ASYNC_EVENT_INCLUDES_BEFORE) != 1:
            raise RuntimeError(
                "AsyncTCP.cpp include block changed; review tools/patch_async_sse.py"
            )
        text = text.replace(ASYNC_EVENT_INCLUDES_BEFORE, ASYNC_EVENT_INCLUDES_AFTER)

    return text


def patch_tcp_accept_heap_guard(text):
    if TCP_ACCEPT_HEAP_GUARD_AFTER not in text:
        if text.count(TCP_ACCEPT_HEAP_GUARD_BEFORE) != 1:
            raise RuntimeError(
                "AsyncTCP.cpp tcp_accept client allocation changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(TCP_ACCEPT_HEAP_GUARD_BEFORE, TCP_ACCEPT_HEAP_GUARD_AFTER)

    if TCP_ACCEPT_HEAP_GUARD_INCLUDES_AFTER not in text:
        if text.count(TCP_ACCEPT_HEAP_GUARD_INCLUDES_BEFORE) != 1:
            raise RuntimeError(
                "AsyncTCP.cpp include block changed for tcp_accept heap guard; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(TCP_ACCEPT_HEAP_GUARD_INCLUDES_BEFORE, TCP_ACCEPT_HEAP_GUARD_INCLUDES_AFTER)

    return text


def patch_webrequest_response_alloc(text):
    if WEBREQUEST_FACTORY_ALLOC_AFTER not in text:
        if text.count(WEBREQUEST_FACTORY_ALLOC_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebRequest.cpp response factory block changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(WEBREQUEST_FACTORY_ALLOC_BEFORE, WEBREQUEST_FACTORY_ALLOC_AFTER)

    if WEBREQUEST_SEND_NULL_AFTER not in text:
        if text.count(WEBREQUEST_SEND_NULL_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebRequest.cpp send(response) block changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(WEBREQUEST_SEND_NULL_BEFORE, WEBREQUEST_SEND_NULL_AFTER)

    if WEBREQUEST_ALLOC_INCLUDES_AFTER not in text:
        if text.count(WEBREQUEST_ALLOC_INCLUDES_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebRequest.cpp include block changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(WEBREQUEST_ALLOC_INCLUDES_BEFORE, WEBREQUEST_ALLOC_INCLUDES_AFTER)

    return text


def patch_eventsource_response_alloc(text):
    if EVENTSOURCE_RESPONSE_ALLOC_AFTER not in text:
        if text.count(EVENTSOURCE_RESPONSE_ALLOC_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer AsyncEventSource.cpp response allocation changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(EVENTSOURCE_RESPONSE_ALLOC_BEFORE, EVENTSOURCE_RESPONSE_ALLOC_AFTER)

    if EVENTSOURCE_ALLOC_INCLUDES_AFTER not in text:
        if text.count(EVENTSOURCE_ALLOC_INCLUDES_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer AsyncEventSource.cpp include block changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(EVENTSOURCE_ALLOC_INCLUDES_BEFORE, EVENTSOURCE_ALLOC_INCLUDES_AFTER)

    return text


LISTEN_BACKLOG_BEFORE = "  static uint8_t backlog = 5;\n"

LISTEN_BACKLOG_AFTER = """\
#ifndef ASYNC_TCP_LISTEN_BACKLOG
#define ASYNC_TCP_LISTEN_BACKLOG 5
#endif
  static uint8_t backlog = ASYNC_TCP_LISTEN_BACKLOG;
"""


def patch_listen_backlog(text):
    if LISTEN_BACKLOG_AFTER not in text:
        if text.count(LISTEN_BACKLOG_BEFORE) != 1:
            raise RuntimeError(
                "AsyncTCP.cpp AsyncServer::begin listen backlog changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(LISTEN_BACKLOG_BEFORE, LISTEN_BACKLOG_AFTER)

    return text


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
    patch_file(
        source_dir / "WebHandlers.cpp",
        patch_static_handler_alloc,
        "made static-file response allocation non-throwing (issue #21)",
    )
    patch_file(
        source_dir / "WebHandlers.cpp",
        patch_static_handler_open_guard,
        "guarded static-file opens before LittleFS FD allocation under critical heap (issue #21)",
    )
    patch_file(
        source_dir / "WebRequest.cpp",
        patch_webrequest_response_alloc,
        "made request response factories non-throwing and send(nullptr) abort one request (issue #21)",
    )
    patch_file(
        source_dir / "AsyncEventSource.cpp",
        patch_eventsource_response_alloc,
        "made SSE response allocation non-throwing (issue #21)",
    )

    asynctcp_source_dir = libdeps_dir / env["PIOENV"] / "AsyncTCP" / "src"
    patch_file(
        asynctcp_source_dir / "AsyncTCP.cpp",
        patch_async_event_dispatch,
        "contained uncaught exceptions in handle_async_event to one request instead of crashing (issue #21)",
    )
    patch_file(
        asynctcp_source_dir / "AsyncTCP.cpp",
        patch_tcp_accept_heap_guard,
        "guarded tcp_accept before AsyncClient allocation under critical heap (issue #21)",
    )
    patch_file(
        asynctcp_source_dir / "AsyncTCP.cpp",
        patch_listen_backlog,
        "made TCP listen backlog build-flag overridable (issue #22)",
    )


try:
    Import("env")
except NameError:
    pass
else:
    patch_async_webserver(env)
