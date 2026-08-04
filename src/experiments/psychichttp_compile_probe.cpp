/**
 * @file psychichttp_compile_probe.cpp
 * @brief Minimal PsychicHttp compile probe for issue #70.
 *
 * This file exists only to verify that PsychicHttp 3.1.2 can compile cleanly
 * on protoArtoo's pinned stack (pioarduino 55.03.37 = ESP-IDF 5.5.2 + arduino-esp32 3.3.7).
 * It is NOT linked into the production firmware; it is compile-evidence only.
 *
 * To compile this probe without building the main firmware:
 *   pio run -e protoArtoo_psychichttp_check
 *
 * This file is guarded by #ifdef so it only compiles when the protoArtoo_psychichttp_check
 * env is used (which passes -DPA_PROBE_PSYCHICHTTP). In regular builds, this file is empty.
 */

#ifdef PA_PROBE_PSYCHICHTTP

#include <Arduino.h>
#include <PsychicHttp.h>

namespace psych_probe {

// Dummy variable that holds a pointer to demonstrate PsychicHttpServer is available.
// Not used at runtime; exists only for compile-time symbol resolution.
// This forces the compiler to resolve PsychicHttpServer symbols, proving the library
// is available and compatible with the pinned stack.
PsychicHttpServer* unused_server_ptr = nullptr;

}  // namespace psych_probe

#endif  // PA_PROBE_PSYCHICHTTP
