// =============================================================================
// include/web_server_psychic.h
//
// PsychicHttp server bring-up for the WebRequest seam (ADR 0021).
// =============================================================================
#pragma once

// Start the PsychicHttp server and register the route table. Must be called
// from the WiFi event callback path (startHttpServerOnce()), never directly
// from setup().
void initPsychicWebServer();
