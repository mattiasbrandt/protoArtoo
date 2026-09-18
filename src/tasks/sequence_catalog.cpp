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
// auto-emits the matching resets (@0T1/@0P1, *ST00, audio stop) on terminal
// transitions, so tables do not repeat standard cleanup steps. Panels are the
// exception in both directions: the engine closes only the ring panels a run
// left open, one at a time, never with a group close and never a pie -- so a
// table that opens pies closes them itself.

// =============================================================================
// Flat sequences
// =============================================================================

// DM:VADER  --  Imperial March visual mode (47 s).
// Holos, logics, and PSI set to MARCH mode; auto-reset at sequence end.
static const SeqStep kVaderSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO_BOUNDED, "$M"),     // Imperial March (Bounded Audio: Track Stop at terminal cleanup)
    // Dome-native red MARCH visual preset via the DV: surface (issue #2 task #5).
    // Dome-source-confirmed to share ROCKMARCH's red MARCH shape. Replaces the raw
    // @HPA0021|47/@0T11/@0P11 approximation, which rendered default blue on hardware
    // (the dome owns the typed rendering). Tagged FX_LOGIC_PSI|FX_HOLO so the
    // engine's terminal cleanup still emits the body-owned reset (@0T1/@0P1/*ST00).
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:VADER"),
    SEQ_TERM(47000),
};

// DM:HELLO  --  "Hello There" greeting (4 s).
// Front and rear logic text, then P1 opens and closes.
//
// It used to send :OP01 five times 160 ms apart, commented open / half / open /
// half / open. :OP is an open, not a pulse, and a panel already opening ignores
// a second one, so the five made ONE open and never the six-pulse wave this
// header promised (#287). One open is what it does, so one open is what it says.
static const SeqStep kHelloSteps[] = {
    SEQ_AUDIO(0, "$H"),                          // happy/greeting clip
    SEQ_DOME(0, FX_NONE, "@1MHello There"),      // front logic text
    SEQ_DOME(0, FX_NONE, "@3MGeneral Kenobi"),
    SEQ_DOME(0, FX_PANEL, ":OP01"),      // P1 open
    SEQ_DOME(800, FX_NONE, ":CL01"),      // P1 close
    SEQ_TERM(950),                               // P1 already closed: no panel cleanup
};

// DM:NOD  --  short acknowledgment: sound + logic text + P1 wave.
// Demonstrates sound-to-motion sync from a single body clock (issue #2).
static const SeqStep kNodSteps[] = {
    SEQ_AUDIO(0, "$H"),                          // ack/happy clip
    SEQ_DOME(0, FX_NONE, "@1MYes"),              // logic text
    SEQ_DOME(0, FX_PANEL, ":OP01"),      // P1 open
    SEQ_DOME(150, FX_NONE, ":CL01"),      // P1 close
    SEQ_TERM(300),                               // P1 already closed: no panel cleanup
};

// DM:FLUTTER  --  ring then pie panels sweep to 75%, then close (10 s window).
// Intent-adapted from previous pulse choreography; partial-open fidelity is
// intentionally not preserved in body-authored form.
static const SeqStep kFlutterSteps[] = {
    // ring to 75%  --  P1,P2,P3,P4,P7,P11,P13
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(150, FX_NONE, ":OP02"),
    SEQ_DOME(300, FX_NONE, ":OP03"),
    SEQ_DOME(450, FX_NONE, ":OP04"),
    SEQ_DOME(600, FX_NONE, ":OP07"),
    SEQ_DOME(750, FX_NONE, ":OP11"),
    SEQ_DOME(900, FX_NONE, ":OP13"),
    // pies to 75%  --  PP1,PP2,PP3,PP4,PP5,PP6
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
    SEQ_TERM(4250),                              // every panel closed above: no panel cleanup
};

// DM:BLOOM  --  pies open together over 1.2 s, wiggle three times, close (8 s).
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
    SEQ_TERM(5650),                              // pies closed above; the engine never closes a pie
};

// DM:LEIA  --  Leia message mode (36 s): front holo Leia, other holos off,
// Leia logics/PSI; everything resets via effect classes at the end.
static const SeqStep kLeiaSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO_BOUNDED, "$L"),     // Leia message (Bounded Audio: Track Stop at terminal cleanup)
    // Dome-native Leia visual preset via the DV: surface (issue #2 task #5).
    // Replaces the raw @HPS101/@HPR02/@HPT02|36 holos + @0T6/@0P6 logic/PSI; the
    // dome owns the typed rendering. Authority body-inferred until codex confirms
    // against DomeSequences.h. FX_LOGIC_PSI|FX_HOLO -> terminal cleanup reset.
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:LEIA"),
    SEQ_TERM(36000),                             // auto @0T1/@0P1/*ST00
};

// DM:ALARM  --  pulsing red holos/logics/PSI (10 s). Audio: random track from
// the alert category, falling back to the named scream track.
static const SeqStep kAlarmSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_ALERT, AUDIO_SLOT_NAMED_SCREAM),
    // Dome-native alarm visual preset via the DV: surface (issue #2 task #5).
    // Replaces the raw @HPA0021|10 holo + @0T3/@0P3 logic/PSI; the dome owns the
    // typed rendering. Authority body-inferred until codex confirms against
    // DomeSequences.h. FX_LOGIC_PSI|FX_HOLO -> terminal cleanup reset.
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:ALARM"),
    SEQ_TERM(10000),                             // auto @0T1/@0P1/*ST00
};

