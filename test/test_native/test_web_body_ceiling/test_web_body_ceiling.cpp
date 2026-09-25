// =============================================================================
// test/test_native/test_web_body_ceiling/test_web_body_ceiling.cpp
//
// The server-wide request-body ceiling the PsychicHttp backend enforces (#427).
//
// The library buffers every body under that ceiling, for every route, before a
// handler runs. It must end at the largest bound a route declares on this
// board, not at the library's own starting value. These tests drive the same
// reset-then-admit sequence initPsychicWebServer() runs; they do not run the
// device server.
// =============================================================================
#include <unity.h>

#include "api_config.h"      // kConfigPostMaxBodyBytes
#include "seq_store_util.h"  // SEQ_FILE_MAX_BYTES
#include "web_body_ceiling.h"
#include "web_request.h"     // kDefaultMaxBodyBytes

void setUp() {
}
void tearDown() {
}

// What PsychicHttpServer's constructor leaves in maxRequestBodySize
// (MAX_REQUEST_BODY_SIZE, PsychicCore.h). Only ever the starting state here.
static const unsigned long kVendorStart = 16 * 1024;

// The bounds webRegisterSeamRoutes() declares, in the shape it declares them:
// every route takes the default except the two that name their own.
static unsigned long registerSeamRouteBounds() {
    unsigned long ceiling = kVendorStart;
    webBodyCeilingReset(ceiling);
    webBodyCeilingAdmitRoute(ceiling, kDefaultMaxBodyBytes);      // /api/identity
    webBodyCeilingAdmitRoute(ceiling, kConfigPostMaxBodyBytes);   // POST /api/config
    webBodyCeilingAdmitRoute(ceiling, kDefaultMaxBodyBytes);      // /api/console
    webBodyCeilingAdmitRoute(ceiling, SEQ_FILE_MAX_BYTES);        // POST /api/seq
    webBodyCeilingAdmitRoute(ceiling, kDefaultMaxBodyBytes);      // /api/rc/debug
    return ceiling;
}

void test_the_ceiling_ends_at_the_largest_route_bound() {
    const size_t largest = kConfigPostMaxBodyBytes > SEQ_FILE_MAX_BYTES
                               ? kConfigPostMaxBodyBytes
                               : SEQ_FILE_MAX_BYTES;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)largest, (uint32_t)registerSeamRouteBounds());
}

void test_default_bounded_routes_alone_buffer_only_the_default() {
    unsigned long ceiling = kVendorStart;
    webBodyCeilingReset(ceiling);
    webBodyCeilingAdmitRoute(ceiling, kDefaultMaxBodyBytes);
    webBodyCeilingAdmitRoute(ceiling, kDefaultMaxBodyBytes);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)kDefaultMaxBodyBytes, (uint32_t)ceiling);
}

void test_a_smaller_route_never_lowers_the_ceiling() {
    unsigned long ceiling = kVendorStart;
    webBodyCeilingReset(ceiling);
    webBodyCeilingAdmitRoute(ceiling, SEQ_FILE_MAX_BYTES);
    webBodyCeilingAdmitRoute(ceiling, 256);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SEQ_FILE_MAX_BYTES, (uint32_t)ceiling);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_the_ceiling_ends_at_the_largest_route_bound);
    RUN_TEST(test_default_bounded_routes_alone_buffer_only_the_default);
    RUN_TEST(test_a_smaller_route_never_lowers_the_ceiling);
    return UNITY_END();
}
