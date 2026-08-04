from pathlib import Path


SSE_PREAMBLE_START = "#if defined(ESP32) || defined(LIBRETINY) || defined(HOST)\n"
SSE_PREAMBLE_END = "#include <ESPAsyncWebServer.h>\n"
SSE_PREAMBLE = """\
#if defined(ESP32) || defined(LIBRETINY) || defined(HOST)
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
    response = new AsyncFileResponse(request->_tempFile, filename, asyncsrv::emptyString, false, _callback);
  }
"""

STATIC_ALLOC_SEARCH_AFTER = """\
  if (notModified) {
    request->_tempFile.close();
    response = new (std::nothrow) AsyncBasicResponse(304);  // Not modified
  } else {
    response = new (std::nothrow) AsyncFileResponse(request->_tempFile, filename, asyncsrv::emptyString, false, _callback);
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

# The SSE upgrade path constructs AsyncEventSourceClient when the response
# head is ACKed, and the constructor immediately dereferences the request's
# client connection. If the connection dies between the ACK and the upgrade
# (observed live: connection churn during a filesystem OTA), the client
# pointer is already null and the constructor panics on it -- coredump-proven
# on 3.10.3, and 3.11.x's clientRelease() rework still returns the null
# unchecked. Guard the handover: per the library's own ownership rules,
# deleting the request also deletes the bound response. The construction is
# also made non-throwing to match the rest of the issue #21 hardening.
EVENTSOURCE_SWITCH_GUARD_BEFORE = """\
void AsyncEventSourceResponse::_switchClient() {
  // AsyncEventSourceClient c-tor will take the ownership of AsyncTCP's client connection
  new AsyncEventSourceClient(_request, _server);
  // AsyncEventSourceClient c-tor would also delete _request and *this
};
"""

EVENTSOURCE_SWITCH_GUARD_AFTER = """\
void AsyncEventSourceResponse::_switchClient() {
  if (_request->client() == nullptr) {
    // Connection died between the header ACK and this upgrade; the client
    // constructor would dereference the null connection. Deleting the
    // request also deletes this bound response.
    async_ws_log_e("SSE handover raced connection teardown; dropping client");
    delete _request;
    return;
  }
  // AsyncEventSourceClient c-tor will take the ownership of AsyncTCP's client connection
  if (new (std::nothrow) AsyncEventSourceClient(_request, _server) == nullptr) {
    delete _request;
  }
  // AsyncEventSourceClient c-tor would also delete _request and *this
};
"""


# Part two of the handover hardening: _addClient() invokes the application's
# onConnect callback while the AsyncEventSourceClient constructor is still
# running. If that callback rejects the client with close(), the disconnect
# path runs synchronously, nulls _client, and the constructor tail then
# dereferences it (coredump-proven twice: AsyncClient::setNoDelay(this=0x0)).
# The application side must not close() from onConnect, but the constructor
# must also not trust _client across the callback.
EVENTSOURCE_CTOR_TAIL_BEFORE = """\
  _server->_addClient(this);
  _client->setNoDelay(true);
"""

EVENTSOURCE_CTOR_TAIL_AFTER = """\
  _server->_addClient(this);
  if (_client != nullptr) {
    // _addClient runs the application's onConnect callback; if it closed
    // the connection, the disconnect path already nulled _client.
    _client->setNoDelay(true);
  }
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


