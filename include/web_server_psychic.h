// =============================================================================
// include/web_server_psychic.h
//
// PsychicHttp server bring-up for the WebRequest seam (ADR 0021). Compiled
// only when PA_WEB_BACKEND_PSYCHIC is defined (env protoArtoo_psychic).
// =============================================================================
#pragma once

// Start the PsychicHttp server and register the ported route groups. Must be
// called from the WiFi event callback path (startHttpServerOnce()), never
// directly from setup().
void initPsychicWebServer();
