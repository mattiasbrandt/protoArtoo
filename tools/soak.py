#!/usr/bin/env python3
"""protoArtoo soak harness -- a permanent instrument, not one epic's scaffold.

Holds a protoArtoo controller's web stack under load for as long as it is
asked to, and answers one question: did it hold up? A run ends in one Run
Verdict (PASS / FAIL / INVALID), one exit code and one JSON artefact carrying
every number the verdict was taken from. Operator documentation is
docs/soak.md; the surface a consumer may rely on -- the exit codes and the
artefact's keys -- is ADR 0035.

Reads only. This tool never flashes, never calls `make ota`, never writes to
the controller's configuration and never touches firmware sources: everything
it does is a GET, plus the one POST that the C6-reset driver exists to make.

Three Image Modes can be driven, selected by --image and never sniffed (see
StatusSchema for why the declaration is checked rather than inferred):

  bench     bringup/p4_hosted_bench.cpp, env firebeetle2_hosted_bench. Built
            to be measured: bootCount, the raw esp_reset_reason_t, flat
            recovery-ladder counters, POST /api/c6/reset, and an /api/events
            stream whose payload is a monotonic frame counter.
  shipping  the firebeetle2 product image, src/web/web_server.cpp
            buildStatusJson(). No bootCount, resetReason as a string, the
            ladder nested under "hostedLink", no reset route at all (#243),
            and an /api/events stream of named status/rc/log events rather
            than a counter.
  artoo     the artoo_esp32 product image -- the same buildStatusJson(),
            minus the "hostedLink" block, which is emitted only inside
            `#if PA_CAP_HOSTED_WIFI` (:724-743) and that capability is 0 on
            artoo-esp32 (include/config.h:69). So: no bootCount, no
            hostedLink, no recovery ladder, no reset route, and the same
            named status/rc/log event stream, because eventStreamTask()
            carries no capability guard at all.

Structural rule, non-negotiable: there is exactly ONE SSE frame-parsing
implementation (SseFrameParser, driven only through BenchClient.stream_sse()),
one continuity wiring point (stream_sse_with_continuity()) and one
status-field map per Image Mode (StatusSchema); --self-test exercises all
three by starting a local http.server fixture and driving those same
production entry points against it -- never a second, hand-rolled parse loop
that only the test sees. This rule is written down because it has been broken:
earlier revisions of this harness carried a self-test with its own parse loop,
which stayed green while the real parser was broken outright. A test that does
not drive the production path proves nothing about it.

Bench endpoint contract, read directly from bringup/p4_hosted_bench.cpp
rather than assumed (b990b88, 1ee0640):
  GET  /api/health    -> "OK" liveness.
  GET  /api/status    -> JSON; see handleStatus() (line ~840) for the full
                          field set this script relies on.
  GET  /api/events    -> SSE, one monotonic-counter frame per second. Every
                          frame is `data: <n>\\r\\n\\r\\n` (emitSseFrame() calls
                          events.send(frame) with id=0, event=nullptr,
                          reconnect=0, and PsychicEventSource.cpp's
                          _generateEventMessage_impl() only emits id:/event:/
                          retry: lines when those arguments are truthy) --
                          there is never an id: or event: line on real frames.
  POST /api/c6/reset   -> Asynchronous (b990b88). 202 {requestId,
                          resetScheduled: true, responseGraceMs} proves
                          *scheduling*, not an edge -- GPIO54 does not fall
                          until responseGraceMs later. 503/409 with
                          {resetScheduled: false, reason} on rejection.

Shipping endpoint contract, read from src/web/web_seam_routes.cpp and
src/web/web_server.cpp:
  GET  /api/status    -> buildStatusJson():393; resetReasonName() at :401,
                          hostedLink at :724-742 behind PA_CAP_HOSTED_WIFI.
  GET  /api/events    -> SSE; eventStreamTask() (:793-900) broadcasts "rc"
                          every 1s tick, "status" on demand and "log" every
                          other tick, framed by webEventStreamFormatPrefix()
                          (src/web/web_event_stream.cpp:106) with millis() as
                          the id. api_events.cpp refuses the stream past
                          PA_ADMISSION_MAX_SSE_CLIENTS with 503.
  POST /api/c6/reset   -> does not exist. run_c6_reset_recovery() refuses
                          rather than reporting anything (#243).

Artoo endpoint contract, read from the same two files: /api/status,
/api/events and /api/health are registered unconditionally in
webRegisterSeamRoutes() (src/web/web_seam_routes.cpp:46,51,54), so the artoo
image serves the same three routes with the same shapes as the shipping
image, and neither product image has a reset route. Everything that differs
follows from the one board capability:
  GET  /api/status    -> no "hostedLink" object (:724-743 is compiled out),
                          hence no recovery-ladder evidence at all, and
                          nothing to corroborate wifiConnected with.
  GET  /api/events    -> identical: eventStreamTask() (:793-902) has no
                          preprocessor guard anywhere in it, and
                          api_events.cpp's client cap carries no board guard
                          either, so a stream past the cap is refused here the
                          same way (resolve_sse_client_cap() reads the number).
  POST /api/c6/reset   -> could not exist: there is no ESP32-C6 companion
                          radio on this board to reset.

Fixture derivation and the ESP_RST_* constants were READ -- from the vendor
source, the ESP-IDF header and the firmware itself (see SseFrame's docstring,
BAD_RESET_REASONS and SHIPPING_CRASH_SHAPED_RESET_NAMES below) -- rather than
recalled. Two earlier revisions recalled them instead and got both wrong, in
ways that would have made a run report the wrong answer confidently.

Nothing the firmware compiles in is restated here; both yardsticks are read
out of the tree the harness ships in, per build environment:

  the heap verdict     against the admission floor the firmware itself
                       refuses ordinary requests at, from platformio.ini --
                       see "The compiled admission floor" below for why a
                       percentage of a baseline sample was the wrong class of
                       proof, and for what replaced it.
  the concurrency      against the client cap /api/events admits, from
                       include/web_event_stream.h -- see "The compiled SSE
                       client cap".

A run that lasts hours has to be legible while it lasts and has to survive
being cut short, so RunMonitor and RunConsole (see "Progress, checkpoints and
interruption" below) own four things no driver owns for itself: a heartbeat
line per interval plus a once-a-second status line on STDERR, a plain
transcript of both in a log file whose path the report carries, a periodic
checkpoint of the --json artefact, and the SIGINT/SIGTERM path that stops the
drivers and still leaves a report. stdout carries the JSON report and nothing
else, because `soak.py > report.json` and `soak.py | jq` are working
contracts. An interrupted run reports the verdict INTERRUPTED / INCOMPLETE
and exits EXIT_INVALID -- never a pass, and never a failure either, because a
truncated run has not covered the contract it was asked to cover: its required
evidence is missing, which is exactly what that exit code means. None of that
changes what a run that finishes concludes: a driver called without a monitor,
and a run that is never interrupted, reach the same verdict by the same code
as before any of it existed.
"""
from __future__ import annotations

import argparse
import configparser
import dataclasses
import http.client
import http.server
import json
import os
import random
import re
import signal
import socket
import struct
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Callable, Optional
from urllib.parse import parse_qs, urlsplit

# The one non-stdlib dependency this harness has, and a hard one: imported at
# module scope with no try/except and no fallback renderer, so there is a
# single presentation path rather than two that can disagree about what a run
# looked like. Declared in tools/requirements.txt, which CI installs. rich owns
# terminal detection, NO_COLOR and TERM=dumb; RunConsole does not second-guess
# any of that.
from rich.console import Console
from rich.live import Live
from rich.text import Text

# ---------------------------------------------------------------------------
# Constants read from source -- never invented (AGENTS.md "no guessing").
# ---------------------------------------------------------------------------

# ~/.platformio-p4/packages/framework-arduinoespressif32-libs/esp32p4/include/
# esp_system/include/esp_system.h -- esp_reset_reason_t. Unnumbered enum
# starting at ESP_RST_UNKNOWN = 0; the mapping below is that declaration
# order, read rather than recalled. An early revision of this harness recalled
# it instead and produced [1, 3, 4, 5], which would reject a normal power-on
# (1 is ESP_RST_POWERON, not a fault) and miss ESP_RST_TASK_WDT entirely (6,
# not 5) -- the one reset AGENTS.md names as the project's top concern.
ESP_RESET_REASON_NAMES = {
    0: "ESP_RST_UNKNOWN",
    1: "ESP_RST_POWERON",
    2: "ESP_RST_EXT",
    3: "ESP_RST_SW",
    4: "ESP_RST_PANIC",
    5: "ESP_RST_INT_WDT",
    6: "ESP_RST_TASK_WDT",
    7: "ESP_RST_WDT",
    8: "ESP_RST_DEEPSLEEP",
    9: "ESP_RST_BROWNOUT",
    10: "ESP_RST_SDIO",
    11: "ESP_RST_USB",
    12: "ESP_RST_JTAG",
    13: "ESP_RST_EFUSE",
    14: "ESP_RST_PWR_GLITCH",
    15: "ESP_RST_CPU_LOCKUP",
}
# PANIC, INT_WDT, TASK_WDT, WDT and CPU_LOCKUP are unambiguously crash-shaped
# resets on their own (the header's own comments: "due to exception/panic",
# "due to interrupt watchdog", "due to task watchdog", "due to other
# watchdogs", "due to CPU lock up (double exception)"). POWERON/EXT/SW/
# DEEPSLEEP/BROWNOUT/SDIO/USB/JTAG/EFUSE/PWR_GLITCH/UNKNOWN are not, by
# themselves, evidence of a panic or watchdog reset.
BAD_RESET_REASONS = {4, 5, 6, 7, 15}

# src/reset_reason.cpp -- resetReasonName()'s switch, read in full. The
# shipping image publishes this NAME, not the enum value
# (src/web/web_server.cpp:401), and the mapping is deliberately not a
# bijection: every reason the switch does not case on (ESP_RST_CPU_LOCKUP,
# ESP_RST_PWR_GLITCH, ESP_RST_USB, ESP_RST_JTAG, ESP_RST_EFUSE) falls to its
# default and is published as "OTHER". A CPU lockup is therefore
# indistinguishable from a JTAG reset on that image, which is why the
# shipping classification below is tri-state: "OTHER" is recorded as unknown
# and never as "not crash-shaped". Widening that switch is a firmware change,
# so this harness reports the collapse rather than working around it.
SHIPPING_CRASH_SHAPED_RESET_NAMES = {"PANIC", "INT_WDT", "TASK_WDT", "WDT"}
SHIPPING_CLEAN_RESET_NAMES = {"POWERON", "EXTERNAL", "SOFTWARE", "DEEPSLEEP", "BROWNOUT", "SDIO"}
SHIPPING_UNKNOWN_RESET_NAMES = {"UNKNOWN", "OTHER"}

# src/web/web_server.cpp:724-743 -- the recovery-ladder object, emitted inside
# `#if PA_CAP_HOSTED_WIFI` and therefore absent from every image built for a
# board whose capability is 0 (include/config.h:69, artoo-esp32). Named once so
# the schema that READS the block and the schema that requires its ABSENCE
# cannot drift apart on the spelling: a rename that reached only one of them
# would leave the other silently accepting the wrong image.
HOSTED_LINK_CONTAINER = "hostedLink"

DEFAULT_HEALTH_PATH = "/api/health"
DEFAULT_STATUS_PATH = "/api/status"
DEFAULT_RESET_PATH = "/api/c6/reset"
DEFAULT_SSE_PATH = "/api/events"

# The process exit codes, which are this tool's contract along with the keys of
# its JSON artefact (ADR 0035). The VALUES never move: a wrapper script, a CI
# step or a scheduled regression run may rely on them. The NAMES are ordinary
# source identifiers and were reworded when the verdict vocabulary was.
EXIT_PASS = 0
EXIT_SELF_TEST_FAILURE = 1
EXIT_FAIL = 2
# "The required evidence is missing" -- reached by a coverage gap, by a
# contract violation, and by a run that was cut short. Not a failure: a
# failure is a controller that was watched and found wanting.
EXIT_INVALID = 3

# ---------------------------------------------------------------------------
# The verdict vocabulary.
#
# ADR 0035: the exit codes and the JSON keys are the contract, the verdict
# WORDING is prose and may be reworded. Every string is therefore named here
# rather than written as a bare literal at the point of comparison, so a
# rewording has one place to land instead of being buried in an output path.
#
# A Soak Driver's verdict and the Run Verdict speak the same three words
# (CONTEXT.md, Run Verdict): a run that says PASS is saying what its drivers
# said, not translating them into a second vocabulary.
# ---------------------------------------------------------------------------

VERDICT_PASS = "PASS"
VERDICT_FAIL = "FAIL"
VERDICT_INVALID = "INVALID"
# A Soak Driver that cannot run on the declared Image Mode at all -- no reset
# route to provoke, for instance. Never a pass: it collapses to INVALID at the
# Run Verdict, because a coverage gap is not evidence of health.
VERDICT_UNAVAILABLE = "UNAVAILABLE"
# A driver that ran and measured, above the concurrency the verdict is defined
# at. It found nothing wrong and claims nothing (see run_sse_soak()).
VERDICT_OBSERVATION_ONLY = "OBSERVATION_ONLY"

# Soak Driver verdicts that mean "this driver found nothing wrong", for the
# footer and the progress surface.
DRIVER_VERDICTS_WITHOUT_A_FINDING = (VERDICT_PASS, VERDICT_OBSERVATION_ONLY)

# The verdicts a run that did not finish carries. Additions to the vocabulary
# rather than changes to it: each describes a state that used to produce no
# report at all (an interrupted run exited 143 and wrote nothing), and none is
# reachable by a run that completes. An interrupted run exits EXIT_INVALID,
# "the required evidence is missing" -- and a truncated soak is precisely that.
VERDICT_INTERRUPTED_RUN = "INTERRUPTED / INCOMPLETE"
VERDICT_INTERRUPTED_DRIVER = "INTERRUPTED"
# Written into a mid-run checkpoint artefact. Deliberately not any verdict a
# finished run can carry: a consumer that finds this string is holding
# evidence from a run that had not reached its own conclusion.
VERDICT_CHECKPOINT = "IN PROGRESS / INCOMPLETE"

# The shape of the JSON artefact, for a consumer that has to decide whether it
# can read this report at all. Bumped when a key is REMOVED or its meaning
# changes; adding a key does not bump it, because a consumer that ignores
# unknown keys is unaffected by an addition.
#
#   4  the top-level "issue" key is gone -- this harness is a permanent
#      instrument and its artefact does not belong to one ticket -- and the
#      Run Verdict speaks its drivers' words (PASS/FAIL/INVALID) instead of a
#      second, gate-shaped vocabulary. Exit codes unchanged.
#   3  the sse_soak heap verdict became the compiled admission floor rather
#      than a percentage of a baseline sample: "heapTolerancePct" left that
#      driver's report and the "admissionFloor" block arrived.
#   2  the harness gained Image Modes: "image" and "statusFieldsRead", and
#      restart evidence published under each image's own key names.
#   1  the first artefact, bench Image Mode only.
REPORT_SCHEMA_VERSION = 4


# ---------------------------------------------------------------------------
# The compiled admission floor -- read from platformio.ini, never restated.
#
# The soak's heap verdict used to be "the largest free 8-bit block fell more
# than N% below the baseline sample". That is not evidence about service, and
# the first graded artoo soak (#194) proved it by failing on exactly that rule
# while nothing that matters had moved: heapFree held ~80 000 throughout,
# heapMin was 43 240 before and after, every refusal counter read 0, and the
# block recovered to 38 900 the moment the clients left. A percentage of an
# arbitrary sample measures how spiky a fragmentation reading is. What an
# operator needs to know is whether the controller was still admitting work.
#
# The level at which it stops admitting work is compiled in, and declared
# exactly once, in platformio.ini [flags_base] under its calibration rationale:
#
#   PA_ADMISSION_MIN_LARGEST_FREE_BLOCK       ordinary requests
#   PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG  /api/status, /api/events and the
#                                             other read-only diagnostics
#   PA_ADMISSION_OVERRIDE_HEAP_FLOOR          bench-only, ordinary class only
#
# Those numbers are READ from that file rather than copied here. A copy would
# be a second source of truth, and the rationale block above those very lines
# invites re-calibration -- so the copy would go stale the first time anyone
# accepted the invitation, and this harness would then be judging a controller
# against a floor it no longer has.
#
# Deliberately NOT consulted: the `#ifndef PA_ADMISSION_MIN_LARGEST_FREE_BLOCK`
# fallbacks at src/web/web_admission_psychic.cpp:95-100. They are a compile-time
# safety net so the guard still builds if a flag goes missing, not a calibrated
# value for any board -- and they are a second place the number is written, with
# nothing keeping the two in step. An environment that does not carry the flag
# is an environment nobody calibrated, so its run is INVALID here rather than
# judged against a net. That is the same "proof of the wrong class" failure this
# whole section exists to remove, one level down.
# ---------------------------------------------------------------------------

# tools/soak.py -> <repo>. The harness reads platformio.ini out of the tree it
# ships in, so a worktree judges its own branch's floor rather than another's.
REPO_ROOT = Path(__file__).resolve().parent.parent
PLATFORMIO_INI = REPO_ROOT / "platformio.ini"

ADMISSION_FLOOR_MACRO = "PA_ADMISSION_MIN_LARGEST_FREE_BLOCK"
ADMISSION_FLOOR_DIAG_MACRO = "PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG"
ADMISSION_FLOOR_OVERRIDE_MACRO = "PA_ADMISSION_OVERRIDE_HEAP_FLOOR"

# The two option names a -D can reach the admission guard through.
# build_flags applies to everything compiled; build_src_flags applies to src/
# only, which is where the guard lives -- so a floor declared in either one is
# in force, and reading only the first would report a false "not declared".
PIO_FLAG_OPTIONS = ("build_flags", "build_src_flags")

# `-D NAME=VALUE` and `-DNAME=VALUE` are both used in platformio.ini, sometimes
# for the same macro in different envs.
_MACRO_DEFINITION_RE = re.compile(r"-D\s*([A-Za-z_][A-Za-z0-9_]*)(?:=(\S+))?")
# PlatformIO's own interpolation, `${section.option}` -- not configparser's
# `${section:option}`, which is why the parser below runs with interpolation
# disabled and this is expanded by hand.
_PIO_REFERENCE_RE = re.compile(r"\$\{([^}]+)\}")


class BuildConstantUnresolved(Exception):
    """A constant compiled into the firmware could not be read out of the tree.

    Two are resolved this way -- the admission floors below, and the SSE client
    cap after them -- and both obey one rule: raised, never defaulted. An
    unresolvable yardstick makes a run INVALID, because a run judged against a
    number this harness invented would be worth less than no run at all: it
    would carry the look of a verdict without the substance of one. That rule
    was settled when the heap verdict stopped being a percentage of a baseline
    sample (#194).
    """


@dataclasses.dataclass(frozen=True)
class AdmissionFloor:
    """The floors one build environment compiles in, with their provenance.

    `ordinary_bytes` is the floor a verdict is taken against: it is what the
    firmware refuses ORDINARY requests at, i.e. the operator's dashboard and
    every asset on it. `diagnostic_bytes` is the lower floor /api/status and
    /api/events keep (webPathIsDiagnostic(), src/web/web_admission.cpp:232-249)
    -- which is exactly why this harness can still be polling a controller that
    has already started shedding the pages an operator wants.
    """

    env: str
    ordinary_bytes: int
    # What [flags_base] declares, before any bench override is applied. Equal to
    # ordinary_bytes unless override_bytes is in force; kept separately so a
    # report shows that 40000 is an override of 9000 rather than a floor
    # somebody calibrated.
    declared_ordinary_bytes: int
    diagnostic_bytes: int
    override_bytes: Optional[int]
    # macro name -> "[section].option" it was read from.
    sources: dict[str, str]
    ini_path: str

    def report(self) -> dict:
        return {
            "env": self.env,
            "ordinaryFloorBytes": self.ordinary_bytes,
            "declaredOrdinaryFloorBytes": self.declared_ordinary_bytes,
            "diagnosticFloorBytes": self.diagnostic_bytes,
            "overrideFloorBytes": self.override_bytes,
            "macros": dict(self.sources),
            "readFrom": self.ini_path,
            "note": (
                "The ordinary floor is what src/web/web_admission.cpp refuses ordinary "
                "requests at (the dashboard and its assets); the diagnostic floor is the "
                "lower one /api/status and /api/events keep (webPathIsDiagnostic(), "
                ":232-249), so this harness's own traffic outlives the point at which an "
                "operator's page load is already being shed. The heap verdict is taken "
                "against the ordinary floor. Values are read from platformio.ini per "
                "environment and never restated in the harness"
            ),
        }


def _pio_config(ini_path: Path) -> configparser.RawConfigParser:
    """platformio.ini as raw sections, with no interpolation applied.

    RawConfigParser rather than ConfigParser: PlatformIO's `${section.option}`
    is not configparser's `${section:option}`, so the built-in interpolations
    would either fail or silently mangle values. '=' is the only delimiter --
    configparser's default set also includes ':', which would split
    `platform = https://github.com/...` in the wrong place on some values.
    """
    config = configparser.RawConfigParser(delimiters=("=",), strict=True)
    # Option names are matched exactly rather than lower-cased: what is wanted
    # here is what the file says, not a normalisation of it.
    config.optionxform = str
    with ini_path.open(encoding="utf-8") as handle:
        config.read_file(handle, source=str(ini_path))
    return config


def pio_environments(config: configparser.RawConfigParser) -> list[str]:
    """Every `[env:*]` name declared in the file, without the prefix."""
    return sorted(
        section[len("env:"):] for section in config.sections() if section.startswith("env:")
    )


def _pio_extends(config: configparser.RawConfigParser, section: str) -> list[str]:
    """The sections `section` extends, in declaration order.

    PlatformIO allows several; this file only ever names one, but reading it as
    a list costs nothing and a single-value reading would silently drop the
    second the day one is added.
    """
    raw = config.get(section, "extends", fallback="")
    return [part.strip() for part in raw.replace("\n", ",").split(",") if part.strip()]


def _pio_flag_sources(
    config: configparser.RawConfigParser, section: str, option: str,
    seen: Optional[set] = None,
) -> list[tuple[str, str]]:
    """(section, text) for every place `section`'s `option` gets its value.

    Two composition rules, both taken from platformio.ini's own documentation of
    them and matched by test/test_tools/test_env_flag_declarations.py, which
    resolves PA_LOG_LEVEL and PA_HEAP_PROFILE the same way:

      extends   a child that declares the option REPLACES the parent's value
                outright rather than appending to it. platformio.ini says so at
                [env:firebeetle2_profiler] ("a child env's build_flags REPLACES
                the parent's list"), and that is why that env has to restate
                PA_BOARD and the USB CDC flags by hand. So the value comes from
                the nearest ancestor that declares the option, and no further.
      ${a.b}    an explicit reference to another section's option, expanded in
                place. [flags_base] is reached ONLY this way -- which is
                precisely why "every env inherits the admission floor" is false:
                [env:native] declares its own build_flags and references
                [flags_base] nowhere, so no floor reaches it at all.
    """
    seen = set() if seen is None else seen
    key = (section, option)
    if key in seen:
        return []
    seen.add(key)
    if not config.has_section(section):
        raise BuildConstantUnresolved(
            f"platformio.ini refers to a section [{section}] that it does not declare "
            f"(reached while resolving {option!r})"
        )
    if not config.has_option(section, option):
        sources: list[tuple[str, str]] = []
        for parent in _pio_extends(config, section):
            sources.extend(_pio_flag_sources(config, parent, option, seen))
        return sources
    raw = config.get(section, option)
    # The references are stripped from this section's own text so a macro is
    # attributed to the section that actually writes it, not to the one that
    # merely pulls another section in.
    sources = [(section, _PIO_REFERENCE_RE.sub(" ", raw))]
    for reference in _PIO_REFERENCE_RE.findall(raw):
        ref_section, dot, ref_option = reference.rpartition(".")
        if not dot:
            raise BuildConstantUnresolved(
                f"[{section}].{option} contains the reference ${{{reference}}}, which "
                "names no section.option pair -- platformio.ini interpolation is "
                "${section.option}"
            )
        sources.extend(_pio_flag_sources(config, ref_section, ref_option, seen))
    return sources


def _pio_macro_definitions(
    config: configparser.RawConfigParser, env_section: str,
) -> dict[str, tuple[str, str]]:
    """{macro: (value, "[section].option")} for one environment.

    A macro defined twice with two different values is raised on rather than
    resolved: two definitions of one macro reaching a single -Werror compile is
    the #244 defect shape, and picking one of them here would be inventing an
    answer the compiler does not agree with.
    """
    definitions: dict[str, tuple[str, str]] = {}
    for option in PIO_FLAG_OPTIONS:
        for section, text in _pio_flag_sources(config, env_section, option):
            origin = f"[{section}].{option}"
            for name, value in _MACRO_DEFINITION_RE.findall(text):
                previous = definitions.get(name)
                if previous is not None:
                    if previous[0] != value:
                        raise BuildConstantUnresolved(
                            f"[{env_section}] resolves {name} to two different values: "
                            f"{previous[0]!r} from {previous[1]} and {value!r} from "
                            f"{origin}. Two definitions of one macro reaching one compile "
                            "is a build defect in its own right; this harness will not "
                            "guess which one the firmware took"
                        )
                    continue
                definitions[name] = (value, origin)
    return definitions


def _macro_int(definitions: dict[str, tuple[str, str]], macro: str, env: str) -> int:
    value, origin = definitions[macro]
    try:
        # base 0 so a hexadecimal calibration would be read rather than refused.
        return int(value, 0)
    except ValueError as not_an_int:
        raise BuildConstantUnresolved(
            f"[env:{env}] defines {macro} as {value!r} ({origin}), which is not an "
            f"integer byte count: {not_an_int}"
        ) from not_an_int


def _pio_config_for(env: str, ini_path: Path) -> configparser.RawConfigParser:
    """The parsed file, with `[env:<env>]` confirmed to exist in it."""
    try:
        config = _pio_config(ini_path)
    except (OSError, configparser.Error) as unreadable:
        raise BuildConstantUnresolved(
            f"could not read {ini_path}: {unreadable}"
        ) from unreadable
    if not config.has_section(f"env:{env}"):
        raise BuildConstantUnresolved(
            f"{ini_path} declares no [env:{env}], so there is no build environment to read "
            f"this run's compiled values from. Declared environments: "
            f"{', '.join(pio_environments(config))}"
        )
    return config


def require_declared_environment(env: str, ini_path: Path = PLATFORMIO_INI) -> None:
    """Raise BuildConstantUnresolved unless platformio.ini declares this env.

    Used on an image that has no floor to resolve: --build-env still appears in
    the report, and a name matching no environment must not be presented there
    with the look of a checked fact.
    """
    _pio_config_for(env, ini_path)


def resolve_admission_floor(env: str, ini_path: Path = PLATFORMIO_INI) -> AdmissionFloor:
    """The admission floors compiled into `env`, read from platformio.ini.

    Raises BuildConstantUnresolved -- never returns a default -- when the file
    cannot be read, the environment is not declared, or the environment does not
    resolve both floors. The last of those is a real case rather than a
    defensive branch: [env:native] declares its own build_flags without ever
    referencing [flags_base], so it carries no floor at all.
    """
    config = _pio_config_for(env, ini_path)
    section = f"env:{env}"
    definitions = _pio_macro_definitions(config, section)
    missing = [
        macro for macro in (ADMISSION_FLOOR_MACRO, ADMISSION_FLOOR_DIAG_MACRO)
        if macro not in definitions
    ]
    if missing:
        raise BuildConstantUnresolved(
            f"[{section}] resolves no {' and no '.join(missing)}. Those are declared in "
            f"{ini_path}'s [flags_base], which an environment only inherits by naming "
            "${flags_base.build_flags} in its own build_flags -- an environment that does "
            "not is an environment nobody calibrated a floor for, so this run has no "
            "yardstick and is INVALID rather than judged against a default"
        )

    declared_ordinary = _macro_int(definitions, ADMISSION_FLOOR_MACRO, env)
    diagnostic = _macro_int(definitions, ADMISSION_FLOOR_DIAG_MACRO, env)
    override = (
        _macro_int(definitions, ADMISSION_FLOOR_OVERRIDE_MACRO, env)
        if ADMISSION_FLOOR_OVERRIDE_MACRO in definitions else None
    )
    # src/web/web_admission.cpp:216-220, read rather than reconstructed: the
    # override replaces the ORDINARY floor and only when it is non-zero, and
    # diagnostics keep their own floor either way -- which is what leaves
    # /api/status answering while an induced-pressure session refuses
    # everything else.
    ordinary = override if override else declared_ordinary

    # A floor at or below zero is not a low floor, it is no floor: the guard's
    # own comparison is `in.largestFreeBlock < floor`
    # (src/web/web_admission.cpp:222), which can never be true at 0, so every
    # run would pass whatever the heap did -- and the margin percentage would
    # divide by it. There is nothing to judge a run against, which is the same
    # INVALID as a floor that could not be read at all.
    if ordinary <= 0 or diagnostic <= 0:
        raise BuildConstantUnresolved(
            f"[{section}] resolves an admission floor of {ordinary} ordinary / "
            f"{diagnostic} diagnostic. A floor of zero or below is a level the firmware "
            "can never refuse at, so a soak judged against it would pass whatever the "
            "heap did"
        )

    sources = {
        ADMISSION_FLOOR_MACRO: definitions[ADMISSION_FLOOR_MACRO][1],
        ADMISSION_FLOOR_DIAG_MACRO: definitions[ADMISSION_FLOOR_DIAG_MACRO][1],
    }
    if override is not None:
        sources[ADMISSION_FLOOR_OVERRIDE_MACRO] = (
            definitions[ADMISSION_FLOOR_OVERRIDE_MACRO][1]
        )
    try:
        readable_path = str(ini_path.relative_to(REPO_ROOT))
    except ValueError:
        readable_path = str(ini_path)
    return AdmissionFloor(
        env=env,
        ordinary_bytes=ordinary,
        declared_ordinary_bytes=declared_ordinary,
        diagnostic_bytes=diagnostic,
        override_bytes=override,
        sources=sources,
        ini_path=readable_path,
    )


# ---------------------------------------------------------------------------
# The compiled SSE client cap -- read from the header, never restated.
#
# PA_ADMISSION_MAX_SSE_CLIENTS is how many concurrent /api/events streams a
# product image admits before src/web/api_events.cpp answers the next one 503.
# It is what makes a soak's --num-clients mean something: at the cap, the soak
# is holding exactly as many streams as the firmware will ever hold, which is
# the concurrency a verdict is defined at. Above it, a run is measuring a
# refusal the firmware is designed to make, so it is recorded and carries no
# verdict of its own.
#
# The value is declared in include/web_event_stream.h, guarded:
#
#     #ifndef PA_ADMISSION_MAX_SSE_CLIENTS
#     #define PA_ADMISSION_MAX_SSE_CLIENTS 3
#     #endif
#
# so the header's number is the default and a build may displace it with a -D,
# exactly like the admission floors. Both halves are therefore read -- the
# header for the default, platformio.ini for a per-environment override --
# rather than the number being copied into this file. A copy is a second source
# of truth with nothing keeping the two in step, and this one would rot
# silently: a stale cap does not fail, it quietly changes what `--num-clients 3`
# MEANS, from "at the cap" to "one short of it" or "one over".
#
# The bench image is a deliberate exception in one direction only. Its stream is
# PsychicEventSource, which has no cap of its own (ADR 0030), so nothing there
# refuses a fourth client -- BenchStatusSchema.enforces_sse_client_cap is False
# and the storm never counts a capacity refusal against it. The cap still
# selects the bench soak's concurrency, because the point of a bench soak is to
# drive the transport at the concurrency the product firmware will actually see.
# ---------------------------------------------------------------------------

WEB_EVENT_STREAM_HEADER = REPO_ROOT / "include" / "web_event_stream.h"
SSE_CLIENT_CAP_MACRO = "PA_ADMISSION_MAX_SSE_CLIENTS"

# The `#define NAME <int>` inside the header's #ifndef guard. Anchored on the
# macro name rather than on a line number so moving the block does not silently
# stop matching -- a regex that found nothing would otherwise read as "the
# header no longer declares a cap".
_HEADER_DEFINE_RE_TEMPLATE = r"^[ \t]*#[ \t]*define[ \t]+{macro}[ \t]+([0-9]+)[ \t]*$"


@dataclasses.dataclass(frozen=True)
class SseClientCap:
    """The concurrent-stream cap one build compiles in, with its provenance.

    `value` is the cap in force. `source` says where it came from in words,
    because "3, from the header default" and "3, because this environment sets
    it to 3" are different facts about a build and an operator reading a report
    a year from now should not have to guess which one they are holding.
    """

    env: str
    value: int
    source: str
    # What the header declares, before any per-environment -D. Equal to `value`
    # unless an environment overrides it; kept separately so a report can show
    # that a cap of 5 is an override of 3 rather than a number somebody chose
    # here.
    header_default: int
    header_path: str

    def report(self) -> dict:
        return {
            "env": self.env,
            "macro": SSE_CLIENT_CAP_MACRO,
            "clients": self.value,
            "headerDefault": self.header_default,
            "readFrom": self.source,
            "note": (
                "The number of concurrent /api/events streams a product image admits "
                "before src/web/api_events.cpp refuses the next one with 503. It is the "
                "concurrency an sse_soak verdict is defined at: at or below it a run "
                "counts toward the verdict, above it the run is recorded as observation "
                "only, because a refusal at the cap is the firmware working as designed. "
                "Read from the header's #ifndef default and from any -D the build "
                "environment adds, never restated in the harness. The bench image's "
                "vendor stream has no cap of its own (ADR 0030), so nothing refuses "
                "there -- the number still selects that run's concurrency"
            ),
        }


def read_header_sse_client_cap(
    header_path: Path = WEB_EVENT_STREAM_HEADER,
) -> tuple[int, str]:
    """(default, readable-path) for the cap the header declares.

    Raises BuildConstantUnresolved rather than defaulting, for the same reason
    the floor resolver does: a soak whose concurrency target this harness made
    up is a soak measuring something nobody specified.
    """
    try:
        text = header_path.read_text(encoding="utf-8")
    except OSError as unreadable:
        raise BuildConstantUnresolved(
            f"could not read {header_path}, which declares {SSE_CLIENT_CAP_MACRO} -- "
            "there is no concurrent-stream cap to drive a soak at"
        ) from unreadable
    matches = re.findall(
        _HEADER_DEFINE_RE_TEMPLATE.format(macro=SSE_CLIENT_CAP_MACRO), text, re.MULTILINE
    )
    try:
        readable_path = str(header_path.relative_to(REPO_ROOT))
    except ValueError:
        readable_path = str(header_path)
    if len(matches) != 1:
        # Zero means the header stopped declaring it; more than one means the
        # header declares it twice and this harness cannot say which one a
        # compile takes. Neither is a number to soak against.
        raise BuildConstantUnresolved(
            f"{readable_path} declares {SSE_CLIENT_CAP_MACRO} {len(matches)} time(s); "
            "exactly one `#define` is expected inside its #ifndef guard, and this "
            "harness will not guess a concurrent-stream cap"
        )
    return int(matches[0]), readable_path


def resolve_sse_client_cap(
    env: str, ini_path: Path = PLATFORMIO_INI,
    header_path: Path = WEB_EVENT_STREAM_HEADER,
) -> SseClientCap:
    """The concurrent-stream cap `env` compiles in.

    The header's `#ifndef` default unless this environment defines the macro
    itself, in which case the -D wins -- which is exactly what the preprocessor
    does with a guarded default, and why both halves have to be read. Resolved
    per environment for the same reason the floors are: two images can publish
    byte-identical payloads and be built with different caps, so nothing in
    /api/status can tell them apart.
    """
    header_default, header_source = read_header_sse_client_cap(header_path)
    config = _pio_config_for(env, ini_path)
    definitions = _pio_macro_definitions(config, f"env:{env}")
    if SSE_CLIENT_CAP_MACRO in definitions:
        value = _macro_int(definitions, SSE_CLIENT_CAP_MACRO, env)
        source = definitions[SSE_CLIENT_CAP_MACRO][1]
    else:
        value = header_default
        source = header_source
    # A cap of zero or below is not a low cap, it is no stream at all:
    # api_events.cpp's `count >= cap` refuses the FIRST client, and the soak
    # would be asked to hold a negative number of them.
    if value < 1:
        raise BuildConstantUnresolved(
            f"[env:{env}] resolves {SSE_CLIENT_CAP_MACRO} to {value} ({source}). A cap "
            "below one admits no stream at all, so there is no concurrency for a soak "
            "to run at"
        )
    return SseClientCap(
        env=env, value=value, source=source,
        header_default=header_default, header_path=header_source,
    )


