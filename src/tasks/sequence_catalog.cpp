// =============================================================================
// src/tasks/sequence_catalog.cpp
//
// Body-owned DM:* sequence catalog and alias tables (ADR 0004, issue #2).
// Choreography source of truth: issue #2 comment 1 (dome agent spec).
//
// Pure data + lookup; no FreeRTOS or hardware dependencies (natively testable).
// Step timing convention: tMs is absolute from sequence start; steps inside a
// STEP_LOOP body are relative to the iteration start.
// =============================================================================

#include <string.h>

#include "audio_playback_policy.h"  // AudioPlaybackCategory / AudioPlaybackSlot
#include "seq_store_index.h"        // runtime (Learned Sequence) name index
#include "sequence_dispatcher.h"
#include "sequence_engine.h"

#define SEQ_STEPCOUNT(arr) ((uint8_t)(sizeof(arr) / sizeof((arr)[0])))

// Effect-class convention: tag the FIRST step that activates each persistent
// effect (panel open, logic/PSI mode, holo effect, long audio). The engine
// auto-emits the matching resets (@0T1/@0P1, *ST00, :CL00, audio stop) on
// terminal transitions, so tables do not repeat standard cleanup steps.

// =============================================================================
// Flat sequences
// =============================================================================

// DM:VADER — Imperial March visual mode (47 s).
// Holos, logics, and PSI set to MARCH mode; auto-reset at sequence end.
static const SeqStep kVaderSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO, "$M"),             // Imperial March (stops on abort)
    SEQ_DOME(0, FX_HOLO, "@HPA0021|47"),         // all holos red 47 s
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T11"),          // MARCH logics
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P11"),          // MARCH PSI
    SEQ_TERM(47000),
};

// DM:HELLO — "Hello There" greeting (4 s).
// Front and rear logic text, then a six-pulse P1 panel wave.
static const SeqStep kHelloSteps[] = {
    SEQ_AUDIO(0, "$H"),                          // happy/greeting clip
    SEQ_DOME(0, FX_NONE, "@1MHello There"),      // front logic text
    SEQ_DOME(0, FX_NONE, "@3MGeneral Kenobi"),
    SEQ_DOME(0, FX_PANEL, ":OP01"),      // P1 open
    SEQ_DOME(160, FX_NONE, ":OP01"),     // P1 half
    SEQ_DOME(320, FX_NONE, ":OP01"),     // P1 open
    SEQ_DOME(480, FX_NONE, ":OP01"),     // P1 half
    SEQ_DOME(640, FX_NONE, ":OP01"),     // P1 open
    SEQ_DOME(800, FX_NONE, ":CL01"),      // P1 close
    SEQ_TERM(950),                               // auto :CL00 (close + release)
};

// DM:NOD — short acknowledgment: sound + logic text + P1 wave.
// Demonstrates sound-to-motion sync from a single body clock (issue #2).
static const SeqStep kNodSteps[] = {
    SEQ_AUDIO(0, "$H"),                          // ack/happy clip
    SEQ_DOME(0, FX_NONE, "@1MYes"),              // logic text
    SEQ_DOME(0, FX_PANEL, ":OP01"),      // P1 open
    SEQ_DOME(150, FX_NONE, ":CL01"),      // P1 close
    SEQ_TERM(300),                               // auto scoped :CL15 (ring-only close + release)
};

// DM:FLUTTER — ring then pie panels sweep to 75%, then close (10 s window).
// Intent-adapted from previous pulse choreography; partial-open fidelity is
// intentionally not preserved in body-authored form.
static const SeqStep kFlutterSteps[] = {
    // ring to 75% — P1,P2,P3,P4,P7,P11,P13
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(150, FX_NONE, ":OP02"),
    SEQ_DOME(300, FX_NONE, ":OP03"),
    SEQ_DOME(450, FX_NONE, ":OP04"),
    SEQ_DOME(600, FX_NONE, ":OP07"),
    SEQ_DOME(750, FX_NONE, ":OP11"),
    SEQ_DOME(900, FX_NONE, ":OP13"),
    // pies to 75% — PP1,PP2,PP3,PP4,PP5,PP6
    SEQ_DOME(1050, FX_NONE, ":OPP1"),
    SEQ_DOME(1200, FX_NONE, ":OPP2"),
    SEQ_DOME(1350, FX_NONE, ":OPP3"),
    SEQ_DOME(1500, FX_NONE, ":OPP4"),
    SEQ_DOME(1650, FX_NONE, ":OPP5"),
    SEQ_DOME(1800, FX_NONE, ":OPP6"),
    // close ring
    SEQ_DOME(1950, FX_NONE, ":CL01"),
    SEQ_DOME(2100, FX_NONE, ":CL02"),
    SEQ_DOME(2250, FX_NONE, ":CL03"),
    SEQ_DOME(2400, FX_NONE, ":CL04"),
    SEQ_DOME(2550, FX_NONE, ":CL07"),
    SEQ_DOME(2700, FX_NONE, ":CL11"),
    SEQ_DOME(2850, FX_NONE, ":CL13"),
    // close pies
    SEQ_DOME(3000, FX_NONE, ":CLP1"),
    SEQ_DOME(3150, FX_NONE, ":CLP2"),
    SEQ_DOME(3300, FX_NONE, ":CLP3"),
    SEQ_DOME(3450, FX_NONE, ":CLP4"),
    SEQ_DOME(3600, FX_NONE, ":CLP5"),
    SEQ_DOME(3750, FX_NONE, ":CLP6"),
    SEQ_TERM(4250),                              // auto :CL00 (release)
};

