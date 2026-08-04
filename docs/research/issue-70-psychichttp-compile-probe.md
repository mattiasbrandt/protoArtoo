# Issue #70: PsychicHttp Compile Probe on Pinned Stack

## Question
Does PsychicHttp 3.1.2 compile cleanly against protoArtoo's EXACT pinned stack with zero dependency changes?

## Stack Under Test
- **Platform**: pioarduino 55.03.37 (ESP-IDF 5.5.2 + arduino-esp32 3.3.7)
- **Board**: wemos_d1_mini32
- **Framework**: Arduino
- **Pinned dependencies**: ESPAsyncWebServer @ 3.11.2, AsyncTCP @ 3.4.10, ArduinoJson @ 7.4.3, Adafruit NeoPixel @ 1.15.1

## Methodology

1. Created new compile-check env `[env:protoArtoo_psychichttp_check]` in `platformio.ini`:
   - Extends `env:protoArtoo_chirp` (matches board's actual AUDIO_CHIRP config)
   - Adds `hoeken/PsychicHttp @ 3.1.2` as additional lib_dep (ADDITIVE, not replacing existing libs)
   - No changes to framework version, platform, or pinned library versions

2. Created minimal probe source (`src/experiments/psychichttp_compile_probe.cpp`):
   - Includes PsychicHttp headers
   - Declares pointer to PsychicHttpServer to force symbol resolution
   - Guarded with `#ifdef PA_PROBE_PSYCHICHTTP` to prevent compilation in regular builds

3. Built with: `pio run -e protoArtoo_psychichttp_check`

## Results

### Build Status
✅ **PASS** — Compilation successful, no errors or warnings related to PsychicHttp.

### Binary Size Impact
Compared against baseline `env:protoArtoo_chirp` (same driver, no PsychicHttp):

| Metric | Baseline | With PsychicHttp | Delta |
|--------|----------|------------------|-------|
| Flash (used) | 1,573,903 bytes | 1,585,627 bytes | +11,724 bytes (+0.7%) |
| Flash (%) | 92.4% | 93.1% | +0.7% |
| RAM (used) | 118,080 bytes | 118,080 bytes | 0 bytes (no change) |
| RAM (%) | 36.0% | 36.0% | (no change) |

### Dependency Resolution
Dependency graph for psychichttp_check env shows:
- ✅ PsychicHttp @ 3.1.2 resolved successfully
- ✅ No new transitive dependencies introduced (PsychicHttp brings only its own code)
- ✅ ESPAsyncWebServer @ 3.11.2 and AsyncTCP @ 3.4.10 remain unchanged and co-exist
- ✅ No version conflicts with pinned framework, ArduinoJson, or other dependencies

### Compatibility Finding
**No incompatibilities detected.**

PsychicHttp 3.1.2 compiles cleanly against:
- pioarduino 55.03.37 (which the library's upstream CI tests against v3.3.11 / IDF 5.5.5, but this proof shows backward compatibility to our pinned v3.3.7 / IDF 5.5.2)
- ESP-IDF 5.5.2
- Arduino-ESP32 3.3.7

## Validation Classification
**software-verified** — Compilation proven via `pio run -e protoArtoo_psychichttp_check` with full build log captured. No hardware required; this is a compile-only probe, not runtime behavior.

## Notes for Future Work
- The +11.7 KB flash cost is acceptable (0.7% of available space)
- This proof enables future tickets #72/#73 (PsychicHttp adapter design and migration)
- The additive probe design (keeping ESPAsyncWebServer/AsyncTCP) is safe for incremental migration planning
- NVS config safety: env extends protoArtoo_chirp to match board's actual driver config, preventing accidental mismatch if uploaded

## Conclusion
PsychicHttp 3.1.2 is **compile-compatible** with protoArtoo's pinned stack. No blocker found for future migration exploration (issue #53 sub-map).
