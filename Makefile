# =============================================================================
# protoArtoo — build facade
#
# Running bare `make` launches the interactive wizard (tools/deploy.py).
#
# Power-user named targets are listed below for scripted / CI use.
# All targets delegate to PlatformIO (pio) — no build logic lives here.
#
# Variables (CLI or user.mk):
#   make ota OTA_IP=192.168.4.1
#   make flash UPLOAD_PORT=/dev/ttyACM0   (required when two boards are attached)
#   make ota BUILD_ENV=artoo_esp32_chirp
#   make ota OTA_HOST_PORT=32000   (only if 32320 is taken; firewall it instead if you can)
# =============================================================================

OTA_IP      ?= artoo.local
BUILD_ENV   ?= artoo_esp32
OTA_TIMEOUT ?= 60
OTA_TRANSFER_TIMEOUT ?= 60

# Local (host) TCP port espota listens on for the device's OTA connect-back.
# Espota picks a random port by default; a default-deny inbound firewall then
# drops the device's connection and the failure surfaces as the misleading
# "[ERROR]: No response from device" — the device accepted the push fine, the
# host just never answered on the port it invited the device to use. Pinning
# one fixed port lets a single firewall rule allow it permanently instead of
# reopening a moving target on every OTA push. See docs/troubleshooting.md
# ("OTA fails with 'No response from device'") for the rule to add.
# Shared by every target that calls tools/ota_upload.py — both boards need it.
OTA_HOST_PORT ?= 32320

# ── USB upload port: resolved or refused, never assumed ───────────────────
# `UPLOAD_PORT ?= /dev/ttyUSB0` used to live above. On a one-board bench it was
# a harmless default; on a two-board bench it is a guess about *which board*.
# On 2026-09-03 it aimed an ESP32-P4 image at the artoo-esp32: PlatformIO logged
# "Looking for upload port... Auto-detected: /dev/ttyUSB0" and esptool refused
# with "This chip is ESP32, not ESP32-P4". The chip check was the only thing
# between a named port and the wrong board, and the artoo was left in the ROM
# download stub, off the network.
#
# Two faults, so two fixes, and both are needed:
#
#   1. tools/resolve_upload_port.py resolves the port or REFUSES with the list
#      of attached devices. It never picks one when more than one could be meant.
#
#   2. The result is exported as PLATFORMIO_UPLOAD_PORT, not just passed as
#      `--upload-port`. Measured the same day: make passed
#      `--upload-port /dev/ttyACM0` and PlatformIO auto-detected anyway, because
#      the upload runs as a nested re-invocation that a CLI flag does not survive.
#      The environment variable does. `--upload-port` is still passed, for the
#      paths where it is honoured and because it keeps the command self-describing.
RESOLVE_PORT = python3 tools/resolve_upload_port.py --origin '$(origin UPLOAD_PORT)'

# ── Toolchain isolation: artoo-esp32 vs ESP32-P4 ─────────────────────────────
# The two chip targets pin different pioarduino platform versions, and those
# versions require *different versions of the same packages*
# (framework-arduinoespressif32 3.3.7 vs 3.3.11, tool-esptoolpy 5.1.2 vs 5.3.0,
# tool-cppcheck 2.11.0 vs 2.20.1). PlatformIO installs packages into
# unversioned directories under its core dir, so a shared core dir means every
# switch between the targets swaps the whole Arduino core in place — slow when
# serialized, and corrupting when two builds overlap.
#
# Each chip target therefore gets its own PLATFORMIO_CORE_DIR. The Makefile
# selects it from BUILD_ENV using the explicit P4_ENVS list below; it does not
# inspect platformio.ini to discover an env's chip target. Adding another P4
# Board Variant costs a config.h pin-map block, a platformio.ini env, and one
# P4_ENVS entry (ADR 0028).
#
#   make build                              -> artoo-esp32 core dir (default)
#   make build BUILD_ENV=firebeetle2        -> P4 core dir
#   make build BUILD_ENV=<new_p4_board>     -> P4 core dir after adding it to P4_ENVS
#
# Override either path in user.mk if you keep toolchains elsewhere.
PIO_CORE_DIR_ARTOO ?= $(HOME)/.platformio
PIO_CORE_DIR_P4    ?= $(HOME)/.platformio-p4