# src/web/web_server.cpp:425-427 -- acceptMinLargestBlockSeen is published as -1
# when g_webAcceptMinLargestBlockSeen is still UINT32_MAX, i.e. the Connection
# Admission guard has not sampled the heap even once this boot (it samples only
# when a connection arrives and the rate check passes). That is "no reading",
# and it must never be read as a byte count: -1 against any floor would report
# the deepest possible breach on a controller that has merely been idle.
ACCEPT_MIN_LARGEST_BLOCK_NEVER_SAMPLED = -1

# Keys of one per-poll heap-series row. Named once so the rows, the aggregates
# derived from them and the tests that read them cannot drift apart on a
# spelling. `tS` is seconds since the soak's own clock started, so the shape of
# a run is recoverable from the report alone -- #194's graded run could not
# distinguish "touched 11 764 once" from "sat near 12 000 for twenty minutes",
# and those are different findings.
SERIES_KEY_ELAPSED_S = "tS"
SERIES_KEY_LARGEST_FREE_8BIT = "largestFree8bitBlock"
SERIES_KEY_HEAP_FREE = "heapFree"
SERIES_KEY_HEAP_MIN = "heapMin"
SERIES_KEY_SSE_CLIENTS = "sseClientsConnected"


# ---------------------------------------------------------------------------
# SSE frame parsing -- the single implementation. An earlier revision of this
# harness kept two, one tested-but-unused and one live-but-untested, which is
# how a broken parser passed its own test suite.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class SseFrame:
    """One complete SSE event, as delimited by a blank line.

    bringup/p4_hosted_bench.cpp:813 calls `events.send(frame)` with only the
    payload -- id=0, event=nullptr, reconnect=0 are PsychicEventSource.h:86's
    defaults, never overridden by this bench. PsychicEventSource.cpp's
    _generateEventMessage_impl() (line 236, seeded at
    .pio/libdeps/firebeetle2_hosted_bench/PsychicHttp/src/PsychicEventSource.cpp)
    only emits the retry:/id:/event: lines when those arguments are truthy,
    so every real frame from this firmware is exactly
    `data: <n>\\r\\n\\r\\n` -- id and event are always None. The other fields
    are still parsed (not merely assumed absent) so this stays correct
    against the general SSE framing, and so a firmware change that started
    sending them would show up in the frame itself rather than being
    silently discarded.
    """

    id: Optional[int]
    event: Optional[str]
    retry: Optional[int]
    data: Optional[str]

    @property
    def counter(self) -> Optional[int]:
        """This bench's payload is always the ASCII decimal frame counter
        (p4_hosted_bench.cpp:812, `snprintf(frame, sizeof(frame), "%lu",
        frameCount)`). None if the payload actually received is not a bare
        non-negative integer, so a payload-shape change surfaces as a parse
        anomaly rather than a silently-wrong counter value."""
        if self.data is not None and self.data.isdigit():
            return int(self.data)
        return None


class SseFrameParser:
    """Incremental SSE frame parser. feed(chunk) returns any frames that
    chunk completed; finish() reports a truncated partial frame, if one is
    pending, when the connection ends. This is the ONLY frame-parsing
    implementation in this file -- both --self-test and the live drivers
    reach it exclusively through BenchClient.stream_sse()."""

    def __init__(self) -> None:
        self._buf = b""
        self._id: Optional[int] = None
        self._event: Optional[str] = None
        self._retry: Optional[int] = None
        self._data_lines: list[str] = []
        self._frame_started = False

    def feed(self, chunk: bytes) -> list[SseFrame]:
        self._buf += chunk
        frames: list[SseFrame] = []
        while b"\n" in self._buf:
            raw_line, self._buf = self._buf.split(b"\n", 1)
            if raw_line.endswith(b"\r"):
                raw_line = raw_line[:-1]
            line = raw_line.decode("utf-8", errors="replace")
            if line == "":
                if self._frame_started:
                    frames.append(self._flush())
                # A blank line with no preceding field is legal SSE (no-op
                # per the WHATWG spec), not truncation.
                continue
            self._frame_started = True
            if line.startswith(":"):
                continue  # SSE comment line.
            field, _, value = line.partition(":")
            if value.startswith(" "):
                value = value[1:]
            if field == "data":
                self._data_lines.append(value)
            elif field == "id":
                if value.isdigit() or (value.startswith("-") and value[1:].isdigit()):
                    self._id = int(value)
            elif field == "event":
                self._event = value
            elif field == "retry":
                if value.isdigit():
                    self._retry = int(value)
            # Unknown field names are ignored, per spec.
        return frames

    def _flush(self) -> SseFrame:
        frame = SseFrame(
            id=self._id,
            event=self._event,
            retry=self._retry,
            data="\n".join(self._data_lines) if self._data_lines else None,
        )
        self._id = None
        self._event = None
        self._retry = None
        self._data_lines = []
        self._frame_started = False
        return frame

    def has_pending_partial_frame(self) -> bool:
        return self._frame_started or bool(self._buf)

    def finish(self) -> Optional[str]:
        """Call once the connection has ended. Returns a description of a
        truncated in-progress frame if one is pending, else None. A
        truncated frame is reported, never silently dropped."""
        if not self.has_pending_partial_frame():
            return None
        pending_data = "\n".join(self._data_lines) if self._data_lines else None
        return (
            f"truncated mid-frame: id={self._id!r} event={self._event!r} "
            f"data={pending_data!r} trailing_bytes={self._buf!r}"
        )


def count_frame_gaps(counters: list[int]) -> int:
    """Count discontinuities in a sequence of frame counters. This bench's
    SSE payload IS the monotonic frame counter (see SseFrame.counter), so a
    gap is any place the sequence does not advance by exactly 1. Single
    implementation, used identically by --self-test's assertions and by
    run_sse_soak()'s total_frame_gaps FAIL condition."""
    gaps = 0
    for previous, current in zip(counters, counters[1:]):
        if current - previous != 1:
            gaps += 1
    return gaps


# ---------------------------------------------------------------------------
# BenchClient -- the one production entry point every driver and --self-test
# both call.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class SseStreamResult:
    frames: list[SseFrame]
    truncated: bool
    truncated_detail: Optional[str]
    error: Optional[str]
    connect_ok: bool
    status_line: Optional[str]
    elapsed_s: float
    # None when the handshake never produced a response at all. Kept apart
    # from status_line so callers can classify a refusal without parsing
    # prose: the shipping image answers 503 when PA_ADMISSION_MAX_SSE_CLIENTS
    # streams are already open (src/web/api_events.cpp), which under a
    # reconnect storm at the cap is admission working, not a transport fault.
    status_code: Optional[int] = None
    # True when the read window expired with no bytes on the wire. Reported as
    # the FACT it is, separately from `error`, because whether that fact is a
    # stall depends on how long the window was: a 250 ms window says nothing
    # about a 1 Hz stream, and a 5 s one says a great deal. The caller decides;
    # see run_reconnect_storm(), which was classifying every expiry of its own
    # poll interval as a stalled stream and failing healthy boards for it.
    read_timed_out: bool = False
    # Seconds between the last byte that arrived on this stream and the moment
    # it ended -- or, when nothing ever arrived, the whole life of the stream.
    # This is the same quantity the read window measures, so a caller can judge
    # silence against its own budget rather than against the poll granularity.
    silence_before_end_s: Optional[float] = None


class BenchClient:
    """The production entry point for every HTTP/SSE interaction with the
    bench firmware. --self-test drives this exact class against a local
    http.server fixture; the live drivers drive it against the device --
    there is no second, parallel implementation for either purpose."""

    def __init__(self, device: str, port: int = 80, connect_timeout_s: float = 10.0) -> None:
        self.device = device
        self.port = port
        self.connect_timeout_s = connect_timeout_s

    def get_json(self, path: str) -> tuple[int, dict]:
        conn = http.client.HTTPConnection(self.device, self.port, timeout=self.connect_timeout_s)
        try:
            conn.request("GET", path)
            resp = conn.getresponse()
            body = resp.read()
            return resp.status, (json.loads(body) if body else {})
        finally:
            conn.close()

    def post_json(self, path: str, payload: Optional[dict] = None) -> tuple[int, dict]:
        conn = http.client.HTTPConnection(self.device, self.port, timeout=self.connect_timeout_s)
        try:
            if payload is not None:
                body = json.dumps(payload).encode("utf-8")
                headers = {"Content-Type": "application/json"}
            else:
                body = b""
                headers = {}
            conn.request("POST", path, body=body, headers=headers)
            resp = conn.getresponse()
            resp_body = resp.read()
            return resp.status, (json.loads(resp_body) if resp_body else {})
        finally:
            conn.close()

    def stream_sse(
        self,
        path: str,
        on_frame: Callable[[SseFrame, float], None],
        stop: threading.Event,
        read_chunk_timeout_s: float = 5.0,
        abrupt_stop: bool = False,
    ) -> SseStreamResult:
        """Open one long-lived SSE connection and read it until `stop` is
        set, the peer closes it, or a transport fault occurs.

        Every transport-shaped exception (refused/reset connection, timeout,
        malformed handshake) is caught HERE and folded into the returned
        SseStreamResult.error -- never swallowed silently, and never left to
        surface as an unrelated crash in a soak thread. An earlier revision's
        handler here referenced a `metrics` name that was neither a parameter
        nor a module global, so a wedged link raised NameError instead of
        being recorded. There is no such free variable now: every field the
        caller needs comes back on SseStreamResult.

        Uses resp.fp.read1(), not resp.read(): verified empirically before
        writing this method that http.client's resp.read(amt), when
        Content-Length is absent (true for this streaming response -- see
        PsychicEventSourceResponse::send()), tries to fill the full `amt`
        before returning and discards already-buffered bytes when the
        socket timeout fires first. read1() returns as soon as one
        underlying socket read succeeds, which is what a 1-frame-per-second
        protocol needs. The same test showed CPython's SocketIO marks
        itself permanently unusable ("cannot read from timed out object")
        after any read timeout -- so a timeout below is treated as terminal
        for the connection and never retried in place.
        """
        started = time.monotonic()
        # Updated on every chunk that actually arrives, so the silence this
        # method reports is measured against the same thing the socket's read
        # window measures: bytes on the wire.
        last_data_at = started
        read_timed_out = False
        frames: list[SseFrame] = []
        parser = SseFrameParser()
        conn = http.client.HTTPConnection(self.device, self.port, timeout=self.connect_timeout_s)
        status_line: Optional[str] = None
        status_code: Optional[int] = None
        error: Optional[str] = None
        connect_ok = False
        resp: Optional[http.client.HTTPResponse] = None
        raw_sock: Optional[socket.socket] = None
        try:
            conn.connect()
            connect_ok = True
            # Captured once, right after connect(), and used from here on
            # instead of conn.sock: verified empirically (AttributeError on
            # conn.sock.setsockopt after getresponse()) that
            # HTTPConnection.getresponse() unconditionally nulls conn.sock
            # for any response with neither Content-Length nor chunked
            # Transfer-Encoding -- see HTTPResponse.begin()'s "if the
            # connection remains open, and we aren't using chunked, and a
            # content-length was not provided, then assume that the
            # connection WILL close". That is exactly this SSE response
            # (PsychicEventSourceResponse::send() sets neither), so
            # conn.sock cannot be relied on past getresponse() -- the
            # underlying socket object itself is still live via resp.fp
            # (sock.makefile()'s refcounting keeps the fd open), which is
            # why the read loop below works at all; raw_sock is what to use
            # for socket options or an explicit close.
            raw_sock = conn.sock
            raw_sock.settimeout(read_chunk_timeout_s)
            conn.request(
                "GET", path,
                headers={
                    "Accept": "text/event-stream",
                    "Cache-Control": "no-cache",
                    "Connection": "keep-alive",
                },
            )
            resp = conn.getresponse()
            status_line = f"{resp.status} {resp.reason}"
            status_code = resp.status
            if resp.status != 200:
                error = f"unexpected status: {status_line}"
            else:
                content_type = resp.getheader("Content-Type", "") or ""
                if "text/event-stream" not in content_type:
                    error = f"unexpected Content-Type: {content_type!r}"
                else:
                    while not stop.is_set():
                        try:
                            chunk = resp.fp.read1(4096)
                        except (socket.timeout, TimeoutError) as timeout_error:
                            # Recorded on the result whether or not it is
                            # treated as an error, so a caller whose read
                            # window is a poll interval rather than a liveness
                            # budget can tell the two apart. The connection is
                            # over either way: socket.SocketIO sets
                            # _timeout_occurred permanently on a timeout and
                            # raises "cannot read from timed out object" on
                            # every later read (CPython Lib/socket.py,
                            # SocketIO.readinto), so there is no continuing in
                            # place after one.
                            read_timed_out = True
                            if not stop.is_set():
                                error = (
                                    f"read timed out after {read_chunk_timeout_s}s with "
                                    f"no data (stream stalled): {timeout_error}"
                                )
                            break
                        except OSError as read_error:
                            error = f"read failed: {read_error}"
                            break
                        if not chunk:
                            break  # peer closed cleanly (EOF)
                        last_data_at = time.monotonic()
                        for frame in parser.feed(chunk):
                            frames.append(frame)
                            on_frame(frame, time.monotonic())
                    if abrupt_stop and stop.is_set() and error is None:
                        # SO_LINGER(on=1, linger=0): the close below sends
                        # RST instead of FIN + orderly shutdown -- standard
                        # POSIX socket semantics. It is what makes the
                        # reconnect storm's abort-mid-stream an actual abrupt
                        # disconnect rather than a clean close, which is a
                        # different thing for the server to survive.
                        raw_sock.setsockopt(
                            socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0),
                        )
        except (OSError, ConnectionError, TimeoutError, socket.timeout, http.client.HTTPException) as transport_error:
            error = f"transport fault: {transport_error}"
        finally:
            # conn.close() is a no-op once getresponse() has already closed
            # `conn` itself (see above) -- resp.close() is what actually
            # releases the underlying socket (and honors any SO_LINGER just
            # set on it) in that case. Fall back to conn.close() only when
            # the handshake never got as far as a response (e.g. connect()
            # or request() itself failed).
            try:
                if resp is not None:
                    resp.close()
                else:
                    conn.close()
            except OSError:
                pass
        truncated_detail = parser.finish()
        ended = time.monotonic()
        return SseStreamResult(
            frames=frames,
            truncated=truncated_detail is not None,
            truncated_detail=truncated_detail,
            error=error,
            connect_ok=connect_ok,
            status_line=status_line,
            elapsed_s=ended - started,
            status_code=status_code,
            read_timed_out=read_timed_out,
            silence_before_end_s=ended - last_data_at,
        )


# ---------------------------------------------------------------------------
# Trust-boundary helpers. A missing key means unknown, not False -- an
# absent boolean must never read as a confident negative (or positive).
# ---------------------------------------------------------------------------


def _type_mismatch(value: Any, expected_type: type) -> bool:
    """True when `value` may not be read as `expected_type`.

    bool is a subclass of int in Python, so a payload that sent
    `"bootCount": true` would pass a bare isinstance(value, int) check and be
    read as the integer 1 -- a JSON type error turned into a plausible
    reading, at exactly the trust boundary this harness exists to police."""
    if not isinstance(value, expected_type):
        return True
    return expected_type is int and isinstance(value, bool)


def _require_field(body: dict, field: str, expected_type: type, context: str,
                   container: Optional[str] = None) -> Any:
    """One-shot, hard-raising validation for baseline/response checks that
    happen once per driver, before any evidence has been gathered -- a
    contract violation here is cheap to fail loudly on. Callers that need
    to survive an occasional malformed sample inside a long polling loop
    use _collect_field / _safe_field below instead.

    `container` names a nested object to read the field out of: the shipping
    image publishes the recovery-ladder counters under "hostedLink"
    (src/web/web_server.cpp:724-742) where the bench publishes them flat at
    the top level. A missing container is reported as a missing container
    rather than as a missing field, so "this image publishes no such block at
    all" can never be read as "this one field happens to be absent"."""
    scope = body
    if container is not None:
        if container not in body:
            raise KeyError(
                f"{context}: response is missing required object {container!r}: {body!r}"
            )
        scope = body[container]
        if not isinstance(scope, dict):
            raise TypeError(
                f"{context}: {container!r} has type {type(scope).__name__}, expected a JSON object"
            )
        context = f"{context} {container!r}"
    if field not in scope:
        raise KeyError(f"{context}: response is missing required field {field!r}: {scope!r}")
    value = scope[field]
    if _type_mismatch(value, expected_type):
        raise TypeError(
            f"{context}: field {field!r} has type {type(value).__name__}, "
            f"expected {expected_type}: {value!r}"
        )
    return value


def _collect_field(samples: list[dict], field: str, expected_type: type, anomalies: list[str],
                   container: Optional[str] = None) -> list:
    """Soft validation for repeated polling samples collected over a long
    (potentially multi-hour) run: one malformed sample is recorded as an
    anomaly and skipped, rather than discarding the whole run's evidence.
    `container` is the nested-object path described on _require_field."""
    values = []
    path = field if container is None else f"{container}.{field}"
    for index, sample in enumerate(samples):
        scope = sample
        if container is not None:
            scope = sample.get(container)
            if not isinstance(scope, dict):
                anomalies.append(
                    f"sample[{index}] missing/invalid object {container!r} (needed for {path!r})"
                )
                continue
        if field not in scope:
            anomalies.append(f"sample[{index}] missing field {path!r}")
            continue
        value = scope[field]
        if _type_mismatch(value, expected_type):
            anomalies.append(
                f"sample[{index}] field {path!r} has type {type(value).__name__}, expected {expected_type}"
            )
            continue
        values.append(value)
    return values


def capture_status(client: BenchClient) -> dict:
    """One /api/status snapshot. Raises whatever get_json() raises
    (connection errors) -- callers decide whether "device unreachable" is
    fatal at that point in the run."""
    _, body = client.get_json(DEFAULT_STATUS_PATH)
    return body


TRANSPORT_EXCEPTIONS = (OSError, ConnectionError, TimeoutError, socket.timeout, http.client.HTTPException)


# ---------------------------------------------------------------------------
# SSE continuity models -- what "the stream stayed continuous" means on each
# image. The framing is the same on both (SseFrameParser reads it); what is
# carried inside it is not:
#
#   bench     bringup/p4_hosted_bench.cpp:812 sends the monotonic frame
#             counter as the whole payload, one frame per second, with no
#             id: and no event: line. Continuity is arithmetic.
#   shipping  eventStreamTask() (src/web/web_server.cpp:793-900) ticks once a
#             second while any client is connected and broadcasts "rc" every
#             tick, "status" on demand and "log" every other tick when there
#             are new lines. webEventStreamFormatPrefix()
#             (src/web/web_event_stream.cpp:106) puts millis() in `id:` and
#             the event name in `event:`; the payload is JSON. There is no
#             counter anywhere, so continuity is arrival timing plus framing
#             shape.
#
# Running the counter model against the shipping stream is not a smaller
# measurement, it is a vacuous one: no shipping payload parses as a bare
# integer, the counter list stays empty, and count_frame_gaps([]) == 0
# reports perfect continuity for a stream that delivered nothing at all.
# ---------------------------------------------------------------------------

# src/web/web_server.cpp:845/869/895 -- the only three event names the
# shipping firmware broadcasts. "rc" is the heartbeat: it is the one the tick
# emits unconditionally, where "status" is on demand and "log" only fires
# when there are new lines.
SHIPPING_SSE_EVENT_NAMES = ("status", "rc", "log")
SHIPPING_SSE_HEARTBEAT_EVENT = "rc"


class SseContinuityTracker:
    """One client's stream, judged by one image's continuity model. Fed
    exclusively through stream_sse_with_continuity() so a live soak and
    --self-test drive identical wiring -- a tracker the self-test feeds by
    hand would prove nothing about the path the device run takes."""

    model = ""

    def stream_started(self, ts: float) -> None:
        """Called once, immediately before the connection is opened. A model
        whose verdict does not depend on timing does not need it."""

    def observe(self, frame: SseFrame, ts: float) -> None:
        raise NotImplementedError

    def stream_ended(self, ts: float, stopped_by_harness: bool) -> None:
        """Called once the stream has ended. `stopped_by_harness` is False
        when the peer (or the transport) ended it before the run asked it
        to."""

    @property
    def frame_count(self) -> int:
        raise NotImplementedError

    def per_client_fields(self) -> dict:
        raise NotImplementedError

    @staticmethod
    def summarize(trackers: list[SseContinuityTracker], max_silence_s: float) -> tuple[dict, list[str]]:
        """Run-level fields and FAIL reasons across every client's tracker,
        in client-index order."""
        raise NotImplementedError


class CounterFrameTracker(SseContinuityTracker):
    """Bench continuity: the payload IS the counter, so a gap is any place
    the sequence does not advance by exactly 1.

    frame_count counts frames that carried a usable counter rather than
    frames received -- a payload that stops being a bare integer is a parse
    anomaly, and counting it as a delivered frame would hide it. This is the
    pre-schema harness's own behaviour (its per-client frameCount was
    len(counters)), kept identical so bench verdicts do not move."""

    model = "counter"

    def __init__(self) -> None:
        self.counters: list[int] = []

    def observe(self, frame: SseFrame, ts: float) -> None:
        if frame.counter is not None:
            self.counters.append(frame.counter)

    @property
    def frame_count(self) -> int:
        return len(self.counters)

    @property
    def gaps(self) -> int:
        return count_frame_gaps(self.counters)

    def per_client_fields(self) -> dict:
        return {
            "gaps": self.gaps,
            "firstCounter": self.counters[0] if self.counters else None,
            "lastCounter": self.counters[-1] if self.counters else None,
        }

    @staticmethod
    def summarize(trackers: list[SseContinuityTracker], max_silence_s: float) -> tuple[dict, list[str]]:
        # max_silence_s carries no verdict for this model: the bench stream's
        # continuity is arithmetic, and a stalled bench stream shows up as a
        # counter gap the moment it resumes (or as a read timeout if it does
        # not). Accepted for one call signature across both models.
        total_gaps = sum(t.gaps for t in trackers)
        fields = {"sseContinuityModel": "counter", "totalFrameGaps": total_gaps}
        reasons: list[str] = []
        if total_gaps > 0:
            reasons.append(f"total_frame_gaps == {total_gaps} (frame continuity broken)")
        return fields, reasons


class HeartbeatFrameTracker(SseContinuityTracker):
    """Shipping continuity: arrival timing plus framing shape.

    Three independent things are watched, because no one of them alone can
    tell a live stream from a dead one:

      silence   the longest interval with no frame at all, measured from the
                moment the connection opened (so "never delivered a first
                frame" is a silence, not an empty list nobody looks at) and,
                when the peer ends the stream early, through to that end.
      shape     every shipping broadcast passes an event name, so a frame
                without one means this is not the stream this model was
                written against -- recorded rather than absorbed.
      id        webEventStreamFormatPrefix() puts millis() in `id:`, which
                only ever moves forward on a running device.
    """

    model = "heartbeat"

    def __init__(self) -> None:
        self.frames = 0
        self.event_counts: dict[str, int] = {}
        self.frames_without_event = 0
        self.id_regressions = 0
        self.last_id: Optional[int] = None
        self.max_silence_s = 0.0
        self.max_heartbeat_silence_s = 0.0
        self.first_frame_latency_s: Optional[float] = None
        self.ended_early = False
        self._stream_started_at: Optional[float] = None
        self._last_frame_at: Optional[float] = None
        self._last_heartbeat_at: Optional[float] = None

    def stream_started(self, ts: float) -> None:
        # Both "last seen" clocks start at the connection, not at the first
        # frame: a stream that opens and then says nothing has to read as a
        # silence rather than as a client with no data to judge.
        self._stream_started_at = ts
        self._last_frame_at = ts
        self._last_heartbeat_at = ts

    def observe(self, frame: SseFrame, ts: float) -> None:
        self.frames += 1
        if self._last_frame_at is not None:
            self.max_silence_s = max(self.max_silence_s, ts - self._last_frame_at)
        self._last_frame_at = ts
        if self.first_frame_latency_s is None and self._stream_started_at is not None:
            self.first_frame_latency_s = ts - self._stream_started_at

        if frame.event:
            self.event_counts[frame.event] = self.event_counts.get(frame.event, 0) + 1
            if frame.event == SHIPPING_SSE_HEARTBEAT_EVENT:
                if self._last_heartbeat_at is not None:
                    self.max_heartbeat_silence_s = max(
                        self.max_heartbeat_silence_s, ts - self._last_heartbeat_at
                    )
                self._last_heartbeat_at = ts
        else:
            self.frames_without_event += 1

        if frame.id is not None:
            if self.last_id is not None and frame.id < self.last_id:
                self.id_regressions += 1
            self.last_id = frame.id

    def stream_ended(self, ts: float, stopped_by_harness: bool) -> None:
        self.ended_early = not stopped_by_harness
        if self.ended_early and self._last_frame_at is not None:
            # A stream the peer closed on its own leaves a silence the run
            # never asked for. Counting it is what makes a mid-run clean EOF
            # visible: without it, a stream that dies quietly halfway through
            # merely produces a smaller frame count, which no threshold here
            # would ever object to.
            self.max_silence_s = max(self.max_silence_s, ts - self._last_frame_at)

    @property
    def frame_count(self) -> int:
        return self.frames

    @property
    def heartbeat_frame_count(self) -> int:
        return self.event_counts.get(SHIPPING_SSE_HEARTBEAT_EVENT, 0)

    def per_client_fields(self) -> dict:
        return {
            "eventCounts": dict(sorted(self.event_counts.items())),
            "heartbeatEvent": SHIPPING_SSE_HEARTBEAT_EVENT,
            "heartbeatFrameCount": self.heartbeat_frame_count,
            "maxSilenceS": round(self.max_silence_s, 3),
            "maxHeartbeatSilenceS": round(self.max_heartbeat_silence_s, 3),
            "firstFrameLatencyS": (
                None if self.first_frame_latency_s is None else round(self.first_frame_latency_s, 3)
            ),
            "idRegressions": self.id_regressions,
            "framesWithoutEventName": self.frames_without_event,
            "endedBeforeHarnessStoppedIt": self.ended_early,
        }

    @staticmethod
    def summarize(trackers: list[SseContinuityTracker], max_silence_s: float) -> tuple[dict, list[str]]:
        reasons: list[str] = []
        observed_names: set[str] = set()
        for index, tracker in enumerate(trackers):
            observed_names.update(tracker.event_counts)
            if tracker.frames == 0:
                # A client that received nothing at all is already reported
                # by run_sse_soak()'s zero-frame condition; repeating it here
                # as a silence would say the same thing twice.
                continue
            if tracker.max_silence_s > max_silence_s:
                reasons.append(
                    f"client {index} saw {tracker.max_silence_s:.1f}s with no SSE frame "
                    f"(limit {max_silence_s}s) -- eventStreamTask() broadcasts once a "
                    "second while a client is connected"
                )
            if tracker.ended_early:
                reasons.append(
                    f"client {index}'s stream ended before the harness stopped it "
                    f"(after {tracker.frames} frame(s))"
                )
            if tracker.heartbeat_frame_count == 0:
                reasons.append(
                    f"client {index} received {tracker.frames} frame(s) but never a "
                    f"{SHIPPING_SSE_HEARTBEAT_EVENT!r} event -- that is the one the 1 Hz "
                    "tick emits unconditionally"
                )
            if tracker.frames_without_event > 0:
                reasons.append(
                    f"client {index} received {tracker.frames_without_event} frame(s) with no "
                    "event: name -- every shipping broadcast passes one, so this stream is "
                    "not the one this schema reads"
                )
            if tracker.id_regressions > 0:
                reasons.append(
                    f"client {index} saw {tracker.id_regressions} frame id(s) go backwards -- "
                    "id is millis() at broadcast time, so it only moves backwards across a "
                    "restart or a 49.7-day millis() wrap"
                )
        fields = {
            "sseContinuityModel": "heartbeat",
            "sseMaxSilenceLimitS": max_silence_s,
            "maxSilenceSObserved": round(max((t.max_silence_s for t in trackers), default=0.0), 3),
            "maxHeartbeatSilenceSObserved": round(
                max((t.max_heartbeat_silence_s for t in trackers), default=0.0), 3
            ),
            "totalHeartbeatFrames": sum(t.heartbeat_frame_count for t in trackers),
            "eventNamesObserved": sorted(observed_names),
            "unexpectedEventNames": sorted(observed_names.difference(SHIPPING_SSE_EVENT_NAMES)),
            "totalFramesWithoutEventName": sum(t.frames_without_event for t in trackers),
            "totalIdRegressions": sum(t.id_regressions for t in trackers),
        }
        return fields, reasons


def stream_sse_with_continuity(
    client: BenchClient, schema: StatusSchema, stop: threading.Event,
    read_chunk_timeout_s: float, path: str = DEFAULT_SSE_PATH,
    on_frame: Optional[Callable[[SseFrame, float], None]] = None,
    tracker: Optional[SseContinuityTracker] = None,
) -> tuple[SseStreamResult, SseContinuityTracker]:
    """Open one SSE stream and feed every frame to this image's continuity
    tracker. The single wiring point between BenchClient.stream_sse() and the
    continuity model: the soak worker and --self-test both go through here,
    so a self-test that passes cannot be exercising wiring the device run
    does not use.

    `tracker` lets the caller supply the tracker instead of receiving it at
    the end, which is what run_sse_soak() needs in order to read a live frame
    count out of a worker thread for its progress line. It is the same object
    this function would have made (schema.new_continuity_tracker()), so the
    numbers a progress line shows are the numbers the report will carry --
    not a parallel count that could disagree with it."""
    if tracker is None:
        tracker = schema.new_continuity_tracker()
    tracker.stream_started(time.monotonic())

    def observe(frame: SseFrame, ts: float) -> None:
        tracker.observe(frame, ts)
        if on_frame is not None:
            on_frame(frame, ts)

    result = client.stream_sse(path, observe, stop, read_chunk_timeout_s=read_chunk_timeout_s)
    tracker.stream_ended(time.monotonic(), stopped_by_harness=stop.is_set())
    return result, tracker


# ---------------------------------------------------------------------------
# Status schema -- the two /api/status payloads this harness can drive.
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class ResetReasonAssessment:
    """What the image said the last reset was, and whether that is
    crash-shaped. `crash_shaped is None` means this image cannot tell -- it
    is never False, because "the mapping collapses this case" and "the device
    started cleanly" are different claims."""

    display: Any
    crash_shaped: Optional[bool]
    caveat: Optional[str] = None


@dataclasses.dataclass(frozen=True)
class LadderReading:
    """One sample of the bounded transport-failure recovery ladder. The two
    images that have a ladder publish the same five quantities under different
    names and, on shipping, inside a nested object. The artoo image has no
    ladder to sample at all -- see StatusSchema.publishes_recovery_ladder."""

    state: str
    transport_failure_count: int
    transport_up_event_count: int
    attempt_count: int
    recovered_count: int


@dataclasses.dataclass
class LadderSamples:
    states: list[str]
    transport_failure_counts: list[int]
    transport_up_event_counts: list[int]
    attempt_counts: list[int]
    recovered_counts: list[int]


@dataclasses.dataclass(frozen=True)
class AdmissionReading:
    """One sample of the firmware's own record of what it turned away.

    The two refusal counters are cumulative since boot and only ever increase,
    so a rise across a run is requests this controller refused DURING the run --
    which is the failure the heap rule exists to catch, and the one a percentage
    of a baseline sample would never have noticed.

    accept_min_largest_block_seen is the Connection Admission guard's own
    running minimum, and is None when the guard has never sampled
    (ACCEPT_MIN_LARGEST_BLOCK_NEVER_SAMPLED). None is not zero and not a
    breach: it is the absence of a reading.
    """

    refused_heap_floor: int
    refused_heap_floor_diag: int
    accept_min_largest_block_seen: Optional[int]


@dataclasses.dataclass
class AdmissionSamples:
    refused_heap_floor: list[int]
    refused_heap_floor_diag: list[int]
    accept_min_largest_block_seen: list[Optional[int]]


@dataclasses.dataclass(frozen=True)
class StatusPollSample:
    """One /api/status poll and when it was attempted.

    The elapsed time is stamped BEFORE the request goes out, so a slow or
    unreachable poll dates from when the harness asked rather than from when it
    gave up -- a poll that times out after 10s would otherwise appear in the
    series 10s later than the moment it describes.

    `body` carries the "_pollError" sentinel key instead of the payload when the
    device could not be reached or did not answer with JSON.
    """

    elapsed_s: float
    body: dict


def series_values(rows: list[dict], key: str) -> list[int]:
    """Every row that carried `key`, in order.

    A row missing it was already recorded as an anomaly when the series was
    built (StatusSchema.collect_heap_series), so it is dropped here rather than
    defaulted -- a zero substituted for a missing reading would drag a minimum
    straight through any floor.
    """
    return [row[key] for row in rows if key in row]