// DM:HEART  --  rainbow holos and a sweet logic message (10 s). Audio: random
// track from the sentimental category, falling back to the named happy track.
static const SeqStep kHeartSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_SENTIMENTAL, AUDIO_SLOT_NAMED_HAPPY),
    // Dome-native heart visual preset via the DV: surface (issue #2 task #5),
    // dome-source-confirmed: DV:HEART renders the FLD scroll text "You're\nWonderful"
    // + front PSI flash-color + rainbow holos (HPF/HPR/HPT006|10). Replaces the raw
    // @HPF006/@HPR006/@HPT006|10 + @1P2 AND the body's old single-line
    // @1MYou're Wonderful text -- the dome owns the (two-line) message natively, which
    // the body's wire could not express (issue #2 gap #3, no escaping). Sending the
    // body text too would be a redundant double-write. FX_LOGIC_PSI|FX_HOLO so the
    // terminal cleanup still emits the body-owned reset (@0T1/@0P1/*ST00).
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:HEART"),
    SEQ_TERM(10000),                             // auto @0T1/@0P1/*ST00
};

// DM:RESET  --  safe ring-panel reset + body latch clear, then reset holos/logics/
// PSI. Pie panels are NOT auto-closed: on this droid pie-close mechanical safety
// is still unverified, and the systemic invariant (2026-06-17/-18 brownout fix)
// is that the body never auto-emits a group close (:CL00/:CL14/:CL15) nor an
// automatic pie close. The old :CL00 here drove every group servo at once
// (brownout) and cleared the toggle latches as a side effect. This re-author
// closes only the ring panels, individually and staggered at the proven safe
// ~450 ms cadence (one servo actuating at a time), and clears the latches with
// an explicit STEP_CLEAR_LATCHES instead of relying on the :CL00 side effect.
// The closes are unconditional physical assurance: DM:RESET seats the ring
// regardless of what this sequence opened, so the closes set the per-run net-open
// mask to 0 and the terminal cleanup emits no further panel commands.
//
// The sound is ended with a Track Stop, not `$s`. `$s` is the mood system's
// Quiet: it stops playback AND turns idle chatter off until the droid reboots,
// so a reset that sent it left the droid permanently muted -- the defect
// DM:ROCKMARCH shipped and ADR 0010 fixed there (#287, #354).
static const SeqStep kResetSteps[] = {
    SEQ_AUDIO_STOP(0),                           // stop playback, keep idle chatter
    SEQ_DOME(0,    FX_NONE, ":CL01"),            // staggered ring closes, ~450 ms
    SEQ_DOME(450,  FX_NONE, ":CL02"),            // apart (brownout-safe; never a
    SEQ_DOME(900,  FX_NONE, ":CL03"),            // group close, never a pie close)
    SEQ_DOME(1350, FX_NONE, ":CL04"),
    SEQ_DOME(1800, FX_NONE, ":CL07"),
    SEQ_DOME(2250, FX_NONE, ":CL11"),
    SEQ_DOME(2700, FX_NONE, ":CL13"),
    SEQ_CLEAR_LATCHES(3200),                     // clear piesOpen/ringOpen latches
    SEQ_DOME(3200, FX_NONE, "*ST00"),            // reset holos
    SEQ_DOME(3400, FX_NONE, "@0T1"),             // reset logics
    SEQ_DOME(3600, FX_NONE, "@0P1"),             // reset PSI
    SEQ_TERM(3900),
};

// =============================================================================
// Loop sequences  --  a STEP_LOOP header repeats the following bodyCount steps
// every periodMs while the iteration start is inside durationMs. Body step
// times are relative to the iteration start. Post-loop steps are authored
// past the worst-case loop end (the final iteration may overhang durationMs).
// =============================================================================

// DM:CANTINA  --  130 BPM alternating panel dance for ~15 s (17 s window).
// Two beats per iteration: group A open / group B closed, then inverted.
// 8 iterations x 1846 ms from t=100 -> loop ends at ~14868 ms.
static const SeqStep kCantinaSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO_BOUNDED, "$C"),     // long Cantina (Bounded Audio: Track Stop at terminal cleanup)
    // Dome-native cantina visual preset via the DV: surface (issue #2 task #5).
    // Replaces the raw @HPA0029|15 holo + @0T2/@0P2 logic/PSI; the dome owns the
    // typed rendering. Authority body-inferred until codex confirms against
    // DomeSequences.h. FX_LOGIC_PSI|FX_HOLO -> terminal cleanup reset. The panel
    // loop and its timeline are unchanged; the loop header is now at step index 2.
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:CANTINA"),
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
    SEQ_TERM(15400),                             // auto @0T1/@0P1/*ST00; ring panels left open close
                                                 // one at a time, pies stay as the last beat left them
};