# Envs that target the ESP32-P4 chip, for PLATFORMIO_CORE_DIR selection.
# Read from build_budgets.json platforms registry (single source of truth).
P4_ENVS := $(shell python3 -c "import json; d=json.load(open('tools/build_budgets.json')); print(' '.join(d['platforms']['esp32p4'].get('envs', [])))")

ifeq ($(strip $(P4_ENVS)),)
$(error P4_ENVS is empty: could not read platforms.esp32p4.envs from tools/build_budgets.json)
endif

# Map BUILD_ENV to chip target, then to core dir. Lookup is chip-aware:
# if BUILD_ENV is in P4_ENVS, use P4 core dir; otherwise use artoo-esp32 core dir.
PIO_CORE_DIR = $(if $(filter $(P4_ENVS),$(BUILD_ENV)),$(PIO_CORE_DIR_P4),$(PIO_CORE_DIR_ARTOO))

# Use $(PIO) for BUILD_ENV-parameterised firmware targets. Targets that hard-code
# an artoo-esp32 env (chirp, mp3trigger, dysv5w, check*) keep bare `pio` on
# purpose: they must stay on the artoo-esp32 core dir even when BUILD_ENV points
# at P4.
PIO = PLATFORMIO_CORE_DIR=$(PIO_CORE_DIR) $(FLOCK) pio

# Only one PlatformIO build may run on this machine at a time (AGENTS.md "The
# build lock"). Every pio invocation below goes through the lock rather than
# relying on the caller to type `flock` in front of make, and tools/pio_lock.py
# holds the lock for exactly as long as the command it execs. Deliberately not
# `?=`: a lock that is one `make FLOCK= build` away from being off is a habit
# again. The escape hatch for a contiguous multi-command window is
# `PROTOARTOO_PIO_LOCK_HELD=1 flock /tmp/protoartoo-pio.lock <commands>`.
FLOCK := python3 tools/pio_lock.py

-include user.mk

.PHONY: all help build test test-web test-tools check check-action-drift check-build-budgets flash ota uploadfs \
        flash-chirp ota-chirp ota-mp3trigger \
        flash-dysv5w ota-dysv5w \
        flash-monitor flash-chirp-monitor \
        check-chirp check-mp3trigger \
        setup setup-wifi clean monitor console bench-rows check-deps

# Default target — launches the interactive wizard
all:
	python3 tools/deploy.py

help: ## Show available targets
	@echo ""
	@echo "  Running 'make' launches the interactive wizard."
	@echo ""
	@echo "  Named targets (power-user / CI):"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(firstword $(MAKEFILE_LIST)) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "    %-22s %s\n", $$1, $$2}'
	@echo ""

# ── Core ─────────────────────────────────────────────────────────────────────

build: ## Compile firmware  (BUILD_ENV=artoo_esp32 by default)
	$(PIO) run -e $(BUILD_ENV)
	@python3 tools/check_framework_envelope.py --env $(BUILD_ENV) --quiet

test: ## Run native unit tests
	$(FLOCK) pio test -e native

# Canonical web-suite invocation. The quoted glob is expanded by node itself:
# `node --test test/test_web/` (directory form) fails with MODULE_NOT_FOUND
# disguised as a one-test failure. The per-test timeout turns a hung test into
# a counted failure instead of a vanished `cancelledByParent` entry.
# tools/slice_verify.py runs the same invocation; keep the flags in sync.
test-web: ## Run web behavioral tests (node:test)
	node --test --test-reporter=tap --test-timeout=10000 'test/test_web/test_*.js'

test-tools: ## Run Python tooling tests (incl. slice gate self-tests)
	python3 -m unittest discover -s test/test_tools -q

check: ## Static analysis with cppcheck
	$(FLOCK) pio check -e artoo_esp32

check-action-drift: ## Ad hoc check that action YAML, C++, and RC fallback metadata align
	python3 tools/check_action_registry_drift.py

check-build-budgets: ## Verify all supported envs stay within flash/RAM budgets
	python3 tools/check_build_budgets.py

# Runs automatically after `make build`. Separate target for checking a build
# someone else produced, or for re-checking after a repair. The build budget
# cannot substitute for this: a pristine (envelope-OFF) image is ~111 KB larger
# and still comfortably inside its budget, which is exactly how the 2026-08-29
# regression stayed invisible.
check-envelope: ## Verify an env's custom_sdkconfig actually held in the built framework libs
	python3 tools/check_framework_envelope.py --env $(BUILD_ENV)

