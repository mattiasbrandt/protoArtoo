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

-include user.mk

.PHONY: all help build test check flash ota uploadfs \
        flash-chirp ota-chirp ota-mp3trigger \
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
	pio run -e $(BUILD_ENV)

test: ## Run native unit tests
	pio test -e native

check: ## Static analysis with cppcheck
	pio check -e protoArtoo

# ── Flash: DY-SV5W (default) ─────────────────────────────────────────────────

flash: test ## Flash via USB  (UPLOAD_PORT=/dev/ttyUSB0)
	pio run -e $(BUILD_ENV) -t upload --upload-port $(UPLOAD_PORT)

ota: test ## Flash via OTA  (OTA_IP=artoo.local by default)
	pio run -e $(BUILD_ENV)_ota -t upload --upload-port $(OTA_IP)

uploadfs: ## Upload LittleFS web UI via OTA  (no test gate)
	pio run -e $(BUILD_ENV)_ota -t uploadfs --upload-port $(OTA_IP)

# ── Flash: CHIRP audio module ────────────────────────────────────────────────

flash-chirp: test ## Flash CHIRP build via USB
	pio run -e protoArtoo_chirp -t upload --upload-port $(UPLOAD_PORT)

ota-chirp: test ## Flash CHIRP build via OTA
	pio run -e protoArtoo_chirp_ota -t upload --upload-port $(OTA_IP)

# ── Flash: MP3 Trigger ───────────────────────────────────────────────────────

ota-mp3trigger: test ## Flash MP3 Trigger build via OTA
	pio run -e protoArtoo_mp3trigger_ota -t upload --upload-port $(OTA_IP)

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
	pio run -t clean

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