// DM:ROCKMARCH  --  Imperial March with one ring panel stepping per beat
// (923 ms) for ~45 s (47 s window). One iteration = one full ring pass:
// open slot k at k*923, close it 773 ms later. 7 iterations x 6461 ms.
static const SeqStep kRockmarchSteps[] = {
    SEQ_AUDIO_FX(0, FX_AUDIO_BOUNDED, "$M"),     // Imperial March (Bounded Audio: Track Stop at terminal cleanup)
    // Dome-native red MARCH visual preset (logic/PSI/holo) via the DV: surface
    // (issue #2 task #5, first acceptance case). Replaces the raw @0T11/@0P11/
    // @HPA0021|47 approximation, which rendered default blue on hardware  --  the
    // dome owns the typed red MARCH rendering. Tagged FX_LOGIC_PSI|FX_HOLO so the
    // engine's terminal cleanup still emits the body-owned visual teardown
    // (@0T1/@0P1/*ST00). Body keeps music + the ring wave + settle close below.
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:ROCKMARCH"),
    // Ring march wave on the ~923 ms beat. Hold is 670 ms (was 773) so every dome
    // command dispatches >= ~253 ms apart: open->close 670, close->next-open 253,
    // loop-wrap :CL13->:OP01 253. The original 150 ms close->next-open gaps could
    // overflow the dome's 8-entry command queue and drop the FINAL iteration's
    // :CL01, leaving P1 open (2026-06-18 hardware finding). Keep dispatch spacing
    // >= ~200 ms in any ring wave; the ~923 ms beat (opens) is unchanged so march
    // sync to $M is preserved.
    SEQ_LOOP(0, 14, 6461, 45000),
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(670, FX_NONE, ":CL01"),
    SEQ_DOME(923, FX_NONE, ":OP02"),
    SEQ_DOME(1593, FX_NONE, ":CL02"),
    SEQ_DOME(1846, FX_NONE, ":OP03"),
    SEQ_DOME(2516, FX_NONE, ":CL03"),
    SEQ_DOME(2769, FX_NONE, ":OP04"),
    SEQ_DOME(3439, FX_NONE, ":CL04"),
    SEQ_DOME(3692, FX_NONE, ":OP07"),
    SEQ_DOME(4362, FX_NONE, ":CL07"),
    SEQ_DOME(4615, FX_NONE, ":OP11"),
    SEQ_DOME(5285, FX_NONE, ":CL11"),
    SEQ_DOME(5538, FX_NONE, ":OP13"),
    SEQ_DOME(6208, FX_NONE, ":CL13"),
    // Physical-assurance close pass (2026-06-18 hardware finding). The rapid
    // in-loop open/close cycling can dispatch a panel's close without it
    // physically seating by sequence end (observed: P1 left open even though the
    // dome received AND dispatched the in-wave :CL01; a single manual :CL01 then
    // closed it). Logical-closed != physically-seated. So after the wave, re-close
    // every touched ring panel once more, staggered individual at ~450-500 ms
    // (brownout-safe; never :CL15/:CL00), leaving the ring physically closed.
    SEQ_DOME(45200, FX_NONE, ":CL01"),
    SEQ_DOME(45650, FX_NONE, ":CL02"),
    SEQ_DOME(46100, FX_NONE, ":CL03"),
    SEQ_DOME(46550, FX_NONE, ":CL04"),
    SEQ_AUDIO_STOP(47000),                       // early Track Stop (ADR 0010): same
                                                  // 47000 ms cutoff the removed $s step
                                                  // used to author, now via Track Stop
                                                  // so idle mood is preserved.
    SEQ_DOME(47050, FX_NONE, ":CL07"),
    SEQ_DOME(47500, FX_NONE, ":CL11"),
    SEQ_DOME(47950, FX_NONE, ":CL13"),
    SEQ_TERM(48250),                             // auto @0T1/@0P1/*ST00; ring already closed by the assurance pass
};

// =============================================================================
// Random sequences  --  STEP_RANDOM resolves a logical panel target and optional
// timing jitter at fire time. SLOTSET_HOLD reuses the previous pick;
// pickDistinct avoids slots already picked this run.
// =============================================================================

// DM:SCREAM  --  panels burst open, red alert, random one-panel flutter, close
// (15 s window). Flutter: 10 iterations of a 380 ms 4-move pattern on a
// randomly picked panel (repeats across iterations allowed, as in the dome's
// original code). Audio: random scream-category track (fallback named scream).
static const SeqStep kScreamSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_SCREAM, AUDIO_SLOT_NAMED_SCREAM),
    // Dome-native scream visual preset via the DV: surface (issue #2 task #5):
    // red-alert logic/PSI + short-circuit/wag holos. Replaces @HPA0070/@HPA105|5 +
    // @0T5/@0P5; the dome owns the typed rendering. Authority body-inferred until
    // codex confirms against DomeSequences.h. FX_LOGIC_PSI|FX_HOLO -> terminal
    // cleanup reset. The burst-open + flutter loop are unchanged; the loop header
    // is now at step index 15.
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:SCREAM"),
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
    // happy all-clear cue before the reset: logics and holos reset, the ring closes, the pies stay open
    SEQ_AUDIO(6800, "$H"),
    SEQ_TERM(7450),                              // auto @0T1/@0P1/*ST00; ring panels left open close
                                                 // one at a time, the pies it burst open stay open
};