def patch_eventsource_switch_guard(text):
    if EVENTSOURCE_SWITCH_GUARD_AFTER not in text:
        if text.count(EVENTSOURCE_SWITCH_GUARD_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer AsyncEventSource.cpp _switchClient changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(EVENTSOURCE_SWITCH_GUARD_BEFORE, EVENTSOURCE_SWITCH_GUARD_AFTER)

    if EVENTSOURCE_CTOR_TAIL_AFTER not in text:
        if text.count(EVENTSOURCE_CTOR_TAIL_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer AsyncEventSource.cpp client constructor tail changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(EVENTSOURCE_CTOR_TAIL_BEFORE, EVENTSOURCE_CTOR_TAIL_AFTER)

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


# Issue #67: write_send_buffs() retries an ASYNC_RESPONCE_BUFF_SIZE allocation
# on every TCP ack/poll (roughly every CONFIG_ASYNC_TCP_POLL_TIMER slow-timer
# tick, ~500ms) once a response has started. Under sustained heap
# fragmentation, that allocation can keep failing indefinitely: the response
# just sits in RESPONSE_CONTENT forever with headers already sent, since
# nothing else ever aborts it. Every other admission/allocation guard in this
# stack fails fast (abort()/close(), milliseconds); this was the one gap that
# didn't, and it produced a live ~25s hang in issue #66's evidence trail. Bound
# the retry count and close the connection once exceeded, same as the
# existing close()-on-error path a few lines above this one.
STATIC_RESPONSE_OOM_STALL_BEFORE = """\
      if (!_send_buffer) {
        auto p = new (std::nothrow) std::array<uint8_t, ASYNC_RESPONCE_BUFF_SIZE>;
        if (p) {
          _send_buffer.reset(p);
          _send_buffer_len = _send_buffer_offset = 0;
        } else {
          break;  // OOM
        }
      }
"""

STATIC_RESPONSE_OOM_STALL_AFTER = """\
      if (!_send_buffer) {
        auto p = new (std::nothrow) std::array<uint8_t, ASYNC_RESPONCE_BUFF_SIZE>;
        if (p) {
          _send_buffer.reset(p);
          _send_buffer_len = _send_buffer_offset = 0;
          _oomStallCount = 0;
        } else {
#ifndef ASYNC_RESPONSE_OOM_STALL_MAX
#define ASYNC_RESPONSE_OOM_STALL_MAX 10
#endif
          if (++_oomStallCount >= ASYNC_RESPONSE_OOM_STALL_MAX) {
            // Fragmented heap hasn't freed one send buffer's worth of memory
            // across ASYNC_RESPONSE_OOM_STALL_MAX retries (issue #67) -- fail
            // this response now instead of stalling it indefinitely.
            request->client()->close();
            return 0;
          }
          break;  // OOM
        }
      }
"""


def patch_static_response_oom_stall(text):
    if STATIC_RESPONSE_OOM_STALL_AFTER not in text:
        if text.count(STATIC_RESPONSE_OOM_STALL_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebResponses.cpp write_send_buffs OOM branch changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(STATIC_RESPONSE_OOM_STALL_BEFORE, STATIC_RESPONSE_OOM_STALL_AFTER)

    return text


RESPONSE_OOM_STALL_MEMBER_BEFORE = "  size_t _send_buffer_offset{0}, _send_buffer_len{0};\n"

RESPONSE_OOM_STALL_MEMBER_AFTER = """\
  size_t _send_buffer_offset{0}, _send_buffer_len{0};
  // Consecutive send-buffer allocation failures under heap pressure
  // (issue #67). See write_send_buffs's OOM branch in WebResponses.cpp.
  uint8_t _oomStallCount{0};
"""


def patch_response_oom_stall_member(text):
    if RESPONSE_OOM_STALL_MEMBER_AFTER not in text:
        if text.count(RESPONSE_OOM_STALL_MEMBER_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebResponseImpl.h AsyncAbstractResponse member list changed (oom stall); "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(RESPONSE_OOM_STALL_MEMBER_BEFORE, RESPONSE_OOM_STALL_MEMBER_AFTER)

    return text


# Issue #60/#67 (distinct from the OOM-stall guard above, same function): a
# zero-length read from _readDataFromCacheOrContent() is treated as
# unconditional EOF, even when the response's own declared Content-Length
# says there should be more data. Under heap pressure a file/content read can
# come back short or empty without that meaning "truly done" -- the response
# still transitions to RESPONSE_END and the connection closes having sent
# fewer bytes than promised, which is exactly issue #60's live
# net::ERR_CONTENT_LENGTH_MISMATCH. Only treat a zero read as done when the
# declared length has actually been reached (or is unknown); otherwise retry
# a bounded number of times, same discipline as the OOM-stall guard.
STATIC_RESPONSE_SHORT_READ_BEFORE = """\
        if (readLen == 0) {
          // no more data to send
          _state = RESPONSE_END;
        } else if (readLen != RESPONSE_TRY_AGAIN) {
"""

STATIC_RESPONSE_SHORT_READ_AFTER = """\
        if (readLen == 0) {
          if (_sendContentLength && _sentLength < _contentLength) {
#ifndef ASYNC_RESPONSE_SHORT_READ_STALL_MAX
#define ASYNC_RESPONSE_SHORT_READ_STALL_MAX 10
#endif
            // Content-Length says we're not done -- this zero read is a
            // stall, not EOF. Bound the retries and fail fast once exceeded,
            // instead of closing early with a truncated body (issue #60).
            if (++_shortReadStallCount >= ASYNC_RESPONSE_SHORT_READ_STALL_MAX) {
              request->client()->close();
              return 0;
            }
          } else {
            // no more data to send
            _state = RESPONSE_END;
          }
        } else if (readLen != RESPONSE_TRY_AGAIN) {
          _shortReadStallCount = 0;
"""


def patch_static_response_short_read(text):
    if STATIC_RESPONSE_SHORT_READ_AFTER not in text:
        if text.count(STATIC_RESPONSE_SHORT_READ_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebResponses.cpp readLen==0 EOF branch changed; "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(STATIC_RESPONSE_SHORT_READ_BEFORE, STATIC_RESPONSE_SHORT_READ_AFTER)

    return text


RESPONSE_SHORT_READ_MEMBER_BEFORE = "  uint8_t _oomStallCount{0};\n"

RESPONSE_SHORT_READ_MEMBER_AFTER = """\
  uint8_t _oomStallCount{0};
  // Consecutive zero-length reads received while Content-Length says the
  // response isn't done yet (issue #60/#67). See the readLen==0 branch in
  // write_send_buffs, WebResponses.cpp.
  uint8_t _shortReadStallCount{0};
"""


def patch_response_short_read_member(text):
    if RESPONSE_SHORT_READ_MEMBER_AFTER not in text:
        if text.count(RESPONSE_SHORT_READ_MEMBER_BEFORE) != 1:
            raise RuntimeError(
                "ESPAsyncWebServer WebResponseImpl.h AsyncAbstractResponse member list changed (short read); "
                "review tools/patch_async_sse.py"
            )
        text = text.replace(RESPONSE_SHORT_READ_MEMBER_BEFORE, RESPONSE_SHORT_READ_MEMBER_AFTER)

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
        # A missing target means a library upgrade moved or renamed the file:
        # silently skipping would drop the hardening this patch carries
        # (heap guards, nothrow allocation, exception containment) without
        # any build-time signal. Fail the build instead.
        raise RuntimeError(
            f"patch target missing: {path} ({description}); "
            "a library update likely moved this file - review tools/patch_async_sse.py"
        )

    text = path.read_text(encoding="utf-8")
    patched = patcher(text)
    if patched != text:
        path.write_text(patched, encoding="utf-8")
        print(f"[patch_async_sse.py] {description}")


def lib_source_dir(libdeps_env_dir, lib_name, required_file):
    versioned_source_dirs = [
        candidate / "src"
        for candidate in sorted(libdeps_env_dir.glob(f"{lib_name}@*"))
        if (candidate / "src" / required_file).exists()
    ]
    if len(versioned_source_dirs) > 1:
        raise RuntimeError(
            f"ambiguous library dependency dirs for {lib_name}; "
            "remove stale libdeps or review tools/patch_async_sse.py"
        )
    if versioned_source_dirs:
        return versioned_source_dirs[0]

    source_dir = libdeps_env_dir / lib_name / "src"
    if not (source_dir / required_file).exists():
        raise RuntimeError(
            f"library dependency missing: {lib_name}/src/{required_file}; "
            "review platformio.ini and tools/patch_async_sse.py"
        )
    return source_dir


def patch_async_webserver(env):
    libdeps_env_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"]
    source_dir = lib_source_dir(libdeps_env_dir, "ESPAsyncWebServer", "AsyncEventSource.h")
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
        source_dir / "WebResponseImpl.h",
        patch_response_oom_stall_member,
        "added AsyncAbstractResponse OOM-stall counter (issue #67)",
    )
    patch_file(
        source_dir / "WebResponses.cpp",
        patch_static_response_oom_stall,
        "bounded write_send_buffs OOM retries so a fragmented heap fails the "
        "response fast instead of stalling it indefinitely (issue #67)",
    )
    patch_file(
        source_dir / "WebResponseImpl.h",
        patch_response_short_read_member,
        "added AsyncAbstractResponse short-read-stall counter (issue #60/#67)",
    )
    patch_file(
        source_dir / "WebResponses.cpp",
        patch_static_response_short_read,
        "stopped a premature zero-length read from being treated as EOF "
        "before Content-Length is reached (issue #60/#67)",
    )
    patch_file(
        source_dir / "AsyncEventSource.cpp",
        patch_eventsource_response_alloc,
        "made SSE response allocation non-throwing (issue #21)",
    )
    patch_file(
        source_dir / "AsyncEventSource.cpp",
        patch_eventsource_switch_guard,
        "guarded the SSE client handover against racing connection teardown (issue #22)",
    )

    asynctcp_source_dir = lib_source_dir(libdeps_env_dir, "AsyncTCP", "AsyncTCP.cpp")
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
