// =============================================================================
// include/hosted_link_degraded_announcement.h
//
// The dome logic-text command fired once when the Hosted Link Supervisor
// (include/hosted_link_supervisor.h) settles into the terminal
// HostedLinkPhase::Degraded (src/web/web_network_manager_hosted.cpp, #189).
//
// Kept as a native-includable constant -- no Arduino/ESP-IDF/FreeRTOS deps --
// rather than a string literal inlined at its one call site, so a native test
// can validate it against the real Protocol Check grammar
// (protocolCheckBranch(), src/protocol_check.cpp) instead of by inspection.
// One source of truth for the string means a later DT: grammar change that
// would reject this exact command fails a native test instead of only being
// discovered on hardware.
// =============================================================================
#pragma once

// DT:<target>:<color>:<durationSec>:<speed>:<encodedText> -- Logic Text
// (docs/dome-visual-authoring-contract.md, src/protocol_check.cpp:400-524).
//
// - target LOGIC = FLD+RLD (both logic displays), so the announcement is
//   visible from either side of the dome.
// - color RED signals a fault condition (matches the DL/DH alarm/flash
//   palette elsewhere in the contract).
// - duration 10, speed 0 match the contract's own worked examples
//   (`DT:FLD:DEFAULT:10:0:...`); no device-proven reason to pick otherwise.
// - "NETWORK DOWN" needs no percent-encoding: it has no ':' or '%', and every
//   character (including the plain space) is printable ASCII
//   (src/protocol_check.cpp isPrintable(), 0x20-0x7E) -- percentDecode()
//   only requires escaping ':', '%', and newline.
inline constexpr const char* kHostedLinkDegradedDomeText = "DT:LOGIC:RED:10:0:NETWORK DOWN";