// DM:BLOOM — pies open together over 1.2 s, wiggle three times, close (8 s).
// Intent-adapted from previous pulse choreography; sine/easing fidelity is
// intentionally not preserved in body-authored form.
static const SeqStep kBloomSteps[] = {
    SEQ_DOME(0, FX_PANEL, ":OPP1"),
    SEQ_DOME(0, FX_NONE, ":OPP2"),
    SEQ_DOME(0, FX_NONE, ":OPP3"),
    SEQ_DOME(0, FX_NONE, ":OPP4"),
    SEQ_DOME(0, FX_NONE, ":OPP5"),
    SEQ_DOME(0, FX_NONE, ":OPP6"),
    // wiggle cycle 1 (bloom hold ends at 3250)
    SEQ_DOME(3250, FX_NONE, ":OPP1"),
    SEQ_DOME(3250, FX_NONE, ":OPP2"),
    SEQ_DOME(3250, FX_NONE, ":OPP3"),
    SEQ_DOME(3250, FX_NONE, ":OPP4"),
    SEQ_DOME(3250, FX_NONE, ":OPP5"),
    SEQ_DOME(3250, FX_NONE, ":OPP6"),
    SEQ_DOME(3430, FX_NONE, ":OPP1"),
    SEQ_DOME(3430, FX_NONE, ":OPP2"),
    SEQ_DOME(3430, FX_NONE, ":OPP3"),
    SEQ_DOME(3430, FX_NONE, ":OPP4"),
    SEQ_DOME(3430, FX_NONE, ":OPP5"),
    SEQ_DOME(3430, FX_NONE, ":OPP6"),
    // wiggle cycle 2
    SEQ_DOME(3610, FX_NONE, ":OPP1"),
    SEQ_DOME(3610, FX_NONE, ":OPP2"),
    SEQ_DOME(3610, FX_NONE, ":OPP3"),
    SEQ_DOME(3610, FX_NONE, ":OPP4"),
    SEQ_DOME(3610, FX_NONE, ":OPP5"),
    SEQ_DOME(3610, FX_NONE, ":OPP6"),
    SEQ_DOME(3790, FX_NONE, ":OPP1"),
    SEQ_DOME(3790, FX_NONE, ":OPP2"),
    SEQ_DOME(3790, FX_NONE, ":OPP3"),
    SEQ_DOME(3790, FX_NONE, ":OPP4"),
    SEQ_DOME(3790, FX_NONE, ":OPP5"),
    SEQ_DOME(3790, FX_NONE, ":OPP6"),
    // wiggle cycle 3
    SEQ_DOME(3970, FX_NONE, ":OPP1"),
    SEQ_DOME(3970, FX_NONE, ":OPP2"),
    SEQ_DOME(3970, FX_NONE, ":OPP3"),
    SEQ_DOME(3970, FX_NONE, ":OPP4"),
    SEQ_DOME(3970, FX_NONE, ":OPP5"),
    SEQ_DOME(3970, FX_NONE, ":OPP6"),
    SEQ_DOME(4150, FX_NONE, ":OPP1"),
    SEQ_DOME(4150, FX_NONE, ":OPP2"),
    SEQ_DOME(4150, FX_NONE, ":OPP3"),
    SEQ_DOME(4150, FX_NONE, ":OPP4"),
    SEQ_DOME(4150, FX_NONE, ":OPP5"),
    SEQ_DOME(4150, FX_NONE, ":OPP6"),
    // close pies together after a 1 s hold
    SEQ_DOME(5150, FX_NONE, ":CLP1"),
    SEQ_DOME(5150, FX_NONE, ":CLP2"),
    SEQ_DOME(5150, FX_NONE, ":CLP3"),
    SEQ_DOME(5150, FX_NONE, ":CLP4"),
    SEQ_DOME(5150, FX_NONE, ":CLP5"),
    SEQ_DOME(5150, FX_NONE, ":CLP6"),
    SEQ_TERM(5650),                              // auto :CL00 (release)
};