class StatusSchema:
    """How to read one firmware image's GET /api/status.

    The three images this harness can drive publish different payloads for
    the same measurements, and the differences are structural rather than
    cosmetic:

      bench     bringup/p4_hosted_bench.cpp handleStatus() -- built to be
                measured: bootCount (RTC_DATA_ATTR, survives a CPU reset and
                not a power cycle), resetReason as the raw esp_reset_reason_t
                int, the ladder counters flat at the top level, and
                POST /api/c6/reset.
      shipping  src/web/web_server.cpp buildStatusJson():393 -- built for the
                dashboard: no bootCount at all, resetReason as
                resetReasonName()'s string (:401), heapLargest8bit and
                sseClients rather than largestFree8bitBlock and
                sseClientsConnected, the ladder nested under "hostedLink"
                (:724-743, behind the PA_CAP_HOSTED_WIFI board capability
                gate), and no reset route anywhere in src/ (that is #243).
      artoo     the same buildStatusJson(), built for the same dashboard on a
                board where PA_CAP_HOSTED_WIFI is 0 -- so the "hostedLink"
                block is not compiled in and there is no recovery ladder to
                read anywhere in the payload. Everything else in the fixed
                system-health block is emitted by one unconditional snprintf
                (:391-393) and is therefore identical.

    Which schema is in use is an operator declaration (--image), never
    sniffed from the payload: sniffing turns a truncated or half-built
    response into a confident claim about which firmware is on the board, and
    every field would then be silently re-labelled. The declaration is
    checked against the payload instead (structural_mismatches()), so a wrong
    --image fails loudly at preflight.

    A field one image does not publish is a property of that image
    (publishes_boot_count, publishes_recovery_ladder), not a .get() that
    quietly returns None: absent must read as absent, never as zero."""

    name = ""
    # The PlatformIO environment that builds this image, and therefore the one
    # whose resolved build flags declare the admission floor a run is judged
    # against. The default for --build-env, which an operator overrides when the
    # board is carrying a variant env (artoo_esp32_recovery_bench and
    # artoo_esp32 publish byte-identical payloads and have different floors, so
    # nothing in the payload can tell them apart -- see --build-env's help).
    build_env = ""
    reset_path: Optional[str] = None
    # The diagnostic run_c6_reset_recovery() refuses with when reset_path is
    # None. Owned by the schema because the two images that have no reset
    # route have it for different reasons, and a driver that names only one of
    # them tells the operator something false about the other board.
    reset_unavailable_reason = ""
    publishes_boot_count = False
    enforces_sse_client_cap = False
    reset_reason_kind = ""
    heap_field = ""
    heap_free_field = ""
    # None on an image that publishes no free-heap low-water mark. Absent, not
    # zero: "this image never reports how low free heap went" and "free heap
    # never went low" are different claims.
    heap_min_field: Optional[str] = None
    sse_clients_field = ""
    restart_field = ""
    restart_verb = ""
    # True when this image compiles the request-admission guard
    # (src/web/web_admission_psychic.cpp) and publishes its refusal counters, so
    # there is a floor it actually refuses at and a count of what it refused.
    # False is a structural property of the image, not a missing measurement --
    # see BenchStatusSchema.admission_absence_note.
    enforces_admission_floor = False
    # What a report says in place of the floor verdict when there is no floor.
    # Only read when enforces_admission_floor is False.
    admission_absence_note = ""
    refused_heap_floor_field = ""
    refused_heap_floor_diag_field = ""
    accept_min_largest_block_field = ""
    # Progress-line only, and deliberately NOT in fields_read(): these are read
    # to make a run legible while it is still running, never to decide
    # anything, so they must not appear in the report's statusFieldsRead map --
    # that map states which payload paths the VERDICT was taken from, and
    # padding it with fields no verdict reads would make it say less, not more.
    # None on an image that publishes no such counter, in which case the
    # progress line omits the key entirely rather than printing a zero.
    sse_refused_cap_field: Optional[str] = None
    sse_evicted_field: Optional[str] = None
    sse_clients_peak_field: Optional[str] = None
    ladder_container: Optional[str] = None
    ladder_fields: dict[str, str] = {}
    # False on an image built for a board with no ESP-Hosted link supervisor:
    # the ladder object is absent from the payload entirely, which is a
    # different claim from "its counters read zero" and is reported as such.
    publishes_recovery_ladder = True
    # What a report says in place of the ladder numbers when there are none.
    # Only read when publishes_recovery_ladder is False.
    ladder_absence_note = ""
    continuity_tracker_class: type = SseContinuityTracker

    # -- continuity ------------------------------------------------------

    def new_continuity_tracker(self) -> SseContinuityTracker:
        return self.continuity_tracker_class()

    def summarize_continuity(
        self, trackers: list[SseContinuityTracker], max_silence_s: float,
    ) -> tuple[dict, list[str]]:
        return self.continuity_tracker_class.summarize(trackers, max_silence_s)

    # -- field map -------------------------------------------------------

    def fields_read(self) -> dict:
        """The exact payload paths this schema reads, published in every
        report so the field map is auditable from the evidence itself rather
        than only from this source file."""
        prefix = f"{self.ladder_container}." if self.ladder_container else ""
        return {
            "image": self.name,
            "heapLargestFreeBlock": self.heap_field,
            "heapFree": self.heap_free_field,
            # Words, not a null: an absent low-water mark is a property of the
            # image, and a consumer must not read the omission as a zero.
            "heapMinFreeEver": self.heap_min_field or "<not published on this image>",
            "sseClients": self.sse_clients_field,
            "restartEvidence": self.restart_field,
            "resetReason": f"resetReason ({self.reset_reason_kind})",
            "bootCount": "bootCount" if self.publishes_boot_count else "<not published>",
            # Same treatment as the recovery ladder below: the absence gets
            # words rather than an empty map, which would read as "this image
            # publishes an empty set of refusal counters".
            "admissionRefusals": (
                {
                    "refusedHeapFloor": self.refused_heap_floor_field,
                    "refusedHeapFloorDiag": self.refused_heap_floor_diag_field,
                    "acceptMinLargestBlockSeen": self.accept_min_largest_block_field,
                }
                if self.enforces_admission_floor
                else self.admission_absence_note
            ),
            # An empty field map would read as "this image publishes an empty
            # ladder". The absence gets words, the same way bootCount and the
            # reset route do.
            "recoveryLadder": (
                {key: prefix + name for key, name in self.ladder_fields.items()}
                if self.publishes_recovery_ladder
                else self.ladder_absence_note
            ),
            "resetRoute": self.reset_path or "<no reset route on this image>",
        }

    # -- generic readers -------------------------------------------------

    def heap_largest_free(self, body: dict, context: str) -> int:
        return _require_field(body, self.heap_field, int, context)

    def sse_clients(self, body: dict, context: str) -> int:
        return _require_field(body, self.sse_clients_field, int, context)

    def collect_heap_series(self, polls: list[StatusPollSample],
                            anomalies: list[str]) -> list[dict]:
        """One row per reachable poll: the heap readings this image publishes,
        stamped with the elapsed seconds they were taken at.

        This is the single per-poll read path for the heap and SSE-client
        numbers -- run_sse_soak() derives its aggregates from these rows with
        series_values() rather than collecting the same fields a second time, so
        one malformed sample produces one anomaly and one hole rather than two
        of each in two differently-shaped lists.

        A field this image does not publish (heap_min_field is None on the
        bench) is absent from the row and is NOT an anomaly: fields_read()
        already states that absence in words. A field this image does publish
        but that this sample got wrong is an anomaly, and is left out of the
        row -- an absent key is recoverable, an invented zero is not.
        """
        rows: list[dict] = []
        for index, poll in enumerate(polls):
            row: dict = {SERIES_KEY_ELAPSED_S: round(poll.elapsed_s, 3)}
            for key, field in (
                (SERIES_KEY_LARGEST_FREE_8BIT, self.heap_field),
                (SERIES_KEY_HEAP_FREE, self.heap_free_field),
                (SERIES_KEY_HEAP_MIN, self.heap_min_field),
                (SERIES_KEY_SSE_CLIENTS, self.sse_clients_field),
            ):
                if not field:
                    continue
                if field not in poll.body:
                    anomalies.append(f"poll[{index}] missing field {field!r}")
                    continue
                value = poll.body[field]
                if _type_mismatch(value, int):
                    anomalies.append(
                        f"poll[{index}] field {field!r} has type {type(value).__name__}, "
                        "expected int"
                    )
                    continue
                row[key] = value
            rows.append(row)
        return rows

    def admission(self, body: dict, context: str) -> Optional[AdmissionReading]:
        """One admission-counter sample, or None on an image that compiles no
        admission guard. None is not "nothing was refused" and not "the sample
        was malformed": there is no gate on that image to refuse anything and no
        counter to read, which no reading can stand in for."""
        if not self.enforces_admission_floor:
            return None
        seen = _require_field(body, self.accept_min_largest_block_field, int, context)
        return AdmissionReading(
            refused_heap_floor=_require_field(
                body, self.refused_heap_floor_field, int, context),
            refused_heap_floor_diag=_require_field(
                body, self.refused_heap_floor_diag_field, int, context),
            accept_min_largest_block_seen=(
                None if seen == ACCEPT_MIN_LARGEST_BLOCK_NEVER_SAMPLED else seen
            ),
        )

    def collect_admission(self, samples: list[dict],
                          anomalies: list[str]) -> Optional[AdmissionSamples]:
        """Poll-loop counterpart to admission(); None for the same reason, and
        never an empty AdmissionSamples, which a caller would read as "sampled
        the counters and saw nothing"."""
        if not self.enforces_admission_floor:
            return None
        seen = _collect_field(samples, self.accept_min_largest_block_field, int, anomalies)
        return AdmissionSamples(
            refused_heap_floor=_collect_field(
                samples, self.refused_heap_floor_field, int, anomalies),
            refused_heap_floor_diag=_collect_field(
                samples, self.refused_heap_floor_diag_field, int, anomalies),
            accept_min_largest_block_seen=[
                None if value == ACCEPT_MIN_LARGEST_BLOCK_NEVER_SAMPLED else value
                for value in seen
            ],
        )

    def restart_marker(self, body: dict, context: str) -> int:
        return _require_field(body, self.restart_field, int, context)

    def collect_restart_markers(self, samples: list[dict], anomalies: list[str]) -> list[int]:
        return _collect_field(samples, self.restart_field, int, anomalies)

    def ladder(self, body: dict, context: str) -> Optional[LadderReading]:
        """One ladder sample, or None when this image publishes no ladder at
        all. None is not "the counters read zero" and not "this sample was
        malformed": it is the whole block being absent by construction, which
        no reading can stand in for."""
        if not self.publishes_recovery_ladder:
            return None
        names = self.ladder_fields
        return LadderReading(
            state=_require_field(body, names["state"], str, context, container=self.ladder_container),
            transport_failure_count=_require_field(
                body, names["transportFailureCount"], int, context, container=self.ladder_container),
            transport_up_event_count=_require_field(
                body, names["transportUpEventCount"], int, context, container=self.ladder_container),
            attempt_count=_require_field(
                body, names["attemptCount"], int, context, container=self.ladder_container),
            recovered_count=_require_field(
                body, names["recoveredCount"], int, context, container=self.ladder_container),
        )

    def collect_ladder(self, samples: list[dict], anomalies: list[str]) -> Optional[LadderSamples]:
        """Poll-loop counterpart to ladder(); None for the same reason, and
        never an empty LadderSamples, which a caller would read as "sampled
        and saw nothing"."""
        if not self.publishes_recovery_ladder:
            return None
        names = self.ladder_fields
        return LadderSamples(
            states=_collect_field(
                samples, names["state"], str, anomalies, container=self.ladder_container),
            transport_failure_counts=_collect_field(
                samples, names["transportFailureCount"], int, anomalies, container=self.ladder_container),
            transport_up_event_counts=_collect_field(
                samples, names["transportUpEventCount"], int, anomalies, container=self.ladder_container),
            attempt_counts=_collect_field(
                samples, names["attemptCount"], int, anomalies, container=self.ladder_container),
            recovered_counts=_collect_field(
                samples, names["recoveredCount"], int, anomalies, container=self.ladder_container),
        )

    def reset_reason_soft(self, sample: dict, anomalies: list[str]) -> Optional[ResetReasonAssessment]:
        """Per-poll reset-reason read for a long loop: one malformed sample
        is an anomaly, not the end of the run. Routed through the same
        reset_reason() the baseline uses -- there is one classification, not
        a soft copy of it that could drift from the strict one."""
        try:
            return self.reset_reason(sample, "poll /api/status")
        except (KeyError, TypeError) as contract_error:
            anomalies.append(str(contract_error))
            return None

    # -- per-image ------------------------------------------------------

    def reset_reason(self, body: dict, context: str) -> ResetReasonAssessment:
        raise NotImplementedError

    def restart_detected(self, baseline: int, markers: list[int]) -> bool:
        raise NotImplementedError

    def restart_report(self, baseline: int, final: int, detected: bool) -> dict:
        raise NotImplementedError

    def link_readiness(self, body: dict) -> tuple[bool, str]:
        """(ready, why-not). `ready` is True only on affirmative evidence of
        an established Hosted link; a missing field means the evidence is
        absent, which is not the same as the link having failed, and the
        message says so."""
        raise NotImplementedError

    def structural_mismatches(self, body: dict) -> list[str]:
        """Ways this payload contradicts the declared image."""
        raise NotImplementedError


def _marker_mismatch(body: dict, field: str, expected_type: type) -> Optional[str]:
    """One structural marker check, phrased as the mismatch it found.
    Reuses _require_field so the presence/type rules a marker is judged by
    are the same ones the drivers read the field with."""
    try:
        _require_field(body, field, expected_type, "declared image")
    except (KeyError, TypeError) as mismatch:
        return str(mismatch)
    return None


class BenchStatusSchema(StatusSchema):
    name = "bench"
    build_env = "firebeetle2_hosted_bench"
    reset_path = DEFAULT_RESET_PATH
    publishes_boot_count = True
    # The bench streams through PsychicEventSource, the vendor class production
    # deliberately replaced (ADR 0030). It has no client cap of its own, so a
    # refused stream is never expected here.
    enforces_sse_client_cap = False
    # [env:firebeetle2_hosted_bench] does resolve the admission floor flags --
    # it extends [env:firebeetle2] and inherits its build_flags -- but its
    # `build_src_filter = -<*> +<../bringup/p4_hosted_bench.cpp>`
    # (platformio.ini:624-627) means NONE of src/ is compiled, so
    # src/web/web_admission_psychic.cpp, the only code that reads those flags,
    # is not in the image. Reporting a floor of 9000 for this board would be a
    # claim about a gate the binary does not contain, and its handleStatus()
    # (bringup/p4_hosted_bench.cpp:840-880) publishes none of the refusal
    # counters that would corroborate one. So the floor is not "unresolvable"
    # here, it is inapplicable, and the report says which.
    enforces_admission_floor = False
    admission_absence_note = (
        "<no admission floor on this image: bringup/p4_hosted_bench.cpp is built with "
        "build_src_filter = -<*> (platformio.ini:624-627), so src/web/web_admission_psychic.cpp "
        "-- the only code that reads PA_ADMISSION_MIN_LARGEST_FREE_BLOCK -- is not compiled "
        "in, and handleStatus() publishes no refusal counters. There is no level at which "
        "this image turns a request away, so its heap readings are recorded (see heapSeries) "
        "and not judged. Judging them against a percentage of a baseline sample was tried "
        "and removed: it measures how spiky a fragmentation reading is, not whether the "
        "controller was still serving>"
    )
    reset_reason_kind = "esp_reset_reason_t int"
    heap_field = "largestFree8bitBlock"
    heap_free_field = "freeHeapBytes"
    # ESP.getMinFreeHeap() has no counterpart in the bench handleStatus().
    heap_min_field = None
    sse_clients_field = "sseClientsConnected"
    restart_field = "bootCount"
    restart_verb = "advanced"
    ladder_container = None
    # bringup/p4_hosted_bench.cpp handleStatus(), flat at the top level.
    ladder_fields = {
        "state": "recoveryLadderState",
        "transportFailureCount": "hostedTransportFailureCount",
        "transportUpEventCount": "hostedTransportUpEventCount",
        "attemptCount": "recoveryAttemptCount",
        "recoveredCount": "recoveryRecoveredCount",
    }
    continuity_tracker_class = CounterFrameTracker

    def reset_reason(self, body: dict, context: str) -> ResetReasonAssessment:
        value = _require_field(body, "resetReason", int, context)
        # The bench publishes the raw enum value and ESP_RESET_REASON_NAMES
        # was read from esp_system.h in full, so there is no collapsed
        # bucket here and no ambiguous case: every value is classified.
        return ResetReasonAssessment(
            display=ESP_RESET_REASON_NAMES.get(value, value),
            crash_shaped=value in BAD_RESET_REASONS,
        )

    def restart_detected(self, baseline: int, markers: list[int]) -> bool:
        # bootCount is RTC_DATA_ATTR: it survives a CPU reset and not a power
        # cycle, so ANY difference from the baseline is a restart -- a power
        # cycle resets it, which is a decrease, not an increase.
        return any(marker != baseline for marker in markers)

    def restart_report(self, baseline: int, final: int, detected: bool) -> dict:
        return {
            "baselineBootCount": baseline,
            "finalBootCount": final,
            "bootCountAdvanced": detected,
        }

    def link_readiness(self, body: dict) -> tuple[bool, str]:
        hosted_initialized = body.get("hostedIsInitialized")
        wifi_connected = body.get("wifiConnected")
        if hosted_initialized is True and wifi_connected is True:
            return True, ""
        return False, (
            "device is reachable but not a confirmed-ready C6: "
            f"hostedIsInitialized={hosted_initialized!r} wifiConnected={wifi_connected!r} "
            "(missing/false means the required evidence -- an established Hosted "
            "link -- is absent, not that the link failed)"
        )

    def structural_mismatches(self, body: dict) -> list[str]:
        mismatches = []
        for field, expected_type in (
            ("bootCount", int), ("resetReason", int), (self.heap_field, int),
            (self.sse_clients_field, int), (self.ladder_fields["state"], str),
        ):
            mismatch = _marker_mismatch(body, field, expected_type)
            if mismatch is not None:
                mismatches.append(mismatch)
        return mismatches


class ProductImageStatusSchema(StatusSchema):
    """What src/web/web_server.cpp buildStatusJson() publishes on EVERY board.

    The fixed system-health block is one unconditional snprintf (:391-393), so
    every product image agrees on these names and types whatever the Board
    Variant. The only board-conditional part of that payload is the
    "hostedLink" object at :724-743, behind PA_CAP_HOSTED_WIFI -- so the
    concrete product schemas below differ from each other in exactly that
    block, in the readiness evidence that depends on it, and in why neither
    has a C6 reset route."""

    # Neither product image has a reset route: grep over src/ + include/
    # returns zero hits for /api/c6/reset, and the seam route table
    # (src/web/web_seam_routes.cpp) registers none. run_c6_reset_recovery()
    # refuses on this, rather than reporting a pass it never provoked. The
    # reason differs per board and is spelled out in each schema's
    # reset_unavailable_reason.
    reset_path = None
    # buildStatusJson() publishes no bootCount at all. Recorded as a
    # structural property so nothing can read the absence as zero.
    publishes_boot_count = False
    # src/web/api_events.cpp refuses a stream once
    # PA_ADMISSION_MAX_SSE_CLIENTS are open, with 503 and a short body. That
    # handler and that cap carry no board guard, so this holds on every
    # product image. Under a reconnect storm at the cap the refusal is
    # admission working as designed, not a transport fault --
    # run_reconnect_storm() counts it separately for exactly that reason.
    enforces_sse_client_cap = True
    # Both product images compile src/web/web_admission_psychic.cpp, whose
    # request middleware carries no board guard, so both refuse at a floor and
    # both publish the same counters out of buildStatusJson()'s unconditional
    # snprintf (src/web/web_server.cpp:393). The floor VALUE is per build
    # environment, which is what resolve_admission_floor() is for.
    enforces_admission_floor = True
    reset_reason_kind = "resetReasonName() string"
    heap_field = "heapLargest8bit"
    # ESP.getFreeHeap() / ESP.getMinFreeHeap() (src/web/web_server.cpp:361-362).
    # heapLargest8bit is the fragmentation reading the floors gate on; these two
    # are what separate fragmentation from exhaustion, which is the distinction
    # #194's graded run turned on.
    heap_free_field = "heapFree"
    heap_min_field = "heapMin"
    sse_clients_field = "sseClients"
    # src/web/web_server.cpp:387-388, 440. Ordinary-class and diagnostic-class
    # request refusals at the heap floor, counted by the middleware itself.
    refused_heap_floor_field = "refusedHeapFloor"
    refused_heap_floor_diag_field = "refusedHeapFloorDiag"
    # :425-427. The Connection Admission guard's own running minimum, published
    # as -1 until it has sampled once (ACCEPT_MIN_LARGEST_BLOCK_NEVER_SAMPLED).
    accept_min_largest_block_field = "acceptMinLargestBlockSeen"
    # Progress-line only (see StatusSchema). The SSE half of admission, from the
    # same unconditional snprintf (src/web/web_server.cpp:393): refusedSseCap is
    # a stream turned away at PA_ADMISSION_MAX_SSE_CLIENTS
    # (src/web/api_events.cpp) and sseEvicted is a stream the broadcaster
    # dropped for missing its send deadline (g_webSseEvicted,
    # src/web/web_request_psychic.cpp:180). Both are what an operator watching a
    # multi-hour soak wants to see move, or not move, while it runs. The bench
    # image publishes neither -- bringup/p4_hosted_bench.cpp handleStatus() has
    # no cap and no evicting registry -- so they stay None there and its
    # progress lines carry no such key.
    sse_refused_cap_field = "refusedSseCap"
    sse_evicted_field = "sseEvicted"
    sse_clients_peak_field = "sseClientsPeak"
    # No bootCount, so the restart evidence is uptimeMs (millis(),
    # web_server.cpp:360) stepping backwards -- see restart_detected().
    restart_field = "uptimeMs"
    restart_verb = "went backwards"
    continuity_tracker_class = HeartbeatFrameTracker
    # Grammatical article for the diagnostics below -- "a shipping image", "an
    # artoo image". These strings are read by an operator, not parsed.
    image_article = "a"

    def _product_marker_mismatches(self, body: dict) -> list[str]:
        """The structural markers every product payload must carry, plus the
        bootCount absence they all share. Each concrete product schema adds
        the markers its own board capability decides."""
        mismatches = []
        # bootCount must be ABSENT. Checked positively so that an image which
        # started publishing one is refused here rather than quietly read
        # through a schema that assumes it cannot exist.
        if "bootCount" in body:
            mismatches.append(
                f"declared image: bootCount is present, but the {self.name} payload "
                f"(src/web/web_server.cpp:393) publishes none -- this is not "
                f"{self.image_article} {self.name} image"
            )
        for field, expected_type in (
            ("resetReason", str), (self.heap_field, int),
            (self.sse_clients_field, int), (self.restart_field, int),
            # The three admission counters are markers, and heapFree/heapMin
            # are not, on one line: these decide the verdict, so a payload
            # without them cannot be judged at all and must be refused at
            # preflight rather than halfway through a 30-minute run. heapFree
            # and heapMin are recorded, so their absence degrades the evidence
            # without invalidating the verdict -- it surfaces as a series
            # anomaly instead.
            (self.refused_heap_floor_field, int),
            (self.refused_heap_floor_diag_field, int),
            (self.accept_min_largest_block_field, int),
        ):
            mismatch = _marker_mismatch(body, field, expected_type)
            if mismatch is not None:
                mismatches.append(mismatch)
        return mismatches

    def reset_reason(self, body: dict, context: str) -> ResetReasonAssessment:
        name = _require_field(body, "resetReason", str, context)
        if name in SHIPPING_CRASH_SHAPED_RESET_NAMES:
            return ResetReasonAssessment(display=name, crash_shaped=True)
        if name in SHIPPING_CLEAN_RESET_NAMES:
            return ResetReasonAssessment(display=name, crash_shaped=False)
        if name in SHIPPING_UNKNOWN_RESET_NAMES:
            return ResetReasonAssessment(
                display=name, crash_shaped=None,
                caveat=(
                    f"resetReasonName() (src/reset_reason.cpp) reports {name!r}, which does not "
                    "identify one reset: 'OTHER' is its default arm and collapses "
                    "ESP_RST_CPU_LOCKUP, ESP_RST_PWR_GLITCH, ESP_RST_USB, ESP_RST_JTAG and "
                    "ESP_RST_EFUSE into one name, and 'UNKNOWN' is esp_reset_reason() itself "
                    "saying it could not tell. Recorded as unknown rather than as a clean start"
                ),
            )
        return ResetReasonAssessment(
            display=name, crash_shaped=None,
            caveat=(
                f"resetReasonName() (src/reset_reason.cpp) does not produce {name!r} -- this "
                f"payload is not the {self.name} mapping this schema was read from"
            ),
        )

    def restart_detected(self, baseline: int, markers: list[int]) -> bool:
        # A product image publishes no bootCount, so the restart evidence
        # is uptimeMs (millis()) stepping backwards: a reboot restarts
        # millis() at 0. Compared against the PREVIOUS sample rather than
        # against the baseline, so a device that reboots and then runs past
        # its old uptime still shows the step down.
        #
        # Two limits, stated rather than hidden: a reboot is missed only if
        # the device rebooted AND accumulated more uptime than the previous
        # sample before the next poll (which needs a poll interval longer
        # than the uptime at that point), and millis() wraps after ~49.7
        # days, which this cannot distinguish from a reboot -- both are
        # reported as a restart, which is the safe direction.
        previous = baseline
        for marker in markers:
            if marker < previous:
                return True
            previous = marker
        return False

    def restart_report(self, baseline: int, final: int, detected: bool) -> dict:
        # No bootCount key on a product-image report, deliberately: a consumer
        # looking for one finds nothing, which is the truth, rather than an
        # uptime reading wearing bootCount's name.
        return {
            "baselineUptimeMs": baseline,
            "finalUptimeMs": final,
            "uptimeMsWentBackwards": detected,
        }


class ShippingStatusSchema(ProductImageStatusSchema):
    name = "shipping"
    # The firebeetle2 product image (AGENTS.md "Flashing and Monitoring": of the
    # four P4 environments, this is the only one that is the firmware).
    build_env = "firebeetle2"
    # The board does have an ESP32-C6, so a reset route is meaningful here and
    # simply is not implemented -- tracked on #243.
    reset_unavailable_reason = (
        "the shipping image publishes no C6 reset route -- POST /api/c6/reset "
        "exists only on bringup/p4_hosted_bench.cpp, and the shipping seam route "
        "table (src/web/web_seam_routes.cpp) registers none. The reset cannot be "
        "provoked, so nothing about recovery can be measured on this image. "
        "Shipping-image C6 reset recovery is tracked on #243; run this driver "
        "against --image bench"
    )
    ladder_container = HOSTED_LINK_CONTAINER
    # src/web/web_server.cpp:724-743, from HostedLinkStatusSnapshot
    # (include/hosted_link_status.h).
    ladder_fields = {
        "state": "phase",
        "transportFailureCount": "transportFailureCount",
        "transportUpEventCount": "transportUpEventCount",
        "attemptCount": "attemptCount",
        "recoveredCount": "recoveredCount",
    }

    def link_readiness(self, body: dict) -> tuple[bool, str]:
        hosted_link = body.get(self.ladder_container)
        phase = hosted_link.get("phase") if isinstance(hosted_link, dict) else None
        wifi_connected = body.get("wifiConnected")
        if wifi_connected is True and phase == "idle":
            return True, ""
        return False, (
            "device is reachable but not a confirmed-ready C6: "
            f"wifiConnected={wifi_connected!r} hostedLink.phase={phase!r}. wifiConnected is "
            "WiFi.status() -- 'what does the radio believe' "
            "(src/web/web_network_manager_hosted.cpp:430-439), which #184 proved stays "
            "WL_CONNECTED through a dead SDIO transport, so it is corroborated here with "
            "the link supervisor's own phase: 'idle' means no transport failure is "
            "outstanding, 'armed'/'attempting' mean a recovery run is in flight, and "
            "'degraded' is terminal for this boot (ADR 0032). A missing hostedLink block "
            "means this image was not built with PA_CAP_HOSTED_WIFI, so the evidence is "
            "absent rather than negative"
        )

    def structural_mismatches(self, body: dict) -> list[str]:
        mismatches = self._product_marker_mismatches(body)
        hosted_link = body.get(self.ladder_container)
        if not isinstance(hosted_link, dict):
            mismatches.append(
                f"declared image: response has no {self.ladder_container!r} object "
                "(src/web/web_server.cpp:724-742, PA_CAP_HOSTED_WIFI) -- the recovery-ladder "
                "evidence this harness exists to record is not published by this image"
            )
        elif _type_mismatch(hosted_link.get("phase"), str):
            mismatches.append(
                f"declared image: {self.ladder_container}.phase is "
                f"{hosted_link.get('phase')!r}, expected the hostedLinkPhaseName() string"
            )
        return mismatches


class ArtooStatusSchema(ProductImageStatusSchema):
    """The artoo_esp32 product image.

    The same buildStatusJson() as `shipping`, on a board where
    PA_CAP_HOSTED_WIFI is 0 (include/config.h:69). Two things follow, and both
    are absences this schema checks for rather than assumes:

      hostedLink  emitted only inside `#if PA_CAP_HOSTED_WIFI`
                  (src/web/web_server.cpp:724-743), so there is no recovery
                  ladder anywhere in the payload.
      reset route could not exist -- there is no companion radio on this board
                  to reset.

    Everything else the harness reads comes out of the unconditional
    system-health snprintf (:391-393) and is byte-for-byte the shipping
    image's, which is why this schema is a sibling of ShippingStatusSchema
    under one product base rather than a fork of it."""

    name = "artoo"
    build_env = "artoo_esp32"
    image_article = "an"
    reset_unavailable_reason = (
        "the artoo image publishes no C6 reset route, and could not: POST /api/c6/reset "
        "resets an ESP32-C6 companion radio over its ESP-Hosted reset line, and "
        "artoo-esp32 has no companion radio at all -- PA_CAP_NATIVE_WIFI is 1 and "
        "PA_CAP_HOSTED_WIFI is 0 (include/config.h:68-69), so the radio is on the same "
        "die as the application. There is nothing to reset and therefore nothing about "
        "C6 recovery to measure on this board. This is not the shipping image's "
        "situation, which is a missing route on a board that does have a C6 (#243); run "
        "this driver against --image bench"
    )
    # No ladder object at all, so no container and no field map: an empty
    # ladder_fields would otherwise read as "publishes an empty ladder".
    ladder_container = None
    ladder_fields = {}
    publishes_recovery_ladder = False
    ladder_absence_note = (
        "<no recovery ladder on this image: the hostedLink block "
        "(src/web/web_server.cpp:724-743) is compiled out where PA_CAP_HOSTED_WIFI is 0 "
        "(include/config.h:69), and artoo-esp32 has no SDIO transport for a link "
        "supervisor to watch>"
    )

    def link_readiness(self, body: dict) -> tuple[bool, str]:
        # wifiConnected ALONE, and that is weaker evidence than the shipping
        # schema's -- said out loud in the diagnostic rather than papered over.
        # There, the same field is corroborated by the link supervisor's own
        # hostedLink.phase, because #184 proved WiFi.status() stays
        # WL_CONNECTED through a dead SDIO transport. Here there is no second
        # opinion to ask: PA_CAP_HOSTED_WIFI is 0, so there is no SDIO
        # transport to die and no supervisor publishing a phase. The check did
        # not get stronger on this board; it is unopposed.
        wifi_connected = body.get("wifiConnected")
        if wifi_connected is True:
            return True, ""
        return False, (
            "device is reachable but does not report WiFi up: "
            f"wifiConnected={wifi_connected!r}. This is WEAKER evidence than the shipping "
            "schema's readiness check, which corroborates the same field with the link "
            "supervisor's hostedLink.phase -- #184 proved WiFi.status() stays WL_CONNECTED "
            "through a dead SDIO transport. artoo-esp32 has no SDIO transport to die and "
            "no supervisor to ask (PA_CAP_HOSTED_WIFI is 0, include/config.h:69), so this "
            "check is not stronger here, it is unopposed. wifiConnected is itself "
            "apEnabled || staConnected (deriveWiFiConnectivityFields(), "
            "src/web/api_status_serializers.cpp:18), so it reads true on a board serving "
            "only its own AP. A missing/false field means the evidence is absent, not that "
            "the link failed"
        )

    def structural_mismatches(self, body: dict) -> list[str]:
        mismatches = self._product_marker_mismatches(body)
        # hostedLink must be ABSENT, checked positively for the same reason
        # bootCount is: an image that started publishing one is a different
        # image, and has to be refused here rather than read through a schema
        # that assumes the block cannot exist.
        if HOSTED_LINK_CONTAINER in body:
            mismatches.append(
                f"declared image: {HOSTED_LINK_CONTAINER!r} is present, but that object is "
                "emitted only inside `#if PA_CAP_HOSTED_WIFI` "
                "(src/web/web_server.cpp:724-743) and that capability is 0 on artoo-esp32 "
                "(include/config.h:69) -- this is not an artoo image"
            )
        return mismatches


SCHEMAS: dict[str, StatusSchema] = {
    "bench": BenchStatusSchema(),
    "shipping": ShippingStatusSchema(),
    "artoo": ArtooStatusSchema(),
}


def identify_schema(body: dict) -> Optional[StatusSchema]:
    """Diagnostic only: which declared schema, if exactly one, this payload
    satisfies. Used to name the likely image in a preflight mismatch message
    -- never to select a schema, for the reason in StatusSchema's docstring."""
    matches = [schema for schema in SCHEMAS.values() if not schema.structural_mismatches(body)]
    return matches[0] if len(matches) == 1 else None


# ---------------------------------------------------------------------------
# Progress, checkpoints and interruption.
#
# Two defects, one root cause, both found while taking a multi-hour soak on a
# real board:
#
#   silence      the SSE soak driver had no print() at all and the CLI had no
#                progress option, so `--duration 10800` emitted nothing for
#                three hours and was indistinguishable from a hang.
#   all-or-none  a 3-hour run stopped at 8m46s exited 143 (SIGTERM) and wrote
#                no JSON artefact whatsoever. Every measurement it had already
#                taken was discarded.
#
# The common cause is that the harness treated "the run finished" as the only
# state in which it had anything to say. This section is the one place that
# treats "the run is still going" and "the run was cut short" as states too.
#
# Where the output goes, and why it is not negotiable: stdout carries the JSON
# report and NOTHING else, because `soak.py > report.json` and `soak.py | jq`
# are working contracts (main() ends with one print(rendered) on stdout).
# Every human-facing byte -- header, heartbeats, status line, footer, the
# admission-floor and verdict lines -- goes to stderr.
#
# What none of it may do, and this is the constraint that shapes the design:
# it must not change what a completed run concludes. Every driver takes the
# monitor as an optional trailing argument and falls back to
# RunMonitor.disabled(), which prints nothing, logs nothing, checkpoints
# nothing and is never interrupted -- so a driver called without one (the
# --self-test, the unit tests) and a run that is never interrupted reach the
# same verdict, by the same code, as before any of this existed.
#
# THE SEAM, for whoever adds the fourth driver: a driver becomes legible by
# writing one function of one argument (see ProgressSource below) and passing
# it to RunMonitor.wait()/tick(). Nothing here knows what any driver measures.
# ---------------------------------------------------------------------------

# What a progress line prints for a reading this image publishes but this
# particular sample did not carry. Never 0 and never "false": "the controller
# did not answer with this field just now" and "the field read zero" are
# different facts, and a soak that confuses them is the failure this whole
# harness exists to avoid. A reading the IMAGE does not publish at all never
# reaches a line -- the key is omitted entirely, so the two absences stay
# distinguishable from each other as well.
PROGRESS_ABSENT = "?"

# Default seconds between heartbeat lines and --json checkpoint writes. A
# presentation cadence, not a property of any board: 30s is ~360 lines across
# a 3-hour soak (legible in a scrollback, not a firehose) and 4 across a
# two-minute reconnect storm. --progress-interval-s overrides it.
DEFAULT_PROGRESS_INTERVAL_S = 30.0

# How often the one in-place status line is redrawn on a terminal. It is a
# clock, not an animation: it exists so a wait between heartbeats still shows
# time advancing. A spinner would say "something is happening" without saying
# how long for, which after two hours is the same as saying nothing.
STATUS_LINE_REFRESH_S = 1.0

# Semantic tokens, ASCII in every environment. Colour is the only thing that
# varies with the terminal, and rich decides that (see RunConsole): these
# never become glyphs or emoji, so a redirected stderr and the transcript log
# read identically to a terminal minus the escape codes.
CONSOLE_KINDS = {
    "ok": ("[OK]", "bold green"),
    "fail": ("[FAIL]", "bold red"),
    "warn": ("[WARN]", "bold yellow"),
    "step": ("[>>]", "bold cyan"),
    "info": ("[..]", "dim"),
}


def format_duration(seconds: float) -> str:
    """H:MM:SS -- the form an operator reads a multi-hour run in. Negative
    input clamps to zero rather than printing a negative clock, which only
    happens when a wait overshoots its deadline by scheduling jitter."""
    total = int(max(0.0, seconds))
    return f"{total // 3600}:{(total % 3600) // 60:02d}:{total % 60:02d}"


def format_timestamp(epoch_s: float) -> str:
    """Local wall-clock ISO 8601 with an offset, for the header/footer and for
    the JSON clocks. Wall clock rather than the monotonic clock the durations
    use: an operator correlates this against a board log, and a monotonic
    reading correlates with nothing."""
    return time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime(epoch_s))


def render_progress_value(value: Any) -> str:
    """One progress-line value. None is PROGRESS_ABSENT, never 0/false."""
    if value is None:
        return PROGRESS_ABSENT
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (list, tuple)):
        return "[" + ",".join(render_progress_value(item) for item in value) + "]"
    if isinstance(value, float):
        return f"{value:.3f}"
    return str(value)


def _render_fields(fields: dict) -> str:
    return " ".join(f"{key}={render_progress_value(value)}" for key, value in fields.items())


@dataclasses.dataclass
class ProgressSnapshot:
    """One moment of a running driver, rendered three ways from one source.

    status_line() is the in-place clock, render() is the appended heartbeat
    and as_dict() is what a --json checkpoint carries. All three read the same
    fields, which is the point: a status line that said something the
    heartbeat did not, or a checkpoint that disagreed with either, would give
    two accounts of one instant.

    Four clocks, and never an indicator without one: run_elapsed_s (the whole
    run), elapsed_s (this driver), remaining_s() (only where it is genuinely
    known) and total_s (the plan). "2/3 reconnect_storm" on its own is useless
    two hours in.
    """

    driver: str
    # 1-based, and how many drivers this run will attempt.
    driver_index: int
    driver_count: int
    run_elapsed_s: float
    elapsed_s: float
    # None when this driver runs to no clock at all. Never a fabricated total:
    # a percentage against a guess is a fake ETA, which is worse than no ETA.
    total_s: Optional[float]
    # How to READ total_s, because the two are not the same promise.
    #   "planned"    the driver intends to run for total_s (a soak duration, a
    #                storm duration), so a countdown is honest.
    #   "timeout"    total_s is the most it will wait, and finishing early is
    #                the GOOD outcome (a recovery budget): shown as a timeout,
    #                never as progress toward completion.
    #   "unbounded"  no clock at all; elapsed plus what it is waiting for.
    total_kind: str = "planned"
    waiting_for: str = ""
    # Driver-local and free to compute -- no request goes out for these, which
    # is what lets the status line redraw once a second at no cost on the wire.
    headline: dict = dataclasses.field(default_factory=dict)
    # Controller-sourced, gathered only when a heartbeat is actually due.
    fields: dict = dataclasses.field(default_factory=dict)

    def remaining_s(self) -> Optional[float]:
        if self.total_s is None:
            return None
        return max(0.0, self.total_s - self.elapsed_s)

    def _clocks(self) -> str:
        head = (
            f"[{self.driver_index}/{self.driver_count} {self.driver}] "
            f"run {format_duration(self.run_elapsed_s)} | "
            f"drv {format_duration(self.elapsed_s)}"
        )
        if self.total_s is None or self.total_s <= 0:
            if self.total_kind == "unbounded" and self.waiting_for:
                return f"{head} (waiting for {self.waiting_for})"
            return head
        remaining = format_duration(self.remaining_s() or 0.0)
        if self.total_kind == "timeout":
            # No percentage here on purpose: 40% of a timeout is not 40% done,
            # it is 40% of the way to giving up.
            return f"{head} (timeout {format_duration(self.total_s)}, {remaining} left)"
        percent = 100.0 * min(self.elapsed_s / self.total_s, 1.0)
        return f"{head}/{format_duration(self.total_s)} ({percent:.1f}%) left {remaining}"

    def status_line(self) -> str:
        body = _render_fields(self.headline)
        return f"{self._clocks()} | {body}" if body else self._clocks()

    def render(self) -> str:
        body = _render_fields({**self.headline, **self.fields})
        return f"{self._clocks()} | {body}" if body else self._clocks()

    def as_dict(self) -> dict:
        return {
            "driver": self.driver,
            "driverIndex": self.driver_index,
            "driverCount": self.driver_count,
            "runElapsedS": round(self.run_elapsed_s, 3),
            "elapsedS": round(self.elapsed_s, 3),
            "totalS": self.total_s,
            "totalKind": self.total_kind,
            "remainingS": (
                None if self.remaining_s() is None else round(self.remaining_s(), 3)
            ),
            "fields": {**self.headline, **self.fields},
        }