# ── Flash: DY-SV5W (default) ─────────────────────────────────────────────────

flash: test ## Flash via USB  (UPLOAD_PORT=/dev/... ; required if two boards are attached)
	@port=$$($(RESOLVE_PORT) --env $(BUILD_ENV)) && \
	  echo "==> flashing $(BUILD_ENV) to $$port" && \
	  PLATFORMIO_UPLOAD_PORT=$$port $(PIO) run -e $(BUILD_ENV) -t upload --upload-port $$port

ota: test ## Flash via OTA  (OTA_IP=artoo.local by default)
	$(PIO) run -e $(BUILD_ENV)_ota
	python3 tools/ota_upload.py --env $(BUILD_ENV)_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT) --host-port $(OTA_HOST_PORT)

# Filesystem upload transport. artoo-esp32 envs upload over OTA through their
# `_ota` env. ESP32-P4 envs have no `_ota` env - there is no OTA transport
# without a network backend - so they upload over USB with the same env and
# port `make flash` uses. Membership in P4_ENVS decides, as for PIO_CORE_DIR.
UPLOADFS_ENV  = $(if $(filter $(P4_ENVS),$(BUILD_ENV)),$(BUILD_ENV),$(BUILD_ENV)_ota)

# The OTA branch goes through tools/ota_upload.py, not pio's `-t uploadfs`.
# PlatformIO invokes espota directly and lets it pick a RANDOM local callback
# port (measured: host_port=21870), which a default-deny inbound firewall drops
# — surfacing as the misleading "No response from device" that OTA_HOST_PORT
# above exists to prevent. `make ota` never hit this because it already routes
# through the wrapper; `make uploadfs` did, and could not upload an FS image to
# the artoo at all. The wrapper also brings the board-identity guard (#252) and
# the project transfer timeout, so the two OTA paths now agree on all three.
uploadfs: ## Upload LittleFS web UI  (OTA to OTA_IP; P4 envs: USB, port resolved; no test gate)
	@if [ -n "$(filter $(P4_ENVS),$(BUILD_ENV))" ]; then \
	  port=$$($(RESOLVE_PORT) --env $(BUILD_ENV)) || exit 1; \
	  echo "==> uploading filesystem for $(UPLOADFS_ENV) to $$port" && \
	  PLATFORMIO_UPLOAD_PORT=$$port $(PIO) run -e $(UPLOADFS_ENV) -t uploadfs --upload-port $$port; \
	else \
	  echo "==> uploading filesystem for $(UPLOADFS_ENV) to $(OTA_IP)" && \
	  $(PIO) run -e $(UPLOADFS_ENV) -t buildfs && \
	  python3 tools/ota_upload.py --env $(UPLOADFS_ENV) --spiffs --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT) --host-port $(OTA_HOST_PORT); \
	fi

# ── Flash: CHIRP audio module ────────────────────────────────────────────────

flash-chirp: test ## Flash CHIRP build via USB
	@port=$$($(RESOLVE_PORT) --env artoo_esp32_chirp) && \
	  echo "==> flashing artoo_esp32_chirp to $$port" && \
	  PLATFORMIO_UPLOAD_PORT=$$port $(FLOCK) pio run -e artoo_esp32_chirp -t upload --upload-port $$port

flash-monitor: test ## Flash default build via USB then capture boot log
	@port=$$($(RESOLVE_PORT) --env $(BUILD_ENV)) && \
	  echo "==> flashing $(BUILD_ENV) to $$port" && \
	  PLATFORMIO_UPLOAD_PORT=$$port $(PIO) run -e $(BUILD_ENV) -t upload --upload-port $$port && \
	  python3 tools/console_client.py --port $$port --until "init complete" --timeout 30

flash-chirp-monitor: test ## Flash CHIRP build via USB then capture boot log
	@port=$$($(RESOLVE_PORT) --env artoo_esp32_chirp) && \
	  echo "==> flashing artoo_esp32_chirp to $$port" && \
	  PLATFORMIO_UPLOAD_PORT=$$port $(FLOCK) pio run -e artoo_esp32_chirp -t upload --upload-port $$port && \
	  python3 tools/console_client.py --port $$port --until "init complete" --timeout 30

ota-chirp: test ## Flash CHIRP build via OTA
	$(FLOCK) pio run -e artoo_esp32_chirp_ota
	python3 tools/ota_upload.py --env artoo_esp32_chirp_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT) --host-port $(OTA_HOST_PORT)