// DM:LEIA — Leia message mode (36 s): front holo Leia, other holos off,
// Leia logics/PSI; everything resets via effect classes at the end.
static const SeqStep kLeiaSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO, "$L"),             // Leia message (stops on abort)
    SEQ_DOME(0, FX_HOLO, "@HPS101|36"),          // front holo Leia sequence 36 s
    SEQ_DOME(0, FX_HOLO, "@HPR02|36"),           // rear holo off 36 s
    SEQ_DOME(0, FX_HOLO, "@HPT02|36"),           // top holo off 36 s
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T6"),           // Leia logics
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P6"),           // Leia PSI
    SEQ_TERM(36000),                             // auto @0T1/@0P1/*ST00
};

// DM:ALARM — pulsing red holos/logics/PSI (10 s). Audio: random track from
// the alert category, falling back to the named scream track.
static const SeqStep kAlarmSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_ALERT, AUDIO_SLOT_NAMED_SCREAM),
    SEQ_DOME(0, FX_HOLO, "@HPA0021|10"),         // all holos red flashes 10 s
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T3"),           // alarm logics
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P3"),           // alarm PSI
    SEQ_TERM(10000),                             // auto @0T1/@0P1/*ST00
};

// DM:HEART — rainbow holos and a sweet logic message (10 s). Audio: random
// track from the sentimental category, falling back to the named happy track.
// Logic text is single-line per issue #2 gap #3 (no wire escaping scheme yet).
static const SeqStep kHeartSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_SENTIMENTAL, AUDIO_SLOT_NAMED_HAPPY),
    SEQ_DOME(0, FX_HOLO, "@HPF006|10"),          // front holo rainbow 10 s
    SEQ_DOME(0, FX_HOLO, "@HPR006|10"),          // rear holo rainbow 10 s
    SEQ_DOME(0, FX_HOLO, "@HPT006|10"),          // top holo rainbow 10 s
    SEQ_DOME(0, FX_NONE, "@1MYou're Wonderful"), // front logic text
    SEQ_DOME(0, FX_LOGIC_PSI, "@1P2"),           // front PSI flash-color
    SEQ_TERM(10000),                             // auto @0T1/@0P1/*ST00
};

// DM:RESET — stop audio, close and release all panels, reset holos/logics/PSI.
// The resets ARE the choreography here, so they are explicit steps; the :CL00
// also clears all body-side toggle latches.
static const SeqStep kResetSteps[] = {
    SEQ_AUDIO(0, "$s"),                          // stop playback
    SEQ_DOME(0, FX_NONE, ":CL00"),               // close + release all panels
    SEQ_DOME(1150, FX_NONE, "*ST00"),            // reset holos
    SEQ_DOME(1150, FX_NONE, "@0T1"),             // reset logics
    SEQ_DOME(1150, FX_NONE, "@0P1"),             // reset PSI
    SEQ_TERM(1500),
};

// =============================================================================
// Loop sequences — a STEP_LOOP header repeats the following bodyCount steps
// every periodMs while the iteration start is inside durationMs. Body step
// times are relative to the iteration start. Post-loop steps are authored
// past the worst-case loop end (the final iteration may overhang durationMs).
// =============================================================================

// DM:CANTINA — 130 BPM alternating panel dance for ~15 s (17 s window).
// Two beats per iteration: group A open / group B closed, then inverted.
// 8 iterations x 1846 ms from t=100 -> loop ends at ~14868 ms.
static const SeqStep kCantinaSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO, "$C"),             // long Cantina (stops on abort)
    SEQ_DOME(0, FX_HOLO, "@HPA0029|15"),         // all holos white flashes 15 s
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T2"),           // flash/color logics
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P2"),           // flash/color PSI
    SEQ_LOOP(100, 26, 1846, 14000),
    // beat A: pies PP1/PP4/PP3 + ring P1/P3/P7/P13 open, the rest closed
    SEQ_DOME(0, FX_PANEL, ":OPP1"),
    SEQ_DOME(0, FX_NONE, ":OPP4"),
    SEQ_DOME(0, FX_NONE, ":OPP3"),
    SEQ_DOME(0, FX_NONE, ":CLP2"),
    SEQ_DOME(0, FX_NONE, ":CLP5"),
    SEQ_DOME(0, FX_NONE, ":CLP6"),
    SEQ_DOME(0, FX_NONE, ":OP01"),
    SEQ_DOME(0, FX_NONE, ":OP03"),
    SEQ_DOME(0, FX_NONE, ":OP07"),
    SEQ_DOME(0, FX_NONE, ":OP13"),
    SEQ_DOME(0, FX_NONE, ":CL02"),
    SEQ_DOME(0, FX_NONE, ":CL04"),
    SEQ_DOME(0, FX_NONE, ":CL11"),
    // beat B: inverted groups
    SEQ_DOME(923, FX_NONE, ":CLP1"),
    SEQ_DOME(923, FX_NONE, ":CLP4"),
    SEQ_DOME(923, FX_NONE, ":CLP3"),
    SEQ_DOME(923, FX_NONE, ":OPP2"),
    SEQ_DOME(923, FX_NONE, ":OPP5"),
    SEQ_DOME(923, FX_NONE, ":OPP6"),
    SEQ_DOME(923, FX_NONE, ":CL01"),
    SEQ_DOME(923, FX_NONE, ":CL03"),
    SEQ_DOME(923, FX_NONE, ":CL07"),
    SEQ_DOME(923, FX_NONE, ":CL13"),
    SEQ_DOME(923, FX_NONE, ":OP02"),
    SEQ_DOME(923, FX_NONE, ":OP04"),
    SEQ_DOME(923, FX_NONE, ":OP11"),
    SEQ_TERM(15400),                             // auto @0T1/@0P1/*ST00/:CL00
};

