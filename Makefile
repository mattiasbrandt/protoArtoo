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

# Only one PlatformIO build may run on this machine at a time (AGENTS.md "The
# build lock"). Every pio invocation below goes through the lock rather than
# relying on the caller to type `flock` in front of make, and tools/pio_lock.py
# holds the lock for exactly as long as the command it execs. Deliberately not
# `?=`: a lock that is one `make FLOCK= build` away from being off is a habit
# again. The escape hatch for a contiguous multi-command window is
# `PROTOARTOO_PIO_LOCK_HELD=1 flock /tmp/protoartoo-pio.lock <commands>`.
FLOCK := python3 tools/pio_lock.py

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
	$(FLOCK) pio run -e $(BUILD_ENV)

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
	$(FLOCK) pio check -e protoArtoo

check-action-drift: ## Ad hoc check that action YAML, C++, and RC fallback metadata align
	python3 tools/check_action_registry_drift.py

# ── Flash: DY-SV5W (default) ─────────────────────────────────────────────────

flash: test ## Flash via USB  (UPLOAD_PORT=/dev/ttyUSB0)
	$(FLOCK) pio run -e $(BUILD_ENV) -t upload --upload-port $(UPLOAD_PORT)

ota: test ## Flash via OTA  (OTA_IP=artoo.local by default)
	$(FLOCK) pio run -e $(BUILD_ENV)_ota
	python3 tools/ota_upload.py --env $(BUILD_ENV)_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

uploadfs: ## Upload LittleFS web UI via OTA  (no test gate)
	$(FLOCK) pio run -e $(BUILD_ENV)_ota -t uploadfs --upload-port $(OTA_IP)

# ── Flash: CHIRP audio module ────────────────────────────────────────────────

flash-chirp: test ## Flash CHIRP build via USB
	$(FLOCK) pio run -e protoArtoo_chirp -t upload --upload-port $(UPLOAD_PORT)

flash-monitor: test ## Flash default build via USB then capture boot log
	$(FLOCK) pio run -e $(BUILD_ENV) -t upload --upload-port $(UPLOAD_PORT)
	python3 tools/serial_monitor.py --until "init complete" --timeout 30

flash-chirp-monitor: test ## Flash CHIRP build via USB then capture boot log
	$(FLOCK) pio run -e protoArtoo_chirp -t upload --upload-port $(UPLOAD_PORT)
	python3 tools/serial_monitor.py --until "init complete" --timeout 30

ota-chirp: test ## Flash CHIRP build via OTA
	$(FLOCK) pio run -e protoArtoo_chirp_ota
	python3 tools/ota_upload.py --env protoArtoo_chirp_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

# ── Flash: MP3 Trigger ───────────────────────────────────────────────────────

ota-mp3trigger: test ## Flash MP3 Trigger build via OTA
	$(FLOCK) pio run -e protoArtoo_mp3trigger_ota
	python3 tools/ota_upload.py --env protoArtoo_mp3trigger_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

# ── Flash: DY-SV5W (named env) ───────────────────────────────────────────────
# Same driver as the default protoArtoo env — these targets exist so DY-SV5W
# has the same explicit, discoverable build/flash surface as CHIRP and MP3 Trigger.

flash-dysv5w: test ## Flash DY-SV5W build via USB
	$(FLOCK) pio run -e protoArtoo_dysv5w -t upload --upload-port $(UPLOAD_PORT)

ota-dysv5w: test ## Flash DY-SV5W build via OTA
	$(FLOCK) pio run -e protoArtoo_dysv5w_ota
	python3 tools/ota_upload.py --env protoArtoo_dysv5w_ota --host $(OTA_IP) --timeout $(OTA_TIMEOUT) --transfer-timeout $(OTA_TRANSFER_TIMEOUT)

# ── Compile-check only ───────────────────────────────────────────────────────

check-chirp: ## Compile-check CHIRP backend  (no flash)
	$(FLOCK) pio run -e protoArtoo_chirp_check

check-mp3trigger: ## Compile-check MP3Trigger backend  (no flash)
	$(FLOCK) pio run -e protoArtoo_mp3trigger_check

# ── Setup & tools ────────────────────────────────────────────────────────────

setup: ## First-time setup wizard  (writes user.mk)
	python3 tools/configure.py

setup-wifi: ## Configure WiFi credentials  (writes src/secrets.h)
	python3 tools/configure.py --wifi

clean: ## Remove PlatformIO build artifacts
	$(FLOCK) pio run -t clean

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