# ── Flash: MP3 Trigger ───────────────────────────────────────────────────────

ota-mp3trigger: test ## Flash MP3 Trigger build via OTA
	$(FLOCK) pio run -e artoo_esp32_mp3trigger_ota
	python3 tools/ota_upload.py --env artoo_esp32_mp3trigger_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT) --host-port $(OTA_HOST_PORT)

# ── Flash: DY-SV5W (named env) ───────────────────────────────────────────────
# Same driver as the default artoo_esp32 env — these targets exist so DY-SV5W
# has the same explicit, discoverable build/flash surface as CHIRP and MP3 Trigger.

flash-dysv5w: test ## Flash DY-SV5W build via USB
	@port=$$($(RESOLVE_PORT) --env artoo_esp32_dysv5w) && \
	  echo "==> flashing artoo_esp32_dysv5w to $$port" && \
	  PLATFORMIO_UPLOAD_PORT=$$port $(FLOCK) pio run -e artoo_esp32_dysv5w -t upload --upload-port $$port

ota-dysv5w: test ## Flash DY-SV5W build via OTA
	$(FLOCK) pio run -e artoo_esp32_dysv5w_ota
	python3 tools/ota_upload.py --env artoo_esp32_dysv5w_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT) --host-port $(OTA_HOST_PORT)

# ── Compile-check only ───────────────────────────────────────────────────────

check-chirp: ## Compile-check CHIRP backend  (no flash)
	$(FLOCK) pio run -e artoo_esp32_chirp_check

check-mp3trigger: ## Compile-check MP3Trigger backend  (no flash)
	$(FLOCK) pio run -e artoo_esp32_mp3trigger_check

# ── Setup & tools ────────────────────────────────────────────────────────────

setup: ## First-time setup wizard  (writes user.mk)
	python3 tools/configure.py

setup-wifi: ## Configure WiFi credentials  (writes src/secrets.h)
	python3 tools/configure.py --wifi

clean: ## Remove PlatformIO build artifacts
	$(PIO) run -t clean

monitor: ## Open serial monitor  (UPLOAD_PORT=/dev/... to pick a board)
	@port=$$($(RESOLVE_PORT)) && python3 tools/console_client.py --port $$port

# ── Controller Console: interactive session and bench-row replay ─────────────
# tools/console_client.py has three modes; `monitor` above only ever reached the
# read-only capture one. These two expose the other two. Both resolve the port
# through RESOLVE_PORT exactly as `monitor` does — a literal or defaulted port
# is the fault c0eca355 removed, after `make flash` aimed an ESP32-P4 image at
# the artoo and only esptool's chip check stopped it.
#
# Reference for the client itself, including the directive grammar these sheets
# are written in: docs/console-client.md.

BENCH_ROWS  ?=
ROWS        ?=
SKIP_MANUAL ?=

console: ## Interactive Console session  (UPLOAD_PORT=/dev/... to pick a board)
	@port=$$($(RESOLVE_PORT)) && python3 tools/console_client.py --port $$port --interactive

bench-rows: ## Replay a Console bench sheet  (BENCH_ROWS=tools/bench_rows/<board>.txt [ROWS=a,b] [SKIP_MANUAL=1])
	@if [ -z "$(BENCH_ROWS)" ]; then \
	  echo "BENCH_ROWS is required: a bench sheet is board-specific and is never guessed."; \
	  echo "  make bench-rows BENCH_ROWS=tools/bench_rows/firebeetle2.txt"; \
	  echo "Sheets in this tree:"; \
	  ls tools/bench_rows/*.txt; \
	  exit 1; \
	fi
	@port=$$($(RESOLVE_PORT)) && python3 tools/console_client.py --port $$port \
	  --script $(BENCH_ROWS) $(if $(ROWS),--rows $(ROWS)) $(if $(SKIP_MANUAL),--skip-manual)

check-deps: ## Check required OS commands and Python packages are installed
	@command -v python3 >/dev/null 2>&1 || { \
		echo "  MISSING   python3"; \
		echo "            sudo dnf install python3 python3-pip   (Fedora/RHEL)"; \
		echo "            sudo apt install python3 python3-pip   (Debian/Ubuntu)"; \
		echo "            brew install python3                   (macOS)"; \
		exit 1; }
	python3 tools/check_deps.py
