// =============================================================================
// include/web_network_bootstrap.h
//
// WiFi, mDNS, OTA, and network recovery bootstrap for protoArtoo.
// This module handles network-level bring-up concerns: WiFi event handling,
// device posture decisions, and network recovery gesture evaluation.
//
// ARCHITECTURE: Callback Pair with HTTP Server
// - web_server.cpp (HTTP layer) owns initialization entry point (webServerInit)
//   and registers the WiFi event handler (handleWiFiEvent)
// - web_network_bootstrap.cpp (bootstrap layer) handles WiFi events and calls
//   back to startHttpServerOnce() (in web_server.cpp) when WiFi is ready
// - This creates an intentional two-way dependency that preserves the critical
//   invariant: initPsychicWebServer() is invoked from the WiFi event callback
//   path, never from setup() - each unit needs the other for correct behavior
// - All symbols declared in their respective owner headers; no cross-include
//   of .cpp files required for linking
// =============================================================================
#pragma once

#include <Arduino.h>

#ifdef ARDUINO
#include <WiFi.h>
#else
// Native test build: WiFiEvent_t is a typedef defined in native_test_stubs.cpp
typedef int WiFiEvent_t;
#endif

// WiFi event handler. Called by the WiFi driver when WiFi events occur
// (AP started, STA connected, etc.). Starts the HTTP server upon successful
// WiFi connection.
void handleWiFiEvent(WiFiEvent_t event);

// Network bootstrap: decide and apply WiFi boot posture. Evaluates the
// network recovery gesture, builds the developer WiFi shortcut, decides the
// boot posture (provisioning, client mode, standalone AP, or recovery), and
// applies it. Called from webServerInit() in web_server.cpp.
void webNetworkBootstrap();
