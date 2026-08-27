# protoArtoo Goal

This document defines the durable technical direction for protoArtoo.

## Table of Contents

- [Objective](#objective)
- [System Scope](#system-scope)
- [Target Hardware Profile](#target-hardware-profile)
- [Technical Support Intent](#technical-support-intent)
- [Open Source and Transparency Commitments](#open-source-and-transparency-commitments)
- [Architecture Goals](#architecture-goals)
- [Operational and Safety Goals](#operational-and-safety-goals)
- [Interface and Integration Goals](#interface-and-integration-goals)
- [Operator Capability Baseline](#operator-capability-baseline)
- [How protoArtoo Differs From Other Community Stacks](#how-protoartoo-differs-from-other-community-stacks)
- [Out of Scope](#out-of-scope)
- [References](#references)

## Objective

protoArtoo targets a complete open-source ESP32 body-controller firmware stack for MK4 astromech droids, with predictable control behavior, explicit subsystem ownership, and maintainable long-term operation.

It also explicitly targets builders whose droids are primarily static display pieces for day-to-day use, where control from tablet or computer browser workflows is preferred over convention-style roaming with large RC transmitters.

## System Scope

| Scope boundary | Included | Excluded |
|---|---|---|
| Firmware ownership | body controller responsibilities: drive, RC input, safety, web API/UI, audio ownership, body actuators | replacing all community dome firmware ecosystems |
| Integration contract | body-dome interoperability for coordinated behavior | one-vendor lock to a single transmitter/receiver family |
| Operations | operator-facing configuration and diagnostics surfaces | proprietary app-only control requirement |
| Engineering workflow | reproducible build, test, verification, and support practices | undocumented ad-hoc operation paths |

## Target Hardware Profile

| Domain | Primary target | Compatibility direction |
|---|---|---|
| Body controller | Artoo Controller PCB family (v1.x), ESP32 D1 Mini form factor | keep board-profile logic extensible where safe |
| Drive subsystem | hoverboard integrations accepting Gen2.x-style UART frames at 115200 baud | protocol-contract compatibility over vendor lock |
| Dome integration | bidirectional serial coordination with AstroPixelsPlus-class dome stacks | explicit command/status ownership boundaries |
| Audio subsystem | body-side ownership; DY-SV5W as primary module | CHIRP Audio Trigger and SparkFun MP3 Trigger via driver abstraction |
| Actuators | utility arms, AUX role-flex outputs, dome ESC path | servo/LED role flexibility without architecture fork |

## Technical Support Intent

| Support dimension | Intent |
|---|---|
| RC modes | standard_pwm, single_sbus, dual_sbus |
| Usage focus | static-display-first operation with convenient tablet/computer control for regular use |
| Configuration model | runtime configuration for normal workflows, persisted state, validated API boundaries |
| Compatibility model | protocol/interface contract first, not binary-vendor lock |
| Integration stability | component variation is expected; external integration surfaces should remain stable |

## Open Source and Transparency Commitments

Commitments that must remain true:
- firmware, build configuration, and web UI source are publicly available
- protocol and integration behavior are documented, not hidden behind binary-only releases
- reused community patterns and ideas are acknowledged through references and repository history
- core body-controller functionality does not depend on private paid firmware blobs

Positioning:
- protoArtoo can reuse shared community hardware patterns while keeping implementation, behavior, and evolution transparent and community-auditable

## Architecture Goals

| Architecture pillar | Target outcome |
|---|---|
| Real-time behavior | deterministic loops, non-blocking control paths, predictable fail-safe outputs |
| Hardware ownership | explicit peripheral ownership by task/subsystem boundaries |
| Modularity | protocol-specific details isolated behind stable internal contracts |
| Separation of concerns | minimal cross-coupling between UI concerns and control-loop concerns |
| Maintainability | architecture/interface-focused docs and extensibility without safety regressions |

## Operational and Safety Goals

| Safety/operations pillar | Target outcome |
|---|---|
| Safety layering | independent stop paths from input acquisition through output transport |
| Estop semantics | latching estop behavior requiring explicit clear action |
| Recovery posture | watchdog-backed fail-safe recovery behavior |
| Operator reliability | no hidden control-mode transitions and explicit link/failsafe visibility |
| Diagnostics | practical field-troubleshooting surfaces for state and fault context |
| Verification posture | repeatable validation with test/build/API evidence paths |

## Interface and Integration Goals

| Interface pillar | Target outcome |
|---|---|
| API contracts | stable operator-facing HTTP/SSE surfaces with explicit validation and bounded error behavior |
| API evolution | compatibility-oriented contract change discipline |
| Web UX model | browser-first workflow from setup through runtime control |
| Operator target devices | desktop-first, tablet-second usage profile |
| Static-display operator flow | practical parked/bench-side operation without mandatory handheld RC transmitter usage |
| Recovery accessibility | direct access to configuration, diagnostics, and recovery actions without app lock-in |
| Dome coordination | explicit body-dome routing boundaries and unambiguous audio ownership during coordinated sequences |

## Operator Capability Baseline

The following baseline captures what protoArtoo is expected to provide in normal operation.

Control and safety baseline:
- RC control supports standard_pwm, single_sbus, and dual_sbus modes
- drive output paths enforce safety limits before transmit
- estop behavior remains latching and explicit-clear
- failsafe status is visible in diagnostics and API surfaces

Operator surface baseline:
- browser-accessible setup, control, and diagnostics are first-class workflows
- static-display operation is a first-class use case, not a secondary mode
- runtime settings persist without firmware rebuild for normal operation
- status and health surfaces expose link state and control state for troubleshooting

Audio and body-dome baseline:
- body-side audio ownership is the default architecture
- dome link behavior supports coordinated command routing with explicit responsibility boundaries

Hardware support baseline:
- target profile remains Artoo PCB + ESP32 D1 Mini form factor
- drive integration on the artoo-esp32 Board Variant remains hoverboard UART contract based, forced by that PCB's fixed wiring (one UART, no spare); other Board Variants may default to a different drive backend where their wiring allows it (ADR 0029)
- component-level compatibility remains contract-driven and documented

## How protoArtoo Differs From Other Community Stacks

### Stack Profile Snapshot (Capabilities + Typical Component Footprint)

This table captures stack posture and typical hardware assumptions.

| Stack | Typical controller hardware profile | Typical control input profile | Typical drive/audio profile | Source and distribution posture |
|---|---|---|---|---|
| protoArtoo | ESP32 body controller on Artoo PCB profile | RC receiver modes (PWM/SBUS) plus browser UI | Hoverboard UART drive focus (artoo-esp32's default; other Board Variants may differ, ADR 0029) plus body-owned audio modules (DY-SV5W primary) | fully open repository target (firmware + web + docs) with no binary-only paywall goal |
| Padawan360 | Arduino 2560 or Mega ADK (UNO possible) plus USB Host Shield | Xbox 360 wireless controller plus Xbox wireless USB receiver | Sabertooth (feet), SyRen (dome), MP3 Trigger audio; I2C-centric peripheral control model | public code and long-running community documentation are available |
| ShadowMD | Arduino 2560/Mega ADK plus USB Host Shield as a master coordinating MarcDuino/Benduino nodes | 1-2 PS3 Move Navigation controllers via CSR-class BT dongle | Sabertooth 2x32A (feet), SyRen 10 (dome), MP3 Trigger-class audio, XBee integration, sequence/animation routing to dependent nodes | ◐ public implementations/docs exist, but maintenance and exact architecture vary by fork/build |
| ShadowRC (Printed-Droid fork lineage) | Arduino Mega 2560/Mega ADK as main controller with MarcDuino integration path | RC transmitter/receiver model (Turnigy Evolution plus iBus-class receivers documented) | Sabertooth (feet), SyRen 10 (dome), direct MP3 Trigger plus parallel MarcDuino sound/panel control | ◐ public documentation and source publication are documented for this fork lineage; name now overlaps with unrelated closed-source software |
| DroidLink | ESP32-S3 master/slave/display model documented publicly | RC-centric operation with display plus web-installer workflow | ESC/Sabertooth/SyRen modes and DFPlayer wiring documented | ◐ docs are public, but firmware is license-gated via paid installer |
| Benduino | MarcDuino-derivative node system running on Arduino Pro Mini 5V/16MHz (or Nano) with multiple board variants | typically integrated into MarcDuino/ShadowMD-style upstream controller stacks | panel movement, lights, sound orchestration; can also be configured as BodyMaster in specific flash profiles | ◐ ecosystem documentation is public, while firmware lineage/rights are split across MarcDuino/BetterDuino-era contributors |
| AstroCAN | CAN-centered modular hardware ecosystem (shield, bridge, AutoDome, 232, lighting modules) | depends on host/controller stack integrated with AstroCAN modules | strong distributed module and transport emphasis | ◐ public ecosystem information is available; firmware openness varies by module/project |

### Capability Alignment Matrix

| Comparison axis (capability + required hardware emphasis) | protoArtoo | Padawan360 | ShadowMD | ShadowRC | DroidLink | Benduino | AstroCAN |
|---|---|---|---|---|---|---|---|
| Source transparency and attribution posture | ✅ open code and documented behavior are explicit goals | ✅ public repo and open sketch lineage | ◐ public forks exist, but not one canonical maintained source line | ◐ published as open fork lineage in documented 2021 release context; name now overlaps with unrelated closed-source software | ◐ docs public, firmware distribution license-gated | ◐ public docs describe lineage and rights boundaries; canonical firmware location varies by generation | ◐ ecosystem docs public; firmware openness depends on module/project |
| Core hardware dependency profile | ✅ ESP32 body controller + RC receiver + browser control path | ✅ Arduino + USB Host Shield + Xbox receiver/controller + Sabertooth/SyRen + MP3 Trigger | ✅ Arduino Mega/ADK + USB Host Shield + CSR BT + PS3 Nav + Sabertooth/SyRen + MarcDuino/Benduino node topology | ✅ Arduino Mega/ADK + RC TX/RX (iBus profile) + Sabertooth/SyRen + MP3 Trigger + MarcDuino stack | ✅ ESP32-S3 Master/Display/Slave model + licensed installer workflow | ✅ Arduino Pro Mini/Nano class node with terminal-rich board variants and compatibility with MarcDuino/BetterDuino ecosystems | ✅ CAN module stack with bridge/shield/module ecosystem |
| Body-controller firmware as primary project identity | ✅ core project identity | ✅ classic body sketch identity on Arduino | ✅ controller sketch identity in public implementations | ✅ RC-first body-control firmware evolution from ShadyRC with MarcDuino integration | ❌ docs emphasize install/use flow over open body firmware internals | ◐ often positioned as subsystem/node firmware, with optional BodyMaster usage in specific configurations | ❌ ecosystem primarily framed as modular CAN platform |
| Browser-first operator workflow without mandatory app | ✅ explicit goal, including static-display-first tablet/computer operation | ❌ controller-first (Xbox) in public baseline | ❌ controller-first in public baseline | ❌ transmitter/receiver-first control model, not browser-first | ◐ web installer/config flow present, but tied to licensed firmware delivery | ❓ unknown/integration-dependent | ◐ depends on integrated host stack |
| RC receiver-first architecture (PWM/SBUS families) | ✅ explicit target | ❌ public baseline emphasizes Xbox controller + USB host | ❌ public baseline emphasizes PS3 navigation controllers | ✅ explicit RC transmitter/receiver-first architecture (iBus profile documented) | ✅ SBUS wiring/configuration is documented | ❓ unknown/integration-dependent | ◐ depends on attached control stack |
| Protocol-contract drive integration, not vendor lock | ✅ explicit technical target (artoo-esp32 defaults to hoverboard UART, forced by that PCB's fixed wiring; other Board Variants may default differently, ADR 0029) | ❌ public baseline uses Sabertooth/SyRen drive stack | ❌ public baseline uses Sabertooth/SyRen drive stack | ❌ documented stack centers on Sabertooth/SyRen plus MarcDuino ecosystem | ❌ public baseline emphasizes ESC and Sabertooth/SyRen modes | ❓ unknown/integration-dependent | ❌ CAN module ecosystem focus, not hoverboard UART focus |
| Body-dome coordination transport model | ✅ bidirectional serial body<->dome goal | ✅ I2C is a documented core mechanism for connected component control | ✅ explicit master-to-MarcDuino/Benduino orchestration model | ✅ full MarcDuino function-code integration for dome/body panel and holo/lighting control | ✅ multi-node master/display/slave model is central | ✅ explicitly designed as MarcDuino-compatible animation/control node within distributed controller topologies | ✅ distributed inter-module communication is a central theme |
| Default audio ownership stance | ✅ body audio authority target | ◐ MP3 Trigger in body baseline; ownership varies by build | ◐ often MarcDuino/stack dependent | ✅ direct MP3 Trigger control plus parallel MarcDuino sound-path support is documented | ◐ DFPlayer path documented; ownership may be architecture-dependent | ◐ Benduino node handles sound triggering in distributed MarcDuino-style setups; final ownership depends on stack wiring/topology | ❓ depends on integrated module topology |

Note on evidence quality:
- Some ecosystems have clear public technical sources (Padawan360 docs + public repo, ShadowMD KB/fork data, ShadowRC fork KB data, DroidLink docs, AstroCAN overview).
- ShadowRC statements in this table refer to the Printed-Droid fork lineage and not to unrelated newer closed-source software using the same name.

## Out of Scope

- replacing all community ecosystems with a single monolithic stack
- undocumented protocol changes that break integration contracts
- one-off hardware hacks as default behavior

## References

Core references:
- [docs/status.md](status.md)
- [docs/topology.md](topology.md)
- [docs/pin_map.md](pin_map.md)
- [docs/api.md](api.md)
- [docs/console-protocol.md](console-protocol.md)
- [docs/failsafe.md](failsafe.md)
- [docs/commands.md](commands.md)

Community references used for comparison context:
- ShadowMD (hardware/profile overview): <https://www.printed-droid.com/kb/shadow-md-droid-control-system/>
- Padawan 360 (hardware/profile overview): <https://www.printed-droid.com/kb/padawan-360/>
- Padawan360 public code: <https://github.com/dankraus/padawan360>
- Padawan360 Astromech Wiki: <https://astromech.net/droidwiki/PADAWAN360>
- ShadowRC / ShadyRC fork overview: <https://www.printed-droid.com/kb/shadowrc-shadyrc-fork/>
- ShadowRC: <https://shadowrc.com>
- DroidLink docs: <https://github.com/DroidLink/DroidLink_Docs>
- Benduino system overview: <https://www.printed-droid.com/kb/benduino-system/>
- AstroCAN overview: <https://nextgenastromech.net/?page_id=614>