// DM:ROCKMARCH — Imperial March with one ring panel stepping per beat
// (923 ms) for ~45 s (47 s window). One iteration = one full ring pass:
// open slot k at k*923, close it 773 ms later. 7 iterations x 6461 ms.
static const SeqStep kRockmarchSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO, "$M"),             // Imperial March (stops on abort)
    SEQ_DOME(0, FX_HOLO, "@HPA0021|47"),         // all holos red flashes 47 s
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T11"),          // MARCH logics
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P11"),          // MARCH PSI
    SEQ_LOOP(0, 14, 6461, 45000),
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(773, FX_NONE, ":CL01"),
    SEQ_DOME(923, FX_NONE, ":OP02"),
    SEQ_DOME(1696, FX_NONE, ":CL02"),
    SEQ_DOME(1846, FX_NONE, ":OP03"),
    SEQ_DOME(2619, FX_NONE, ":CL03"),
    SEQ_DOME(2769, FX_NONE, ":OP04"),
    SEQ_DOME(3542, FX_NONE, ":CL04"),
    SEQ_DOME(3692, FX_NONE, ":OP07"),
    SEQ_DOME(4465, FX_NONE, ":CL07"),
    SEQ_DOME(4615, FX_NONE, ":OP11"),
    SEQ_DOME(5388, FX_NONE, ":CL11"),
    SEQ_DOME(5538, FX_NONE, ":OP13"),
    SEQ_DOME(6311, FX_NONE, ":CL13"),
    SEQ_AUDIO(47000, "$s"),                      // stop the march; $M outlives the sequence otherwise
    SEQ_TERM(47300),                             // auto @0T1/@0P1/*ST00 + scoped :CL15 (ring-only)
};

// =============================================================================
// Random sequences — STEP_RANDOM resolves a logical panel target and optional
// timing jitter at fire time. SLOTSET_HOLD reuses the previous pick;
// pickDistinct avoids slots already picked this run.
// =============================================================================

// DM:SCREAM — panels burst open, red alert, random one-panel flutter, close
// (15 s window). Flutter: 10 iterations of a 380 ms 4-move pattern on a
// randomly picked panel (repeats across iterations allowed, as in the dome's
// original code). Audio: random scream-category track (fallback named scream).
static const SeqStep kScreamSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_SCREAM, AUDIO_SLOT_NAMED_SCREAM),
    SEQ_DOME(0, FX_HOLO, "@HPA0070"),            // all holos short-circuit color
    SEQ_DOME(0, FX_HOLO, "@HPA105|5"),           // all holos wag 5 times
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T5"),           // red alert logics
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P5"),           // red alert PSI
    // burst open: all pies then all ring panels together
    SEQ_DOME(0, FX_PANEL, ":OPP1"),
    SEQ_DOME(0, FX_NONE, ":OPP2"),
    SEQ_DOME(0, FX_NONE, ":OPP3"),
    SEQ_DOME(0, FX_NONE, ":OPP4"),
    SEQ_DOME(0, FX_NONE, ":OPP5"),
    SEQ_DOME(0, FX_NONE, ":OPP6"),
    SEQ_DOME(0, FX_NONE, ":OP01"),
    SEQ_DOME(0, FX_NONE, ":OP02"),
    SEQ_DOME(0, FX_NONE, ":OP03"),
    SEQ_DOME(0, FX_NONE, ":OP04"),
    SEQ_DOME(0, FX_NONE, ":OP07"),
    SEQ_DOME(0, FX_NONE, ":OP11"),
    SEQ_DOME(0, FX_NONE, ":OP13"),
    // random flutter: pick a panel, half-close/reopen it twice per iteration
    SEQ_LOOP(200, 4, 380, 3800),
    SEQ_RAND(0, SLOTSET_ALL, RAND_FLUTTER, 0, 100, 0, 0),
    SEQ_RAND(100, SLOTSET_HOLD, RAND_OPEN, 0, 100, 0, 0),
    SEQ_RAND(180, SLOTSET_HOLD, RAND_FLUTTER, 0, 100, 0, 0),
    SEQ_RAND(280, SLOTSET_HOLD, RAND_OPEN, 0, 100, 0, 0),
    // happy all-clear cue before the auto-reset closes everything
    SEQ_AUDIO(6800, "$H"),
    SEQ_TERM(7450),                              // auto @0T1/@0P1/*ST00/:CL00
};

