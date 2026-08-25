// =============================================================================
// include/web_network_bootstrap.h
//
// WiFi boot posture decision and application for protoArtoo.
// This module handles device-level posture concerns: evaluating network recovery
// gestures, building developer WiFi shortcuts, deciding boot postures, and
// applying them. Event handling and registration are the network manager seam's
// job (web_network_manager.h).
//
// ARCHITECTURE: Vendor-Free Abstraction via Network Manager Seam
// - web_server.cpp (HTTP layer) owns initialization entry point (webServerInit)
//   and calls networkManagerInitialize() (web_network_manager.h) instead of
//   registering a WiFi event handler directly
// - web_network_bootstrap.cpp (bootstrap layer) provides pure posture decision
//   logic and coordinates posture application with the network manager backend
// - The network manager backend handles vendor event translation and registration;
//   this module handles only platform-agnostic boot decisions
// - This creates an intentional three-way contract that preserves the critical
//   invariant: initPsychicWebServer() is invoked from the WiFi event callback
//   path, never from setup() - each unit needs the others for correct behavior
// =============================================================================
#pragma once

// Network bootstrap: decide and apply WiFi boot posture. Evaluates the
// network recovery gesture, builds the developer WiFi shortcut, decides the
// boot posture (provisioning, client mode, standalone AP, or recovery), and
// applies it. Called from webServerInit() in web_server.cpp.
void webNetworkBootstrap();
