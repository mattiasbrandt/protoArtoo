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
#   make flash UPLOAD_PORT=/dev/ttyUSB1
#   make ota BUILD_ENV=protoArtoo_chirp
# =============================================================================

OTA_IP      ?= artoo.local
BUILD_ENV   ?= protoArtoo
UPLOAD_PORT ?= /dev/ttyUSB0
OTA_TIMEOUT ?= 60
OTA_TRANSFER_TIMEOUT ?= 60

# ── Toolchain isolation: artoo-esp32 vs ESP32-P4 ─────────────────────────────
# The two chip targets pin different pioarduino platform versions, and those
# versions require *different versions of the same packages*
# (framework-arduinoespressif32 3.3.7 vs 3.3.11, tool-esptoolpy 5.1.2 vs 5.3.0,
# tool-cppcheck 2.11.0 vs 2.20.1). PlatformIO installs packages into
# unversioned directories under its core dir, so a shared core dir means every
# switch between the targets swaps the whole Arduino core in place — slow when
# serialized, and corrupting when two builds overlap.
#
# Each chip target therefore gets its own PLATFORMIO_CORE_DIR, selected from
# BUILD_ENV via the board declaration in platformio.ini. This is chip-aware
# (per ADR 0028): adding a new P4 board variant costs only a platformio.ini env
# (with board = dfrobot_firebeetle2_esp32p4) and a config.h pin-map block.
# The Makefile auto-discovers the board type from platformio.ini, so no changes
# are needed here — satisfying ADR 0028's acceptance test.
#
#   make build                              -> artoo-esp32 core dir (default)
#   make build BUILD_ENV=protoArtoo_p4      -> P4 core dir (board = dfrobot_firebeetle2_esp32p4)
#   make build BUILD_ENV=<new_p4_board>     -> P4 core dir (auto-detected if board = dfrobot_firebeetle2_esp32p4)
#
# Override either path in user.mk if you keep toolchains elsewhere.
PIO_CORE_DIR_ARTOO ?= $(HOME)/.platformio
PIO_CORE_DIR_P4    ?= $(HOME)/.platformio-p4

# Envs that target the ESP32-P4 chip, for PLATFORMIO_CORE_DIR selection only.
# Keyed on chip, not board name, per ADR 0028 - but this IS a hard-coded list:
# adding a P4 board variant means adding its env here as well as to
# platformio.ini and config.h. ADR 0028's "a new board costs only a pin map plus
# an env" does not hold for this line, and that is a known, accepted exception.
P4_ENVS := protoArtoo_p4 protoArtoo_p4_hosted_bench

# Map BUILD_ENV to chip target, then to core dir. Lookup is chip-aware:
# if BUILD_ENV is in P4_ENVS, use P4 core dir; otherwise use artoo-esp32 core dir.
PIO_CORE_DIR = $(if $(filter $(P4_ENVS),$(BUILD_ENV)),$(PIO_CORE_DIR_P4),$(PIO_CORE_DIR_ARTOO))

# Use $(PIO) for BUILD_ENV-parameterised firmware targets. Targets that hard-code
# an artoo-esp32 env (chirp, mp3trigger, dysv5w, check*) keep bare `pio` on
# purpose: they must stay on the artoo-esp32 core dir even when BUILD_ENV points
# at P4.
PIO = PLATFORMIO_CORE_DIR=$(PIO_CORE_DIR) pio

-include user.mk

.PHONY: all help build test test-web test-tools check check-action-drift flash ota uploadfs \
        flash-chirp ota-chirp ota-mp3trigger \
        flash-dysv5w ota-dysv5w \
        flash-monitor flash-chirp-monitor \
        check-chirp check-mp3trigger \
        setup setup-wifi clean monitor check-deps

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

build: ## Compile firmware  (BUILD_ENV=protoArtoo by default)
	$(PIO) run -e $(BUILD_ENV)

test: ## Run native unit tests
	pio test -e native

# Canonical web-suite invocation. The quoted glob is expanded by node itself:
# `node --test test/test_web/` (directory form) fails with MODULE_NOT_FOUND
# disguised as a one-test failure. The per-test timeout turns a hung test into
# a counted failure instead of a vanished `cancelledByParent` entry.
# tools/slice_verify.py runs the same invocation; keep the flags in sync.
test-web: ## Run web behavioral tests (node:test)
	node --test --test-timeout=10000 'test/test_web/test_*.js'