# The seam a long-running driver reports through, and the whole of it:
#
#     snapshot(full: bool) -> ProgressSnapshot
#
#   full=False  the once-a-second status line. MUST be cheap and MUST NOT
#               touch the network -- it is called every second for the whole
#               run, so a request in here would be thousands of extra requests
#               that the measurement never asked for. Fill `headline` only.
#   full=True   the heartbeat line and the checkpoint. May read the controller
#               to fill `fields`, and is called once per --progress-interval-s.
#
# Build the returned snapshot with RunMonitor.snapshot(), which stamps the
# run-level clocks the driver does not know. A driver added years from now
# becomes legible by writing that one function; nothing in this section has to
# learn what it measures.
ProgressSource = Callable[[bool], ProgressSnapshot]


def _progress_int(scope: dict, field: Optional[str]) -> Optional[int]:
    """One int out of a status sample, for a progress line only.

    None means "this sample carried no usable reading", which renders as
    PROGRESS_ABSENT. Type-checked through the same _type_mismatch() the verdict
    readers use, so a `true` where an int belongs reads as absent here too
    rather than as the integer 1.
    """
    if not field:
        return None
    value = scope.get(field)
    if _type_mismatch(value, int):
        return None
    return value


def progress_status_fields(
    schema: StatusSchema, body: Optional[dict],
    admission_floor: Optional[AdmissionFloor] = None,
) -> dict:
    """The controller-side signals a progress line carries, per image.

    Reads only what the declared image genuinely publishes. A key this image
    has no field for is left out of the mapping entirely (the bench publishes
    no admission counters and no eviction count; a board with no companion
    radio publishes no recovery ladder), while a key this image DOES publish
    but this sample did not carry is present with None and renders as
    PROGRESS_ABSENT. The two absences therefore stay distinguishable from each
    other, and neither is ever a zero.

    Nothing here reaches a verdict, which is why these field names live in the
    schema's progress-only block and NOT in fields_read(): that map states
    which payload paths the verdict was taken from, and padding it with
    fields no verdict reads would make it say less, not more.
    """
    fields: dict = {}
    if body is None:
        return fields
    poll_error = body.get("_pollError")
    if poll_error is not None:
        # The sentinel the soak's poller writes when the device could not be
        # reached. Reported as the unreachable poll it was; every other key is
        # omitted rather than carried over from an older sample, which would
        # show an operator a stale reading as if it were current.
        fields["statusPoll"] = f"unreachable ({poll_error})"
        return fields
    largest_free = _progress_int(body, schema.heap_field)
    fields[schema.heap_field] = largest_free
    if admission_floor is not None:
        fields["floorMargin"] = (
            None if largest_free is None else largest_free - admission_floor.ordinary_bytes
        )
    if schema.enforces_admission_floor:
        fields["refusedHeapFloor"] = _progress_int(body, schema.refused_heap_floor_field)
        fields["refusedHeapFloorDiag"] = _progress_int(
            body, schema.refused_heap_floor_diag_field)
    if schema.sse_refused_cap_field:
        fields["refusedSseCap"] = _progress_int(body, schema.sse_refused_cap_field)
    if schema.sse_evicted_field:
        fields["sseEvicted"] = _progress_int(body, schema.sse_evicted_field)
    if schema.publishes_recovery_ladder:
        names = schema.ladder_fields
        scope = body
        if schema.ladder_container is not None:
            container = body.get(schema.ladder_container)
            # A missing container leaves every ladder key absent rather than
            # zero: the block being gone and its counters reading zero are
            # different claims, and preflight already refuses an image whose
            # container is missing outright.
            scope = container if isinstance(container, dict) else {}
        state = scope.get(names["state"])
        # Named for the thing rather than abbreviated: "recovery ladder" is the
        # project's word for it (CONTEXT.md), and a progress line an operator
        # quotes a year from now should use the same word the report and the
        # firmware do.
        fields["recoveryLadder"] = state if isinstance(state, str) else None
        fields["recoveryLadderFailures"] = _progress_int(
            scope, names["transportFailureCount"])
        fields["recoveryLadderUpEvents"] = _progress_int(
            scope, names["transportUpEventCount"])
        fields["recoveryLadderAttempts"] = _progress_int(scope, names["attemptCount"])
        fields["recoveryLadderRecovered"] = _progress_int(scope, names["recoveredCount"])
    return fields


class RunConsole:
    """Everything a human sees, on stderr, plus the transcript log file.

    Presentation rules this class exists to hold in one place:

      no TUI          no alt-screen, no keybinds, no screen clearing. An
                      append-only stream of lines, plus at most ONE status
                      line that updates in place.
      no spinner      the status line carries clocks. An animation says
                      "something is happening" without saying for how long.
      semantic only   colour and the [OK]/[FAIL]/[WARN] tokens mark meaning,
                      never decoration, and the tokens are ASCII everywhere so
                      only the colour varies with the terminal.
      log always      every line an operator would have seen is also written
                      to a plain transcript, with no ANSI and no emoji, whose
                      path the JSON report carries so the next tool can find
                      it without parsing a terminal.

    rich owns the degradation and is not second-guessed here: Console(file=
    sys.stderr) reports is_terminal False for a redirected stream and drops
    colour, and it honours NO_COLOR and TERM=dumb on its own. Measured against
    a real pty rather than assumed, because the two are not the same:
    TERM=dumb makes rich report is_terminal False, so nothing is emitted but
    plain text; NO_COLOR removes the COLOUR codes and deliberately keeps bold
    and dim and keeps the status line, which is what NO_COLOR asks for -- it
    is a colour switch, not a cursor-control switch. Neither is worth
    overriding, and a "fix" that stripped bold under NO_COLOR would be going
    beyond the standard.

    The one thing this class decides is that the in-place status line exists
    only on a terminal -- carriage returns and erase-to-end-of-line are noise
    in a redirected stream or a CI log, so off a terminal the heartbeats are
    the whole display.

    markup=False and highlight=False are deliberate, not defensive: a
    heartbeat contains "[1/3 sse_soak]" and a status token is literally
    "[OK]", both of which rich would otherwise read as markup and swallow.
    soft_wrap=True keeps a long heartbeat on one line, because a wrapped
    transcript is a transcript nobody can grep.
    """

    def __init__(
        self, stream: Any = None, quiet: bool = False, log_path: Optional[Path] = None,
        force_terminal: Optional[bool] = None,
    ) -> None:
        self.console = Console(
            file=stream if stream is not None else sys.stderr,
            highlight=False, markup=False, soft_wrap=True,
            force_terminal=force_terminal,
        )
        self.quiet = quiet
        self.log_path = log_path
        # Append, never truncate: a transcript is evidence, and a second run
        # pointed at the same path must not delete the first one's.
        self._log = None if log_path is None else log_path.open("a", encoding="utf-8")
        # Serialises the log file against the driver threads that reach it
        # through detail() while the main thread is emitting lines.
        self._lock = threading.Lock()
        self._live: Optional[Live] = None
        self._status_text = ""
        # The widest label any block in this run has used, so the header, the
        # board rows and the footer line up as one column even though they are
        # emitted from three different places at three different times.
        self._label_width = 0

    # -- lifecycle -------------------------------------------------------

    def start_status_line(self) -> None:
        """Begin the one updating status line, on a terminal only.

        auto_refresh is off and transient is on: nothing redraws unless this
        process asks it to (so a wedged run looks wedged rather than animated),
        and the line disappears at the end instead of leaving a stale clock in
        the scrollback -- the heartbeats are the permanent record.
        """
        if self.quiet or self._live is not None or not self.console.is_terminal:
            return
        self._live = Live(
            Text(self._status_text), console=self.console,
            auto_refresh=False, transient=True,
        )
        self._live.start()

    def stop_status_line(self) -> None:
        """Tear down the in-place status line while leaving the transcript
        open. Called before anything is written to stdout: the JSON report is
        the only thing on that stream and must not share a terminal frame with
        a line that is still updating."""
        if self._live is not None:
            self._live.stop()
            self._live = None

    def close(self) -> None:
        self.stop_status_line()
        with self._lock:
            if self._log is not None:
                self._log.close()
                self._log = None

    # -- output ----------------------------------------------------------

    def line(self, text: str, kind: Optional[str] = None) -> None:
        """One appended line. `kind` selects a semantic token and colour; None
        is a plain data line (a heartbeat), which is what keeps the transcript
        greppable."""
        token, style = CONSOLE_KINDS.get(kind or "", ("", ""))
        plain = f"{token} {text}" if token else text
        self._write_log(plain)
        if self.quiet:
            return
        rendered = Text()
        if token:
            rendered.append(token, style=style)
            rendered.append(" ")
        rendered.append(text)
        self.console.print(rendered)
        # Live intercepts console output and reprints the status line beneath
        # it, so an appended line never has to erase anything by hand -- but
        # it only does so when asked, because auto-refresh is off.
        if self._live is not None:
            self._live.refresh()

    def status(self, text: str) -> None:
        """Redraw the in-place status line. A no-op off a terminal, where the
        heartbeats already carry everything this would say."""
        self._status_text = text
        if self._live is None:
            return
        self._live.update(Text(text, style="dim"), refresh=True)

    def detail(self, text: str) -> None:
        """Log-only, and safe from any thread. For per-event device detail
        that should be complete in the file and summarised on the terminal: an
        unreachable status poll every few seconds is a flood on stderr and
        exactly the record an operator wants afterwards."""
        self._write_log(text)

    def _write_log(self, text: str) -> None:
        with self._lock:
            if self._log is None:
                return
            self._log.write(f"{time.strftime('%H:%M:%S')}  {text}\n")
            self._log.flush()

    def rule(self, title: str) -> None:
        self.line(f"=== {title} ===")

    def rows(self, rows: list[tuple[str, str]]) -> None:
        """A header/footer block: aligned label/value pairs, one per line. Not
        a rich table -- a table is box-drawing in a transcript that has to stay
        plain, and these are a handful of short pairs."""
        if not rows:
            return
        self._label_width = max(
            self._label_width, max(len(label) for label, _ in rows))
        for label, value in rows:
            self.line(f"  {label.ljust(self._label_width)}  {value}")