// DM:OVERLOAD  --  failure logics/PSI, holos short-circuit, six panels flutter
// on random logical targets, then everything resets (12 s
// window). Drift gaps are randomized as 0..500 ms jitter on fixed 650 ms
// offsets (issue #2: gap randomness, absolute schedule preserved). Audio:
// random sad-category track (fallback named faint).
static const SeqStep kOverloadSteps[] = {
    SEQ_AUDIO_CAT(0, AUDIO_CATEGORY_SAD, AUDIO_SLOT_NAMED_FAINT),
    // Dome-native overload visual preset via the DV: surface (issue #2 task #5):
    // front/rear FLD/RLD failure logics + PSI failure + short-circuit holos.
    // Replaces @1T4/@2T4/@HPA0070/@0P4; the dome owns the typed rendering. Authority
    // body-inferred until codex confirms against DomeSequences.h. FX_LOGIC_PSI|
    // FX_HOLO -> terminal cleanup reset. The flutter schedule is unchanged.
    SEQ_DOME(0, (uint8_t)(FX_LOGIC_PSI | FX_HOLO), "DV:OVERLOAD"),
    // four distinct ring panels + two distinct pies flutter with timing jitter
    SEQ_RAND(400, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(1050, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(1700, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(2350, SLOTSET_RING, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(3000, SLOTSET_PIE, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_RAND(3650, SLOTSET_PIE, RAND_FLUTTER, 0, 300, 500, 1),
    SEQ_TERM(7000),                              // auto @0T1/@0P1/*ST00; a flutter marks no panel
                                                 // open, so no panel cleanup
};

// =============================================================================
// Toggle sequences (ADR 0004 decision 8)  --  `steps` is the open branch,
// `closeSteps` the close branch; the engine picks by latched group state and
// flips the latch on normal completion. Close branches end without a release;
// once no group remains latched open the engine closes any ring panel still
// open, one at a time, and never sends :CL00 (issue #2 gap #1).
// =============================================================================

// DM:PIES open  --  pie wave: open PP1->PP6, close PP6->PP1, reopen, twice (12 s).
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
    // cycle 2: reopen PP1->PP6  --  pies end open
    SEQ_DOME(3100, FX_NONE, ":OPP1"),
    SEQ_DOME(3200, FX_NONE, ":OPP2"),
    SEQ_DOME(3300, FX_NONE, ":OPP3"),
    SEQ_DOME(3400, FX_NONE, ":OPP4"),
    SEQ_DOME(3500, FX_NONE, ":OPP5"),
    SEQ_DOME(3600, FX_NONE, ":OPP6"),
    SEQ_TERM(4600),
};

// DM:PIES close  --  reset holos, close PP1->PP6 serially.
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

// DM:LOW open  --  ring wave twice, then all ring panels open (15 s).
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
    // final open: P11, P13, P1, then P2, P3, P4, P7  --  ring ends open.
    // One panel at a time, 200 ms apart. The first three used to share t=4400
    // and the rest followed 100 ms apart: the same-timestamp burst that
    // overflowed the dome's eight-entry command queue and silently dropped a
    // close on 2026-06-18 (DM:ROCKMARCH above keeps >= ~200 ms for that reason;
    // #287, #354).
    SEQ_DOME(4400, FX_NONE, ":OP11"),
    SEQ_DOME(4600, FX_NONE, ":OP13"),
    SEQ_DOME(4800, FX_NONE, ":OP01"),
    SEQ_DOME(5000, FX_NONE, ":OP02"),
    SEQ_DOME(5200, FX_NONE, ":OP03"),
    SEQ_DOME(5400, FX_NONE, ":OP04"),
    SEQ_DOME(5600, FX_NONE, ":OP07"),
    SEQ_TERM(5900),
};

// DM:LOW close  --  reset holos, then close ring panels ONE AT A TIME with ~500 ms
// settle between each. The wide cadence is deliberate, not cosmetic: closing all 7
// ring servos in the original tight 150 ms burst from a fully-open state browned out
// the dome (esp_reset_reason=BROWNOUT, code 9, 2026-06-17 hardware repro) -- the dome
// dropped by ~the 3rd close, so overlapping servo inrush current exceeded the dome
// supply. The holo reset (*ST00) is isolated at t=0 so its draw does not stack with
// the first panel close. Terminal cleanup has nothing left to close by the end,
// and it would never send a group :CL15 anyway. See
// tasks/issue2-panel-intent-rewrite-plan.md "Hardware regression 2026-06-17".
static const SeqStep kLowCloseSteps[] = {
    SEQ_DOME(0, FX_NONE, "*ST00"),
    SEQ_AUDIO(0, "$H"),
    SEQ_DOME(500, FX_NONE, ":CL04"),
    SEQ_DOME(1000, FX_NONE, ":CL02"),
    SEQ_DOME(1500, FX_NONE, ":CL01"),
    SEQ_DOME(2000, FX_NONE, ":CL03"),
    SEQ_DOME(2500, FX_NONE, ":CL13"),
    SEQ_DOME(3000, FX_NONE, ":CL07"),
    SEQ_DOME(3500, FX_NONE, ":CL11"),
    SEQ_TERM(4500),
};

// DM:OPENALL open  --  pie sweep, ring panels together, then P1/P2 + PP2/PP4
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
    // open ring panels one at a time, 200 ms apart. All seven used to leave at
    // t=900 together: the same-timestamp burst that overflowed the dome's
    // eight-entry command queue on 2026-06-18 (#287, #354). Everything after
    // the ring moves 1200 ms later with it, so the twinkle keeps its own shape.
    SEQ_DOME(900, FX_NONE, ":OP11"),
    SEQ_DOME(1100, FX_NONE, ":OP13"),
    SEQ_DOME(1300, FX_NONE, ":OP01"),
    SEQ_DOME(1500, FX_NONE, ":OP02"),
    SEQ_DOME(1700, FX_NONE, ":OP03"),
    SEQ_DOME(1900, FX_NONE, ":OP04"),
    SEQ_DOME(2100, FX_NONE, ":OP07"),
    // twinkle cycle 1: P1, P2, PP2, PP4
    SEQ_DOME(2200, FX_NONE, ":OP01"),
    SEQ_DOME(2300, FX_NONE, ":OP01"),
    SEQ_DOME(2380, FX_NONE, ":OP02"),
    SEQ_DOME(2480, FX_NONE, ":OP02"),
    SEQ_DOME(2560, FX_NONE, ":OP02"),
    SEQ_DOME(2660, FX_NONE, ":OPP2"),
    SEQ_DOME(2760, FX_NONE, ":OPP2"),
    SEQ_DOME(2940, FX_NONE, ":OPP4"),
    SEQ_DOME(3040, FX_NONE, ":OPP4"),
    // twinkle cycle 2
    SEQ_DOME(3140, FX_NONE, ":OP01"),
    SEQ_DOME(3240, FX_NONE, ":OP01"),
    SEQ_DOME(3320, FX_NONE, ":OP02"),
    SEQ_DOME(3420, FX_NONE, ":OP02"),
    SEQ_DOME(3500, FX_NONE, ":OP02"),
    SEQ_DOME(3600, FX_NONE, ":OPP2"),
    SEQ_DOME(3700, FX_NONE, ":OPP2"),
    SEQ_DOME(3880, FX_NONE, ":OPP4"),
    SEQ_DOME(3980, FX_NONE, ":OPP4"),
    SEQ_TERM(4880),
};

// DM:OPENALL close  --  close every panel serially in all-panels order.
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
// Body routines  --  :SE30..:SE36 (ADR 0049, #354)
//
// The seven numbered body buttons a builder arriving from ShadowMD already has
// bound. Each used to run one shared open-wait-close state machine in ServoTask,
// so all seven did the same thing; here each is the routine its name describes,
// written as Body Steps that name Parts. A Part nothing drives yet reports
// part-not-assigned and the routine carries on, so a droid with only the two
// utility arms wired still runs every one of them (#301).
//
// Lineage. The choreography -- which part moves, in what order, and when -- is
// read off the timing tables in BetterDuino Firmware V4, include/PanelSequences.h
// at 3682082a (github.com/RealNobser/BetterDuinoFirmwareV4): body_utility_arms_open,
// body_panel_all_test, body_panel_spook, body_panel_use_gripper,
// body_panel_use_interface_tool and body_panel_pingpong_Doors, credited there to
// Tim Hebel (github.com/Eebel/SHADOW_MD_EEBEL), and bt_body_panel_use_claws by
// David Steinke. Neither repository declares a license; no code was copied, and
// the attribution is docs/sequence-credits.md.
//
// How the tables were read. A row there holds every servo at a position for the
// row's duration in hundredths of a second, and the sequencer moves the servos
// the moment a row starts (MDuinoSequencer::nextStep). So a step here fires at
// the sum of the durations before its row, and only a Part whose position
// changed gets a step -- a row that restates a position is not a move, and
// sending it again would be the repeated-command shape #287 found in DM:HELLO.
// Every source routine begins by closing its servos; that row is kept, for the
// Parts each routine moves, so a routine starts from shut whatever was left open.
//
// Servo columns -> Parts (BetterDuino README, Body Master servo table):
//   1 DPL -> dataport       2 UtlArmU -> utilUp     3 UtlArmL -> utilLo
//   4 LBdyDr -> doorFL      5 LArm -> gripArm       6 LArmTool -> gripClaw
//   7 RBdyDr -> doorFR      8 RArm -> interArm      9 RArmTool -> interTool
//
// What was deliberately not taken: the source sets a servo speed per routine.
// Here a routine carries no physics -- how fast a door moves is its Output's
// Motion Profile, set once by the builder (ADR 0049, ADR 0052).
// =============================================================================

#define BODY_OPEN(t, part)  SEQ_BODY((t), (part), BODY_SHAPE_OPEN, 0, 0)
#define BODY_CLOSE(t, part) SEQ_BODY((t), (part), BODY_SHAPE_CLOSE, 0, 0)

// DM:SE30  --  :SE30, utility arm open-and-close.
// Both utility arms swing out, then flick in and out twice before they close.
static const SeqStep kSe30Steps[] = {
    BODY_CLOSE(0, "utilUp"),    BODY_CLOSE(0, "utilLo"),
    BODY_OPEN(200, "utilUp"),   BODY_OPEN(200, "utilLo"),
    BODY_CLOSE(1700, "utilUp"), BODY_CLOSE(1700, "utilLo"),
    BODY_OPEN(2000, "utilUp"),  BODY_OPEN(2000, "utilLo"),
    BODY_CLOSE(2300, "utilUp"), BODY_CLOSE(2300, "utilLo"),
    BODY_OPEN(2600, "utilUp"),  BODY_OPEN(2600, "utilLo"),
    BODY_CLOSE(2900, "utilUp"), BODY_CLOSE(2900, "utilLo"),
    SEQ_TERM(4800),
};

// DM:SE31  --  :SE31, all body panels open and close.
// The breadpan doors and utility arms open, both arms rise with their tools, the
// dataport opens last (it hits a tool if it opens first), the tools and utility
// arms work, and everything folds away in order.
static const SeqStep kSe31Steps[] = {
    BODY_CLOSE(0, "dataport"), BODY_CLOSE(0, "utilUp"),   BODY_CLOSE(0, "utilLo"),
    BODY_CLOSE(0, "doorFL"),   BODY_CLOSE(0, "gripArm"),  BODY_CLOSE(0, "gripClaw"),
    BODY_CLOSE(0, "doorFR"),   BODY_CLOSE(0, "interArm"), BODY_CLOSE(0, "interTool"),
    // open the doors and the utility arms
    BODY_OPEN(200, "utilUp"), BODY_OPEN(200, "utilLo"), BODY_OPEN(200, "doorFL"), BODY_OPEN(200, "doorFR"),
    // raise the arms, open the tools
    BODY_OPEN(1700, "gripArm"), BODY_OPEN(1700, "gripClaw"), BODY_OPEN(1700, "interArm"), BODY_OPEN(1700, "interTool"),
    // open the dataport
    BODY_OPEN(3200, "dataport"),
    // close the tools and the utility arms
    BODY_CLOSE(6200, "utilUp"), BODY_CLOSE(6200, "utilLo"), BODY_CLOSE(6200, "gripClaw"), BODY_CLOSE(6200, "interTool"),
    // open them again
    BODY_OPEN(6700, "utilUp"), BODY_OPEN(6700, "utilLo"), BODY_OPEN(6700, "gripClaw"), BODY_OPEN(6700, "interTool"),
    // tools: close, open, close
    BODY_CLOSE(7200, "gripClaw"), BODY_CLOSE(7200, "interTool"),
    BODY_OPEN(7700, "gripClaw"),  BODY_OPEN(7700, "interTool"),
    BODY_CLOSE(8200, "dataport"), BODY_CLOSE(8200, "gripClaw"), BODY_CLOSE(8200, "interTool"),
    // lower the arms, fold the utility arms
    BODY_CLOSE(8900, "utilUp"), BODY_CLOSE(8900, "utilLo"), BODY_CLOSE(8900, "gripArm"), BODY_CLOSE(8900, "interArm"),
    // close the doors
    BODY_CLOSE(11400, "doorFL"), BODY_CLOSE(11400, "doorFR"),
    SEQ_TERM(13400),
};

// DM:SE32  --  :SE32, all body doors open and wiggle-close.
// The breadpan doors, the dataport and both utility arms spring open, then
// wiggle shut: open and closed again three times, quickly, before the last close.
static const SeqStep kSe32Steps[] = {
    BODY_CLOSE(0, "dataport"),    BODY_CLOSE(0, "utilUp"),    BODY_CLOSE(0, "utilLo"),
    BODY_CLOSE(0, "doorFL"),      BODY_CLOSE(0, "doorFR"),
    BODY_OPEN(200, "dataport"),   BODY_OPEN(200, "utilUp"),   BODY_OPEN(200, "utilLo"),
    BODY_OPEN(200, "doorFL"),     BODY_OPEN(200, "doorFR"),
    BODY_CLOSE(700, "dataport"),  BODY_CLOSE(700, "utilUp"),  BODY_CLOSE(700, "utilLo"),
    BODY_CLOSE(700, "doorFL"),    BODY_CLOSE(700, "doorFR"),
    BODY_OPEN(800, "dataport"),   BODY_OPEN(800, "utilUp"),   BODY_OPEN(800, "utilLo"),
    BODY_OPEN(800, "doorFL"),     BODY_OPEN(800, "doorFR"),
    BODY_CLOSE(900, "dataport"),  BODY_CLOSE(900, "utilUp"),  BODY_CLOSE(900, "utilLo"),
    BODY_CLOSE(900, "doorFL"),    BODY_CLOSE(900, "doorFR"),
    BODY_OPEN(1000, "dataport"),  BODY_OPEN(1000, "utilUp"),  BODY_OPEN(1000, "utilLo"),
    BODY_OPEN(1000, "doorFL"),    BODY_OPEN(1000, "doorFR"),
    BODY_CLOSE(1200, "dataport"), BODY_CLOSE(1200, "utilUp"), BODY_CLOSE(1200, "utilLo"),
    BODY_CLOSE(1200, "doorFL"),   BODY_CLOSE(1200, "doorFR"),
    BODY_OPEN(1400, "dataport"),  BODY_OPEN(1400, "utilUp"),  BODY_OPEN(1400, "utilLo"),
    BODY_OPEN(1400, "doorFL"),    BODY_OPEN(1400, "doorFR"),
    BODY_CLOSE(1500, "dataport"), BODY_CLOSE(1500, "utilUp"), BODY_CLOSE(1500, "utilLo"),
    BODY_CLOSE(1500, "doorFL"),   BODY_CLOSE(1500, "doorFR"),
    SEQ_TERM(4000),
};

// DM:SE33  --  :SE33, use the gripper arm.
// The left breadpan door opens, the gripper arm rises and snaps its claw three
// times, then the arm lowers and the door closes.
static const SeqStep kSe33Steps[] = {
    BODY_CLOSE(0, "doorFL"), BODY_CLOSE(0, "gripArm"), BODY_CLOSE(0, "gripClaw"),
    BODY_OPEN(200, "doorFL"),
    BODY_OPEN(1700, "gripArm"),
    BODY_OPEN(3200, "gripClaw"),
    BODY_CLOSE(3300, "gripClaw"),
    BODY_OPEN(3400, "gripClaw"),
    BODY_CLOSE(3500, "gripClaw"),
    BODY_OPEN(3600, "gripClaw"),
    BODY_CLOSE(3700, "gripClaw"),
    BODY_CLOSE(3900, "gripArm"),
    BODY_CLOSE(5600, "doorFL"),
    SEQ_TERM(8100),
};

// DM:SE34  --  :SE34, use the interface tool.
// The right breadpan door opens, the interface arm rises and works its tool three
// times, then the arm lowers and the door closes.
static const SeqStep kSe34Steps[] = {
    BODY_CLOSE(0, "doorFR"), BODY_CLOSE(0, "interArm"), BODY_CLOSE(0, "interTool"),
    BODY_OPEN(200, "doorFR"),
    BODY_OPEN(1700, "interArm"),
    BODY_OPEN(3200, "interTool"),
    BODY_CLOSE(3400, "interTool"),
    BODY_OPEN(3600, "interTool"),
    BODY_CLOSE(3800, "interTool"),
    BODY_OPEN(4000, "interTool"),
    BODY_CLOSE(4200, "interTool"),
    BODY_CLOSE(4600, "interArm"),
    BODY_CLOSE(6300, "doorFR"),
    SEQ_TERM(8800),
};

// DM:SE35  --  :SE35, ping-pong body doors.
// The two breadpan doors take turns, the gaps between turns shrinking from 1.5 s
// to 0.5 s and then opening out again, before both close.
static const SeqStep kSe35Steps[] = {
    BODY_CLOSE(0, "doorFL"),     BODY_CLOSE(0, "doorFR"),
    BODY_OPEN(200, "doorFL"),
    BODY_CLOSE(1700, "doorFL"),  BODY_OPEN(1700, "doorFR"),
    BODY_OPEN(3200, "doorFL"),   BODY_CLOSE(3200, "doorFR"),
    BODY_CLOSE(4300, "doorFL"),  BODY_OPEN(4300, "doorFR"),
    BODY_OPEN(5400, "doorFL"),   BODY_CLOSE(5400, "doorFR"),
    BODY_CLOSE(6200, "doorFL"),  BODY_OPEN(6200, "doorFR"),
    BODY_OPEN(7000, "doorFL"),   BODY_CLOSE(7000, "doorFR"),
    BODY_CLOSE(7500, "doorFL"),  BODY_OPEN(7500, "doorFR"),
    BODY_OPEN(8000, "doorFL"),   BODY_CLOSE(8000, "doorFR"),
    BODY_CLOSE(9300, "doorFL"),  BODY_OPEN(9300, "doorFR"),
    BODY_CLOSE(10600, "doorFR"),
    SEQ_TERM(12600),
};

// DM:SE36  --  :SE36, the BT-1 two-gripper sequence.
// Both breadpan doors open, both arms rise, and the two claws snap together five
// times before the arms lower and the doors close. A BT-1 carries a claw on each
// arm; on this catalog the right arm's end is interTool, which is what a BT-1
// builder assigns their right claw to.
static const SeqStep kSe36Steps[] = {
    BODY_CLOSE(0, "doorFL"),       BODY_CLOSE(0, "gripArm"),  BODY_CLOSE(0, "gripClaw"),
    BODY_CLOSE(0, "doorFR"),       BODY_CLOSE(0, "interArm"), BODY_CLOSE(0, "interTool"),
    BODY_OPEN(200, "doorFL"),      BODY_OPEN(200, "doorFR"),
    BODY_OPEN(1100, "gripArm"),    BODY_OPEN(1100, "interArm"),
    BODY_OPEN(2000, "gripClaw"),   BODY_OPEN(2000, "interTool"),
    BODY_CLOSE(2100, "gripClaw"),  BODY_CLOSE(2100, "interTool"),
    BODY_OPEN(2200, "gripClaw"),   BODY_OPEN(2200, "interTool"),
    BODY_CLOSE(2300, "gripClaw"),  BODY_CLOSE(2300, "interTool"),
    BODY_OPEN(2400, "gripClaw"),   BODY_OPEN(2400, "interTool"),
    BODY_CLOSE(2500, "gripClaw"),  BODY_CLOSE(2500, "interTool"),
    BODY_OPEN(2600, "gripClaw"),   BODY_OPEN(2600, "interTool"),
    BODY_CLOSE(2700, "gripClaw"),  BODY_CLOSE(2700, "interTool"),
    BODY_OPEN(2800, "gripClaw"),   BODY_OPEN(2800, "interTool"),
    BODY_CLOSE(2900, "gripClaw"),  BODY_CLOSE(2900, "interTool"),
    BODY_CLOSE(3400, "gripArm"),   BODY_CLOSE(3400, "interArm"),
    BODY_CLOSE(4600, "doorFL"),    BODY_CLOSE(4600, "doorFR"),
    SEQ_TERM(7100),
};

#undef BODY_OPEN
#undef BODY_CLOSE

// =============================================================================
// Catalog table
// =============================================================================

static const SequenceEntry kCatalog[] = {
    { "DM:VADER",   kVaderSteps,   SEQ_STEPCOUNT(kVaderSteps),   47000, TOGGLE_NONE, nullptr, 0,
      "Imperial March with red MARCH-mode holos, logics, and PSI; auto-resets at the end (47 s)." },
    { "DM:HELLO",   kHelloSteps,   SEQ_STEPCOUNT(kHelloSteps),   4000,  TOGGLE_NONE, nullptr, 0,
      "\"Hello There\" greeting: front and rear logic text, then P1 opens and closes (4 s)." },
    { "DM:NOD",     kNodSteps,     SEQ_STEPCOUNT(kNodSteps),     3000,  TOGGLE_NONE, nullptr, 0,
      "Short acknowledgment: a sound, logic text, and a P1 panel wave (3 s)." },
    { "DM:FLUTTER", kFlutterSteps, SEQ_STEPCOUNT(kFlutterSteps), 10000, TOGGLE_NONE, nullptr, 0,
      "Ring then pie panels sweep partway open, then close (10 s)." },
    { "DM:BLOOM",   kBloomSteps,   SEQ_STEPCOUNT(kBloomSteps),   8000,  TOGGLE_NONE, nullptr, 0,
      "Pies open together over 1.2 s, wiggle three times, then close (8 s)." },
    { "DM:LEIA",    kLeiaSteps,    SEQ_STEPCOUNT(kLeiaSteps),    36000, TOGGLE_NONE, nullptr, 0,
      "Leia message mode: front holo plays Leia, other holos off, Leia logics and PSI; resets at the end (36 s)." },
    { "DM:ALARM",   kAlarmSteps,   SEQ_STEPCOUNT(kAlarmSteps),   10000, TOGGLE_NONE, nullptr, 0,
      "Pulsing red holos, logics, and PSI with a random alarm track (10 s)." },
    { "DM:HEART",   kHeartSteps,   SEQ_STEPCOUNT(kHeartSteps),   10000, TOGGLE_NONE, nullptr, 0,
      "Rainbow holos with a sweet logic message and a random track (10 s)." },
    { "DM:RESET",   kResetSteps,   SEQ_STEPCOUNT(kResetSteps),   4500,  TOGGLE_NONE, nullptr, 0,
      "Safe reset: staggered close of the ring panels and body latch clear, then resets holos, logics, and PSI; pies are left untouched for mechanical safety (4.5 s)." },
    { "DM:CANTINA",   kCantinaSteps,   SEQ_STEPCOUNT(kCantinaSteps),   17000, TOGGLE_NONE, nullptr, 0,
      "130 BPM alternating panel dance: groups swap open and closed each beat for about 15 s (17 s window)." },
    { "DM:ROCKMARCH", kRockmarchSteps, SEQ_STEPCOUNT(kRockmarchSteps), 49000, TOGGLE_NONE, nullptr, 0,
      "Imperial March with one ring panel stepping open per beat, a full ring pass repeated for about 45 s (47 s window)." },
    { "DM:SCREAM",    kScreamSteps,    SEQ_STEPCOUNT(kScreamSteps),    15000, TOGGLE_NONE, nullptr, 0,
      "Panels burst open with a red alert and a random one-panel flutter, then close (15 s)." },
    { "DM:OVERLOAD",  kOverloadSteps,  SEQ_STEPCOUNT(kOverloadSteps),  12000, TOGGLE_NONE, nullptr, 0,
      "Failure logics and PSI with holos short-circuiting and six panels fluttering on random targets, then everything resets (12 s)." },
    { "DM:PIES",    kPiesOpenSteps,    SEQ_STEPCOUNT(kPiesOpenSteps),    12000,
      TOGGLE_PIES, kPiesCloseSteps,    SEQ_STEPCOUNT(kPiesCloseSteps),
      "Toggle: pie wave opening PP1 to PP6 and back, twice; toggle again to reset holos and close the pies (12 s)." },
    { "DM:LOW",     kLowOpenSteps,     SEQ_STEPCOUNT(kLowOpenSteps),     15000,
      TOGGLE_LOW,  kLowCloseSteps,     SEQ_STEPCOUNT(kLowCloseSteps),
      "Toggle: ring-panel wave twice then all ring panels open; toggle again to close them one at a time, safely staggered (15 s)." },
    { "DM:OPENALL", kOpenallOpenSteps, SEQ_STEPCOUNT(kOpenallOpenSteps), 10000,
      TOGGLE_ALL,  kOpenallCloseSteps, SEQ_STEPCOUNT(kOpenallCloseSteps),
      "Toggle: pie sweep, all ring panels open, then a P1/P2 and PP2/PP4 twinkle; toggle again to close every panel in order (10 s)." },
    { "DM:SE30", kSe30Steps, SEQ_STEPCOUNT(kSe30Steps), 5000, TOGGLE_NONE, nullptr, 0,
      ":SE30 - Both utility arms swing out, then flick in and out twice before they close (5 s)." },
    { "DM:SE31", kSe31Steps, SEQ_STEPCOUNT(kSe31Steps), 14000, TOGGLE_NONE, nullptr, 0,
      ":SE31 - Every body door and arm opens and works, then folds away in order (14 s)." },
    { "DM:SE32", kSe32Steps, SEQ_STEPCOUNT(kSe32Steps), 5000, TOGGLE_NONE, nullptr, 0,
      ":SE32 - The breadpan doors, dataport and utility arms spring open, then wiggle shut (4 s)." },
    { "DM:SE33", kSe33Steps, SEQ_STEPCOUNT(kSe33Steps), 9000, TOGGLE_NONE, nullptr, 0,
      ":SE33 - The left breadpan door opens and the gripper arm rises and snaps its claw three times, then folds away (8 s)." },
    { "DM:SE34", kSe34Steps, SEQ_STEPCOUNT(kSe34Steps), 9000, TOGGLE_NONE, nullptr, 0,
      ":SE34 - The right breadpan door opens and the interface arm rises and works its tool three times, then folds away (9 s)." },
    { "DM:SE35", kSe35Steps, SEQ_STEPCOUNT(kSe35Steps), 13000, TOGGLE_NONE, nullptr, 0,
      ":SE35 - The two breadpan doors take turns opening, faster and then slower, then both close (13 s)." },
    { "DM:SE36", kSe36Steps, SEQ_STEPCOUNT(kSe36Steps), 8000, TOGGLE_NONE, nullptr, 0,
      ":SE36 - Both breadpan doors open and both grippers snap together five times, then fold away (7 s)." },
};
static constexpr uint8_t kCatalogSize =
    (uint8_t)(sizeof(kCatalog) / sizeof(kCatalog[0]));

// =============================================================================
// Alias table  --  DM:* names that forward directly to the dome unchanged or
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
// Lookup  --  pure, no side effects, native-testable.
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

// :SE30..:SE36 -> the Factory Sequence that is that body routine. The name is
// the number a builder already types, so a Retrained Sequence saved as DM:SE32
// is what :SE32 then runs on every trigger path (ADR 0006, ADR 0049).
const char* sequenceBodyRoutineName(int seId) {
    static const char* const kNames[] = {
        "DM:SE30", "DM:SE31", "DM:SE32", "DM:SE33", "DM:SE34", "DM:SE35", "DM:SE36",
    };
    if (seId < 30 || seId > 36) {
        return nullptr;
    }
    return kNames[seId - 30];
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
