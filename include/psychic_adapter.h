// =============================================================================
// include/psychic_adapter.h
//
// PsychicHttp adapter prototype for issue #72.
// =============================================================================

#pragma once

#ifdef PA_USE_PSYCHICHTTP_PROTOTYPE

/// Initialize PsychicHttp server (replaces AsyncWebServer in prototype mode).
void initPsychicHttpServer();

/// Broadcast an SSE event to all connected clients.
void broadcastEvent(const char* eventType, const char* data);

#endif  // PA_USE_PSYCHICHTTP_PROTOTYPE