class RunMonitor:
    """Progress cadence, periodic checkpoints and the operator's stop signal.

    Every long-running driver takes one and reports to it through a
    ProgressSource; a driver given none uses RunMonitor.disabled() and behaves
    exactly as it did before this class existed, which is what keeps the
    --self-test and the unit tests measuring the same code path a device run
    takes.

    Only the thread running a driver ever calls line()/tick()/wait(), so
    nothing interleaves on stderr; the worker and poller threads a driver
    starts reach the transcript through detail() and never the console. The
    JSON report is printed after every driver has returned, on stdout, so it
    cannot interleave either.
    """

    def __init__(
        self, interval_s: float = DEFAULT_PROGRESS_INTERVAL_S, quiet: bool = False,
        console: Optional[RunConsole] = None,
        checkpoint_writer: Optional[Callable[[dict], None]] = None,
        interrupt: Optional[threading.Event] = None,
        started_at: Optional[float] = None,
    ) -> None:
        # <= 0 disables the cadence entirely: no heartbeats, no status line and
        # no checkpoints either, since all three hang off the same tick.
        self.interval_s = interval_s
        # Silences stderr and nothing else. The transcript is a separate
        # concern: a scripted run still wants a log, and still wants its
        # artefact to survive a kill.
        self.quiet = quiet
        self.console = console
        # Wall clock for the header, the footer and the JSON; monotonic for
        # every duration. `started_at` is accepted so main() can stamp the
        # transcript's filename and the report with the same instant rather
        # than two that differ by however long it took to open a file.
        self.started_at = time.time() if started_at is None else started_at
        self._run_started = time.monotonic()
        self._checkpoint_writer = checkpoint_writer
        self._checkpoint_source: Optional[Callable[[Optional[ProgressSnapshot]], dict]] = None
        self._interrupt = interrupt if interrupt is not None else threading.Event()
        self._signal_name: Optional[str] = None
        # Seeded from the run's own clock rather than from 0.0: a wait() that
        # ran before any driver was announced would otherwise see a deadline
        # decades in the past, spin at zero timeout, and make the catch-up
        # below count every interval since the machine booted.
        self._next_tick_at = self._run_started
        self._next_status_at = self._run_started
        self._driver_index = 0
        self._driver_count = 0

    @classmethod
    def disabled(cls) -> RunMonitor:
        """A monitor that prints nothing, logs nothing, checkpoints nothing
        and is never interrupted. The default for every driver, so calling a
        driver directly is indistinguishable from calling it before this
        existed."""
        return cls(interval_s=0.0, quiet=True)

    # -- clocks ----------------------------------------------------------

    @property
    def run_elapsed_s(self) -> float:
        return time.monotonic() - self._run_started

    @property
    def log_path(self) -> Optional[Path]:
        return None if self.console is None else self.console.log_path

    # -- interruption ----------------------------------------------------

    @property
    def interrupted(self) -> bool:
        return self._interrupt.is_set()

    @property
    def signal_name(self) -> Optional[str]:
        """"SIGINT"/"SIGTERM", or None when nothing has been received."""
        return self._signal_name

    def install_signal_handlers(self) -> None:
        """Make SIGINT and SIGTERM stop the run instead of killing it.

        The handler does nothing but record the signal and set the flag: it
        does not print and does not write. Python runs a signal handler in the
        main thread between bytecodes, so writing from here can land in the
        middle of another write and hit `RuntimeError: reentrant call inside
        <_io.BufferedWriter>`; the wait loops notice the flag within
        milliseconds (threading.Event.wait returns the moment it is set) and
        report it from ordinary code instead.

        The signal's default disposition is restored before the handler
        returns, so a SECOND Ctrl-C (or a second SIGTERM from an impatient
        script) kills the process immediately. An interrupt path that cannot
        itself be interrupted is a hang, and the artefact is written through
        os.replace() so even a hard kill leaves the last checkpoint intact.
        """
        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, self._handle_signal)

    def _handle_signal(self, signum: int, _frame: Any) -> None:
        if self._signal_name is None:
            self._signal_name = signal.Signals(signum).name
        signal.signal(signum, signal.SIG_DFL)
        self._interrupt.set()

    # -- output ----------------------------------------------------------

    def line(self, text: str, kind: Optional[str] = None) -> None:
        if self.console is not None:
            self.console.line(text, kind)

    def detail(self, text: str) -> None:
        """Log-only detail, safe to call from a driver's background thread."""
        if self.console is not None:
            self.console.detail(text)

    def rule(self, title: str) -> None:
        if self.console is not None:
            self.console.rule(title)

    def rows(self, rows: list[tuple[str, str]]) -> None:
        if self.console is not None:
            self.console.rows(rows)

    # -- checkpoints -----------------------------------------------------

    def bind_checkpoint_source(
        self, source: Optional[Callable[[Optional[ProgressSnapshot]], dict]],
    ) -> None:
        """Give the monitor the run-level report composer. Held by run(),
        because only run() knows which drivers have finished."""
        self._checkpoint_source = source

    def checkpoint(self, snapshot: Optional[ProgressSnapshot] = None) -> None:
        """Write the artefact from what has been collected so far. A no-op
        with no writer or no source bound."""
        if self._checkpoint_writer is None or self._checkpoint_source is None:
            return
        self._checkpoint_writer(self._checkpoint_source(snapshot))

    # -- the cadence -----------------------------------------------------

    @property
    def _shows_progress(self) -> bool:
        return self.console is not None and not self.quiet

    @property
    def _consumes_snapshot(self) -> bool:
        """Whether a snapshot would reach anyone at all. Checked before one is
        built, because a full snapshot may cost a request: a disabled monitor
        must put nothing on the wire that the driver would not have put there
        itself."""
        return self._shows_progress or (
            self._checkpoint_writer is not None and self._checkpoint_source is not None
        )

    def snapshot(
        self, driver: str, elapsed_s: float, total_s: Optional[float],
        headline: dict, fields: Optional[dict] = None,
        total_kind: str = "planned", waiting_for: str = "",
    ) -> ProgressSnapshot:
        """Stamp a driver's own numbers with the run-level clocks. The driver
        knows its activity; only the monitor knows how long the whole run has
        been going and which of how many drivers this is."""
        return ProgressSnapshot(
            driver=driver, driver_index=self._driver_index,
            driver_count=self._driver_count, run_elapsed_s=self.run_elapsed_s,
            elapsed_s=elapsed_s, total_s=total_s, total_kind=total_kind,
            waiting_for=waiting_for, headline=headline, fields=fields or {},
        )

    def start_run(self, driver_count: int) -> None:
        """How many drivers this run will attempt, for the i/N in every clock
        line. Set once by the orchestrator before the first driver."""
        self._driver_count = driver_count

    def begin_driver(self, index: int, name: str) -> None:
        """Announce a driver and reset both cadences for it.

        Called by the orchestrator rather than by the driver, so that a driver
        which refuses before it starts (no reset route on this image) is still
        announced, and so the i/N stays the orchestrator's count rather than
        something each driver has to be told.
        """
        self._driver_index = index
        now = time.monotonic()
        self._next_tick_at = now
        self._next_status_at = now
        self.line(f"{index}/{self._driver_count} {name}: starting", kind="step")

    def tick(self, snapshot_fn: Optional[ProgressSource] = None, force: bool = False) -> None:
        """Emit a heartbeat and write a checkpoint if the interval is up.

        `force` bypasses the cadence for the once-per-driver boundaries.
        Cheap to call in a tight poll loop: the common case is one comparison.
        """
        if not self._consumes_snapshot:
            return
        now = time.monotonic()
        if not force:
            if self.interval_s <= 0 or now < self._next_tick_at:
                return
        if self.interval_s > 0:
            # Advance from the scheduled time rather than from now, so a slow
            # status read does not make the cadence drift later every tick.
            # Computed rather than looped: a driver that blocked for an hour
            # must not cost an hour's worth of iterations to catch up.
            self._next_tick_at += self.interval_s * (
                1 + int((now - self._next_tick_at) // self.interval_s))
        snapshot = None if snapshot_fn is None else snapshot_fn(True)
        if snapshot is not None:
            self.line(snapshot.render())
            self._refresh_status(snapshot.status_line())
        self.checkpoint(snapshot)

    def status_tick(self, snapshot_fn: Optional[ProgressSource] = None) -> None:
        """Redraw the in-place status line if its (much faster) cadence is up.
        Costs nothing on the wire: the snapshot is built with full=False, which
        the seam defines as local state only."""
        if not self._shows_progress or snapshot_fn is None or self.interval_s <= 0:
            return
        now = time.monotonic()
        if now < self._next_status_at:
            return
        self._next_status_at += STATUS_LINE_REFRESH_S * (
            1 + int((now - self._next_status_at) // STATUS_LINE_REFRESH_S))
        self._refresh_status(snapshot_fn(False).status_line())

    def _refresh_status(self, text: str) -> None:
        if self._shows_progress:
            self.console.status(text)

    # -- waiting ---------------------------------------------------------

    def wait(self, seconds: float, snapshot_fn: Optional[ProgressSource] = None) -> bool:
        """Sleep up to `seconds`, keeping both cadences, returning early the
        instant the run is interrupted.

        True when the full time elapsed, False when the interrupt cut it
        short. On a disabled monitor this is exactly time.sleep(seconds) --
        threading.Event.wait() on an event nothing ever sets.
        """
        deadline = time.monotonic() + max(0.0, seconds)
        while True:
            if self.interrupted:
                return False
            now = time.monotonic()
            if now >= deadline:
                return True
            chunk = deadline - now
            if self.interval_s > 0:
                chunk = min(chunk, max(0.0, self._next_tick_at - now))
                if self._shows_progress:
                    chunk = min(chunk, max(0.0, self._next_status_at - now))
            if self._interrupt.wait(chunk):
                return False
            self.tick(snapshot_fn)
            self.status_tick(snapshot_fn)

    def wait_for_interrupt(self, seconds: float) -> bool:
        """Block up to `seconds`, returning True if the run was interrupted.
        Thread-safe and cadence-free -- for a helper thread that only needs to
        stop waiting, not to report anything."""
        return self._interrupt.wait(max(0.0, seconds))

    # -- what a truncated report says ------------------------------------

    def truncation_fields(self, observed_s: float, requested_s: Optional[float] = None) -> dict:
        """The keys a driver adds to its report when it was cut short, and
        NOTHING when it was not.

        Conditional on purpose: their presence is what tells a consumer --
        machine or human -- that it is holding a truncated run, alongside the
        verdict. A completed run's report is untouched.
        """
        if not self.interrupted:
            return {}
        fields = {
            "interrupted": True,
            "interruptSignal": self.signal_name,
            "durationSObserved": round(observed_s, 3),
        }
        if requested_s is not None:
            # Only for a driver whose report carries no durationSRequested of
            # its own; passing it twice would be two names for one number.
            fields["durationSRequested"] = requested_s
        return fields

    def truncation_reason(self, observed_s: float, requested_s: Optional[float] = None) -> str:
        of_requested = (
            f" of the {format_duration(requested_s)} requested" if requested_s is not None else ""
        )
        return (
            f"the run was interrupted ({self.signal_name}) after "
            f"{format_duration(observed_s)}{of_requested} -- this driver's evidence covers "
            "only the window actually observed, so it carries no verdict about the window "
            "it did not"
        )


def write_json_artifact(path: Path, rendered: str) -> None:
    """Write the --json artefact through a temporary file in the same
    directory and os.replace() it into place.

    A checkpoint is written repeatedly during a multi-hour run, so the write
    itself is a window in which a hard kill or a power loss could truncate the
    file. os.replace() is atomic within a filesystem on POSIX, and a temporary
    in the destination's own directory guarantees that filesystem -- so an
    interrupted write leaves the PREVIOUS checkpoint intact rather than a
    half-written one, which is the whole value of checkpointing.
    """
    temporary = path.with_name(path.name + ".partial")
    temporary.write_text(rendered)
    os.replace(temporary, path)


# ---------------------------------------------------------------------------
# Driver 1 -- SSE soak: N concurrent long-lived readers, gap detection,
# heap-trend and reboot/reset-reason guards, recovery-ladder visibility.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class ClientSoakResult:
    index: int
    tracker: SseContinuityTracker
    error: Optional[str]
    truncated: bool
    connect_ok: bool

    @property
    def frame_count(self) -> int:
        return self.tracker.frame_count


def _sse_soak_worker(
    client: BenchClient, schema: StatusSchema, index: int, stop: threading.Event,
    results: list[ClientSoakResult], first_frame_event: threading.Event,
    tracker: SseContinuityTracker,
) -> None:
    stream_result, tracker = stream_sse_with_continuity(
        client, schema, stop, read_chunk_timeout_s=5.0,
        on_frame=lambda _frame, _ts: first_frame_event.set(),
        tracker=tracker,
    )
    results.append(ClientSoakResult(
        index=index, tracker=tracker, error=stream_result.error,
        truncated=stream_result.truncated, connect_ok=stream_result.connect_ok,
    ))


def run_sse_soak(
    client: BenchClient, schema: StatusSchema, num_clients: int, duration_s: float,
    status_poll_interval_s: float, admission_floor: Optional[AdmissionFloor],
    sse_client_cap: SseClientCap, early_stall_check_s: float, max_silence_s: float,
    monitor: Optional[RunMonitor] = None,
) -> dict:
    monitor = monitor or RunMonitor.disabled()
    # A missing floor on an image that HAS one would silently drop the heap
    # verdict, so it is refused here rather than absorbed. Not a contract error
    # -- the device said nothing wrong -- so it propagates past
    # _run_driver_safely() as the harness bug it is, per AGENTS.md "never
    # swallow an error to keep moving".
    if schema.enforces_admission_floor and admission_floor is None:
        raise ValueError(
            f"the {schema.name} image refuses requests at a compiled admission floor, but "
            "run_sse_soak() was called without one -- run() resolves it from platformio.ini "
            "before any driver starts"
        )
    if not schema.enforces_admission_floor and admission_floor is not None:
        raise ValueError(
            f"the {schema.name} image compiles no admission guard "
            f"({schema.admission_absence_note}), so a floor of "
            f"{admission_floor.ordinary_bytes} would describe a gate this binary does not "
            "contain"
        )

    baseline = capture_status(client)
    baseline_restart_marker = schema.restart_marker(baseline, "baseline /api/status")
    baseline_reset = schema.reset_reason(baseline, "baseline /api/status")
    baseline_heap = schema.heap_largest_free(baseline, "baseline /api/status")
    # The firmware's own count of what it turned away, taken before the clients
    # arrive so a rise is attributable to this run rather than to whatever the
    # controller was doing beforehand. Required at the baseline on every image
    # that has an admission guard, for the same reason the ladder is: a soak
    # with no refusal baseline cannot say afterwards whether anything was
    # refused. None on an image with no guard at all.
    baseline_admission = schema.admission(baseline, "baseline /api/status")
    # The recovery ladder, required at the baseline on every image that has
    # one. Its transportUpEventCount is posted by the SDIO driver's own
    # transport_active_cb() (bringup/p4_hosted_bench.cpp:574-589 on the bench,
    # hostedTransportUpHandler() at src/web/web_network_manager_hosted.cpp:317
    # on the shipping image), independent of anything the sketch believes --
    # the corroborating signal #184 added after WiFi.status() was shown to
    # report CONNECTED through a dead transport. Required here rather than
    # read softly, for the same reason the driver that provokes the ladder
    # requires it: a soak with no ladder baseline cannot say afterwards
    # whether the ladder fired. None on an image that publishes no ladder at
    # all (schema.publishes_recovery_ladder) -- see the ladder_fields branch
    # below, which reports that absence in words instead of numbers.
    baseline_ladder = schema.ladder(baseline, "baseline /api/status")

    # Above the compiled cap, a run is provoking a refusal the firmware is
    # designed to make (api_events.cpp answers the extra stream 503), so it is
    # recorded and carries no verdict: what it measures is admission working,
    # not the transport holding up. At or below the cap is real transport
    # evidence, and exactly AT the cap is that evidence taken at the
    # concurrency the product firmware will actually see -- recorded
    # separately, because the two are different claims. The number itself is
    # resolved from the header and the build environment; see SseClientCap.
    counts_toward_verdict = num_clients <= sse_client_cap.value
    tested_at_production_cap = num_clients == sse_client_cap.value

    # One clock for the whole run, taken before anything starts: the poll series
    # and the early-stall/duration windows are both measured from it, so an
    # elapsed second in the series means the same thing as an elapsed second in
    # the duration.
    run_started = time.monotonic()

    stop_events = [threading.Event() for _ in range(num_clients)]
    first_frame_events = [threading.Event() for _ in range(num_clients)]
    results: list[ClientSoakResult] = []
    # Made here rather than inside each worker so the progress line can read a
    # live frame count out of a running stream. These are the very objects the
    # report is summarised from, so what a line shows at t+2h is what the
    # report will say about t+2h -- not a second count kept alongside.
    trackers = [schema.new_continuity_tracker() for _ in range(num_clients)]
    threads = []
    for i in range(num_clients):
        t = threading.Thread(
            target=_sse_soak_worker,
            args=(client, schema, i, stop_events[i], results, first_frame_events[i],
                  trackers[i]),
            name=f"sse-soak-{i}", daemon=True,
        )
        threads.append(t)
        t.start()

    poll_samples: list[StatusPollSample] = []
    stop_polling = threading.Event()

    def _poll_status() -> None:
        while not stop_polling.is_set():
            elapsed_s = time.monotonic() - run_started
            try:
                body = capture_status(client)
            except TRANSPORT_EXCEPTIONS as error:
                body = {"_pollError": str(error)}
            except json.JSONDecodeError as error:
                body = {"_pollError": f"malformed JSON: {error}"}
            poll_samples.append(StatusPollSample(elapsed_s=elapsed_s, body=body))
            if "_pollError" in body:
                # Complete in the transcript, summarised on the terminal: a
                # controller that goes unreachable for a minute produces a
                # failed poll every few seconds, which is a flood on stderr and
                # precisely the record an operator wants afterwards. The
                # heartbeat carries the running pollsUnreachable count instead.
                monitor.detail(
                    f"sse_soak: status poll at t+{elapsed_s:.1f}s failed: "
                    f"{body['_pollError']}"
                )
            stop_polling.wait(status_poll_interval_s)

    poller = threading.Thread(target=_poll_status, name="sse-soak-status-poll", daemon=True)
    poller.start()

    def _progress(full: bool) -> ProgressSnapshot:
        """This driver's ProgressSource. Nothing here puts a request on the
        wire at all, even when full: the poller thread above is already
        sampling /api/status on its own cadence, so the heartbeat reads its
        most recent sample. Watching a soak therefore does not change it."""
        headline = {
            "frames": sum(tracker.frame_count for tracker in trackers),
            "perClientFrames": [tracker.frame_count for tracker in trackers],
            "polls": len(poll_samples),
            "pollsUnreachable": sum(1 for p in poll_samples if "_pollError" in p.body),
        }
        fields: dict = {}
        latest = poll_samples[-1] if poll_samples else None
        if full and latest is not None:
            # The controller's own count of open SSE clients, next to the
            # number the harness is holding. Two keys rather than one "3/3":
            # the same mapping is what a checkpoint carries, and a machine
            # reading it needs the number, not a rendering of it. Absent (not
            # 0) when this sample carried no count.
            fields["clients"] = _progress_int(latest.body, schema.sse_clients_field)
            fields["clientsHeldOpen"] = num_clients
            fields.update(progress_status_fields(schema, latest.body, admission_floor))
        return monitor.snapshot(
            "sse_soak", elapsed_s=time.monotonic() - run_started, total_s=duration_s,
            headline=headline, fields=fields,
        )

    # The first heartbeat as soon as there is anything to report, rather than
    # one full interval into the run: the defect being fixed here is silence,
    # and 30s of it at the start still reads as a hang.
    monitor.tick(_progress, force=True)

    # Early-fail: if by early_stall_check_s no worker has received even one
    # frame, don't burn the operator's whole --duration on a dead stream. A
    # stream that never starts is a finding on its own, and waiting three hours
    # to report it costs the operator the session.
    early_deadline = run_started + min(early_stall_check_s, duration_s)
    early_window_completed = True
    while not any(e.is_set() for e in first_frame_events):
        remaining_early = early_deadline - time.monotonic()
        if remaining_early <= 0:
            break
        if not monitor.wait(min(remaining_early, 0.2), _progress):
            early_window_completed = False
            break
    # An interrupt inside the early window is not evidence of a stall: the run
    # was stopped before the window it needed had elapsed. Claiming
    # "SSE immediately stalled" there would put a fault on the controller for
    # something the operator did, which is the same class of error as claiming
    # a pass from a truncated run.
    immediate_stall = (
        early_window_completed and not any(e.is_set() for e in first_frame_events)
    )

    if not immediate_stall:
        remaining = duration_s - (time.monotonic() - run_started)
        if remaining > 0:
            monitor.wait(remaining, _progress)
    duration_s_observed = time.monotonic() - run_started
    if monitor.interrupted:
        monitor.line(
            f"sse_soak: interrupt ({monitor.signal_name}) after "
            f"{format_duration(duration_s_observed)} of {format_duration(duration_s)} -- "
            "stopping the readers and reporting what was collected",
            kind="warn",
        )

    for event in stop_events:
        event.set()
    for t in threads:
        t.join(timeout=15.0)
    stop_polling.set()
    poller.join(timeout=status_poll_interval_s + 15.0)

    results.sort(key=lambda r: r.index)

    total_frames = sum(r.frame_count for r in results)
    clients_failed = [r for r in results if r.error is not None]
    zero_frame_clients = [r for r in results if r.frame_count == 0]
    continuity_fields, continuity_reasons = schema.summarize_continuity(
        [r.tracker for r in results], max_silence_s
    )

    reachable_polls = [p for p in poll_samples if "_pollError" not in p.body]
    reachable_samples = [p.body for p in reachable_polls]
    schema_anomalies: list[str] = []
    restart_marker_samples = schema.collect_restart_markers(reachable_samples, schema_anomalies)
    # The per-poll heap series, and the aggregates derived from it. Recorded so
    # the SHAPE of a run is recoverable: #194's graded run could say the largest
    # block reached 11 764 and could not say whether it touched that once or sat
    # near it for twenty minutes, and those are different findings.
    heap_series = schema.collect_heap_series(reachable_polls, schema_anomalies)
    heap_samples = series_values(heap_series, SERIES_KEY_LARGEST_FREE_8BIT)
    sse_clients_samples = series_values(heap_series, SERIES_KEY_SSE_CLIENTS)
    ladder_samples = schema.collect_ladder(reachable_samples, schema_anomalies)
    admission_samples = schema.collect_admission(reachable_samples, schema_anomalies)

    reasons: list[str] = []

    restart_detected = False
    final_restart_marker = baseline_restart_marker
    if restart_marker_samples:
        final_restart_marker = restart_marker_samples[-1]
        restart_detected = schema.restart_detected(baseline_restart_marker, restart_marker_samples)
    elif reachable_samples:
        reasons.append(f"no /api/status poll sample carried a valid {schema.restart_field} -- cannot "
                        "confirm the P4 did not reboot during the soak")

    min_heap = min([baseline_heap] + heap_samples) if heap_samples else baseline_heap

    # -- the heap verdict -------------------------------------------------
    #
    # Two questions, neither of which is "did a spiky reading move away from an
    # arbitrary sample":
    #
    #   did the controller refuse?  refusedHeapFloor / refusedHeapFloorDiag are
    #       the firmware's own count of requests it turned away at the floor. A
    #       rise across the run is the real failure this rule exists to catch,
    #       and the percentage rule it replaces would not have noticed it at
    #       all -- #194's run failed on the percentage with both counters at 0.
    #   how close did it come?      the lowest largest-free-8-bit block observed,
    #       against the ORDINARY floor -- the level at which an operator's page
    #       load starts being shed. Below it is a FAIL; above it is a margin,
    #       reported with no second band invented in between.
    #
    # Neither question exists on an image with no admission guard; that absence
    # is reported in words rather than as a set of passing numbers.
    heap_verdict_fields: dict = {}
    if admission_floor is None:
        heap_verdict_fields["admissionFloorEvidence"] = schema.admission_absence_note
    else:
        floor = admission_floor.ordinary_bytes
        margin = min_heap - floor
        heap_verdict_fields.update({
            "admissionFloorEnv": admission_floor.env,
            "admissionOrdinaryFloorBytes": floor,
            "admissionDiagnosticFloorBytes": admission_floor.diagnostic_bytes,
            "minLargestFreeBlockMarginBytes": margin,
            # Margin as a share of the floor, which is a real denominator (the
            # level the firmware acts on) rather than a baseline sample.
            "minLargestFreeBlockMarginPct": round(100.0 * margin / floor, 1),
        })
        if margin < 0:
            reasons.append(
                f"{schema.heap_field} fell to {min_heap}, below the {floor}-byte admission "
                f"floor this image refuses ordinary requests at "
                f"({ADMISSION_FLOOR_MACRO}, resolved for build environment "
                f"{admission_floor.env!r} from "
                f"{admission_floor.sources[ADMISSION_FLOOR_MACRO]}) -- an operator's page "
                "load would have been shed at that point"
            )

    if baseline_admission is not None and admission_samples is not None:
        for label, field, report_keys, baseline_count, counts in (
            ("ordinary requests", schema.refused_heap_floor_field,
             ("baselineRefusedHeapFloor", "finalRefusedHeapFloor",
              "refusedHeapFloorAdvancedBy"),
             baseline_admission.refused_heap_floor, admission_samples.refused_heap_floor),
            ("read-only diagnostics", schema.refused_heap_floor_diag_field,
             ("baselineRefusedHeapFloorDiag", "finalRefusedHeapFloorDiag",
              "refusedHeapFloorDiagAdvancedBy"),
             baseline_admission.refused_heap_floor_diag,
             admission_samples.refused_heap_floor_diag),
        ):
            final_count = counts[-1] if counts else baseline_count
            advanced_by = final_count - baseline_count
            baseline_key, final_key, advanced_key = report_keys
            heap_verdict_fields[baseline_key] = baseline_count
            heap_verdict_fields[final_key] = final_count
            heap_verdict_fields[advanced_key] = advanced_by
            if advanced_by > 0:
                reasons.append(
                    f"{field} advanced by {advanced_by} during the soak ({baseline_count} -> "
                    f"{final_count}): the controller refused {advanced_by} {label} at its "
                    "heap floor. This is the firmware's own count of work it turned away"
                )
            elif advanced_by < 0:
                # Cumulative and monotonic within a boot, so this cannot happen
                # without the counters being reset. Reported rather than read as
                # "no refusals": a run whose refusal evidence is not comparable
                # end to end has no refusal evidence.
                reasons.append(
                    f"{field} went backwards during the soak ({baseline_count} -> "
                    f"{final_count}). It is cumulative within a boot, so the counters were "
                    "reset -- this run's refusal evidence is not comparable end to end"
                )
        # The guard's own running minimum, since boot rather than since this
        # run, so it is corroboration and never a verdict of its own: a low
        # value may predate the soak entirely. On #194's graded run it read
        # 11 764, exactly what the harness saw, which is what made that
        # measurement trustworthy in the first place.
        # The LAST sample, whatever it says -- including None, which after a
        # real reading means the guard's state was reset. Skipping to the last
        # non-None would report a reading the controller is no longer making.
        seen_series = admission_samples.accept_min_largest_block_seen
        final_seen = (
            seen_series[-1] if seen_series
            else baseline_admission.accept_min_largest_block_seen
        )
        heap_verdict_fields.update({
            "baselineAcceptMinLargestBlockSeen":
                baseline_admission.accept_min_largest_block_seen,
            "finalAcceptMinLargestBlockSeen": final_seen,
            "acceptMinLargestBlockSeenNote": (
                "The Connection Admission guard's own low-water reading, since BOOT and not "
                "since this run, so a value below the floor here is not by itself evidence "
                "about this soak. null means the guard has not sampled the heap even once "
                "this boot (published as -1, src/web/web_server.cpp:425-427) -- no reading, "
                "never zero bytes"
            ),
        })

    max_sse_clients_observed = max(sse_clients_samples, default=0)
    admission_reached_target = max_sse_clients_observed >= num_clients

    # An image with no ladder cannot have reached 'degraded' -- and must not
    # be reported as having stayed out of it either, which is what a bare
    # `"degraded" in ...` over an empty list would say.
    ladder_reached_degraded = (
        ladder_samples is not None and "degraded" in ladder_samples.states
    )

    if ladder_samples is None or baseline_ladder is None:
        # Reported as a stated absence, never as a set of zeroes: a report
        # carrying recoveryLadderReachedDegraded: false would claim this run
        # watched a ladder the image does not publish.
        ladder_fields = {"recoveryLadderEvidence": schema.ladder_absence_note}
    else:
        # transport_active_cb() only ever increments; a rise during the soak
        # means the SDIO link independently reported at least one additional
        # active transition (recovery ladder or otherwise) beyond the initial
        # boot-time connect captured in the baseline -- not itself a FAIL
        # condition (a ladder that fires and recovers without reaching
        # 'degraded' is the ladder working as designed), but the corroborating
        # count #184 named this field for.
        transport_up_event_count_end = (
            ladder_samples.transport_up_event_counts[-1]
            if ladder_samples.transport_up_event_counts
            else baseline_ladder.transport_up_event_count
        )
        transport_up_events_during_soak = (
            transport_up_event_count_end - baseline_ladder.transport_up_event_count
        )
        ladder_fields = {
            "recoveryLadderReachedDegraded": ladder_reached_degraded,
            "recoveryLadderStatesObserved": sorted(set(ladder_samples.states)),
            "baselineRecoveryLadderState": baseline_ladder.state,
            "baselineHostedTransportUpEventCount": baseline_ladder.transport_up_event_count,
            "finalHostedTransportUpEventCount": transport_up_event_count_end,
            "hostedTransportUpEventCountAdvancedBy": transport_up_events_during_soak,
        }

    if immediate_stall:
        reasons.append(
            f"SSE immediately stalled: no client received a frame within {early_stall_check_s}s"
        )
    if total_frames == 0:
        reasons.append("total_frames_received == 0 (measured nothing)")
    if zero_frame_clients:
        reasons.append(
            f"{len(zero_frame_clients)} client(s) received zero frames: "
            f"{[r.index for r in zero_frame_clients]}"
        )
    reasons.extend(continuity_reasons)
    if clients_failed:
        reasons.append(
            f"{len(clients_failed)} client(s) reported a transport fault: "
            f"{[(r.index, r.error) for r in clients_failed]}"
        )
    if restart_detected:
        reasons.append(
            f"{schema.restart_field} {schema.restart_verb} from {baseline_restart_marker} to "
            f"{final_restart_marker} during the soak (the P4 rebooted; ADR 0032 forbids "
            "relying on a host restart)"
        )
    if baseline_reset.crash_shaped:
        reasons.append(
            f"resetReason at soak start was {baseline_reset.display} -- "
            "the device was already in a crash-shaped reset state before this run began"
        )
    if counts_toward_verdict and not admission_reached_target and total_frames > 0:
        reasons.append(
            f"server-reported {schema.sse_clients_field} never reached {num_clients} "
            f"(max observed {max_sse_clients_observed}) though the harness held "
            f"{num_clients} connection(s) open"
        )
    if ladder_reached_degraded:
        reasons.append(
            f"{schema.ladder_fields['state']} reached 'degraded' during the soak -- the bounded "
            "transport-failure recovery ladder exhausted its attempts and is terminal "
            "for this boot by design"
        )

    # A truncated soak reports its truncation first and carries every reason it
    # did observe underneath it. A partial result is where "verification that
    # cannot judge itself" comes back: neither a PASS nor a FAIL may be claimed
    # from a window the operator cut short. PASS is obvious; FAIL matters just
    # as much, because most of the reasons above are absence-shaped
    # ("sseClients never reached the cap", "no poll carried a bootCount") and a
    # run stopped after 20 seconds would produce them against a healthy board.
    if monitor.interrupted:
        reasons.insert(0, monitor.truncation_reason(duration_s_observed, duration_s))
        verdict = VERDICT_INTERRUPTED_DRIVER
    elif not counts_toward_verdict:
        verdict = VERDICT_OBSERVATION_ONLY
    else:
        verdict = VERDICT_FAIL if reasons else VERDICT_PASS

    # Report-key convention, and the reason it is not uniform: heap and SSE
    # client keys keep one harness-level name across both images because both
    # images publish the SAME quantity under different names
    # (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), and the count of
    # open streams) -- statusFieldsRead above says which field each reading
    # actually came from. The restart evidence does NOT get that treatment:
    # bootCount is an RTC counter and uptimeMs is a millisecond clock, so
    # calling an uptime reading "baselineBootCount" would be an invented
    # measurement rather than a renamed one. Each image's restart keys are
    # therefore its own (schema.restart_report()).
    report = {
        "driver": "sse_soak",
        "verdict": verdict,
        "image": schema.name,
        "statusFieldsRead": schema.fields_read(),
        "countsTowardVerdict": counts_toward_verdict,
        "testedAtProductionCap": tested_at_production_cap,
        # The number those two booleans were decided against, beside them
        # rather than only in the run header: a reader holding one driver's
        # report has to be able to see what "at the cap" meant for this run.
        "sseClientCap": sse_client_cap.value,
        "sseClientCapReadFrom": sse_client_cap.source,
        "reasons": reasons,
        "numClientsRequested": num_clients,
        "durationSRequested": duration_s,
        # Empty on a run that finished, so the completed-run report is
        # unchanged; on an interrupted one it carries the duration actually
        # observed next to the duration requested (durationSRequested, right
        # above), so no consumer can read a truncated run as a completed one.
        **monitor.truncation_fields(duration_s_observed),
        "immediateStall": immediate_stall,
        "totalFramesReceived": total_frames,
        "clientsFailed": len(clients_failed),
        "perClient": [
            dict(
                {
                    "index": r.index, "frameCount": r.frame_count,
                    "error": r.error, "truncated": r.truncated, "connectOk": r.connect_ok,
                },
                **r.tracker.per_client_fields(),
            )
            for r in results
        ],
        "baselineResetReason": baseline_reset.display,
        "baselineLargestFree8bitBlock": baseline_heap,
        "minLargestFree8bitBlockObserved": min_heap,
        # Either the floor verdict and the refusal counters, or the one stated
        # absence -- inlined here rather than update()d on afterwards so the
        # report's key order is the same on every image.
        **heap_verdict_fields,
        "maxSseClientsConnectedObserved": max_sse_clients_observed,
        "admissionReachedTarget": admission_reached_target,
        **ladder_fields,
        "statusPollSampleCount": len(poll_samples),
        "statusPollUnreachableCount": len(poll_samples) - len(reachable_polls),
        # The list is truncated; the count is not. A capped list on its own
        # cannot distinguish 50 anomalies from 5000.
        "statusPollSchemaAnomalyCount": len(schema_anomalies),
        "statusPollSchemaAnomalies": schema_anomalies[:50],
        # Uncapped by design: a truncated series would answer "how low did it
        # go" and lose "for how long", which is the question this record exists
        # to answer. Keys are named by SERIES_KEY_*; statusFieldsRead says which
        # payload field each one was read from on this image.
        "heapSeries": heap_series,
        "note": (
            "Per-client frameCount is not cross-checked against the server's own frame "
            "counter as an independent pass/fail gate: clients connect at different "
            "points in a shared stream, so raw counts are not directly comparable across "
            "different connection windows (an accepted 25-minute run showed exactly this: "
            "client counts of 1401/1403/1399 against a server delta of 1397). The "
            "continuity model named in sseContinuityModel is the authoritative per-client "
            "check; the per-client fields let an operator do the cross-check by hand."
        ),
    }
    report.update(continuity_fields)
    report.update(schema.restart_report(baseline_restart_marker, final_restart_marker, restart_detected))
    # Never emitted as False when the image cannot tell: resetReasonName()
    # collapses several distinct reasons into "OTHER" on the shipping image,
    # so an unknown assessment omits the boolean entirely and carries the
    # reason it is unknown instead.
    if baseline_reset.crash_shaped is None:
        report["baselineResetReasonAssessment"] = "unknown"
        report["baselineResetReasonCaveat"] = baseline_reset.caveat
    else:
        report["baselineResetReasonBad"] = baseline_reset.crash_shaped
        report["baselineResetReasonAssessment"] = (
            "crashShaped" if baseline_reset.crash_shaped else "notCrashShaped"
        )
    return report


# ---------------------------------------------------------------------------
# Driver 2 -- reconnect storm: concurrent clients repeatedly connect to
# /api/events and abort mid-stream.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class StormCycleResult:
    """One connect-hold-abort cycle, and what it is entitled to conclude.

    `liveness` is the whole reason this class is not just a pair of counters.
    A cycle that held an open stream for less than the silence budget saw too
    small a window to say anything about a stream that ticks about once a
    second, so its liveness is Not Assessed -- never "fine". A cycle that
    watched one for at least the budget and received nothing is SILENT, and
    that is a real stall. A cycle that received a frame DELIVERED. What the
    storm did not observe reads as not measured; a Soak Driver that passes
    because it stopped looking is worse than one that fails a healthy board.
    """

    connect_ok: bool
    frame_count: int
    error: Optional[str]
    status_code: Optional[int]
    # The window this cycle intended to hold the stream open for, and the
    # window it actually observed (shorter when the peer closed first, longer
    # when a read was still blocked when the hold expired).
    hold_s: float
    observed_s: float
    liveness: str
    # True when the read window expired. Distinguished from `error` so a
    # timeout that is merely this cycle's window closing is never counted as a
    # transport fault -- see run_reconnect_storm().
    read_timed_out: bool


# What one storm cycle is entitled to say about the stream staying alive.
LIVENESS_DELIVERED = "delivered"
LIVENESS_SILENT = "silent"
LIVENESS_NOT_ASSESSED = "notAssessed"


def _storm_cycle_liveness(
    result: SseStreamResult, frame_count: int, max_silence_s: float,
) -> str:
    """Classify one cycle's liveness evidence. See StormCycleResult."""
    if not result.connect_ok or result.status_code != 200:
        # Nothing was ever open to observe. Whether that is a fault is the
        # connect/capacity question, judged separately.
        return LIVENESS_NOT_ASSESSED
    if frame_count > 0:
        return LIVENESS_DELIVERED
    if result.elapsed_s >= max_silence_s:
        return LIVENESS_SILENT
    return LIVENESS_NOT_ASSESSED


def _storm_hold_seconds(
    cycle_index: int, cycle_min_s: float, cycle_max_s: float,
    max_silence_s: float, liveness_cycle_every: int,
) -> float:
    """How long cycle number `cycle_index` (0-based, per worker) holds.

    Most cycles hold a random short window, which is what makes this a storm.
    Every liveness_cycle_every-th cycle holds longer than the silence budget
    instead, so that receiving no frame during it is a stall rather than an
    unremarkable short look -- without such a cycle the storm can never
    conclude anything about the stream staying alive, and a driver that cannot
    fail is not a measurement.

    The long hold is derived from the arguments already given
    (max_silence_s + cycle_max_s) rather than from a factor invented here: it
    is one full silence budget plus one full ordinary cycle, so the budget can
    elapse inside the hold with room for the connect.
    """
    if liveness_cycle_every > 0 and cycle_index % liveness_cycle_every == 0:
        return max_silence_s + cycle_max_s
    return random.uniform(cycle_min_s, cycle_max_s)


def _storm_worker(
    client: BenchClient, stop_all: threading.Event,
    cycle_min_s: float, cycle_max_s: float, cycles: list[StormCycleResult],
    max_silence_s: float, liveness_cycle_every: int,
) -> None:
    cycle_index = 0
    while not stop_all.is_set():
        hold_s = _storm_hold_seconds(
            cycle_index, cycle_min_s, cycle_max_s, max_silence_s, liveness_cycle_every)
        cycle_index += 1
        cycle_stop = threading.Event()

        def _timer(cycle_stop: threading.Event = cycle_stop, hold_s: float = hold_s) -> None:
            stop_all.wait(hold_s)
            cycle_stop.set()

        timer_thread = threading.Thread(target=_timer, daemon=True)
        timer_thread.start()

        frame_count_holder = [0]

        def on_frame(_frame: SseFrame, _ts: float, holder: list[int] = frame_count_holder) -> None:
            holder[0] += 1

        # The read window IS the silence budget, not the abort granularity.
        # It used to be a fixed 0.25s so that an abort landed close to hold_s,
        # and stream_sse() treats an expired window as a stalled stream -- so
        # every stream slower than 4 Hz was classified as stalled. Both the
        # shipping /api/events tick and the bench sketch emit about one frame a
        # second, and a device run of this driver duly failed the whole run
        # with 218 stalls out of 218 cycles against a board that was fine.
        #
        # Abort promptness does not need the short window: the read loop checks
        # `stop` after every chunk, so on a live 1 Hz stream it notices within
        # about one frame interval. Only a stream that has actually gone quiet
        # keeps a read blocked for the whole budget, and that is precisely the
        # case worth waiting for. The loop cannot simply carry on past an
        # expiry instead -- socket.SocketIO latches _timeout_occurred and
        # refuses every later read on that socket (CPython Lib/socket.py).
        result = client.stream_sse(
            DEFAULT_SSE_PATH, on_frame, cycle_stop,
            read_chunk_timeout_s=max_silence_s, abrupt_stop=True,
        )
        timer_thread.join(timeout=2.0)
        cycles.append(StormCycleResult(
            connect_ok=result.connect_ok, frame_count=frame_count_holder[0], error=result.error,
            status_code=result.status_code, hold_s=hold_s, observed_s=result.elapsed_s,
            liveness=_storm_cycle_liveness(result, frame_count_holder[0], max_silence_s),
            read_timed_out=result.read_timed_out,
        ))


def run_reconnect_storm(
    client: BenchClient, schema: StatusSchema, storm_clients: int, duration_s: float,
    cycle_min_s: float, cycle_max_s: float, settle_s: float, heap_tolerance_pct: float,
    max_silence_s: float, liveness_cycle_every: int,
    monitor: Optional[RunMonitor] = None,
) -> dict:
    monitor = monitor or RunMonitor.disabled()
    baseline = capture_status(client)
    baseline_sse_clients = schema.sse_clients(baseline, "baseline /api/status")
    baseline_heap = schema.heap_largest_free(baseline, "baseline /api/status")
    baseline_restart_marker = schema.restart_marker(baseline, "baseline /api/status")

    stop_all = threading.Event()
    cycles_by_worker: list[list[StormCycleResult]] = [[] for _ in range(storm_clients)]
    threads = []
    for i in range(storm_clients):
        t = threading.Thread(
            target=_storm_worker,
            args=(client, stop_all, cycle_min_s, cycle_max_s, cycles_by_worker[i],
                  max_silence_s, liveness_cycle_every),
            name=f"reconnect-storm-{i}", daemon=True,
        )
        threads.append(t)
        t.start()

    storm_started = time.monotonic()

    def _progress(full: bool) -> ProgressSnapshot:
        """This driver's ProgressSource. Cycles are its natural unit.

        Unlike the soak, this driver keeps no status poller of its own, so the
        controller-side half costs one extra GET /api/status -- but only on a
        heartbeat (full), never on the once-a-second status line. Four extra
        requests across a two-minute storm is negligible against a storm that
        opens and RSTs a stream every 0.5-3s per worker, and it is the only
        way the recovery ladder and the refusal counters are visible while the
        storm is running rather than only after it.
        """
        # A copy per worker: these lists are appended to by their own worker
        # thread while this reads them, and a copy cannot see a half-built
        # view of one.
        observed = [cycle for worker in cycles_by_worker for cycle in list(worker)]
        headline: dict = {
            "cycles": len(observed),
            "streamsOpened": sum(1 for c in observed if c.status_code == 200),
            "capacityRefusals": sum(
                1 for c in observed
                if schema.enforces_sse_client_cap and c.status_code == 503
            ),
            "connectFailures": sum(1 for c in observed if not c.connect_ok),
            "frames": sum(c.frame_count for c in observed),
            # The liveness census as it stands, so an operator can see the
            # storm accumulating evidence rather than only learn at the end
            # whether it had any.
            "delivered": sum(1 for c in observed if c.liveness == LIVENESS_DELIVERED),
            "silent": sum(1 for c in observed if c.liveness == LIVENESS_SILENT),
            "notAssessed": sum(
                1 for c in observed if c.liveness == LIVENESS_NOT_ASSESSED),
        }
        fields: dict = {}
        if full:
            try:
                body = capture_status(client)
            except TRANSPORT_EXCEPTIONS as error:
                # Reported, never absorbed: a controller that has stopped
                # answering /api/status mid-storm is exactly what an operator
                # is watching for, and the storm carries on either way.
                fields["statusPoll"] = f"unreachable ({error})"
                monitor.detail(f"reconnect_storm: status poll unreachable: {error}")
            except json.JSONDecodeError as error:
                fields["statusPoll"] = f"not JSON ({error})"
                monitor.detail(f"reconnect_storm: status poll was not JSON: {error}")
            else:
                fields["clients"] = _progress_int(body, schema.sse_clients_field)
                fields.update(progress_status_fields(schema, body))
        return monitor.snapshot(
            "reconnect_storm", elapsed_s=time.monotonic() - storm_started,
            total_s=duration_s, headline=headline, fields=fields,
        )

    monitor.tick(_progress, force=True)
    monitor.wait(duration_s, _progress)
    duration_s_observed = time.monotonic() - storm_started
    if monitor.interrupted:
        monitor.line(
            f"reconnect_storm: interrupt ({monitor.signal_name}) after "
            f"{format_duration(duration_s_observed)} of {format_duration(duration_s)} -- "
            "stopping the workers and reporting the cycles already completed",
            kind="warn",
        )
    stop_all.set()
    for t in threads:
        t.join(timeout=cycle_max_s + 10.0)

    # The settle window still runs after an interrupt: the post-storm readings
    # it makes possible (did the client count come back, did the heap recover)
    # are the storm's own evidence, and skipping them to exit a second sooner
    # would throw away the measurement the interrupt was trying to preserve. A
    # second signal kills the process outright (RunMonitor.install_signal_
    # handlers), so this is never an unkillable wait.
    time.sleep(settle_s)
    post = capture_status(client)
    post_sse_clients = schema.sse_clients(post, "post-storm /api/status")
    post_heap = schema.heap_largest_free(post, "post-storm /api/status")
    post_restart_marker = schema.restart_marker(post, "post-storm /api/status")

    all_cycles = [c for worker_cycles in cycles_by_worker for c in worker_cycles]
    total_frames = sum(c.frame_count for c in all_cycles)
    connect_failures = [c for c in all_cycles if not c.connect_ok]
    # A stream refused at the client cap is not a transport fault: the
    # shipping image answers 503 once PA_ADMISSION_MAX_SSE_CLIENTS streams
    # are open (src/web/api_events.cpp), and a storm run at the cap races its
    # own closing sockets, so some cycles legitimately arrive one slot short.
    # Counted separately rather than excused: a run in which the cap refused
    # EVERY cycle measured no reconnect at all, which the zero-stream reason
    # below catches, and a cap that stops releasing slots shows up as the
    # leaked-socket reason.
    def is_capacity_refusal(cycle: StormCycleResult) -> bool:
        return schema.enforces_sse_client_cap and cycle.status_code == 503

    capacity_refusals = [c for c in all_cycles if is_capacity_refusal(c)]
    # A read window that expired is NOT a transport fault. It is this cycle's
    # silence budget elapsing, and whether that means the stream stalled is the
    # liveness question below -- judged from how long the cycle actually
    # watched, not from the fact that a socket timeout fired. Conflating the
    # two is what made this driver fail a healthy 1 Hz stream.
    unexpected_errors = [
        c for c in all_cycles
        if c.connect_ok and c.error is not None and not is_capacity_refusal(c)
        and not c.read_timed_out
    ]
    streams_opened = [c for c in all_cycles if c.status_code == 200]

    # What the storm is entitled to say about the stream staying alive. Every
    # cycle lands in exactly one bucket, and the third one is the point: a
    # cycle that watched for less than one silence budget saw too small a
    # window to judge a stream that ticks about once a second, so it says
    # nothing rather than saying "fine".
    delivered = [c for c in all_cycles if c.liveness == LIVENESS_DELIVERED]
    silent = [c for c in all_cycles if c.liveness == LIVENESS_SILENT]
    not_assessed = [c for c in all_cycles if c.liveness == LIVENESS_NOT_ASSESSED]
    # Cycles deliberately held longer than the budget, so that receiving
    # nothing during one is a stall rather than an unremarkable short look.
    liveness_holds = [c for c in all_cycles if c.hold_s >= max_silence_s]

    heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)
    restart_detected = schema.restart_detected(baseline_restart_marker, [post_restart_marker])

    reasons: list[str] = []
    if not all_cycles:
        reasons.append("reconnect storm completed zero cycles (measured nothing)")
    elif not streams_opened:
        reasons.append(
            f"none of {len(all_cycles)} cycle(s) opened an SSE stream "
            f"({len(capacity_refusals)} refused at the client cap, "
            f"{len(connect_failures)} never connected) -- measured no reconnect at all"
        )
    if connect_failures:
        reasons.append(
            f"{len(connect_failures)} of {len(all_cycles)} cycle(s) failed to establish "
            "the SSE connection at all"
        )
    if unexpected_errors:
        reasons.append(
            f"{len(unexpected_errors)} cycle(s) reported a transport fault during a "
            f"deliberate hold window, before the harness aborted it: "
            f"{[c.error for c in unexpected_errors][:5]}"
        )
    if silent:
        reasons.append(
            f"{len(silent)} cycle(s) held an open SSE stream for at least {max_silence_s}s "
            f"and received no frame at all (longest watched {max(c.observed_s for c in silent):.1f}s) "
            "-- the stream stopped delivering while it was held open"
        )
    elif streams_opened and not delivered:
        # The vacuity guard, and the reason this driver cannot pass by having
        # stopped looking: not one cycle watched an open stream long enough to
        # see a frame OR long enough for its absence to mean anything, so this
        # run has no evidence either way about the stream surviving the storm.
        reasons.append(
            f"no cycle produced any liveness evidence: {len(streams_opened)} stream(s) were "
            f"opened but none received a frame and none was held for the {max_silence_s}s "
            "silence budget, so this run has not shown the stream survives the storm. Raise "
            "--storm-cycle-max-s, lower --storm-max-silence-s, or set "
            "--storm-liveness-cycle-every to a non-zero value"
        )
    if post_sse_clients > baseline_sse_clients:
        reasons.append(
            f"{schema.sse_clients_field} did not return to baseline after settling "
            f"({post_sse_clients} > baseline {baseline_sse_clients}) -- leaked socket(s)"
        )
    if post_heap < heap_floor:
        reasons.append(
            f"{schema.heap_field} did not recover within tolerance after the storm "
            f"({post_heap} < floor {heap_floor:.0f}, baseline {baseline_heap})"
        )
    if restart_detected:
        reasons.append(
            f"{schema.restart_field} {schema.restart_verb} from {baseline_restart_marker} to "
            f"{post_restart_marker} during the reconnect storm (the P4 rebooted)"
        )

    # Same rule as the soak, and for the same reason: a storm stopped after
    # 20 of its 120 seconds has neither passed nor failed. Several of the
    # reasons above are absence-shaped ("zero cycles", "none opened a
    # stream"), so a truncated run would manufacture them against a healthy
    # controller.
    if monitor.interrupted:
        reasons.insert(0, monitor.truncation_reason(duration_s_observed, duration_s))
        verdict = VERDICT_INTERRUPTED_DRIVER
    else:
        verdict = VERDICT_FAIL if reasons else VERDICT_PASS
    report = {
        "driver": "reconnect_storm",
        "verdict": verdict,
        "image": schema.name,
        "statusFieldsRead": schema.fields_read(),
        "reasons": reasons,
        "stormClients": storm_clients,
        "durationSRequested": duration_s,
        # Empty unless the storm was cut short -- see run_sse_soak().
        **monitor.truncation_fields(duration_s_observed),
        "cycleMinS": cycle_min_s,
        "cycleMaxS": cycle_max_s,
        "cycleCount": len(all_cycles),
        "streamsOpened": len(streams_opened),
        "capacityRefusals": len(capacity_refusals),
        "totalFramesReceived": total_frames,
        "connectFailures": len(connect_failures),
        "unexpectedErrorsDuringHold": len(unexpected_errors),
        # Which cycles carried liveness evidence, and which could not. The
        # three counts always sum to cycleCount, so a reader can see at a
        # glance how much of the run was actually able to judge the stream.
        "sseMaxSilenceLimitS": max_silence_s,
        "livenessHoldCycleEvery": liveness_cycle_every,
        "livenessHoldCycles": len(liveness_holds),
        "cyclesDeliveredFrames": len(delivered),
        "cyclesSilentPastBudget": len(silent),
        "cyclesLivenessNotAssessed": len(not_assessed),
        "livenessNotAssessedNote": (
            "Not Assessed: a cycle that held an open stream for less than the silence budget "
            "watched too small a window to judge a stream that ticks about once a second, so "
            "it records no liveness result at all. It is still judged on what it CAN judge -- "
            "whether it connected, whether the cap refused it, and whether it tore down "
            "cleanly. What was not observed reads as not measured, never as healthy, and a "
            "run in which nothing was assessed fails rather than passes"
        ),
        "readWindowExpiries": sum(1 for c in all_cycles if c.read_timed_out),
        "longestCycleObservedS": round(
            max((c.observed_s for c in all_cycles), default=0.0), 3),
        "baselineSseClientsConnected": baseline_sse_clients,
        "postSseClientsConnected": post_sse_clients,
        "baselineLargestFree8bitBlock": baseline_heap,
        "postLargestFree8bitBlock": post_heap,
        "heapTolerancePct": heap_tolerance_pct,
    }
    report.update(
        schema.restart_report(baseline_restart_marker, post_restart_marker, restart_detected)
    )
    return report


# ---------------------------------------------------------------------------
# Driver 3 -- C6 reset recovery: schedule an abrupt C6 reset, prove
# host-side rejoin without a P4 restart, prove a fresh SSE stream resumes.
# ---------------------------------------------------------------------------


def run_c6_reset_recovery(
    client: BenchClient, schema: StatusSchema, recovery_timeout_s: float, poll_interval_s: float,
    heap_tolerance_pct: float, sse_resume_timeout_s: float,
    monitor: Optional[RunMonitor] = None,
) -> dict:
    monitor = monitor or RunMonitor.disabled()
    # Refused here, before any request is sent, rather than skipped by the
    # orchestrator: this driver's whole claim is that it provoked a WiFi Module
    # reset and watched the link come back, and on an image with no reset route
    # there is nothing to provoke. A skip would leave a driver that can report
    # a pass without having measured anything -- verification that cannot judge
    # itself, which is the failure this whole harness was rebuilt to avoid.
    if schema.reset_path is None:
        return {
            "driver": "c6_reset_recovery",
            "verdict": VERDICT_UNAVAILABLE,
            "image": schema.name,
            # The schema's own words: "no route on a board that has a C6" and
            # "no C6 to reset" are different facts, and an operator reading
            # one about the other board is being misinformed.
            "reasons": [schema.reset_unavailable_reason],
        }

    baseline = capture_status(client)
    baseline_restart_marker = schema.restart_marker(baseline, "baseline /api/status")
    baseline_reset = schema.reset_reason(baseline, "baseline /api/status")
    baseline_heap = schema.heap_largest_free(baseline, "baseline /api/status")
    # Recovery-ladder baseline. This driver deliberately provokes
    # ESP_HOSTED_EVENT_TRANSPORT_FAILURE through the reset it is about to
    # trigger, so it is the one place in this harness where these fields carry
    # the most evidence -- everywhere else the ladder is watched rather than
    # exercised. Types read from
    # bringup/p4_hosted_bench.cpp:937-957 -- recoveryLadderState is
    # recoveryPhaseName()'s const char* (idle/armed/attempting/degraded),
    # the rest are unsigned int counters.
    #
    # Not Optional here, unlike in run_sse_soak(): the refusal above already
    # returned for every image with no reset route, and an image that CAN
    # provoke a transport failure necessarily publishes the ladder that
    # failure drives. That coupling is pinned by a test rather than left to
    # this comment (test/test_tools/test_soak_schema.py pins reset_path
    # implies publishes_recovery_ladder) -- a schema that broke it would
    # deref None right here.
    baseline_ladder = schema.ladder(baseline, "baseline /api/status")

    # `is not False`, not `is True`: an image whose reset-reason mapping
    # cannot classify the value it published has not shown a clean starting
    # point, and "unknown" must not pass a precondition that "crash-shaped"
    # would fail.
    if baseline_reset.crash_shaped is not False:
        return {
            "driver": "c6_reset_recovery",
            "verdict": VERDICT_INVALID,
            "image": schema.name,
            "reasons": [
                "device was not in a confirmed-clean reset state "
                f"({baseline_reset.display}) before this test began -- required evidence "
                "(a clean starting point) is missing"
                + (f". {baseline_reset.caveat}" if baseline_reset.caveat else "")
            ],
        }

    status_code, reset_body = client.post_json(schema.reset_path)

    if status_code in (503, 409):
        reason = reset_body.get("reason", "<no reason field>")
        return {
            "driver": "c6_reset_recovery",
            "verdict": VERDICT_INVALID,
            "reasons": [
                f"reset was rejected ({status_code}): {reason} -- required evidence (a "
                "scheduled and executed reset) is missing, so this driver has nothing to "
                "judge rather than something to fail"
            ],
            "rejectionStatus": status_code,
            "rejectionReason": reason,
        }
    if status_code != 202:
        return {
            "driver": "c6_reset_recovery",
            "verdict": VERDICT_FAIL,
            "reasons": [f"POST /api/c6/reset returned unexpected status {status_code}: {reset_body}"],
        }

    reset_scheduled = _require_field(reset_body, "resetScheduled", bool, "POST /api/c6/reset response")
    if not reset_scheduled:
        return {
            "driver": "c6_reset_recovery",
            "verdict": VERDICT_INVALID,
            "reasons": [f"202 response did not set resetScheduled: true: {reset_body}"],
        }
    response_grace_ms = _require_field(reset_body, "responseGraceMs", int, "POST /api/c6/reset response")

    # The 202 proves scheduling, not an edge (#184: "A harness that treats
    # the response as 'the pulse has happened' will mis-time every recovery
    # measurement"). GPIO54 does not fall until responseGraceMs after this.
    request_accepted_at = time.monotonic()

    recovered = False
    recovery_reasons: list[str] = []
    saw_unreachable = False
    poll_schema_anomalies: list[str] = []
    ladder_status_samples: list[dict] = []
    last_status: Optional[dict] = None
    # Empty until a poll actually reached the device: a recovery window in
    # which /api/status never answered has no readiness evidence to quote.
    link_why_not = ""
    deadline = request_accepted_at + recovery_timeout_s

    def _progress(full: bool) -> ProgressSnapshot:
        """This driver's ProgressSource. No request goes out for it even when
        full: the recovery loop below already polls /api/status every
        poll_interval_s, so the heartbeat reads its most recent sample."""
        headline: dict = {
            "recovered": recovered,
            "polls": len(ladder_status_samples),
            "sawUnreachable": saw_unreachable,
        }
        fields = progress_status_fields(schema, last_status) if full else {}
        return monitor.snapshot(
            "c6_reset_recovery", elapsed_s=time.monotonic() - request_accepted_at,
            # A budget, not a plan: recovering at 4s of a 30s window is the
            # GOOD outcome, so the clock is shown as a timeout counting down to
            # giving up, never as a percentage of progress toward completion.
            total_s=recovery_timeout_s, total_kind="timeout",
            headline=headline, fields=fields,
        )

    monitor.tick(_progress, force=True)
    while time.monotonic() < deadline:
        if monitor.interrupted:
            break
        try:
            last_status = capture_status(client)
        except TRANSPORT_EXCEPTIONS:
            saw_unreachable = True
            monitor.wait(poll_interval_s, _progress)
            continue
        except json.JSONDecodeError:
            saw_unreachable = True
            monitor.wait(poll_interval_s, _progress)
            continue

        # Recorded on every reachable poll regardless of what the rest of
        # this iteration decides (recording, not gating -- see the batch
        # extraction after the loop for why no new FAIL branch reads these).
        ladder_status_samples.append(last_status)

        restart_marker_field = last_status.get(schema.restart_field)
        if _type_mismatch(restart_marker_field, int):
            poll_schema_anomalies.append(
                f"poll sample missing/invalid {schema.restart_field}: {last_status!r}"
            )
            monitor.wait(poll_interval_s, _progress)
            continue
        if schema.restart_detected(baseline_restart_marker, [restart_marker_field]):
            recovery_reasons.append(
                f"{schema.restart_field} {schema.restart_verb} from {baseline_restart_marker} to "
                f"{restart_marker_field} -- the host controller rebooted. The link is "
                "required to rejoin WITHOUT one: ADR 0032 forbids relying on a host restart "
                "to recover a network fault, so a rejoin that needed one is not a rejoin"
            )
            break

        poll_reset = schema.reset_reason_soft(last_status, poll_schema_anomalies)
        if poll_reset is not None and poll_reset.crash_shaped:
            recovery_reasons.append(f"resetReason became {poll_reset.display} during recovery")
            break

        # Missing/non-bool means unknown, not "not yet connected" -- treat
        # as still-recovering (keep polling) rather than asserting either
        # way on an unrecognized shape.
        link_ready, link_why_not = schema.link_readiness(last_status)
        if link_ready:
            recovered = True
            break

        monitor.wait(poll_interval_s, _progress)

    recovered_at_s = time.monotonic() - request_accepted_at
    if monitor.interrupted:
        monitor.line(
            f"c6_reset_recovery: interrupt ({monitor.signal_name}) after "
            f"{format_duration(recovered_at_s)} of the {format_duration(recovery_timeout_s)} "
            "recovery budget -- reporting what was observed",
            kind="warn",
        )

    # Batch-extract the recovery-ladder telemetry gathered above, the same
    # way run_sse_soak() extracts its poll samples: soft per-field
    # validation through poll_schema_anomalies, never a silent default that
    # would misreport "ladder never fired" as "ladder field never sampled".
    # Purely observational -- see the comment on recoveryLadderReachedDegraded
    # below for why this does not gate the verdict.
    ladder_samples = schema.collect_ladder(ladder_status_samples, poll_schema_anomalies)

    final_recovery_ladder_state = (
        ladder_samples.states[-1] if ladder_samples.states else baseline_ladder.state
    )
    final_transport_failure_count = (
        ladder_samples.transport_failure_counts[-1]
        if ladder_samples.transport_failure_counts
        else baseline_ladder.transport_failure_count
    )
    final_transport_up_event_count = (
        ladder_samples.transport_up_event_counts[-1]
        if ladder_samples.transport_up_event_counts
        else baseline_ladder.transport_up_event_count
    )
    final_recovery_attempt_count = (
        ladder_samples.attempt_counts[-1]
        if ladder_samples.attempt_counts
        else baseline_ladder.attempt_count
    )
    final_recovery_recovered_count = (
        ladder_samples.recovered_counts[-1]
        if ladder_samples.recovered_counts
        else baseline_ladder.recovered_count
    )
    # Recorded, and deliberately NOT a FAIL condition of this driver.
    # run_sse_soak() treats a ladder that reaches 'degraded' as a FAIL because
    # a ladder going terminal during an otherwise-idle soak is itself the
    # anomaly. Here the ladder is deliberately provoked, so reaching 'degraded'
    # is one possible outcome of the provocation rather than a surprise -- and
    # it is already caught by the "did not observe an established Hosted link
    # again" reason below, since a ladder that went terminal cannot also have
    # restored the transport. A second, redundant FAIL path on the same fact
    # would say the same thing twice.
    ladder_reached_degraded = "degraded" in ladder_samples.states

    if not recovery_reasons and not recovered:
        # The last readiness check's own words rather than a fixed field
        # list: which fields prove an established link is a property of the
        # image, and this message is what an operator reads first.
        recovery_reasons.append(
            f"did not observe an established Hosted link again within "
            f"{recovery_timeout_s}s of the scheduled reset"
            + (f" -- last readiness check: {link_why_not}" if link_why_not else "")
        )

    sse_resumed = False
    sse_frame_count = 0
    sse_resume_error = None
    if recovered:
        resume_stop = threading.Event()

        def _resume_timer(resume_stop: threading.Event = resume_stop) -> None:
            # Ends the resume window on its own timeout, on the second frame
            # (on_frame sets resume_stop) or on an operator interrupt. Polled
            # rather than waited on, because threading.Event cannot wait on
            # two events at once and a resume window that ignores Ctrl-C is a
            # ten-second hang at the very end of a run.
            resume_deadline = time.monotonic() + sse_resume_timeout_s
            while time.monotonic() < resume_deadline:
                if resume_stop.wait(0.1) or monitor.interrupted:
                    break
            resume_stop.set()

        timer = threading.Thread(target=_resume_timer, daemon=True)
        timer.start()
        frames_holder: list[SseFrame] = []

        def on_frame(frame: SseFrame, _ts: float, holder: list[SseFrame] = frames_holder) -> None:
            holder.append(frame)
            if len(holder) >= 2:
                resume_stop.set()

        # Straight through stream_sse(), not stream_sse_with_continuity():
        # this check asks only "did a fresh stream advance at all after the
        # rejoin", which is a two-frame question, not a continuity one.
        resume_result = client.stream_sse(
            DEFAULT_SSE_PATH, on_frame, resume_stop, read_chunk_timeout_s=2.0,
        )
        timer.join(timeout=2.0)
        sse_frame_count = len(frames_holder)
        sse_resumed = sse_frame_count > 0
        sse_resume_error = resume_result.error
        if not sse_resumed:
            recovery_reasons.append(
                f"a fresh SSE stream did not advance within {sse_resume_timeout_s}s of "
                f"recovery (stream error: {sse_resume_error})"
            )

    # The last marker actually read, kept as None when no poll produced a
    # usable one -- an unread marker must not report as "unchanged".
    final_restart_marker = (last_status or {}).get(schema.restart_field)
    if _type_mismatch(final_restart_marker, int):
        final_restart_marker = None
    restart_observed = (
        final_restart_marker is not None
        and schema.restart_detected(baseline_restart_marker, [final_restart_marker])
    )

    post_heap = (last_status or {}).get(schema.heap_field)
    heap_recovered: Optional[bool] = None
    if not _type_mismatch(post_heap, int):
        heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)
        heap_recovered = post_heap >= heap_floor
        if not heap_recovered:
            recovery_reasons.append(
                f"{schema.heap_field} did not recover within tolerance after reset+rejoin "
                f"({post_heap} < floor {heap_floor:.0f}, baseline {baseline_heap})"
            )

    # Same rule as the other two drivers. It bites hardest here: an interrupt
    # inside the recovery window leaves `recovered` False and would otherwise
    # be reported as FAIL -- "the C6 never rejoined" -- when what actually
    # happened is that nobody waited to find out.
    if monitor.interrupted:
        recovery_reasons.insert(
            0, monitor.truncation_reason(recovered_at_s, recovery_timeout_s))
        verdict = VERDICT_INTERRUPTED_DRIVER
    else:
        verdict = (
            VERDICT_PASS if recovered and sse_resumed and not recovery_reasons
            else VERDICT_FAIL
        )

    report = {
        "driver": "c6_reset_recovery",
        "verdict": verdict,
        "image": schema.name,
        "statusFieldsRead": schema.fields_read(),
        "reasons": recovery_reasons,
        # Empty unless the recovery window was cut short. This driver has no
        # durationSRequested of its own, so the truncation block carries the
        # recovery budget alongside the time actually spent in it.
        **monitor.truncation_fields(recovered_at_s, recovery_timeout_s),
        "requestId": reset_body.get("requestId"),
        "responseGraceMs": response_grace_ms,
        "resetPulseMsFromStatus": (last_status or {}).get("resetPulseMs"),
        "sawUnreachableWindow": saw_unreachable,
        "pollSchemaAnomalies": poll_schema_anomalies[:50],
        "recoveredAtSeconds": round(recovered_at_s, 3) if recovered else None,
        "recovered": recovered,
        "sseResumed": sse_resumed,
        "sseFrameCountAfterRecovery": sse_frame_count,
        "sseResumeError": sse_resume_error,
        "baselineLargestFree8bitBlock": baseline_heap,
        "finalLargestFree8bitBlock": post_heap,
        "heapTolerancePct": heap_tolerance_pct,
        "heapRecoveredWithinTolerance": heap_recovered,
        "baselineRecoveryLadderState": baseline_ladder.state,
        "finalRecoveryLadderState": final_recovery_ladder_state,
        "recoveryLadderStatesObserved": sorted(set(ladder_samples.states)),
        "recoveryLadderReachedDegraded": ladder_reached_degraded,
        "baselineHostedTransportFailureCount": baseline_ladder.transport_failure_count,
        "finalHostedTransportFailureCount": final_transport_failure_count,
        "hostedTransportFailureCountAdvancedBy":
            final_transport_failure_count - baseline_ladder.transport_failure_count,
        "baselineHostedTransportUpEventCount": baseline_ladder.transport_up_event_count,
        "finalHostedTransportUpEventCount": final_transport_up_event_count,
        "hostedTransportUpEventCountAdvancedBy":
            final_transport_up_event_count - baseline_ladder.transport_up_event_count,
        "baselineRecoveryAttemptCount": baseline_ladder.attempt_count,
        "finalRecoveryAttemptCount": final_recovery_attempt_count,
        "recoveryAttemptCountAdvancedBy": final_recovery_attempt_count - baseline_ladder.attempt_count,
        "baselineRecoveryRecoveredCount": baseline_ladder.recovered_count,
        "finalRecoveryRecoveredCount": final_recovery_recovered_count,
        "recoveryRecoveredCountAdvancedBy":
            final_recovery_recovered_count - baseline_ladder.recovered_count,
        "resetEvidenceBoundary": (last_status or {}).get(
            "resetEvidenceBoundary",
            "GPIO API results require external logic capture plus C6 UART reboot proof",
        ),
        "note": (
            "This driver proves host-side rejoin -- Hosted link teardown, "
            "hostedIsInitialized() again, and /api/status answering again through the "
            "same link -- plus SSE freshness. It does NOT prove the physical reset "
            "edge or its pulse width: that needs a synchronised logic capture on the "
            "reset line, which no host-side harness can perform."
        ),
    }
    report.update(
        schema.restart_report(baseline_restart_marker, final_restart_marker, restart_observed)
    )
    return report


# ---------------------------------------------------------------------------
# Orchestration and the Run Verdict.
# ---------------------------------------------------------------------------


# A response that is not JSON at all is the same class of problem as one
# missing a field: the device answered with something this harness cannot
# read. json.JSONDecodeError is a ValueError, so it is named explicitly
# rather than caught as one -- an ordinary ValueError from anywhere else is a
# bug in this harness and must still propagate.
CONTRACT_ERRORS = (KeyError, TypeError, json.JSONDecodeError)


def _run_driver_safely(name: str, fn: Callable[..., dict], *args: Any) -> dict:
    """Bounds the blast radius of a response-contract violation to "this
    driver is INVALID" rather than crashing the whole (possibly
    multi-hour) run with a bare traceback. Only contract-shape errors are
    caught here (CONTRACT_ERRORS: _require_field's KeyError/TypeError, plus a
    body that is not JSON -- an HTML error page from a proxy, or a route that
    does not exist on this image) -- anything else is a bug in this harness
    and must propagate, per AGENTS.md "never swallow an error to keep
    moving"."""
    try:
        return fn(*args)
    except CONTRACT_ERRORS as contract_error:
        return {
            "driver": name,
            "verdict": VERDICT_INVALID,
            "reasons": [f"response contract violation while running {name}: {contract_error}"],
        }


def _compose_overall_verdict(driver_results: dict[str, dict]) -> tuple[str, int]:
    """The Run Verdict, in the same three words its Soak Drivers use.

    Ranked, most serious first. The ranking is the contract; the words are
    prose (ADR 0035), which is why every one of them is a named constant.
    """
    verdicts = [d["verdict"] for d in driver_results.values()]
    # Ranked above everything else, and deliberately so: a run the operator
    # stopped has not covered the contract it was asked to cover, so no
    # conclusion drawn from it is safe in either direction -- neither a pass,
    # nor a failure manufactured out of an observation window that was cut
    # short. The exit code is EXIT_INVALID, "the required evidence is
    # missing"; the mapping is not extended, only reached from one more state.
    if any(v == VERDICT_INTERRUPTED_DRIVER for v in verdicts):
        return VERDICT_INTERRUPTED_RUN, EXIT_INVALID
    if any(v == VERDICT_INVALID for v in verdicts):
        return VERDICT_INVALID, EXIT_INVALID
    if any(v == VERDICT_FAIL for v in verdicts):
        return VERDICT_FAIL, EXIT_FAIL
    # UNAVAILABLE is ranked BELOW FAIL and above pass: a driver that could not
    # run on this Image Mode left a coverage gap, and a coverage gap is never a
    # pass -- but a driver that actually failed is the more actionable answer,
    # so a real failure is never masked by a gap. An operator who wants an exit
    # code covering only what this image can measure names the drivers
    # explicitly. That collapse is the rule, not the wording (ADR 0035).
    if any(v == VERDICT_UNAVAILABLE for v in verdicts):
        return VERDICT_INVALID, EXIT_INVALID
    # PASS and OBSERVATION_ONLY both fall through here: a run above the client
    # cap measured something real and claims nothing, which is not a finding.
    return VERDICT_PASS, EXIT_PASS


# How long each driver intends to occupy, from the arguments it was given.
# Used for the planned-duration clocks in the header and in the JSON, so a
# reader can tell "this run is 12 minutes into a planned 3 hours" without
# adding the flags up by hand.
#
# Every value is derived from the caller's own arguments; nothing here is a
# constant, because a plan built from a constant would be wrong for the next
# board and for the next set of flags.
def planned_driver_duration_s(name: str, args: argparse.Namespace) -> Optional[float]:
    """Seconds this driver plans to take, or None where there is no plan.

    Not an ETA and not a promise: c6_reset_recovery's number is a BUDGET it
    hopes not to spend (it stops as soon as the link is back), which is why
    the progress clock labels that one "timeout" rather than counting down to
    completion.
    """
    if name == "sse_soak":
        return args.duration
    if name == "reconnect_storm":
        # The settle window is part of the wall-clock cost of this driver even
        # though nothing is being driven during it, and an operator waiting for
        # the run to end is waiting for it too.
        return args.storm_duration + args.storm_settle_s
    if name == "c6_reset_recovery":
        return args.reset_recovery_timeout_s + args.sse_resume_timeout_s
    return None


def run(args: argparse.Namespace, monitor: Optional[RunMonitor] = None) -> tuple[dict, int]:
    monitor = monitor or RunMonitor.disabled()
    client = BenchClient(args.device, args.port, connect_timeout_s=args.connect_timeout_s)
    schema = SCHEMAS[args.image]
    build_env = args.build_env or schema.build_env
    header = {
        "schemaVersion": REPORT_SCHEMA_VERSION, "device": args.device, "port": args.port,
        "image": schema.name, "buildEnv": build_env,
        "statusFieldsRead": schema.fields_read(),
    }

    drivers_to_run = (
        ["sse_soak", "reconnect_storm", "c6_reset_recovery"] if args.driver == "all" else [args.driver]
    )
    monitor.start_run(len(drivers_to_run))
    planned = {name: planned_driver_duration_s(name, args) for name in drivers_to_run}
    planned_total_s = sum(value for value in planned.values() if value is not None)
    # One record per driver that started, appended as each finishes: the
    # per-phase durations the report publishes, and what the footer reads to
    # name the slowest phase. A driver that never ran leaves no phase, which is
    # the truth -- a zero-second phase would claim it ran and took no time.
    phases: list[dict] = []
    started_at = monitor.started_at

    def finish(payload: dict, exit_code: int) -> tuple[dict, int]:
        """Every exit from this function goes through here, so the clocks are
        on the report whether the run measured anything or refused at
        preflight. Appended after the payload, so nothing that existed before
        them moved."""
        ended_at = time.time()
        return dict(header, **payload, **{
            "startedAt": format_timestamp(started_at),
            "endedAt": format_timestamp(ended_at),
            "durationS": round(monitor.run_elapsed_s, 3),
            "plannedDurationS": planned_total_s,
            "phases": phases,
            # Where the plain transcript of this run went, so the next tool
            # does not have to parse a terminal to find it. null when no
            # transcript was written (a driver driven directly by a test).
            "logPath": None if monitor.log_path is None else str(monitor.log_path),
        }), exit_code

    # Both yardsticks are resolved out of the tree before the first request. A
    # run this harness cannot judge is INVALID whether or not the device
    # answers, and there is no reason to put load on a controller in order to
    # find that out.
    admission_floor: Optional[AdmissionFloor] = None
    try:
        if schema.enforces_admission_floor:
            admission_floor = resolve_admission_floor(build_env)
            header["admissionFloor"] = admission_floor.report()
        else:
            require_declared_environment(build_env)
            header["admissionFloor"] = schema.admission_absence_note
        sse_client_cap = resolve_sse_client_cap(build_env)
        header["sseClientCap"] = sse_client_cap.report()
    except BuildConstantUnresolved as unresolved:
        # An unresolvable yardstick is INVALID, never a default (#194). A
        # verdict taken against a number this harness picked would read like
        # evidence and be none.
        reason = (
            f"a compiled value this run has to be judged against, for --image "
            f"{schema.name} (build environment {build_env!r}), could not be determined: "
            f"{unresolved}"
        )
        monitor.line(reason, kind="fail")
        return finish({
            "verdict": VERDICT_INVALID,
            "reasons": [reason],
            "drivers": {},
        }, EXIT_INVALID)

    # --num-clients defaults to the cap rather than to a number written here,
    # so the default always means "at the concurrency the firmware admits" --
    # a literal would go on saying 3 after a build changed it.
    num_clients = args.num_clients if args.num_clients is not None else sse_client_cap.value

    # Preflight: an unreachable device, or a WiFi Module that never came up,
    # cannot produce any of the required evidence, so this is INVALID rather
    # than FAIL. A FAIL is a live link that then failed; this is "there was
    # never a link to test".
    try:
        preflight_status = capture_status(client)
    except TRANSPORT_EXCEPTIONS as error:
        reason = f"device unreachable at {args.device}:{args.port}: {error}"
        monitor.line(reason, kind="fail")
        return finish({
            "verdict": VERDICT_INVALID,
            "reasons": [reason],
            "drivers": {},
        }, EXIT_INVALID)
    except json.JSONDecodeError as error:
        # Answered, but not with JSON. INVALID for the same reason as
        # unreachable -- the required evidence never arrived -- and reported
        # rather than raised, so a run started overnight leaves a verdict
        # instead of a traceback.
        reason = (
            f"{args.device}:{args.port}{DEFAULT_STATUS_PATH} answered with a body that is "
            f"not JSON: {error}"
        )
        monitor.line(reason, kind="fail")
        return finish({
            "verdict": VERDICT_INVALID,
            "reasons": [reason],
            "drivers": {},
        }, EXIT_INVALID)

    # The board half of the header. It cannot be printed with the rest of the
    # header in main(), because these two values are only knowable once the
    # controller has answered -- and stating them from anywhere but the board
    # itself is how a soak ends up describing the wrong firmware.
    monitor.rows([
        ("firmware", str(preflight_status.get("firmwareVersion", PROGRESS_ABSENT))),
        ("filesystem", str(preflight_status.get("fsVersion", PROGRESS_ABSENT))),
    ])

    # The declared --image is checked against the payload before anything is
    # measured. Without this every subsequent read would silently re-label
    # fields: a bench payload read as shipping finds no heapLargest8bit and
    # fails obscurely deep inside a driver, and a shipping payload read as
    # bench would be worse still if the two ever shared enough names.
    mismatches = schema.structural_mismatches(preflight_status)
    if mismatches:
        looks_like = identify_schema(preflight_status)
        hint = (
            f" This payload matches the {looks_like.name!r} schema -- did you mean "
            f"--image {looks_like.name}?"
            if looks_like is not None and looks_like.name != schema.name
            else ""
        )
        reason = (
            f"--image {schema.name} was declared, but /api/status does not match that "
            f"image's payload: {mismatches}.{hint}"
        )
        monitor.line(
            f"--image {schema.name} does not match this board's /api/status payload.{hint}",
            kind="fail",
        )
        return finish({
            "verdict": VERDICT_INVALID,
            "reasons": [reason],
            "drivers": {},
        }, EXIT_INVALID)

    link_ready, why_not = schema.link_readiness(preflight_status)
    if not link_ready:
        monitor.line(why_not, kind="fail")
        return finish({
            "verdict": VERDICT_INVALID,
            "reasons": [why_not],
            "drivers": {},
        }, EXIT_INVALID)

    driver_results: dict[str, dict] = {}
    # One dispatch table rather than three `if name in drivers_to_run` blocks:
    # the interrupt check, the announcement and the checkpoint boundary have to
    # happen around every driver, and three copies of that is three places for
    # one of them to drift. The iteration order is drivers_to_run's, so
    # driver_results is keyed in the same order it always was.
    driver_calls: dict[str, tuple[Callable[..., dict], tuple]] = {
        "sse_soak": (run_sse_soak, (
            client, schema, num_clients, args.duration, args.status_poll_interval_s,
            admission_floor, sse_client_cap, args.early_stall_check_s,
            args.sse_max_silence_s,
        )),
        "reconnect_storm": (run_reconnect_storm, (
            client, schema, args.storm_clients, args.storm_duration, args.storm_cycle_min_s,
            args.storm_cycle_max_s, args.storm_settle_s, args.heap_recovery_tolerance_pct,
            args.storm_max_silence_s, args.storm_liveness_cycle_every,
        )),
        "c6_reset_recovery": (run_c6_reset_recovery, (
            client, schema, args.reset_recovery_timeout_s, args.reset_poll_interval_s,
            args.heap_recovery_tolerance_pct, args.sse_resume_timeout_s,
        )),
    }

    def _checkpoint_report(snapshot: Optional[ProgressSnapshot]) -> dict:
        """The mid-run artefact: what has been collected so far, and nothing
        that looks like a conclusion. VERDICT_CHECKPOINT is not a verdict any
        finished run can carry, so a consumer that finds one is holding
        evidence from a run that had not reached its own conclusion -- which
        is the honest description of a checkpoint."""
        report, _ = finish({
            "checkpoint": True,
            "verdict": VERDICT_CHECKPOINT,
            "reasons": [
                "mid-run checkpoint: written periodically so a hard kill or a power loss "
                "still leaves the measurements taken up to this point. It is replaced by "
                "the final report when the run ends, and it carries no verdict -- the "
                "drivers it names have not finished"
            ],
            "driverInFlight": None if snapshot is None else snapshot.as_dict(),
            "drivers": dict(driver_results),
        }, EXIT_INVALID)
        return report

    monitor.bind_checkpoint_source(_checkpoint_report)
    for index, name in enumerate(drivers_to_run, start=1):
        if monitor.interrupted:
            # Recorded, never omitted: a driver missing from the report reads
            # as "not asked for", and a driver that collected nothing reads as
            # "measured nothing wrong" unless it says otherwise itself.
            monitor.begin_driver(index, name)
            monitor.line(
                f"{name}: not started -- the run was already interrupted", kind="warn")
            driver_results[name] = {
                "driver": name,
                "verdict": VERDICT_INTERRUPTED_DRIVER,
                "image": schema.name,
                "reasons": [
                    f"the run was interrupted ({monitor.signal_name}) before this driver "
                    "started, so it collected no evidence at all -- which is not the same "
                    "as having measured nothing wrong"
                ],
                "started": False,
            }
            continue
        fn, call_args = driver_calls[name]
        monitor.begin_driver(index, name)
        phase_started = time.monotonic()
        result = _run_driver_safely(name, fn, *call_args, monitor)
        driver_results[name] = result
        phases.append({
            "driver": name,
            "verdict": result["verdict"],
            "durationS": round(time.monotonic() - phase_started, 3),
            "plannedDurationS": planned[name],
        })
        monitor.line(
            f"{name}: {result['verdict']} in "
            f"{format_duration(phases[-1]['durationS'])}",
            kind=("ok" if result["verdict"] in DRIVER_VERDICTS_WITHOUT_A_FINDING else "fail"),
        )
        # A driver boundary is the most valuable checkpoint there is: it is
        # the only moment a whole driver's evidence exists and the next one
        # has not started perturbing the controller yet.
        monitor.checkpoint()

    verdict, exit_code = _compose_overall_verdict(driver_results)
    interrupt_fields: dict = {}
    if monitor.interrupted:
        # Deliberately redundant with the VERDICT_INTERRUPTED_DRIVER rank in
        # _compose_overall_verdict(): two independent paths -- the run-level
        # flag and the per-driver verdicts -- have to agree that a truncated
        # run is not a pass. This harness exists because earlier attempts at it
        # shipped verification that could not judge itself, and one check
        # standing alone between a truncated run and a green exit code is the
        # shape of that failure.
        verdict, exit_code = VERDICT_INTERRUPTED_RUN, EXIT_INVALID
        interrupt_fields = {
            "interrupted": True,
            "interruptSignal": monitor.signal_name,
            "reasons": [
                f"the run was interrupted ({monitor.signal_name}); it covers only the "
                "window actually observed and therefore reaches no verdict about the "
                "controller. Each driver's own report says how far it got"
            ],
        }
    return finish({
        "verdict": verdict,
        # Empty on a run that finished, so a completed report is unchanged.
        **interrupt_fields,
        "driversUnavailableOnThisImage": [
            name for name, result in driver_results.items() if result["verdict"] == VERDICT_UNAVAILABLE
        ],
        "drivers": driver_results,
    }, exit_code)


# ---------------------------------------------------------------------------
# --self-test: starts a local http.server fixture serving byte-exact
# PsychicEventSource framing and drives BenchClient (the real production
# entry point) against it. No inline parse loop.
# ---------------------------------------------------------------------------


def _fixture_frame_bytes(counter: int) -> bytes:
    """Byte-exact reproduction of PsychicEventSource.cpp's
    _generateEventMessage_impl() (line 236) for exactly the call
    bringup/p4_hosted_bench.cpp's emitSseFrame() makes: events.send(frame)
    with event=nullptr, id=0, reconnect=0 (PsychicEventSource.h:86
    defaults). Because id and reconnect are 0 (falsy in the vendor's
    `if (id)` / `if (reconnect)` checks) and event is NULL, only the data:
    line and the unconditional blank-line terminator are ever emitted --
    never id:/event:/retry:."""
    return f"data: {counter}\r\n\r\n".encode("ascii")


def _shipping_fixture_frame_bytes(event: Optional[str], data: str, frame_id: int) -> bytes:
    """Byte-exact reproduction of what the shipping image puts on the wire:
    webEventStreamFormatPrefix() (src/web/web_event_stream.cpp:106) followed
    by the payload and kWebEventStreamTerminator (:19, "\\r\\n\\r\\n"), sent
    as three segments by webEventStreamBroadcast()
    (src/web/web_request_psychic.cpp:456). An id of 0 and a null/empty event
    name are omitted rather than sent empty, exactly as that function does --
    which is why a nameless frame is reachable as a fixture at all, and worth
    a scenario: it is what a bench stream looks like to a shipping reader."""
    prefix = ""
    if frame_id:
        prefix += f"id: {frame_id}\r\n"
    if event:
        prefix += f"event: {event}\r\n"
    return f"{prefix}data: {data}\r\n\r\n".encode("utf-8")


FIXTURE_STATUS_BODY = {
    "firmwareVersion": "self-test",
    "bootCount": 1,
    "resetReason": 1,  # ESP_RST_POWERON
    "wifiConnected": True,
    "hostedIsInitialized": True,
    "sseFramesSent": 42,
    "sseClientsConnected": 1,
    # bringup/p4_hosted_bench.cpp:876-877. No heapMin counterpart on this
    # image, and no admission counters at all -- that absence is the schema's
    # enforces_admission_floor = False, and it is what the fixture must look
    # like for the bench half of the self-test to mean anything.
    "freeHeapBytes": 260000,
    "largestFree8bitBlock": 123456,
    "recoveryLadderState": "idle",
    "hostedTransportFailureCount": 0,
    "hostedTransportUpEventCount": 0,
    "recoveryAttemptCount": 0,
    "recoveryRecoveredCount": 0,
}

# A representative slice of buildStatusJson() (src/web/web_server.cpp:393),
# with the neighbouring fields kept so the fixture is shaped like the real
# payload rather than only carrying what the reader happens to want: no
# bootCount, resetReason as resetReasonName()'s string (:401), heapLargest8bit
# and sseClients rather than the bench's names, and the hostedLink block from
# :724-742 / include/hosted_link_status.h.
FIXTURE_SHIPPING_STATUS_BODY = {
    "estop": False,
    "uptimeMs": 3600000,
    "firmwareVersion": "self-test",
    "fsVersion": "self-test",
    "resetReason": "POWERON",
    "heapFree": 260000,
    "heapMin": 240000,
    "heapLargestBlock": 150000,
    "heapLargest8bit": 123456,
    "sseClients": 1,
    "sseClientsPeak": 3,
    # The admission evidence, from the same unconditional snprintf
    # (src/web/web_server.cpp:393, values at :425-427 and :440). A healthy
    # controller: nothing refused, and a guard low-water reading well above
    # the 9000-byte ordinary floor.
    "acceptRejectLargestBlock": 0,
    "acceptMinLargestBlockSeen": 40000,
    "refusedInflightCap": 0,
    "refusedSseCap": 0,
    # :433-439. Stalled-client evictions and how long ago the last one fired
    # (-1 until one has). Emitted by the same unconditional snprintf as the
    # rest of this block, so a fixture without them is not shaped like the
    # payload; the progress line reads sseEvicted and would otherwise only
    # ever see it absent.
    "sseEvicted": 0,
    "sseEvictAgeMs": -1,
    "refusedHeapFloor": 0,
    "refusedHeapFloorDiag": 0,
    "wifiRssi": -55,
    "wifiConnected": True,
    "wifiClientConnected": True,
    "littleFsReady": True,
    "hostedLink": {
        "phase": "idle",
        "terminal": False,
        "transportFailureCount": 0,
        "transportUpEventCount": 1,
        "attemptCount": 0,
        "totalAttemptCount": 0,
        "recoveredCount": 0,
        "lastFailureAtMs": 0,
        "lastAttemptAtMs": 0,
        "degradedAtMs": 0,
    },
}
# The artoo payload IS the shipping payload minus the one capability-gated
# block, so it is derived that way rather than retyped: buildStatusJson()'s
# fixed system-health snprintf (src/web/web_server.cpp:391-393) is
# unconditional and identical on both boards, and `#if PA_CAP_HOSTED_WIFI`
# (:724-743) is the only preprocessor guard anywhere in that function. Writing
# the difference as a subtraction keeps the fixture honest if the shared block
# ever grows a field.
FIXTURE_ARTOO_STATUS_BODY = {
    key: value for key, value in FIXTURE_SHIPPING_STATUS_BODY.items()
    if key != HOSTED_LINK_CONTAINER
}

FIXTURE_RESET_BODY = {"requestId": 7, "resetScheduled": True, "responseGraceMs": 1000}


class _FixtureHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format: str, *args: Any) -> None:  # noqa: A002 - stdlib signature
        pass  # keep self-test output focused on assertions, not access logs

    def do_GET(self) -> None:
        path = urlsplit(self.path).path
        if path == "/api/events":
            self._serve_sse()
        elif path == "/api/status":
            # Counted so a scenario can assert that a run which claims to have
            # refused BEFORE touching the device really did: a preflight that
            # went out and was discarded is not a refusal.
            self.server.status_get_count += 1
            body = self.server.status_body
            # A callable lets a scenario move a counter mid-run -- an admission
            # refusal is a change over time, and a static body cannot express
            # one.
            self._serve_json(200, body() if callable(body) else body)
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        # Counted so a scenario can assert that a driver which claims to have
        # refused really did refuse -- i.e. that it never sent the request at
        # all, rather than sending it and discarding the answer.
        self.server.post_count += 1
        if self.path == "/api/c6/reset":
            length = int(self.headers.get("Content-Length", 0) or 0)
            self.rfile.read(length)
            self._serve_json(202, FIXTURE_RESET_BODY)
        else:
            self.send_error(404)

    def _serve_json(self, status: int, body_dict: dict) -> None:
        body = json.dumps(body_dict).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_live(self, frame_for: Callable[[int], bytes], count: int, interval_s: float) -> None:
        """A stream that keeps going, for the scenarios that run a whole
        driver rather than one read. Ends on the client's own disconnect: a
        broken pipe here IS the expected end of a fixture stream (the soak
        stopping, or the storm's deliberate RST), not an error to report."""
        for index in range(count):
            try:
                self.wfile.write(frame_for(index))
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                break
            time.sleep(interval_s)
        self.close_connection = True

    def _serve_sse(self) -> None:
        query = parse_qs(urlsplit(self.path).query)
        mode = query.get("mode", [self.server.default_sse_mode])[0]

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()

        if mode == "normal":
            for n in (0, 1, 2):
                self.wfile.write(_fixture_frame_bytes(n))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "gapped":
            for n in (0, 1, 3, 4, 6):
                self.wfile.write(_fixture_frame_bytes(n))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "truncated":
            self.wfile.write(_fixture_frame_bytes(0))
            self.wfile.write(b"data: 1\r\n")  # deliberately missing the closing blank line
            self.wfile.flush()
            self.close_connection = True
        elif mode == "connreset":
            # Send one real frame first: verified empirically (before
            # writing this fixture) that RSTing before any data has been
            # sent has nothing in flight to discard, so Linux's recv()
            # reports a plain EOF (0 bytes) rather than ECONNRESET -- the
            # reset needs to land while the client is actively expecting
            # more data on an established stream to reproduce a genuine
            # mid-stream ConnectionResetError.
            self.wfile.write(_fixture_frame_bytes(0))
            self.wfile.flush()
            # SO_LINGER(1, 0) makes close() send an RST instead of a clean
            # FIN -- but only if THIS close() call is the one that actually
            # tears the socket down. Verified empirically that it is not
            # enough to set it and let socketserver do its normal teardown:
            # BaseServer.shutdown_request() calls
            # `request.shutdown(socket.SHUT_WR)` (a graceful half-close)
            # BEFORE close_request()'s close(), independent of SO_LINGER, so
            # the client always saw a clean EOF instead of a reset. Closing
            # the socket here, before that framework teardown runs, is what
            # makes the RST land on the wire. socketserver's own
            # shutdown()/close() calls on this now-already-closed fd
            # afterwards are `except OSError: pass`-guarded no-ops.
            self.connection.setsockopt(
                socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0),
            )
            self.connection.close()
            self.close_connection = True
        elif mode == "shipping_normal":
            # One tick's worth of what eventStreamTask() broadcasts, twice:
            # "rc" every tick, "status" on demand, "log" every other tick,
            # all three sharing that tick's millis() as their id.
            for frame_id, event, payload in (
                (100000, "rc", '{"ch":[1500,1500]}'),
                (100000, "status", '{"estop":false}'),
                (101000, "rc", '{"ch":[1500,1500]}'),
                (101000, "log", "boot line one"),
                (102000, "rc", '{"ch":[1500,1500]}'),
            ):
                self.wfile.write(_shipping_fixture_frame_bytes(event, payload, frame_id))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "shipping_silence":
            # A real gap on the wire, not a synthesised timestamp: the
            # continuity tracker is fed by the live read loop, so the only
            # honest way to test the silence limit is to make the fixture go
            # quiet. 0.35s is long enough to exceed the 0.2s limit the
            # scenario sets and short enough to keep the self-test quick.
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 200000))
            self.wfile.flush()
            time.sleep(0.35)
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 200400))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "shipping_id_regression":
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 300000))
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 299000))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "bench_live":
            self._serve_live(_fixture_frame_bytes, count=25, interval_s=0.2)
        elif mode == "shipping_live":
            self._serve_live(
                lambda index: _shipping_fixture_frame_bytes(
                    "rc", '{"ch":[1500,1500]}', 400000 + index * 200),
                count=25, interval_s=0.2,
            )
        elif mode == "silent_stream":
            # A stream that opens correctly and then says nothing. This is
            # what a real stall looks like from the client side, and it is the
            # only way to prove the storm's silence budget can still fail --
            # the budget was pushed to 5s precisely because the old 0.25s one
            # failed healthy streams, and a budget that can no longer fail
            # anything would be the worse defect.
            deadline = time.time() + 3.0
            while time.time() < deadline:
                time.sleep(0.05)
            self.close_connection = True
        elif mode == "empty_stream":
            # Opens with the right status and content type and then hangs up
            # without a single frame. Too short a look to call a stall, which
            # is exactly why it must read as "not assessed" rather than as
            # "fine" -- and why a storm made only of these has to say it
            # measured nothing.
            self.wfile.flush()
            self.close_connection = True
        elif mode == "shipping_nameless":
            # Exactly what the bench stream looks like: no id:, no event:,
            # a bare counter payload. A shipping reader pointed at it must
            # say so rather than count it as a delivered event.
            self.wfile.write(_fixture_frame_bytes(7))
            self.wfile.flush()
            self.close_connection = True
        else:
            self.close_connection = True