test-tools: ## Run Python tooling tests (incl. slice gate self-tests)
	python3 -m unittest discover -s test/test_tools -q

check: ## Static analysis with cppcheck
	pio check -e protoArtoo

check-action-drift: ## Ad hoc check that action YAML, C++, and RC fallback metadata align
	python3 tools/check_action_registry_drift.py

# ── Flash: DY-SV5W (default) ─────────────────────────────────────────────────

flash: test ## Flash via USB  (UPLOAD_PORT=/dev/ttyUSB0)
	$(PIO) run -e $(BUILD_ENV) -t upload --upload-port $(UPLOAD_PORT)

ota: test ## Flash via OTA  (OTA_IP=artoo.local by default)
	$(PIO) run -e $(BUILD_ENV)_ota
	python3 tools/ota_upload.py --env $(BUILD_ENV)_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

uploadfs: ## Upload LittleFS web UI via OTA  (no test gate)
	$(PIO) run -e $(BUILD_ENV)_ota -t uploadfs --upload-port $(OTA_IP)

# ── Flash: CHIRP audio module ────────────────────────────────────────────────

flash-chirp: test ## Flash CHIRP build via USB
	pio run -e protoArtoo_chirp -t upload --upload-port $(UPLOAD_PORT)

flash-monitor: test ## Flash default build via USB then capture boot log
	$(PIO) run -e $(BUILD_ENV) -t upload --upload-port $(UPLOAD_PORT)
	python3 tools/serial_monitor.py --until "init complete" --timeout 30

flash-chirp-monitor: test ## Flash CHIRP build via USB then capture boot log
	pio run -e protoArtoo_chirp -t upload --upload-port $(UPLOAD_PORT)
	python3 tools/serial_monitor.py --until "init complete" --timeout 30

ota-chirp: test ## Flash CHIRP build via OTA
	pio run -e protoArtoo_chirp_ota
	python3 tools/ota_upload.py --env protoArtoo_chirp_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

# ── Flash: MP3 Trigger ───────────────────────────────────────────────────────

ota-mp3trigger: test ## Flash MP3 Trigger build via OTA
	pio run -e protoArtoo_mp3trigger_ota
	python3 tools/ota_upload.py --env protoArtoo_mp3trigger_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

# ── Flash: DY-SV5W (named env) ───────────────────────────────────────────────
# Same driver as the default protoArtoo env — these targets exist so DY-SV5W
# has the same explicit, discoverable build/flash surface as CHIRP and MP3 Trigger.

flash-dysv5w: test ## Flash DY-SV5W build via USB
	pio run -e protoArtoo_dysv5w -t upload --upload-port $(UPLOAD_PORT)

ota-dysv5w: test ## Flash DY-SV5W build via OTA
	pio run -e protoArtoo_dysv5w_ota
	python3 tools/ota_upload.py --env protoArtoo_dysv5w_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

# ── Compile-check only ───────────────────────────────────────────────────────

check-chirp: ## Compile-check CHIRP backend  (no flash)
	pio run -e protoArtoo_chirp_check

check-mp3trigger: ## Compile-check MP3Trigger backend  (no flash)
	pio run -e protoArtoo_mp3trigger_check

# ── Setup & tools ────────────────────────────────────────────────────────────

setup: ## First-time setup wizard  (writes user.mk)
	python3 tools/configure.py

setup-wifi: ## Configure WiFi credentials  (writes src/secrets.h)
	python3 tools/configure.py --wifi

clean: ## Remove PlatformIO build artifacts
	$(PIO) run -t clean

monitor: ## Open serial monitor
	python3 tools/serial_monitor.py

check-deps: ## Check required OS commands and Python packages are installed
	@command -v python3 >/dev/null 2>&1 || { \
		echo "  MISSING   python3"; \
		echo "            sudo dnf install python3 python3-pip   (Fedora/RHEL)"; \
		echo "            sudo apt install python3 python3-pip   (Debian/Ubuntu)"; \
		echo "            brew install python3                   (macOS)"; \
		exit 1; }
	python3 tools/check_deps.py