// DM:OVERLOAD — failure logics/PSI, holos short-circuit, six panels flutter
// on random logical targets, then everything resets (12 s
// window). Drift gaps are randomized as 0..500 ms jitter on fixed 650 ms
// offsets (issue #2: gap randomness, absolute schedule preserved). Audio:
// random sad-category track (fallback named faint).
static const SeqStep kOverloadSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_SAD, AUDIO_SLOT_NAMED_FAINT),
    SEQ_DOME(0, FX_LOGIC_PSI, "@1T4"),           // FLD failure
    SEQ_DOME(0, FX_LOGIC_PSI, "@2T4"),           // RLD failure
    SEQ_DOME(0, FX_HOLO, "@HPA0070"),            // all holos short circuit
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P4"),           // PSI failure
    // four distinct ring panels + two distinct pies flutter with timing jitter
    SEQ_RAND(400, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(1050, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(1700, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(2350, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(3000, SLOTSET_PIE, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(3650, SLOTSET_PIE, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_TERM(7000),                              // auto @0T1/@0P1/*ST00/:CL00
};

// =============================================================================
// Toggle sequences (ADR 0004 decision 8) — `steps` is the open branch,
// `closeSteps` the close branch; the engine picks by latched group state and
// flips the latch on normal completion. Close branches end without a release;
// the engine emits :CL00 once no group remains latched open (issue #2 gap #1).
// =============================================================================

// DM:PIES open — pie wave: open PP1->PP6, close PP6->PP1, reopen, twice (12 s).
static const SeqStep kPiesOpenSteps[] = {
    SEQ_AUDIO(100, "$H"),
    // cycle 1: open PP1->PP6
    SEQ_DOME(100, FX_PANEL, ":OPP1"),
    SEQ_DOME(200, FX_NONE, ":OPP2"),
    SEQ_DOME(300, FX_NONE, ":OPP3"),
    SEQ_DOME(400, FX_NONE, ":OPP4"),
    SEQ_DOME(500, FX_NONE, ":OPP5"),
    SEQ_DOME(600, FX_NONE, ":OPP6"),
    // cycle 1: close PP6->PP1
    SEQ_DOME(700, FX_NONE, ":CLP6"),
    SEQ_DOME(800, FX_NONE, ":CLP5"),
    SEQ_DOME(900, FX_NONE, ":CLP4"),
    SEQ_DOME(1000, FX_NONE, ":CLP3"),
    SEQ_DOME(1100, FX_NONE, ":CLP2"),
    SEQ_DOME(1200, FX_NONE, ":CLP1"),
    // cycle 1: reopen PP1->PP6
    SEQ_DOME(1300, FX_NONE, ":OPP1"),
    SEQ_DOME(1400, FX_NONE, ":OPP2"),
    SEQ_DOME(1500, FX_NONE, ":OPP3"),
    SEQ_DOME(1600, FX_NONE, ":OPP4"),
    SEQ_DOME(1700, FX_NONE, ":OPP5"),
    SEQ_DOME(1800, FX_NONE, ":OPP6"),
    // cycle 2: open PP1->PP6
    SEQ_DOME(1900, FX_NONE, ":OPP1"),
    SEQ_DOME(2000, FX_NONE, ":OPP2"),
    SEQ_DOME(2100, FX_NONE, ":OPP3"),
    SEQ_DOME(2200, FX_NONE, ":OPP4"),
    SEQ_DOME(2300, FX_NONE, ":OPP5"),
    SEQ_DOME(2400, FX_NONE, ":OPP6"),
    // cycle 2: close PP6->PP1
    SEQ_DOME(2500, FX_NONE, ":CLP6"),
    SEQ_DOME(2600, FX_NONE, ":CLP5"),
    SEQ_DOME(2700, FX_NONE, ":CLP4"),
    SEQ_DOME(2800, FX_NONE, ":CLP3"),
    SEQ_DOME(2900, FX_NONE, ":CLP2"),
    SEQ_DOME(3000, FX_NONE, ":CLP1"),
    // cycle 2: reopen PP1->PP6 — pies end open
    SEQ_DOME(3100, FX_NONE, ":OPP1"),
    SEQ_DOME(3200, FX_NONE, ":OPP2"),
    SEQ_DOME(3300, FX_NONE, ":OPP3"),
    SEQ_DOME(3400, FX_NONE, ":OPP4"),
    SEQ_DOME(3500, FX_NONE, ":OPP5"),
    SEQ_DOME(3600, FX_NONE, ":OPP6"),
    SEQ_TERM(4600),
};

// DM:PIES close — reset holos, close PP1->PP6 serially.
static const SeqStep kPiesCloseSteps[] = {
    SEQ_DOME(0, FX_NONE, "*ST00"),
    SEQ_AUDIO(0, "$H"),
    SEQ_DOME(0, FX_NONE, ":CLP1"),
    SEQ_DOME(150, FX_NONE, ":CLP2"),
    SEQ_DOME(300, FX_NONE, ":CLP3"),
    SEQ_DOME(450, FX_NONE, ":CLP4"),
    SEQ_DOME(600, FX_NONE, ":CLP5"),
    SEQ_DOME(750, FX_NONE, ":CLP6"),
    SEQ_TERM(1700),
};

// DM:LOW open — ring wave twice, then all ring panels open (15 s).
static const SeqStep kLowOpenSteps[] = {
    SEQ_AUDIO(0, "$H"),
    // cycle 1: open P1,P13,P11,P2,P3,P4,P7
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(150, FX_NONE, ":OP13"),
    SEQ_DOME(300, FX_NONE, ":OP11"),
    SEQ_DOME(450, FX_NONE, ":OP02"),
    SEQ_DOME(600, FX_NONE, ":OP03"),
    SEQ_DOME(750, FX_NONE, ":OP04"),
    SEQ_DOME(900, FX_NONE, ":OP07"),
    // cycle 1: close P7,P4,P3,P2,P1, then P13, then P11 (50 ms breaths)
    SEQ_DOME(1050, FX_NONE, ":CL07"),
    SEQ_DOME(1200, FX_NONE, ":CL04"),
    SEQ_DOME(1350, FX_NONE, ":CL03"),
    SEQ_DOME(1500, FX_NONE, ":CL02"),
    SEQ_DOME(1650, FX_NONE, ":CL01"),
    SEQ_DOME(1850, FX_NONE, ":CL13"),
    SEQ_DOME(2050, FX_NONE, ":CL11"),
    // cycle 2: open
    SEQ_DOME(2200, FX_NONE, ":OP01"),
    SEQ_DOME(2350, FX_NONE, ":OP13"),
    SEQ_DOME(2500, FX_NONE, ":OP11"),
    SEQ_DOME(2650, FX_NONE, ":OP02"),
    SEQ_DOME(2800, FX_NONE, ":OP03"),
    SEQ_DOME(2950, FX_NONE, ":OP04"),
    SEQ_DOME(3100, FX_NONE, ":OP07"),
    // cycle 2: close
    SEQ_DOME(3250, FX_NONE, ":CL07"),
    SEQ_DOME(3400, FX_NONE, ":CL04"),
    SEQ_DOME(3550, FX_NONE, ":CL03"),
    SEQ_DOME(3700, FX_NONE, ":CL02"),
    SEQ_DOME(3850, FX_NONE, ":CL01"),
    SEQ_DOME(4050, FX_NONE, ":CL13"),
    SEQ_DOME(4250, FX_NONE, ":CL11"),
    // final open: P11/P13/P1 together, then P2,P3,P4,P7 — ring ends open
    SEQ_DOME(4400, FX_NONE, ":OP11"),
    SEQ_DOME(4400, FX_NONE, ":OP13"),
    SEQ_DOME(4400, FX_NONE, ":OP01"),
    SEQ_DOME(4500, FX_NONE, ":OP02"),
    SEQ_DOME(4600, FX_NONE, ":OP03"),
    SEQ_DOME(4700, FX_NONE, ":OP04"),
    SEQ_DOME(4800, FX_NONE, ":OP07"),
    SEQ_TERM(5900),
};

// DM:LOW close — reset holos, close P4,P2,P1,P3,P13,P7,P11 serially.
static const SeqStep kLowCloseSteps[] = {
    SEQ_DOME(0, FX_NONE, "*ST00"),
    SEQ_AUDIO(0, "$H"),
    SEQ_DOME(0, FX_NONE, ":CL04"),
    SEQ_DOME(150, FX_NONE, ":CL02"),
    SEQ_DOME(300, FX_NONE, ":CL01"),
    SEQ_DOME(450, FX_NONE, ":CL03"),
    SEQ_DOME(600, FX_NONE, ":CL13"),
    SEQ_DOME(750, FX_NONE, ":CL07"),
    SEQ_DOME(900, FX_NONE, ":CL11"),
    SEQ_TERM(2050),
};

// DM:OPENALL open — pie sweep, ring panels together, then P1/P2 + PP2/PP4
// twinkle twice (10 s).
static const SeqStep kOpenallOpenSteps[] = {
    SEQ_AUDIO(0, "$H"),
    // open pies PP1->PP6
    SEQ_DOME(0, FX_PANEL, ":OPP1"),
    SEQ_DOME(150, FX_NONE, ":OPP2"),
    SEQ_DOME(300, FX_NONE, ":OPP3"),
    SEQ_DOME(450, FX_NONE, ":OPP4"),
    SEQ_DOME(600, FX_NONE, ":OPP5"),
    SEQ_DOME(750, FX_NONE, ":OPP6"),
    // open ring panels together
    SEQ_DOME(900, FX_NONE, ":OP11"),
    SEQ_DOME(900, FX_NONE, ":OP13"),
    SEQ_DOME(900, FX_NONE, ":OP01"),
    SEQ_DOME(900, FX_NONE, ":OP02"),
    SEQ_DOME(900, FX_NONE, ":OP03"),
    SEQ_DOME(900, FX_NONE, ":OP04"),
    SEQ_DOME(900, FX_NONE, ":OP07"),
    // twinkle cycle 1: P1, P2, PP2, PP4
    SEQ_DOME(1000, FX_NONE, ":OP01"),
    SEQ_DOME(1100, FX_NONE, ":OP01"),
    SEQ_DOME(1180, FX_NONE, ":OP02"),
    SEQ_DOME(1280, FX_NONE, ":OP02"),
    SEQ_DOME(1360, FX_NONE, ":OP02"),
    SEQ_DOME(1460, FX_NONE, ":OPP2"),
    SEQ_DOME(1560, FX_NONE, ":OPP2"),
    SEQ_DOME(1740, FX_NONE, ":OPP4"),
    SEQ_DOME(1840, FX_NONE, ":OPP4"),
    // twinkle cycle 2
    SEQ_DOME(1940, FX_NONE, ":OP01"),
    SEQ_DOME(2040, FX_NONE, ":OP01"),
    SEQ_DOME(2120, FX_NONE, ":OP02"),
    SEQ_DOME(2220, FX_NONE, ":OP02"),
    SEQ_DOME(2300, FX_NONE, ":OP02"),
    SEQ_DOME(2400, FX_NONE, ":OPP2"),
    SEQ_DOME(2500, FX_NONE, ":OPP2"),
    SEQ_DOME(2680, FX_NONE, ":OPP4"),
    SEQ_DOME(2780, FX_NONE, ":OPP4"),
    SEQ_TERM(3680),
};

// DM:OPENALL close — close every panel serially in all-panels order.
static const SeqStep kOpenallCloseSteps[] = {
    SEQ_AUDIO(0, "$H"),
    SEQ_DOME(0, FX_NONE, ":CL01"),
    SEQ_DOME(150, FX_NONE, ":CL02"),
    SEQ_DOME(300, FX_NONE, ":CL03"),
    SEQ_DOME(450, FX_NONE, ":CL04"),
    SEQ_DOME(600, FX_NONE, ":CL07"),
    SEQ_DOME(750, FX_NONE, ":CL11"),
    SEQ_DOME(900, FX_NONE, ":CL13"),
    SEQ_DOME(1050, FX_NONE, ":CLP1"),
    SEQ_DOME(1200, FX_NONE, ":CLP2"),
    SEQ_DOME(1350, FX_NONE, ":CLP3"),
    SEQ_DOME(1500, FX_NONE, ":CLP4"),
    SEQ_DOME(1650, FX_NONE, ":CLP5"),
    SEQ_DOME(1800, FX_NONE, ":CLP6"),
    SEQ_TERM(2450),
};

// =============================================================================
// Catalog table
// =============================================================================

static const SequenceEntry kCatalog[] = {
    { "DM:VADER",   kVaderSteps,   SEQ_STEPCOUNT(kVaderSteps),   47000, TOGGLE_NONE, nullptr, 0 },
    { "DM:HELLO",   kHelloSteps,   SEQ_STEPCOUNT(kHelloSteps),   4000,  TOGGLE_NONE, nullptr, 0 },
    { "DM:NOD",     kNodSteps,     SEQ_STEPCOUNT(kNodSteps),     3000,  TOGGLE_NONE, nullptr, 0 },
    { "DM:FLUTTER", kFlutterSteps, SEQ_STEPCOUNT(kFlutterSteps), 10000, TOGGLE_NONE, nullptr, 0 },
    { "DM:BLOOM",   kBloomSteps,   SEQ_STEPCOUNT(kBloomSteps),   8000,  TOGGLE_NONE, nullptr, 0 },
    { "DM:LEIA",    kLeiaSteps,    SEQ_STEPCOUNT(kLeiaSteps),    36000, TOGGLE_NONE, nullptr, 0 },
    { "DM:ALARM",   kAlarmSteps,   SEQ_STEPCOUNT(kAlarmSteps),   10000, TOGGLE_NONE, nullptr, 0 },
    { "DM:HEART",   kHeartSteps,   SEQ_STEPCOUNT(kHeartSteps),   10000, TOGGLE_NONE, nullptr, 0 },
    { "DM:RESET",   kResetSteps,   SEQ_STEPCOUNT(kResetSteps),   4000,  TOGGLE_NONE, nullptr, 0 },
    { "DM:CANTINA",   kCantinaSteps,   SEQ_STEPCOUNT(kCantinaSteps),   17000, TOGGLE_NONE, nullptr, 0 },
    { "DM:ROCKMARCH", kRockmarchSteps, SEQ_STEPCOUNT(kRockmarchSteps), 48000, TOGGLE_NONE, nullptr, 0 },
    { "DM:SCREAM",    kScreamSteps,    SEQ_STEPCOUNT(kScreamSteps),    15000, TOGGLE_NONE, nullptr, 0 },
    { "DM:OVERLOAD",  kOverloadSteps,  SEQ_STEPCOUNT(kOverloadSteps),  12000, TOGGLE_NONE, nullptr, 0 },
    { "DM:PIES",    kPiesOpenSteps,    SEQ_STEPCOUNT(kPiesOpenSteps),    12000,
      TOGGLE_PIES, kPiesCloseSteps,    SEQ_STEPCOUNT(kPiesCloseSteps) },
    { "DM:LOW",     kLowOpenSteps,     SEQ_STEPCOUNT(kLowOpenSteps),     15000,
      TOGGLE_LOW,  kLowCloseSteps,     SEQ_STEPCOUNT(kLowCloseSteps) },
    { "DM:OPENALL", kOpenallOpenSteps, SEQ_STEPCOUNT(kOpenallOpenSteps), 10000,
      TOGGLE_ALL,  kOpenallCloseSteps, SEQ_STEPCOUNT(kOpenallCloseSteps) },
};
static constexpr uint8_t kCatalogSize =
    (uint8_t)(sizeof(kCatalog) / sizeof(kCatalog[0]));

// =============================================================================
// Alias table — DM:* names that forward directly to the dome unchanged or
// mapped to a :SE## / $NNN target. No body execution; dome owns these.
// =============================================================================

struct AliasEntry {
    const char* name;
    const char* target;
};

static const AliasEntry kAliases[] = {
    { "DM:STOP",           ":SE00" },
    { "DM:SESCREAM",       ":SE01" },
    { "DM:WAVE",           ":SE02" },
    { "DM:SMIRKWAVE",      ":SE03" },
    { "DM:OCWAVE",         ":SE04" },
    { "DM:BEEPCANTINA",    ":SE05" },
    { "DM:SHORT",          ":SE06" },
    { "DM:SECANTINA",      ":SE07" },
    { "DM:SELEIA",         ":SE08" },
    { "DM:DISCO",          ":SE09" },
    { "DM:SCREAMNOPANEL",  ":SE50" },
    { "DM:SCREAMPANEL",    ":SE51" },
    { "DM:WAVEPANEL",      ":SE52" },
    { "DM:SMIRKWAVEPANEL", ":SE53" },
    { "DM:OPENWAVE",       ":SE54" },
    { "DM:MARCHINGANTS",   ":SE55" },
    { "DM:FAINT",          ":SE56" },
    { "DM:RYTHMIC",        ":SE57" },
    { "DM:HARLEMSHAKE",    "$815"  },
    { "DM:GIRLONFIRE",     "$821"  },
    { "DM:YODA",           "$720"  },
    { "DM:TOPPANELS",      ":SE12" },
    { "DM:WIGGLE",         ":SE16" },
    { "DM:BYEBYE",         ":SE58" },
};
static constexpr uint8_t kAliasSize =
    (uint8_t)(sizeof(kAliases) / sizeof(kAliases[0]));

// =============================================================================
// Lookup — pure, no side effects, native-testable.
// =============================================================================

const SequenceEntry* sequenceCatalogFind(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (uint8_t i = 0; i < kCatalogSize; ++i) {
        if (strcmp(kCatalog[i].name, name) == 0) {
            return &kCatalog[i];
        }
    }
    return nullptr;
}

uint8_t sequenceCatalogCount() {
    return kCatalogSize;
}

const SequenceEntry* sequenceCatalogAt(uint8_t i) {
    return (i < kCatalogSize) ? &kCatalog[i] : nullptr;
}

SequenceLookupResult sequenceLookup(const char* name) {
    SequenceLookupResult r = { SEQ_FALLBACK, {} };

    if (name == nullptr || name[0] == '\0') {
        return r;
    }

    // Runtime-first precedence (ADR 0006): a Learned Sequence shadows a Factory
    // one of the same name (Retrained Sequence). Memory Wipe (delete) removes
    // the index entry and the factory entry resurfaces below.
    if (seqStoreIndexFind(name) != nullptr) {
        r.kind = SEQ_RUNTIME;
        return r;
    }

    if (sequenceCatalogFind(name) != nullptr) {
        r.kind = SEQ_CATALOG;
        return r;
    }

    for (uint8_t i = 0; i < kAliasSize; ++i) {
        if (strcmp(kAliases[i].name, name) == 0) {
            r.kind = SEQ_ALIAS;
            strncpy(r.aliasTarget, kAliases[i].target, sizeof(r.aliasTarget) - 1);
            r.aliasTarget[sizeof(r.aliasTarget) - 1] = '\0';
            return r;
        }
    }

    return r;  // SEQ_FALLBACK
}