def _start_fixture_server(
    status_body: Optional[Any] = None, sse_mode: str = "normal",
) -> tuple[http.server.ThreadingHTTPServer, threading.Thread]:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _FixtureHandler)
    # Per-server, not per-handler: BaseHTTPRequestHandler is instantiated
    # once per request, so anything a scenario configures or counts has to
    # live on the server the handler can reach through self.server.
    # A dict serves the same payload every time; a zero-argument callable is
    # re-invoked per request, for the scenarios whose subject is a counter
    # moving during a run.
    server.status_body = FIXTURE_STATUS_BODY if status_body is None else status_body
    # The mode used when the request carries no ?mode= -- which is how the
    # drivers themselves fetch /api/events, since they use the real path.
    server.default_sse_mode = sse_mode
    server.post_count = 0
    server.status_get_count = 0
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def _stop_fixture_server(server: http.server.ThreadingHTTPServer, thread: threading.Thread) -> None:
    server.shutdown()
    server.server_close()
    thread.join(timeout=5.0)


def _record_scenario(name: str, body: Callable[[], None], failures: list[str]) -> None:
    """Run one scenario body and record its outcome.

    CONTRACT_ERRORS are recorded as failures alongside assertion failures
    rather than escaping as a traceback. A schema reader that looks for a
    field the payload does not carry raises KeyError/TypeError by design
    (_require_field), so a mutation that mis-names a field would otherwise
    kill the suite mid-run and skip every scenario after it -- the exit code
    would still be non-zero, but the output would say less about which
    measurement broke. BuildConstantUnresolved is caught for the same reason
    and no other: a mutation to either yardstick resolver -- the admission
    floor or the SSE client cap -- must land as one red scenario naming it, not
    as a traceback that hides the rest."""
    try:
        body()
        print(f"  PASS  {name}")
    except (AssertionError, BuildConstantUnresolved) + CONTRACT_ERRORS as failure:
        print(f"  FAIL  {name}: {failure}")
        failures.append(f"{name}: {failure}")


def _run_sse_scenario(
    name: str, mode: str, check: Callable[[SseStreamResult, list[SseFrame]], None], failures: list[str],
) -> None:
    server, thread = _start_fixture_server()
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)
        frames_seen: list[SseFrame] = []
        stop = threading.Event()
        result = client.stream_sse(
            f"/api/events?mode={mode}",
            on_frame=lambda frame, _ts: frames_seen.append(frame),
            stop=stop,
            read_chunk_timeout_s=2.0,
        )
        _record_scenario(name, lambda: check(result, frames_seen), failures)
    finally:
        _stop_fixture_server(server, thread)


