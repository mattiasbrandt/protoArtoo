// =============================================================================
// include/marcduino.h
//
// Marcduino command string constants for protoArtoo body controller.
// These are the commands sent TO the dome (body->dome) and received FROM
// the dome (dome->body).
//
// Protocol: ASCII strings terminated with \r
// Baud: 9600 on UART1 (dome serial via slip ring)
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Body->Dome heartbeat (sent at 1 Hz by DomeLinkTask)
// Dome responds with #APHB
// -----------------------------------------------------------------------------
#define MD_BODY_HB "#PAHB\r"

// -----------------------------------------------------------------------------
// Dome->Body heartbeat (received from dome)
// -----------------------------------------------------------------------------
#define MD_DOME_HB "#APHB\r"

// -----------------------------------------------------------------------------
// Body->Dome sleep sync (state change notifications)
// -----------------------------------------------------------------------------
#define MD_BODY_SLEEP "#PASL\r"
#define MD_BODY_WAKE "#PAWU\r"

// -----------------------------------------------------------------------------
// Body->Dome: full-droid sequences (triggers coordinated dome + body action)
// -----------------------------------------------------------------------------
#define MD_SEQ_SCREAM ":SE01\r"        // Scream sequence
#define MD_SEQ_WAVE ":SE02\r"          // Wave sequence
#define MD_SEQ_FAST_WAVE ":SE03\r"     // Fast wave
#define MD_SEQ_OPEN_WAVE ":SE04\r"     // Open wave
#define MD_SEQ_BEEP_CANTINA ":SE05\r"  // Beep cantina
#define MD_SEQ_FAINT ":SE06\r"         // Faint/fall
#define MD_SEQ_CANTINA ":SE07\r"       // Cantina
#define MD_SEQ_LEIA ":SE08\r"          // Leia message
#define MD_SEQ_DISCO ":SE09\r"         // Disco
#define MD_SEQ_QUIET ":SE10\r"         // Quiet mode
#define MD_SEQ_WIDE_AWAKE ":SE11\r"    // Wide awake
#define MD_SEQ_TOP_PANELS ":SE12\r"    // Top panels open
#define MD_SEQ_MEDIUM_AWAKE ":SE13\r"  // Medium awake
#define MD_SEQ_RYTHM ":SE14\r"         // Rythm
#define MD_SEQ_HAPPY ":SE15\r"         // Happy

// -----------------------------------------------------------------------------
// Body->Dome: arm commands (dome triggers body arm via dome->body serial)
// These are received BY the body from the dome
// -----------------------------------------------------------------------------
#define MD_ARM1_OPEN ":OP01\r"
#define MD_ARM1_CLOSE ":CL01\r"
#define MD_ARM2_OPEN ":OP02\r"
#define MD_ARM2_CLOSE ":CL02\r"

// -----------------------------------------------------------------------------
// Audio play commands ($ prefix = play sound on body audio module)
// Sent dome->body; body AudioTask handles these
// -----------------------------------------------------------------------------
#define MD_AUDIO_RANDOM "$S\r"         // Random sound from configured range
#define MD_AUDIO_TRACK(n) "$" #n "\r"  // Play specific track n