def _check_normal(result: SseStreamResult, frames: list[SseFrame]) -> None:
    assert result.error is None, f"unexpected error: {result.error}"
    assert not result.truncated, f"unexpectedly truncated: {result.truncated_detail}"
    counters = [f.counter for f in frames]
    assert counters == [0, 1, 2], f"expected [0, 1, 2], got {counters}"
    assert all(f.id is None and f.event is None for f in frames), (
        "this firmware never sends id:/event: -- a non-None value means the parser "
        "invented a field the fixture did not send"
    )
    assert count_frame_gaps(counters) == 0, "normal frames must report zero gaps"


def _check_gapped(result: SseStreamResult, frames: list[SseFrame]) -> None:
    assert result.error is None, f"unexpected error: {result.error}"
    counters = [f.counter for f in frames]
    assert counters == [0, 1, 3, 4, 6], f"expected [0, 1, 3, 4, 6], got {counters}"
    gaps = count_frame_gaps(counters)
    assert gaps == 2, f"expected 2 gap events (1->3, 4->6), got {gaps}"


def _check_truncated(result: SseStreamResult, frames: list[SseFrame]) -> None:
    counters = [f.counter for f in frames]
    assert counters == [0], f"expected exactly the one complete frame [0], got {counters}"
    assert result.truncated, "a truncated fixture must be reported as truncated, not silently dropped"
    assert result.truncated_detail is not None and "data=" in result.truncated_detail, (
        f"truncated_detail should describe the pending partial frame, got {result.truncated_detail!r}"
    )


def _check_connreset(result: SseStreamResult, _frames: list[SseFrame]) -> None:
    assert result.connect_ok, "the TCP connect itself succeeded; only the stream broke"
    assert result.error is not None, (
        "a reset mid-stream must be recorded on SseStreamResult.error, not swallowed and "
        "not left to crash the caller with an unrelated exception"
    )


def _run_continuity_scenario(
    name: str, mode: str, schema: StatusSchema, max_silence_s: float,
    check: Callable[[SseStreamResult, SseContinuityTracker, dict, list[str]], None],
    failures: list[str], stop_after_frames: Optional[int] = None,
) -> None:
    """Drive stream_sse_with_continuity() -- the exact wiring
    _sse_soak_worker() uses -- against a fixture, then assert on the tracker
    and on the run-level summary the schema produces from it. Nothing here
    parses SSE or re-derives continuity; both come back from production
    code.

    `stop_after_frames` stops the stream from the harness side once that many
    frames have arrived, which is what a real soak does when its duration
    expires. Without it the fixture's own close is what ends the stream, and
    the tracker correctly reports that as an early end."""
    server, thread = _start_fixture_server()
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)
        stop = threading.Event()
        seen = [0]

        def on_frame(_frame: SseFrame, _ts: float) -> None:
            seen[0] += 1
            if stop_after_frames is not None and seen[0] >= stop_after_frames:
                stop.set()

        result, tracker = stream_sse_with_continuity(
            client, schema, stop, read_chunk_timeout_s=2.0, path=f"/api/events?mode={mode}",
            on_frame=on_frame,
        )
        fields, reasons = schema.summarize_continuity([tracker], max_silence_s)
        _record_scenario(name, lambda: check(result, tracker, fields, reasons), failures)
    finally:
        _stop_fixture_server(server, thread)


def _check_shipping_normal(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert result.error is None, f"unexpected error: {result.error}"
    assert tracker.model == "heartbeat", f"shipping must use the heartbeat model, got {tracker.model!r}"
    assert tracker.frame_count == 5, f"expected 5 frames, got {tracker.frame_count}"
    assert tracker.event_counts == {"rc": 3, "status": 1, "log": 1}, (
        f"event names must come off the wire, got {tracker.event_counts}"
    )
    assert tracker.heartbeat_frame_count == 3, (
        f"expected 3 'rc' heartbeats, got {tracker.heartbeat_frame_count}"
    )
    assert tracker.frames_without_event == 0, "every fixture frame carried an event: name"
    assert tracker.id_regressions == 0, "the fixture ids only move forward"
    assert fields["totalIdRegressions"] == 0 and fields["totalFramesWithoutEventName"] == 0
    assert fields["eventNamesObserved"] == ["log", "rc", "status"], fields["eventNamesObserved"]
    assert fields["unexpectedEventNames"] == [], fields["unexpectedEventNames"]
    assert not tracker.ended_early, "the harness stopped this stream, so it did not end early"
    assert reasons == [], f"a healthy shipping stream must produce no FAIL reason: {reasons}"


def _check_shipping_ended_early(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.frame_count == 5, f"the frames were delivered, got {tracker.frame_count}"
    assert tracker.ended_early, (
        "the fixture closed the stream on its own, which the harness never asked for"
    )
    assert any("ended before the harness stopped it" in reason for reason in reasons), (
        "a stream that ends early must be a FAIL reason -- otherwise a mid-run clean EOF "
        f"only shows up as a smaller frame count nothing objects to: {reasons}"
    )


def _check_shipping_silence(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.frame_count == 2, f"expected both frames, got {tracker.frame_count}"
    assert tracker.max_silence_s >= 0.35, (
        f"the fixture went quiet for 0.35s; tracker measured {tracker.max_silence_s:.3f}s"
    )
    assert any("no SSE frame" in reason for reason in reasons), (
        f"a silence past the limit must be a FAIL reason, got {reasons}"
    )
    assert fields["maxSilenceSObserved"] >= 0.35, fields["maxSilenceSObserved"]


def _check_shipping_id_regression(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.id_regressions == 1, (
        f"id 300000 -> 299000 is one regression, tracker saw {tracker.id_regressions}"
    )
    assert any("go backwards" in reason for reason in reasons), (
        f"an id regression must be a FAIL reason, got {reasons}"
    )


def _check_shipping_nameless(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.frame_count == 1, f"the frame was delivered, got {tracker.frame_count}"
    assert tracker.frames_without_event == 1, (
        "a bench-style frame has no event: name and must be counted as such, got "
        f"{tracker.frames_without_event}"
    )
    assert tracker.heartbeat_frame_count == 0, "a nameless frame is not an 'rc' heartbeat"
    assert any("no event: name" in reason for reason in reasons), (
        f"a nameless frame must be a FAIL reason, not absorbed: {reasons}"
    )


def _run_shipping_status_scenarios(failures: list[str]) -> None:
    """Read the shipping fixture payload through the production schema
    readers, reached the same way a driver reaches them: capture_status()
    over real HTTP, then ShippingStatusSchema's own accessors."""
    schema = SCHEMAS["shipping"]
    bench_schema = SCHEMAS["bench"]
    server, thread = _start_fixture_server(status_body=FIXTURE_SHIPPING_STATUS_BODY)
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)

        def check(name: str, body: Callable[[], None]) -> None:
            _record_scenario(name, body, failures)

        status = capture_status(client)

        def field_map() -> None:
            assert schema.heap_largest_free(status, "self-test") == 123456, "heapLargest8bit"
            assert schema.sse_clients(status, "self-test") == 1, "sseClients"
            assert schema.restart_marker(status, "self-test") == 3600000, "uptimeMs"
            ladder = schema.ladder(status, "self-test")
            assert ladder.state == "idle", ladder
            assert ladder.transport_up_event_count == 1, ladder
            assert ladder.transport_failure_count == 0, ladder
            assert ladder.attempt_count == 0 and ladder.recovered_count == 0, ladder
            reset = schema.reset_reason(status, "self-test")
            assert reset.display == "POWERON" and reset.crash_shaped is False, reset
            ready, why_not = schema.link_readiness(status)
            assert ready, f"the fixture is a healthy shipping payload: {why_not}"

        check("shipping field map reads nested hostedLink, heapLargest8bit, sseClients", field_map)

        def boot_count_absent() -> None:
            assert schema.publishes_boot_count is False, (
                "the shipping image publishes no bootCount; claiming otherwise makes an "
                "absent field readable as a value"
            )
            assert "bootCount" not in status, "fixture sanity: the shipping payload has no bootCount"
            report = schema.restart_report(3600000, 3601000, False)
            assert "bootCount" not in json.dumps(report), (
                f"a shipping restart report must not carry a bootCount key at all: {report}"
            )
            assert report == {
                "baselineUptimeMs": 3600000, "finalUptimeMs": 3601000,
                "uptimeMsWentBackwards": False,
            }, report

        check("absent bootCount reads as absent, never as zero", boot_count_absent)

        def declared_image_is_checked() -> None:
            assert schema.structural_mismatches(status) == [], (
                "the shipping fixture must satisfy the shipping schema"
            )
            bench_mismatches = bench_schema.structural_mismatches(status)
            assert bench_mismatches, (
                "reading a shipping payload as --image bench must be refused, not accepted"
            )
            assert any("bootCount" in m for m in bench_mismatches), bench_mismatches
            shipping_vs_bench = schema.structural_mismatches(FIXTURE_STATUS_BODY)
            assert shipping_vs_bench, (
                "reading a bench payload as --image shipping must be refused, not accepted"
            )
            assert any("bootCount is present" in m for m in shipping_vs_bench), shipping_vs_bench
            assert identify_schema(status) is schema, "the shipping payload identifies as shipping"
            assert identify_schema(FIXTURE_STATUS_BODY) is bench_schema, (
                "the bench payload identifies as bench"
            )

        check("a wrong --image is refused by the payload itself", declared_image_is_checked)

        def reset_reason_tri_state() -> None:
            crash = schema.reset_reason(dict(status, resetReason="TASK_WDT"), "self-test")
            assert crash.crash_shaped is True, crash
            ambiguous = schema.reset_reason(dict(status, resetReason="OTHER"), "self-test")
            assert ambiguous.crash_shaped is None, (
                "resetReasonName() collapses CPU_LOCKUP/PWR_GLITCH/USB/JTAG/EFUSE into "
                f"'OTHER'; that must read as unknown, never as clean: {ambiguous}"
            )
            assert ambiguous.caveat and "CPU_LOCKUP" in ambiguous.caveat, ambiguous
            unmapped = schema.reset_reason(dict(status, resetReason="NOPE"), "self-test")
            assert unmapped.crash_shaped is None, unmapped

        check("shipping resetReason is a string and its unknowns stay unknown", reset_reason_tri_state)

        def link_readiness_needs_the_ladder() -> None:
            no_block = {k: v for k, v in status.items() if k != "hostedLink"}
            ready, why_not = schema.link_readiness(no_block)
            assert not ready and "PA_CAP_HOSTED_WIFI" in why_not, why_not
            degraded = dict(status, hostedLink=dict(status["hostedLink"], phase="degraded"))
            ready, why_not = schema.link_readiness(degraded)
            assert not ready, "a degraded ladder is terminal for the boot; not a ready link"

        check("wifiConnected alone is not readiness on shipping", link_readiness_needs_the_ladder)

        def restart_semantics() -> None:
            assert schema.restart_detected(3600000, [3601000, 3602000]) is False, (
                "rising uptime is the device still running"
            )
            assert schema.restart_detected(3600000, [3601000, 1200]) is True, (
                "uptimeMs stepping backwards is the reboot evidence this image has"
            )
            assert schema.restart_detected(3600000, [1200, 5000]) is True, (
                "compared against the previous sample, so a reboot that then runs on is "
                "still caught"
            )
            assert bench_schema.restart_detected(4, [4, 4]) is False
            assert bench_schema.restart_detected(4, [4, 5]) is True
            assert bench_schema.restart_detected(4, [3]) is True, (
                "a power cycle resets bootCount downward; any change is a restart"
            )

        check("restart evidence: uptime regression on shipping, any bootCount change on bench",
              restart_semantics)

        def reset_driver_refuses() -> None:
            posts_before = server.post_count
            result = run_c6_reset_recovery(
                client, schema, recovery_timeout_s=1.0, poll_interval_s=0.1,
                heap_tolerance_pct=20.0, sse_resume_timeout_s=1.0,
            )
            assert result["verdict"] == "UNAVAILABLE", result
            assert any("#243" in reason for reason in result["reasons"]), result["reasons"]
            assert server.post_count == posts_before, (
                "the driver must refuse BEFORE sending anything -- a request that went out "
                "and was discarded is a stub, not a refusal"
            )
            verdict, exit_code = _compose_overall_verdict({"c6_reset_recovery": result})
            assert (verdict, exit_code) == ("INVALID", EXIT_INVALID), (
                f"an unavailable driver must never reach a passing exit code: {verdict} {exit_code}"
            )

        check("c6_reset_recovery refuses on shipping without touching the network",
              reset_driver_refuses)
    finally:
        _stop_fixture_server(server, thread)


def _run_artoo_status_scenarios(failures: list[str]) -> None:
    """The artoo schema, read the way a driver reads it: capture_status() over
    real HTTP, then ArtooStatusSchema's own accessors. The two absences this
    image is defined by -- no hostedLink, no bootCount -- are asserted in both
    directions, because a schema that merely tolerates an absence would also
    accept a payload from the other board."""
    schema = SCHEMAS["artoo"]
    shipping_schema = SCHEMAS["shipping"]
    bench_schema = SCHEMAS["bench"]
    server, thread = _start_fixture_server(status_body=FIXTURE_ARTOO_STATUS_BODY)
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)

        def check(name: str, body: Callable[[], None]) -> None:
            _record_scenario(name, body, failures)

        status = capture_status(client)

        def field_map() -> None:
            assert schema.heap_largest_free(status, "self-test") == 123456, "heapLargest8bit"
            assert schema.sse_clients(status, "self-test") == 1, "sseClients"
            assert schema.restart_marker(status, "self-test") == 3600000, "uptimeMs"
            reset = schema.reset_reason(status, "self-test")
            assert reset.display == "POWERON" and reset.crash_shaped is False, reset
            # The shared system-health block, so these must be exactly the
            # names the shipping schema reads out of the same snprintf.
            assert schema.heap_field == shipping_schema.heap_field, schema.heap_field
            assert schema.sse_clients_field == shipping_schema.sse_clients_field
            assert schema.restart_field == shipping_schema.restart_field

        check("artoo field map is the shared buildStatusJson() block, read identically",
              field_map)

        def hosted_link_absent() -> None:
            assert schema.publishes_recovery_ladder is False, (
                "the hostedLink block is compiled out where PA_CAP_HOSTED_WIFI is 0; "
                "claiming a ladder makes an absent block readable as counters"
            )
            assert HOSTED_LINK_CONTAINER not in status, (
                "fixture sanity: the artoo payload carries no hostedLink object"
            )
            assert schema.ladder(status, "self-test") is None, (
                "an absent ladder must read as None, never as a LadderReading of zeroes"
            )
            assert schema.collect_ladder([status], []) is None, (
                "and the poll-loop reader must not return an empty LadderSamples, which "
                "a caller would read as 'sampled and saw nothing'"
            )
            assert schema.fields_read()["recoveryLadder"] == schema.ladder_absence_note, (
                "the published field map must say the ladder is absent, not show an "
                "empty field map"
            )
            assert "PA_CAP_HOSTED_WIFI" in schema.ladder_absence_note, (
                schema.ladder_absence_note
            )

        check("an absent recovery ladder reads as absent, never as zeroed counters",
              hosted_link_absent)

        def boot_count_absent() -> None:
            assert schema.publishes_boot_count is False
            assert "bootCount" not in status, "fixture sanity: no bootCount on this image"
            report = schema.restart_report(3600000, 3601000, False)
            assert "bootCount" not in json.dumps(report), (
                f"an artoo restart report must not carry a bootCount key at all: {report}"
            )
            assert report == {
                "baselineUptimeMs": 3600000, "finalUptimeMs": 3601000,
                "uptimeMsWentBackwards": False,
            }, report
            assert schema.restart_detected(3600000, [3601000, 1200]) is True, (
                "uptimeMs stepping backwards is the reboot evidence this image has"
            )
            assert schema.restart_detected(3600000, [3601000, 3602000]) is False

        check("artoo restart evidence is uptimeMs, with no bootCount key anywhere",
              boot_count_absent)

        def declared_image_is_checked_both_ways() -> None:
            assert schema.structural_mismatches(status) == [], (
                "the artoo fixture must satisfy the artoo schema"
            )
            # A firebeetle shipping payload read as --image artoo.
            artoo_vs_shipping = schema.structural_mismatches(FIXTURE_SHIPPING_STATUS_BODY)
            assert artoo_vs_shipping, (
                "reading a shipping payload as --image artoo must be refused, not accepted"
            )
            assert any("'hostedLink' is present" in m for m in artoo_vs_shipping), (
                artoo_vs_shipping
            )
            # An artoo payload read as --image shipping.
            shipping_vs_artoo = shipping_schema.structural_mismatches(status)
            assert shipping_vs_artoo, (
                "reading an artoo payload as --image shipping must be refused, not accepted"
            )
            assert any("no 'hostedLink' object" in m for m in shipping_vs_artoo), (
                shipping_vs_artoo
            )
            # And a bench payload, whose bootCount is the giveaway.
            artoo_vs_bench = schema.structural_mismatches(FIXTURE_STATUS_BODY)
            assert any("bootCount is present" in m for m in artoo_vs_bench), artoo_vs_bench
            assert identify_schema(status) is schema, "the artoo payload identifies as artoo"
            assert identify_schema(FIXTURE_SHIPPING_STATUS_BODY) is shipping_schema
            assert identify_schema(FIXTURE_STATUS_BODY) is bench_schema

        check("an artoo payload and a shipping payload each refuse the other's --image",
              declared_image_is_checked_both_ways)

        def readiness_is_wifi_connected_alone_and_says_so() -> None:
            ready, why_not = schema.link_readiness(status)
            assert ready and why_not == "", (
                f"the fixture reports wifiConnected true: {why_not}"
            )
            # Nothing but wifiConnected is consulted: the shipping schema's
            # phase would be missing here, and adding one must not matter.
            with_phase = dict(status)
            with_phase[HOSTED_LINK_CONTAINER] = {"phase": "degraded"}
            assert schema.link_readiness(with_phase)[0] is True, (
                "this schema reads wifiConnected alone; a stray phase must not change it"
            )
            not_ready, diagnostic = schema.link_readiness(dict(status, wifiConnected=False))
            assert not_ready is False, "wifiConnected false is not ready"
            assert "WEAKER evidence" in diagnostic, diagnostic
            assert "hostedLink.phase" in diagnostic and "#184" in diagnostic, diagnostic
            assert "apEnabled || staConnected" in diagnostic, diagnostic
            missing = {k: v for k, v in status.items() if k != "wifiConnected"}
            assert schema.link_readiness(missing)[0] is False, (
                "a missing field is absent evidence, not a ready link"
            )
            assert "not that the link failed" in schema.link_readiness(missing)[1]

        check("artoo readiness reads wifiConnected alone and calls that weaker evidence",
              readiness_is_wifi_connected_alone_and_says_so)

        def reset_driver_refuses() -> None:
            posts_before = server.post_count
            result = run_c6_reset_recovery(
                client, schema, recovery_timeout_s=1.0, poll_interval_s=0.1,
                heap_tolerance_pct=20.0, sse_resume_timeout_s=1.0,
            )
            assert result["verdict"] == "UNAVAILABLE", result
            assert result["image"] == "artoo", result
            reasons = " ".join(result["reasons"])
            assert "no companion radio" in reasons, reasons
            assert "PA_CAP_HOSTED_WIFI is 0" in reasons, reasons
            assert server.post_count == posts_before, (
                "the driver must refuse BEFORE sending anything -- a request that went "
                "out and was discarded is a stub, not a refusal"
            )
            verdict, exit_code = _compose_overall_verdict({"c6_reset_recovery": result})
            assert (verdict, exit_code) == ("INVALID", EXIT_INVALID), (
                f"an unavailable driver must never reach a passing exit code: "
                f"{verdict} {exit_code}"
            )

        check("c6_reset_recovery refuses on artoo, naming the missing radio, without "
              "touching the network", reset_driver_refuses)
    finally:
        _stop_fixture_server(server, thread)


def _run_end_to_end_driver_scenarios(failures: list[str]) -> None:
    """Run the SSE-soak and reconnect-storm drivers themselves, end to end,
    against a fixture serving each image's stream and status payload. The
    scenarios above prove the pieces; these prove the drivers are wired to
    them -- that the bench driver still reports gaps and bootCount after the
    schema split, that the shipping driver reports the heartbeat model and
    never grows a bootCount reading it does not have, and that the artoo
    driver reports neither bootCount nor ladder numbers.

    All three artoo/shipping runs use the same "shipping_live" stream
    deliberately: eventStreamTask() (src/web/web_server.cpp:793-902) carries
    no capability guard, so the two product images put the identical framing
    on the wire and a separate artoo stream fixture would be inventing a
    difference that does not exist."""
    for image, status_body, sse_mode in (
        ("bench", FIXTURE_STATUS_BODY, "bench_live"),
        ("shipping", FIXTURE_SHIPPING_STATUS_BODY, "shipping_live"),
        ("artoo", FIXTURE_ARTOO_STATUS_BODY, "shipping_live"),
    ):
        schema = SCHEMAS[image]
        server, thread = _start_fixture_server(status_body=status_body, sse_mode=sse_mode)
        try:
            port = server.server_address[1]
            client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)

            # The driver call is inside the scenario body, not before it: a
            # mutated reader raises out of the driver itself, and that has to
            # land as this scenario's red line rather than as a traceback.
            def soak_report(image: str = image, schema: StatusSchema = schema) -> None:
                floor = (
                    resolve_admission_floor(schema.build_env)
                    if schema.enforces_admission_floor else None
                )
                cap = resolve_sse_client_cap(schema.build_env)
                soak = run_sse_soak(
                    client, schema, num_clients=1, duration_s=1.0, status_poll_interval_s=0.3,
                    admission_floor=floor, sse_client_cap=cap, early_stall_check_s=1.0,
                    max_silence_s=5.0,
                )
                assert soak["verdict"] == "PASS", soak["reasons"]
                assert soak["image"] == image, soak["image"]
                assert soak["totalFramesReceived"] > 0, soak["totalFramesReceived"]
                assert soak["statusFieldsRead"] == schema.fields_read(), soak["statusFieldsRead"]
                # The cap the run was judged against travels with the driver's
                # own report, not only in the run header.
                assert soak["sseClientCap"] == cap.value, soak
                assert soak["sseClientCapReadFrom"] == cap.source, soak
                assert soak["countsTowardVerdict"] is True, soak
                assert soak["testedAtProductionCap"] is (cap.value == 1), soak
                assert soak["baselineResetReasonAssessment"] == "notCrashShaped", soak
                assert "heapTolerancePct" not in soak, (
                    "the sse_soak heap verdict is no longer a percentage of a baseline "
                    "sample (#194); a report still carrying the tolerance would advertise "
                    "a rule this driver does not apply"
                )
                # The per-poll series, on every image: recording, not judging.
                series = soak["heapSeries"]
                assert series, "a reachable poll must produce a series row"
                assert [row[SERIES_KEY_ELAPSED_S] for row in series] == sorted(
                    row[SERIES_KEY_ELAPSED_S] for row in series
                ), f"series rows must be in poll order: {series}"
                for row in series:
                    assert row[SERIES_KEY_LARGEST_FREE_8BIT] == 123456, row
                    assert row[SERIES_KEY_HEAP_FREE] == 260000, row
                    assert (SERIES_KEY_HEAP_MIN in row) is (schema.heap_min_field is not None), (
                        "a heapMin key on an image that publishes no low-water mark would "
                        f"be an invented reading: {row}"
                    )
                assert soak["statusPollSchemaAnomalyCount"] == 0, soak["statusPollSchemaAnomalies"]
                if image == "bench":
                    assert soak["sseContinuityModel"] == "counter", soak["sseContinuityModel"]
                    assert soak["totalFrameGaps"] == 0, soak["totalFrameGaps"]
                    assert soak["baselineBootCount"] == 1 and soak["bootCountAdvanced"] is False, soak
                    assert soak["perClient"][0]["firstCounter"] == 0, soak["perClient"]
                else:
                    assert soak["sseContinuityModel"] == "heartbeat", soak["sseContinuityModel"]
                    assert soak["totalHeartbeatFrames"] > 0, soak["totalHeartbeatFrames"]
                    assert "totalFrameGaps" not in soak, (
                        "a counter gap count against a stream with no counter would read 0 "
                        "however badly the stream stalled -- the vacuous pass this schema "
                        "mode exists to avoid"
                    )
                    assert "baselineBootCount" not in soak, (
                        "the shipping image publishes no bootCount; a report carrying one "
                        "would be an invented reading"
                    )
                    assert soak["baselineUptimeMs"] == 3600000, soak
                    assert soak["uptimeMsWentBackwards"] is False, soak
                if image == "artoo":
                    assert soak["recoveryLadderEvidence"] == schema.ladder_absence_note, soak
                    for ladder_key in (
                        "recoveryLadderReachedDegraded", "recoveryLadderStatesObserved",
                        "baselineRecoveryLadderState",
                        "baselineHostedTransportUpEventCount",
                        "finalHostedTransportUpEventCount",
                        "hostedTransportUpEventCountAdvancedBy",
                    ):
                        assert ladder_key not in soak, (
                            f"{ladder_key!r} on an artoo report would claim this run "
                            "watched a ladder the image does not publish"
                        )
                else:
                    assert "recoveryLadderEvidence" not in soak, soak
                    assert soak["recoveryLadderReachedDegraded"] is False, soak

                if schema.enforces_admission_floor:
                    assert soak["admissionFloorEnv"] == schema.build_env, soak
                    assert soak["admissionOrdinaryFloorBytes"] == floor.ordinary_bytes, soak
                    assert soak["admissionDiagnosticFloorBytes"] == floor.diagnostic_bytes, soak
                    assert soak["minLargestFreeBlockMarginBytes"] == (
                        123456 - floor.ordinary_bytes), soak
                    assert soak["refusedHeapFloorAdvancedBy"] == 0, soak
                    assert soak["refusedHeapFloorDiagAdvancedBy"] == 0, soak
                    assert soak["finalAcceptMinLargestBlockSeen"] == 40000, soak
                    assert "admissionFloorEvidence" not in soak, soak
                else:
                    # An image with no admission guard reports the absence in
                    # words. A refusal count of 0 here would claim this run
                    # watched a gate the binary does not contain.
                    assert soak["admissionFloorEvidence"] == schema.admission_absence_note, soak
                    for admission_key in (
                        "admissionFloorEnv", "admissionOrdinaryFloorBytes",
                        "minLargestFreeBlockMarginBytes", "baselineRefusedHeapFloor",
                        "refusedHeapFloorAdvancedBy", "finalAcceptMinLargestBlockSeen",
                    ):
                        assert admission_key not in soak, (
                            f"{admission_key!r} on a {image} report would claim a floor "
                            "this image has no code to refuse at"
                        )

            _record_scenario(
                f"{image}: sse_soak runs end to end and reports its own image's numbers",
                soak_report, failures)

            def storm_report(image: str = image, schema: StatusSchema = schema) -> None:
                storm = run_reconnect_storm(
                    client, schema, storm_clients=1, duration_s=0.6, cycle_min_s=0.1,
                    cycle_max_s=0.2, settle_s=0.1, heap_tolerance_pct=20.0,
                    max_silence_s=0.5, liveness_cycle_every=1,
                )
                assert storm["verdict"] == "PASS", storm["reasons"]
                assert storm["image"] == image, storm["image"]
                assert storm["cycleCount"] > 0 and storm["streamsOpened"] > 0, storm
                assert storm["capacityRefusals"] == 0, storm
                # The fixture streams a frame every 200ms, so every cycle that
                # opened one saw frames: liveness is genuinely assessed, and
                # no cycle is silent.
                assert storm["cyclesDeliveredFrames"] > 0, storm
                assert storm["cyclesSilentPastBudget"] == 0, storm
                assert (storm["cyclesDeliveredFrames"] + storm["cyclesSilentPastBudget"]
                        + storm["cyclesLivenessNotAssessed"]) == storm["cycleCount"], storm
                if image == "bench":
                    assert storm["baselineBootCount"] == 1, storm
                else:
                    assert "baselineBootCount" not in storm, storm
                    assert storm["baselineUptimeMs"] == 3600000, storm

            _record_scenario(
                f"{image}: reconnect_storm runs end to end and reports its own image's numbers",
                storm_report, failures)
        finally:
            _stop_fixture_server(server, thread)


def _run_admission_floor_scenarios(failures: list[str]) -> None:
    """The heap verdict's yardstick, read from the real platformio.ini through
    the production resolver, and then driven end to end through run() -- the
    same entry point main() calls, argv and all.

    Nothing here restates 9000. A scenario that asserted the number would pass
    against a harness that had stopped reading the file, which is the whole
    defect this rule exists to avoid; what is asserted instead is that the
    values come from [flags_base], that an environment which does not reference
    it resolves nothing, and that the bench override displaces the ordinary
    floor and only the ordinary floor. The literal values are pinned against an
    independent read of platformio.ini in
    test/test_tools/test_soak_schema.py."""

    def check(name: str, body: Callable[[], None]) -> None:
        _record_scenario(name, body, failures)

    def floors_come_from_flags_base() -> None:
        for env in ("artoo_esp32", "firebeetle2"):
            floor = resolve_admission_floor(env)
            assert floor.env == env, floor
            assert floor.sources[ADMISSION_FLOOR_MACRO].startswith("[flags_base]"), (
                f"{env} must resolve its floor from [flags_base], got "
                f"{floor.sources[ADMISSION_FLOOR_MACRO]!r}"
            )
            assert floor.diagnostic_bytes < floor.ordinary_bytes, (
                "read-only diagnostics keep a LOWER floor than ordinary requests, which is "
                f"why /api/status still answers under pressure: {floor}"
            )
            assert floor.override_bytes is None, floor
            assert floor.ordinary_bytes == floor.declared_ordinary_bytes, floor
        # Both product images ship the same calibrated floor today; if a board
        # ever needs its own, per-env resolution is already what reports it.
        assert (resolve_admission_floor("artoo_esp32").ordinary_bytes
                == resolve_admission_floor("firebeetle2").ordinary_bytes)

    check("the admission floor is read per environment from platformio.ini [flags_base]",
          floors_come_from_flags_base)

    def an_env_that_never_references_flags_base_resolves_nothing() -> None:
        # Not a hypothetical: [env:native] declares its own build_flags and
        # names ${flags_base.build_flags} nowhere, so no floor reaches it. An
        # environment nobody calibrated is INVALID, never judged against a
        # default (#194).
        try:
            floor = resolve_admission_floor("native")
        except BuildConstantUnresolved as unresolved:
            message = str(unresolved)
            assert ADMISSION_FLOOR_MACRO in message, message
            assert "flags_base" in message, message
            assert "INVALID" in message, message
            return
        raise AssertionError(
            f"env:native declares no admission floor, but the resolver produced {floor}"
        )

    check("an environment that does not inherit [flags_base] resolves no floor at all",
          an_env_that_never_references_flags_base_resolves_nothing)

    def an_unknown_env_is_refused_by_name() -> None:
        try:
            resolve_admission_floor("not_an_environment")
        except BuildConstantUnresolved as unresolved:
            assert "not_an_environment" in str(unresolved), str(unresolved)
            assert "artoo_esp32" in str(unresolved), (
                f"the refusal must list what IS declared: {unresolved}"
            )
            return
        raise AssertionError("an environment platformio.ini does not declare must be refused")

    check("an environment platformio.ini does not declare is refused, listing the real ones",
          an_unknown_env_is_refused_by_name)

    def the_bench_override_displaces_only_the_ordinary_floor() -> None:
        # src/web/web_admission.cpp:216-220: the override replaces the ordinary
        # floor when non-zero; diagnostics keep their own either way, which is
        # what leaves /api/status readable during an induced-pressure session.
        product = resolve_admission_floor("artoo_esp32")
        induced = resolve_admission_floor("artoo_esp32_recovery_bench")
        assert induced.override_bytes is not None, induced
        assert induced.ordinary_bytes == induced.override_bytes, induced
        assert induced.ordinary_bytes > product.ordinary_bytes, (
            "the induced-pressure env raises the ordinary floor above the resting heap; "
            f"got {induced.ordinary_bytes} against {product.ordinary_bytes}"
        )
        assert induced.declared_ordinary_bytes == product.ordinary_bytes, (
            "the displaced value is still reported, so a reader can see that the floor in "
            f"force is an override rather than a calibration: {induced}"
        )
        assert induced.diagnostic_bytes == product.diagnostic_bytes, induced

    check("a bench floor override displaces the ordinary floor and not the diagnostic one",
          the_bench_override_displaces_only_the_ordinary_floor)

    def every_schema_names_a_real_environment() -> None:
        for name, schema in SCHEMAS.items():
            require_declared_environment(schema.build_env)
            if schema.enforces_admission_floor:
                assert resolve_admission_floor(schema.build_env).ordinary_bytes > 0, name
            else:
                assert schema.admission_absence_note, (
                    f"{name} judges no floor, so it owes the report a reason in words"
                )

    check("every image names a build environment platformio.ini actually declares",
          every_schema_names_a_real_environment)


def _cap_override_env_section(env: str, clients: int) -> str:
    """A platformio.ini environment that declares the client cap, appended to a
    copy of the real file so an override is resolved by the file's own
    composition rules rather than by a synthetic stand-in.

    A child that declares build_src_flags replaces its parent's outright
    (see _pio_flag_sources), so this is the whole of that option for this env,
    while build_flags still reaches [flags_base] through `extends`.
    """
    return (
        f"\n[env:{env}]\n"
        "extends = env:artoo_esp32\n"
        f"build_src_flags = -D {SSE_CLIENT_CAP_MACRO}={clients}\n"
    )


def _run_sse_client_cap_scenarios(failures: list[str]) -> None:
    """The concurrency yardstick, read from the real include/web_event_stream.h
    through the production resolver.

    Nothing here restates the number, for the same reason the floor scenarios
    do not restate 9000: a scenario that asserted 3 would keep passing against
    a harness that had stopped reading the header, which is the exact rot this
    derivation exists to remove. What is asserted instead is that the value
    comes from the header, that a per-environment -D displaces it, and that an
    unreadable or ambiguous header is refused rather than defaulted.
    """

    def check(name: str, body: Callable[[], None]) -> None:
        _record_scenario(name, body, failures)

    def the_cap_comes_from_the_header() -> None:
        # The header is read HERE, by this scenario's own regex, and the
        # resolver is judged against that. Asking read_header_sse_client_cap()
        # what the header says and then checking the resolver agrees would be
        # the reader agreeing with itself -- a resolver that had stopped
        # reading the file and returned a constant would pass. That shape of
        # self-agreeing check is what this whole harness exists not to ship.
        text = WEB_EVENT_STREAM_HEADER.read_text(encoding="utf-8")
        assert f"#ifndef {SSE_CLIENT_CAP_MACRO}" in text, (
            "the cap must stay #ifndef-guarded, or a build could not override it and "
            "resolving an override would be resolving something unreachable"
        )
        declared = re.findall(
            rf"(?m)^\s*#\s*define\s+{re.escape(SSE_CLIENT_CAP_MACRO)}\s+(\d+)\s*$", text)
        assert len(declared) == 1, f"expected exactly one #define, found {declared}"
        header_default = int(declared[0])
        header_source = str(WEB_EVENT_STREAM_HEADER.relative_to(REPO_ROOT))
        assert read_header_sse_client_cap() == (header_default, header_source), (
            f"the header reader must return what the file says: {declared}"
        )
        for name, schema in SCHEMAS.items():
            cap = resolve_sse_client_cap(schema.build_env)
            assert cap.env == schema.build_env, cap
            assert cap.header_default == header_default, cap
            assert cap.value == header_default, (
                f"{name}: no environment declares {SSE_CLIENT_CAP_MACRO} today, so every "
                f"image resolves the header default: {cap}"
            )
            assert cap.source == header_source, cap

    check("the SSE client cap is read from include/web_event_stream.h, per image",
          the_cap_comes_from_the_header)

    def an_environment_d_displaces_the_header_default() -> None:
        # No environment declares the macro today, so the override path is
        # exercised against the REAL platformio.ini with one extra environment
        # appended -- a branch nobody has shown working is a branch nobody
        # should rely on. Appended as a child of artoo_esp32 rather than edited
        # into it, so the file's own composition rules are what resolve it: the
        # child's build_src_flags replaces the parent's, and build_flags still
        # reaches [flags_base] through `extends`.
        header_default, header_source = read_header_sse_client_cap()
        overridden = header_default + 2
        with tempfile.TemporaryDirectory() as directory:
            ini_path = Path(directory) / "platformio.ini"
            ini_path.write_text(
                PLATFORMIO_INI.read_text(encoding="utf-8")
                + _cap_override_env_section("soak_selftest_cap_override", overridden),
                encoding="utf-8",
            )
            cap = resolve_sse_client_cap("soak_selftest_cap_override", ini_path=ini_path)
            assert cap.value == overridden, cap
            assert cap.header_default == header_default, (
                "the displaced default is still reported, so a reader can see that the "
                f"cap in force is an override rather than the header's number: {cap}"
            )
            assert cap.source.startswith("[env:soak_selftest_cap_override]"), cap
            # And an environment with no -D still gets the header's number out
            # of the same file, so the override is local rather than global.
            neighbour = resolve_sse_client_cap("artoo_esp32", ini_path=ini_path)
            assert neighbour.value == header_default, neighbour
            assert neighbour.source == header_source, neighbour

    check("a build environment's -D displaces the header default, and is reported as an "
          "override", an_environment_d_displaces_the_header_default)

    def a_header_that_declares_no_cap_is_refused() -> None:
        with tempfile.TemporaryDirectory() as directory:
            empty = Path(directory) / "web_event_stream.h"
            empty.write_text("// no cap here\n", encoding="utf-8")
            try:
                read_header_sse_client_cap(empty)
            except BuildConstantUnresolved as unresolved:
                assert SSE_CLIENT_CAP_MACRO in str(unresolved), str(unresolved)
                assert "0 time(s)" in str(unresolved), str(unresolved)
            else:
                raise AssertionError(
                    "a header with no cap must be refused, never defaulted -- a soak run at "
                    "a concurrency this harness invented is a soak measuring nothing anyone "
                    "specified"
                )
            missing = Path(directory) / "not_a_file.h"
            try:
                read_header_sse_client_cap(missing)
            except BuildConstantUnresolved as unresolved:
                assert "could not read" in str(unresolved), str(unresolved)
            else:
                raise AssertionError("an unreadable header must be refused, never defaulted")

    check("a header that declares no cap, or cannot be read, is refused rather than "
          "defaulted", a_header_that_declares_no_cap_is_refused)

    def a_cap_below_one_is_refused() -> None:
        with tempfile.TemporaryDirectory() as directory:
            ini_path = Path(directory) / "platformio.ini"
            ini_path.write_text(
                PLATFORMIO_INI.read_text(encoding="utf-8")
                + _cap_override_env_section("soak_selftest_cap_zero", 0),
                encoding="utf-8",
            )
            try:
                resolve_sse_client_cap("soak_selftest_cap_zero", ini_path=ini_path)
            except BuildConstantUnresolved as unresolved:
                assert "admits no stream at all" in str(unresolved), str(unresolved)
                return
        raise AssertionError(
            "a cap of zero refuses the first client, so there is no concurrency to soak at"
        )

    check("a cap of zero is refused: there would be no concurrency to run at",
          a_cap_below_one_is_refused)


def _run_orchestrated_scenario(
    name: str, status_body: Any, extra_args: list[str],
    check: Callable[[dict, int, http.server.ThreadingHTTPServer], None],
    failures: list[str],
) -> None:
    """Drive run() -- main()'s own entry point, through the real argument
    parser -- against an artoo fixture, and assert on the report and exit code
    it produces. Nothing here reimplements the orchestration or the verdict."""
    server, thread = _start_fixture_server(status_body=status_body, sse_mode="shipping_live")
    try:
        port = server.server_address[1]
        args = build_parser().parse_args([
            "--device", "127.0.0.1", "--port", str(port), "--image", "artoo",
            "--driver", "sse_soak", "--duration", "1.0", "--num-clients", "1",
            "--status-poll-interval-s", "0.2", "--early-stall-check-s", "1.0",
            *extra_args,
        ])
        report, exit_code = run(args)
        _record_scenario(name, lambda: check(report, exit_code, server), failures)
    finally:
        _stop_fixture_server(server, thread)


def _run_heap_verdict_scenarios(failures: list[str]) -> None:
    """The heap verdict itself, end to end: a healthy run, a run that crossed
    the floor, a run the controller refused during, and a run whose floor could
    not be resolved."""
    floor = resolve_admission_floor(SCHEMAS["artoo"].build_env)

    def healthy(report: dict, exit_code: int, _server) -> None:
        assert exit_code == EXIT_PASS, (exit_code, report)
        assert report["verdict"] == "PASS", report
        assert report["buildEnv"] == "artoo_esp32", report
        assert report["admissionFloor"]["ordinaryFloorBytes"] == floor.ordinary_bytes, report
        # Both yardsticks are on the run header, with their provenance, so the
        # artefact says what it judged against and not only what it concluded.
        cap = resolve_sse_client_cap("artoo_esp32")
        assert report["sseClientCap"]["clients"] == cap.value, report
        assert report["sseClientCap"]["readFrom"] == cap.source, report
        assert report["sseClientCap"]["env"] == "artoo_esp32", report
        # The artefact belongs to this instrument, not to the ticket that
        # produced it: a consumer switches on schemaVersion and finds no ticket
        # number to key off.
        assert report["schemaVersion"] == REPORT_SCHEMA_VERSION, report
        assert "issue" not in report, report
        soak = report["drivers"]["sse_soak"]
        assert soak["verdict"] == "PASS", soak["reasons"]
        assert soak["minLargestFreeBlockMarginBytes"] > 0, soak
        assert soak["minLargestFreeBlockMarginPct"] > 0, soak

    _run_orchestrated_scenario(
        "a run that stayed above the floor passes, and reports its margin",
        FIXTURE_ARTOO_STATUS_BODY, [], healthy, failures)

    # Above the diagnostic floor and below the ordinary one -- the real shape
    # of this failure, and the reason it is observable at all: /api/status is a
    # diagnostic path (webPathIsDiagnostic(), src/web/web_admission.cpp:232-249)
    # and keeps answering while ordinary page loads are already being shed.
    below_floor = floor.ordinary_bytes - 1
    assert below_floor > floor.diagnostic_bytes, (
        "fixture derivation: this scenario needs a reading between the two floors"
    )

    def crossed(report: dict, exit_code: int, _server) -> None:
        assert exit_code == EXIT_FAIL, (exit_code, report)
        soak = report["drivers"]["sse_soak"]
        assert soak["verdict"] == "FAIL", soak
        assert soak["minLargestFreeBlockMarginBytes"] < 0, soak
        assert any("admission floor" in reason for reason in soak["reasons"]), soak["reasons"]
        assert any(str(floor.ordinary_bytes) in reason for reason in soak["reasons"]), (
            soak["reasons"]
        )

    _run_orchestrated_scenario(
        "a run whose largest free block fell below the ordinary floor is a FAIL",
        dict(FIXTURE_ARTOO_STATUS_BODY, heapLargest8bit=below_floor), [], crossed, failures)

    # A counter that MOVES, which a static payload cannot express: the failure
    # this rule exists to catch is a rise across the run, and #194's graded run
    # would have passed every other check while it happened.
    refusal_clock: list[float] = []

    def refusing_body() -> dict:
        # The clock starts at the FIRST status read -- run()'s preflight -- not
        # when this scenario was set up, so the baseline is always taken before
        # the counter moves however slow the machine is. A wall-clock offset
        # from setup time would make this scenario pass or fail on load.
        if not refusal_clock:
            refusal_clock.append(time.monotonic())
        refused = 0 if time.monotonic() - refusal_clock[0] < 0.4 else 7
        return dict(FIXTURE_ARTOO_STATUS_BODY, refusedHeapFloor=refused)

    def refused(report: dict, exit_code: int, _server) -> None:
        assert exit_code == EXIT_FAIL, (exit_code, report)
        soak = report["drivers"]["sse_soak"]
        assert soak["refusedHeapFloorAdvancedBy"] == 7, soak
        assert any("refused 7 ordinary requests" in reason for reason in soak["reasons"]), (
            soak["reasons"]
        )
        # The heap reading never moved: this FAIL comes from the controller's
        # own count and from nothing else, which is the point of reading it.
        assert soak["minLargestFreeBlockMarginBytes"] > 0, soak

    _run_orchestrated_scenario(
        "a run the controller refused requests during is a FAIL on the counter alone",
        refusing_body, [], refused, failures)

    # The counters are cumulative within a boot, so a DECREASE cannot happen
    # without them being reset. Read as "no refusals" it would be a silent pass
    # on a run whose refusal evidence does not span the run at all.
    reset_clock: list[float] = []

    def counter_reset_body() -> dict:
        if not reset_clock:
            reset_clock.append(time.monotonic())
        refused = 5 if time.monotonic() - reset_clock[0] < 0.4 else 0
        return dict(FIXTURE_ARTOO_STATUS_BODY, refusedHeapFloor=refused)

    def counters_reset(report: dict, exit_code: int, _server) -> None:
        assert exit_code == EXIT_FAIL, (exit_code, report)
        soak = report["drivers"]["sse_soak"]
        assert soak["refusedHeapFloorAdvancedBy"] == -5, soak
        assert any("went backwards" in reason for reason in soak["reasons"]), soak["reasons"]

    _run_orchestrated_scenario(
        "a refusal counter that went backwards is reported, never read as 'nothing refused'",
        counter_reset_body, [], counters_reset, failures)

    def unresolvable(report: dict, exit_code: int, server) -> None:
        assert exit_code == EXIT_INVALID, (exit_code, report)
        assert report["verdict"] == "INVALID", report
        assert report["drivers"] == {}, report
        assert "admissionFloor" not in report, (
            "an unresolved floor must leave no floor in the report at all -- a null there "
            f"would still look like an answer: {report}"
        )
        assert "sseClientCap" not in report, (
            "the run stopped at the floor, so no cap was resolved either -- a cap in the "
            f"report would claim a yardstick this run never took: {report}"
        )
        assert any(ADMISSION_FLOOR_MACRO in reason for reason in report["reasons"]), report
        assert server.status_get_count == 0, (
            "the floor is resolved before the first request, so a run that cannot be judged "
            f"never disturbs the controller: {server.status_get_count} status GET(s) went out"
        )

    _run_orchestrated_scenario(
        "an environment with no floor is INVALID, decided before the device is touched",
        FIXTURE_ARTOO_STATUS_BODY, ["--build-env", "native"], unresolvable, failures)


def _run_storm_liveness_scenarios(failures: list[str]) -> None:
    """What the reconnect storm is entitled to conclude about the stream
    staying alive, driven through run_reconnect_storm() itself.

    The driver used to fail every healthy board: it passed its 0.25s abort
    granularity to stream_sse() as the read window, and an expired read window
    was reported as a stalled stream -- so any stream slower than 4 Hz was
    "stalled", which is every stream this harness drives. Fixing that moves the
    line between pass and fail, so these three scenarios pin BOTH sides of it:
    a healthy stream must pass, a genuinely silent one must still fail, and a
    storm that never watched long enough to know must say so rather than pass.
    """
    def storm_against(mode: str, **kwargs) -> dict:
        server, thread = _start_fixture_server(
            status_body=FIXTURE_ARTOO_STATUS_BODY, sse_mode=mode)
        try:
            client = BenchClient("127.0.0.1", server.server_address[1], connect_timeout_s=5.0)
            options = dict(
                storm_clients=1, duration_s=1.2, cycle_min_s=0.1, cycle_max_s=0.2,
                settle_s=0.1, heap_tolerance_pct=20.0, max_silence_s=0.5,
                liveness_cycle_every=1,
            )
            options.update(kwargs)
            return run_reconnect_storm(client, SCHEMAS["artoo"], **options)
        finally:
            _stop_fixture_server(server, thread)

    def a_one_hz_stream_is_not_a_stall() -> None:
        # 1 Hz is four times slower than the old fixed 0.25s window, so this
        # is the exact shape of stream the driver used to fail.
        storm = storm_against("shipping_live", duration_s=2.5, max_silence_s=3.0,
                              cycle_min_s=1.2, cycle_max_s=1.5, liveness_cycle_every=0)
        assert storm["verdict"] == "PASS", storm["reasons"]
        assert storm["cyclesSilentPastBudget"] == 0, storm
        assert storm["cyclesDeliveredFrames"] > 0, storm
        assert storm["unexpectedErrorsDuringHold"] == 0, storm

    _record_scenario(
        "a stream slower than the old read window is no longer called a stall",
        a_one_hz_stream_is_not_a_stall, failures)

    def a_silent_stream_still_fails() -> None:
        storm = storm_against("silent_stream")
        assert storm["verdict"] == "FAIL", storm
        assert storm["cyclesSilentPastBudget"] > 0, storm
        assert storm["cyclesDeliveredFrames"] == 0, storm
        assert any("received no frame at all" in reason for reason in storm["reasons"]), (
            storm["reasons"]
        )

    _record_scenario(
        "a stream that opens and then says nothing is still a stall",
        a_silent_stream_still_fails, failures)

    def a_storm_that_never_looked_says_so() -> None:
        # The server hangs up before a frame can arrive, so every cycle
        # watched too little to judge. That must not read as a pass.
        storm = storm_against("empty_stream", liveness_cycle_every=0)
        assert storm["verdict"] == "FAIL", storm
        assert storm["cyclesDeliveredFrames"] == 0, storm
        assert storm["cyclesSilentPastBudget"] == 0, storm
        assert storm["cyclesLivenessNotAssessed"] == storm["cycleCount"], storm
        assert any("liveness evidence" in reason for reason in storm["reasons"]), (
            storm["reasons"]
        )

    _record_scenario(
        "a storm that never watched long enough reports that, rather than passing",
        a_storm_that_never_looked_says_so, failures)


def _run_interrupt_scenarios(failures: list[str]) -> None:
    """An interrupted run, driven through run() -- main()'s own entry point,
    through the real argument parser -- against a live fixture.

    This is the most dangerous code in the harness, because a truncated run is
    exactly where "verification that cannot judge itself" comes back: a soak
    stopped after twenty seconds has neither passed nor failed, and reporting
    either would be a claim about a window nobody watched. So an operator at
    the bench can prove the property here, without the repo's test suite.

    No signal is sent: a real SIGTERM would land on whatever process is
    running the self-test. The interrupt flag is set directly, which is the
    only thing the signal handler itself does.
    """
    server, thread = _start_fixture_server(
        status_body=FIXTURE_ARTOO_STATUS_BODY, sse_mode="shipping_live")
    try:
        port = server.server_address[1]

        def interrupted_run(driver: str, after_s: Optional[float]) -> tuple[dict, int]:
            """Run `driver` and interrupt it after `after_s` seconds, or before
            it starts at all when that is None."""
            interrupt = threading.Event()
            timer: Optional[threading.Timer] = None
            if after_s is None:
                interrupt.set()
            else:
                timer = threading.Timer(after_s, interrupt.set)
                timer.daemon = True
                timer.start()
            monitor = RunMonitor(interval_s=0.0, quiet=True, interrupt=interrupt)
            monitor._signal_name = "SIGTERM"
            args = build_parser().parse_args([
                "--device", "127.0.0.1", "--port", str(port), "--image", "artoo",
                "--driver", driver, "--duration", "30.0", "--num-clients", "1",
                "--status-poll-interval-s", "0.2", "--early-stall-check-s", "1.0",
                "--storm-duration", "30.0", "--storm-clients", "1",
                "--storm-cycle-min-s", "0.1", "--storm-cycle-max-s", "0.2",
                "--storm-settle-s", "0.1",
            ])
            try:
                return run(args, monitor)
            finally:
                if timer is not None:
                    timer.cancel()

        def a_truncated_soak_is_not_a_pass() -> None:
            report, exit_code = interrupted_run("sse_soak", after_s=0.8)
            assert exit_code == EXIT_INVALID, (exit_code, report["verdict"])
            assert report["verdict"] == VERDICT_INTERRUPTED_RUN, report["verdict"]
            soak = report["drivers"]["sse_soak"]
            assert soak["verdict"] == VERDICT_INTERRUPTED_DRIVER, soak["verdict"]
            assert soak["verdict"] != "PASS", soak
            assert soak["interrupted"] is True, soak
            assert soak["durationSObserved"] < soak["durationSRequested"], soak
            assert any("interrupted" in reason for reason in soak["reasons"]), soak["reasons"]

        _record_scenario(
            "an interrupted soak reports INTERRUPTED, not PASS, and records both durations",
            a_truncated_soak_is_not_a_pass, failures)

        def a_truncated_storm_keeps_what_it_collected() -> None:
            report, exit_code = interrupted_run("reconnect_storm", after_s=0.8)
            assert exit_code == EXIT_INVALID, (exit_code, report["verdict"])
            storm = report["drivers"]["reconnect_storm"]
            assert storm["verdict"] == VERDICT_INTERRUPTED_DRIVER, storm["verdict"]
            # The post-storm evidence is still gathered: the settle window runs
            # even on an interrupt, because those readings are the storm's own
            # measurement and are what the interrupt was trying to preserve.
            assert "postSseClientsConnected" in storm, storm

        _record_scenario(
            "an interrupted storm still reports the cycles and readings it did take",
            a_truncated_storm_keeps_what_it_collected, failures)

        def every_driver_is_accounted_for() -> None:
            report, _ = interrupted_run("all", after_s=None)
            assert set(report["drivers"]) == {
                "sse_soak", "reconnect_storm", "c6_reset_recovery"}, report["drivers"]
            for name, result in report["drivers"].items():
                assert result["verdict"] == VERDICT_INTERRUPTED_DRIVER, (name, result)
                assert result["started"] is False, (name, result)
            assert report["phases"] == [], (
                "a driver that never ran must leave no phase; a zero-second phase would "
                f"claim it ran and took no time: {report['phases']}"
            )

        _record_scenario(
            "a driver the interrupt reached before it started is recorded, never omitted",
            every_driver_is_accounted_for, failures)
    finally:
        _stop_fixture_server(server, thread)


def _run_json_scenarios(failures: list[str]) -> None:
    server, thread = _start_fixture_server()
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)
        try:
            status_code, body = client.get_json("/api/status")
            assert status_code == 200, f"expected 200, got {status_code}"
            assert body == FIXTURE_STATUS_BODY, f"body mismatch: {body}"
            print("  PASS  get_json() parses /api/status through the real BenchClient")
        except AssertionError as failure:
            print(f"  FAIL  get_json(): {failure}")
            failures.append(f"get_json: {failure}")
        try:
            status_code, body = client.post_json("/api/c6/reset")
            assert status_code == 202, f"expected 202, got {status_code}"
            assert body == FIXTURE_RESET_BODY, f"body mismatch: {body}"
            print("  PASS  post_json() parses /api/c6/reset through the real BenchClient")
        except AssertionError as failure:
            print(f"  FAIL  post_json(): {failure}")
            failures.append(f"post_json: {failure}")
    finally:
        _stop_fixture_server(server, thread)


def run_self_test() -> int:
    print(
        "Running offline self-tests -- the real BenchClient.stream_sse()/get_json()/"
        "post_json(), stream_sse_with_continuity(), the status-schema readers, the "
        "admission-floor and SSE-client-cap resolvers that read platformio.ini and "
        "include/web_event_stream.h, and run() itself, against a local http.server "
        "fixture serving byte-exact PsychicEventSource (bench) and "
        "webEventStreamFormatPrefix (shipping) framing. No device required, no inline "
        "parse loop.\n"
    )
    failures: list[str] = []
    shipping = SCHEMAS["shipping"]

    print("bench image:")
    _run_sse_scenario("normal frames parse with zero gaps", "normal", _check_normal, failures)
    _run_sse_scenario("gapped fixture reports exactly 2 gaps", "gapped", _check_gapped, failures)
    _run_sse_scenario(
        "truncated fixture is reported, not silently dropped", "truncated", _check_truncated, failures,
    )
    _run_sse_scenario(
        "mid-stream reset is recorded on the result, never raised", "connreset", _check_connreset, failures,
    )
    _run_json_scenarios(failures)

    print("\nshipping image:")
    _run_continuity_scenario(
        "a healthy shipping stream reports its event census and no FAIL reason",
        "shipping_normal", shipping, 5.0, _check_shipping_normal, failures,
        stop_after_frames=5,
    )
    _run_continuity_scenario(
        "a stream the peer ends early is a FAIL reason",
        "shipping_normal", shipping, 5.0, _check_shipping_ended_early, failures,
    )
    _run_continuity_scenario(
        "a silence past the limit is a FAIL reason",
        "shipping_silence", shipping, 0.2, _check_shipping_silence, failures,
    )
    _run_continuity_scenario(
        "a frame id going backwards is a FAIL reason",
        "shipping_id_regression", shipping, 5.0, _check_shipping_id_regression, failures,
    )
    _run_continuity_scenario(
        "a bench-style nameless frame is reported, not counted as an event",
        "shipping_nameless", shipping, 5.0, _check_shipping_nameless, failures,
    )
    _run_shipping_status_scenarios(failures)

    print("\nartoo image:")
    _run_artoo_status_scenarios(failures)

    print("\nadmission floor, read from platformio.ini:")
    _run_admission_floor_scenarios(failures)

    print("\nSSE client cap, read from include/web_event_stream.h:")
    _run_sse_client_cap_scenarios(failures)

    print("\nheap verdict, driven through run():")
    _run_heap_verdict_scenarios(failures)

    print("\nall three images, drivers end to end:")
    _run_end_to_end_driver_scenarios(failures)

    print("\nreconnect storm liveness:")
    _run_storm_liveness_scenarios(failures)

    print("\ninterrupted runs, driven through run():")
    _run_interrupt_scenarios(failures)

    if failures:
        print(f"\n{len(failures)} self-test failure(s) detected.")
        return EXIT_SELF_TEST_FAILURE
    print("\nAll self-tests passed.")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "protoArtoo soak harness: hold a controller's web stack under load for as "
            "long as you ask, and say in one verdict and one exit code whether it held "
            "up. Reads only -- it never flashes, never calls `make ota` and never writes "
            "to the controller's configuration. See docs/soak.md."
        ),
    )
    parser.add_argument(
        "--self-test", action="store_true",
        help="run offline self-tests against a local http.server fixture (no device "
             "required); exits non-zero if any assertion fails",
    )
    parser.add_argument(
        "--device", default=None,
        help="controller hostname or IP. Required for any real run: the bench image "
             "registers no mDNS name, so there is no default that would be right for "
             "every board",
    )
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--connect-timeout-s", type=float, default=10.0)
    parser.add_argument(
        "--image", choices=sorted(SCHEMAS), default="bench",
        help="which firmware image is on the board, and therefore which /api/status "
             "schema to read: 'bench' (bringup/p4_hosted_bench.cpp, env "
             "firebeetle2_hosted_bench), 'shipping' (the firebeetle2 product image) or "
             "'artoo' (the artoo_esp32 product image). Declared, never sniffed -- the "
             "declaration is checked against the payload at preflight and a mismatch is "
             "INVALID. Neither product image has a C6 reset route, so --driver "
             "c6_reset_recovery is refused on both, for different reasons (#243 on "
             "shipping; artoo-esp32 has no companion radio at all). The artoo image also "
             "publishes no recovery ladder, so its readiness check reads wifiConnected "
             "with nothing to corroborate it -- weaker evidence than shipping's, and its "
             "diagnostic says so",
    )
    parser.add_argument(
        "--build-env", default=None,
        help="the PlatformIO environment the board is running, and therefore the one "
             "whose compiled values this run is judged against: "
             "PA_ADMISSION_MIN_LARGEST_FREE_BLOCK for the heap verdict and "
             "PA_ADMISSION_MAX_SSE_CLIENTS for the concurrency one. Defaults to the "
             "product environment for --image (artoo -> artoo_esp32, shipping -> "
             "firebeetle2, bench -> firebeetle2_hosted_bench). Set it when the board "
             "carries a variant build: artoo_esp32_recovery_bench raises the ordinary "
             "floor to 40000 and publishes a payload byte-identical to artoo_esp32's, so "
             "nothing in /api/status can tell the two apart and the harness will not "
             "guess. Both values are READ -- from platformio.ini and from "
             "include/web_event_stream.h -- and never restated here; an environment that "
             "resolves no floor (env:native declares its own build_flags and never "
             "references [flags_base]) makes the run INVALID rather than judged against a "
             "default",
    )
    parser.add_argument(
        "--driver", choices=["all", "sse_soak", "reconnect_storm", "c6_reset_recovery"], default="all",
    )
    parser.add_argument(
        "--duration", type=float, default=1800.0,
        help="sse_soak duration in seconds (default 1800 = 30 minutes). A soak is worth "
             "the time it is given: hours are what this driver is for",
    )
    parser.add_argument(
        "--num-clients", type=int, default=None,
        help="concurrent SSE clients for sse_soak. Defaults to the client cap the "
             "firmware compiles in (PA_ADMISSION_MAX_SSE_CLIENTS, resolved for "
             "--build-env from include/web_event_stream.h and any -D that displaces it) "
             "-- deliberately not a number written into this tool, which would go on "
             "meaning 'at the cap' after the cap had moved. At or below the cap a run "
             "counts toward the verdict; above it the run is recorded as observation "
             "only, because past the cap the firmware refuses the extra stream by "
             "design and what is being measured is admission, not the transport",
    )
    parser.add_argument("--status-poll-interval-s", type=float, default=5.0)
    parser.add_argument(
        "--early-stall-check-s", type=float, default=10.0,
        help="if no sse_soak client has received a single frame within this many "
             "seconds, fail fast as 'SSE immediately stalled' instead of waiting out "
             "the full --duration",
    )
    parser.add_argument(
        "--sse-max-silence-s", type=float, default=5.0,
        help="shipping-image continuity limit: the longest interval a client may go "
             "with no SSE frame before it is a FAIL. eventStreamTask() "
             "(src/web/web_server.cpp) ticks once a second while any client is "
             "connected, so 5s allows several missed ticks of scheduling jitter without "
             "accepting a stall. Ignored for --image bench, whose stream carries a "
             "monotonic counter and is judged arithmetically instead",
    )
    parser.add_argument("--storm-clients", type=int, default=3)
    parser.add_argument("--storm-duration", type=float, default=120.0)
    parser.add_argument("--storm-cycle-min-s", type=float, default=0.5)
    parser.add_argument("--storm-cycle-max-s", type=float, default=3.0)
    parser.add_argument("--storm-settle-s", type=float, default=5.0)
    parser.add_argument(
        "--storm-max-silence-s", type=float, default=5.0,
        help="reconnect_storm liveness budget: how long a held-open stream may go with no "
             "frame before that cycle counts as a stall. The same 5s default and the same "
             "reasoning as --sse-max-silence-s -- both /api/events implementations tick "
             "about once a second, so 5s allows several missed ticks of scheduling jitter "
             "without accepting silence. This is ALSO the socket read window, which used to "
             "be a fixed 0.25s poll interval whose every expiry was reported as a stalled "
             "stream: that classified any stream slower than 4 Hz as broken, which is every "
             "stream this harness drives",
    )
    parser.add_argument(
        "--storm-liveness-cycle-every", type=int, default=4,
        help="hold every Nth cycle per worker for longer than --storm-max-silence-s, so "
             "that receiving no frame during it is a real stall rather than an "
             "unremarkable short look (default 4). Without such a cycle the storm's "
             "ordinary sub-second holds can never conclude anything about the stream "
             "staying alive. 0 disables them, in which case a run whose short cycles "
             "happen to see no frames reports that it could not judge liveness rather "
             "than passing",
    )
    parser.add_argument("--reset-recovery-timeout-s", type=float, default=30.0)
    parser.add_argument("--reset-poll-interval-s", type=float, default=0.5)
    parser.add_argument("--sse-resume-timeout-s", type=float, default=10.0)
    parser.add_argument(
        "--heap-recovery-tolerance-pct", type=float, default=20.0,
        help="allowed percentage drop in largestFree8bitBlock when comparing the reading "
             "AFTER a deliberate disturbance against the one before it: the post-storm "
             "settle in reconnect_storm and the post-rejoin reading in c6_reset_recovery. "
             "It governs recovery comparisons only. The sse_soak heap verdict is NOT a "
             "percentage of a baseline -- it is taken against the compiled admission floor "
             "(--build-env), because a percentage of an arbitrary sample says nothing about "
             "whether the controller was still serving",
    )
    parser.add_argument(
        "--progress-interval-s", type=float, default=DEFAULT_PROGRESS_INTERVAL_S,
        help="seconds between heartbeat lines on stderr and between --json checkpoint "
             f"writes (default {DEFAULT_PROGRESS_INTERVAL_S:g}). 0 disables both. A "
             "heartbeat carries the run and driver clocks, the driver's own activity, and "
             "whatever the declared image publishes of the heap floor, the admission and "
             "eviction counters and the recovery ladder; a reading this image publishes "
             f"but a given sample did not carry prints as {PROGRESS_ABSENT!r}, and a "
             "reading the image does not publish at all is left off the line entirely -- "
             "never a zero. On a terminal a single status line also refreshes once a "
             "second between heartbeats, so a wait never looks like a hang. All of it is "
             "stderr: stdout carries the JSON report and nothing else",
    )
    parser.add_argument(
        "--no-progress", action="store_true",
        help="silence stderr progress: no header, no heartbeats, no status line, no "
             "footer. The transcript log and the --json checkpoints are unaffected, "
             "because a quiet run still wants a record and still wants its artefact to "
             "survive a kill",
    )
    parser.add_argument(
        "--log", default=None,
        help="path for the plain transcript of this run -- every line stderr showed, with "
             "no colour and no cursor control, so it can be read, grepped and archived. "
             "Always written: defaults to the --json path with '.log' appended, or to "
             "./soak-<timestamp>.log when there is no --json. Appended to, never "
             "truncated, so pointing two runs at one path keeps both transcripts. The "
             "path is recorded in the report as logPath so the next tool does not have to "
             "parse a terminal to find it",
    )
    parser.add_argument(
        "--json", default=None,
        help="also write the full report to this path. Written periodically during the "
             "run as well as at the end (see --progress-interval-s), through a temporary "
             "file in the same directory and os.replace(), so a hard kill leaves the last "
             "checkpoint intact rather than a half-written file. A checkpoint carries the "
             f"verdict {VERDICT_CHECKPOINT!r} and can never be mistaken for a finished run",
    )
    return parser


def default_log_path(json_path: Optional[Path], started_at: float) -> Path:
    """Where the transcript goes when --log was not given.

    Beside the --json artefact when there is one, so a run's two files travel
    together and an operator who tars up a results directory gets both.
    Otherwise a timestamped file in the working directory, because a run with
    no transcript is exactly the state this harness was in when a three-hour
    soak produced nothing at all.
    """
    if json_path is not None:
        return json_path.with_suffix(json_path.suffix + ".log")
    return Path(f"soak-{time.strftime('%Y%m%dT%H%M%S', time.localtime(started_at))}.log")


def first_failure(report: dict) -> Optional[str]:
    """The first reason from the first driver that did not pass, for the
    footer. None when every driver passed -- a footer that invented a failure
    line would be worse than one that omits it."""
    for name, result in report.get("drivers", {}).items():
        if result.get("verdict") in DRIVER_VERDICTS_WITHOUT_A_FINDING:
            continue
        reasons = result.get("reasons") or []
        return f"{name}: {reasons[0] if reasons else result.get('verdict')}"
    return None


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if not args.device:
        parser.error("--device is required (the bench sketch has no mDNS name to default to)")
    # None means "not given", which run() resolves to the compiled client cap.
    # Only a number the operator actually typed can be out of range here.
    if args.num_clients is not None and args.num_clients < 1:
        parser.error("--num-clients must be >= 1")
    if args.storm_clients < 1:
        parser.error("--storm-clients must be >= 1")

    json_path: Optional[Path] = None
    if args.json:
        json_path = Path(args.json)
        # Checked before the device is touched, not after the run. A missing
        # directory used to surface as an exception at the very last statement
        # of a multi-hour run, which threw the whole run away -- the same
        # all-or-nothing failure the checkpointing exists to remove.
        if not json_path.parent.is_dir():
            parser.error(
                f"--json {json_path}: directory {json_path.parent} does not exist, so "
                "neither the checkpoints nor the final report could be written"
            )

    started_at = time.time()
    log_path = Path(args.log) if args.log else default_log_path(json_path, started_at)
    if not log_path.parent.is_dir():
        parser.error(f"--log {log_path}: directory {log_path.parent} does not exist")

    console = RunConsole(quiet=args.no_progress, log_path=log_path)
    monitor = RunMonitor(
        interval_s=args.progress_interval_s,
        quiet=args.no_progress,
        console=console,
        started_at=started_at,
        checkpoint_writer=(
            None if json_path is None
            else lambda checkpoint, path=json_path: write_json_artifact(
                path, json.dumps(checkpoint, indent=2, default=str) + "\n")
        ),
    )
    # Installed before anything long-running starts, so a Ctrl-C two seconds in
    # is handled the same way as one two hours in.
    monitor.install_signal_handlers()

    drivers_to_run = (
        ["sse_soak", "reconnect_storm", "c6_reset_recovery"]
        if args.driver == "all" else [args.driver]
    )
    planned = {name: planned_driver_duration_s(name, args) for name in drivers_to_run}
    monitor.rule("protoArtoo soak")
    monitor.rows([
        ("started", format_timestamp(started_at)),
        ("device", f"{args.device}:{args.port}"),
        # "image mode" and "soak driver" are the project's words for these
        # (CONTEXT.md): the image whose /api/status schema is read, declared
        # here and checked against the payload at preflight, and the named
        # Soak Drivers this run will attempt.
        ("image mode", f"{args.image} (build env {args.build_env or SCHEMAS[args.image].build_env})"),
        ("soak drivers", ", ".join(
            f"{name} {format_duration(seconds)}" if seconds is not None else name
            for name, seconds in planned.items())),
        ("planned", format_duration(
            sum(value for value in planned.values() if value is not None))),
        ("log", str(log_path)),
        ("report", str(json_path) if json_path is not None else "<stdout only>"),
    ])
    console.start_status_line()

    try:
        report, exit_code = run(args, monitor)
    except BaseException as failure:
        # Recorded in the transcript and then re-raised untouched: a harness
        # bug must still reach the operator as a traceback (AGENTS.md, never
        # swallow an error), but a multi-hour run whose log simply stops is a
        # log that does not say why it stopped.
        console.stop_status_line()
        monitor.line(f"run aborted: {type(failure).__name__}: {failure}", kind="fail")
        console.close()
        raise
    finally:
        # The status line goes away before anything reaches stdout; the
        # transcript stays open for the footer below.
        console.stop_status_line()

    rendered = json.dumps(report, indent=2, default=str)
    print(rendered)
    if json_path is not None:
        # The same bytes the pre-checkpoint harness wrote, through the atomic
        # write so the final report cannot be the one truncated write either.
        write_json_artifact(json_path, rendered + "\n")

    slowest = max(report.get("phases") or [], key=lambda phase: phase["durationS"], default=None)
    failure = first_failure(report)
    verdict_kind = (
        "ok" if exit_code == EXIT_PASS
        else "warn" if exit_code == EXIT_INVALID else "fail"
    )
    monitor.rule("finished")
    monitor.rows([
        ("elapsed", f"{format_duration(report['durationS'])} of a planned "
                    f"{format_duration(report['plannedDurationS'])}"),
        ("soak drivers", ", ".join(
            f"{name} {result['verdict']}"
            for name, result in report.get("drivers", {}).items()) or "<none ran>"),
        ("slowest", "<none ran>" if slowest is None
                    else f"{slowest['driver']} {format_duration(slowest['durationS'])}"),
        ("first failure", failure or "<none>"),
        ("log", str(log_path)),
        ("report", str(json_path) if json_path is not None else "<stdout only>"),
    ])
    # The word, not the string: the verdict itself is interpolated from the
    # report, so a later rewording of the verdict vocabulary (ADR 0035) does
    # not have to unpick this line.
    monitor.line(f"run verdict: {report['verdict']} (exit {exit_code})", kind=verdict_kind)
    console.close()

    # The yardsticks are printed next to the verdict, not only buried in the
    # JSON: a reader scanning the tail of a run has to be able to see WHICH
    # floor the heap verdict was taken against, and WHICH cap decided whether
    # the run counted, without going looking for them.
    floor_report = report.get("admissionFloor")
    if isinstance(floor_report, dict):
        print(
            f"\nAdmission floor: {floor_report['ordinaryFloorBytes']} bytes ordinary / "
            f"{floor_report['diagnosticFloorBytes']} diagnostic, from build environment "
            f"{floor_report['env']} ({floor_report['macros'][ADMISSION_FLOOR_MACRO]} in "
            f"{floor_report['readFrom']})",
            file=sys.stderr,
        )
    elif isinstance(floor_report, str):
        print(f"\nAdmission floor: {floor_report}", file=sys.stderr)
    cap_report = report.get("sseClientCap")
    if isinstance(cap_report, dict):
        print(
            f"SSE client cap: {cap_report['clients']} concurrent stream(s) for build "
            f"environment {cap_report['env']} ({cap_report['macro']} in "
            f"{cap_report['readFrom']})",
            file=sys.stderr,
        )
    if report.get("interrupted"):
        print(
            f"\nInterrupted by {report.get('interruptSignal')}: the report above covers "
            "only the window actually observed and reaches no verdict about the controller"
            + (f". Written to {json_path}" if json_path is not None else ""),
            file=sys.stderr,
        )
    print(f"\nVerdict: {report['verdict']} (exit {exit_code})", file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
