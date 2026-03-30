# =============================================================================
# protoArtoo — build facade
#
# All targets delegate to PlatformIO (pio) or project tools.
# No build logic lives here — this is a discoverable command surface only.
#
# Variables: override in user.mk (gitignored) or on the command line.
#   make ota OTA_IP=192.168.4.1
#
# First-time setup: make setup (local build vars) + make setup-wifi (credentials)
# =============================================================================

OTA_IP      ?= 10.0.0.22
BUILD_ENV   ?= protoArtoo
UPLOAD_PORT ?= /dev/ttyUSB0

-include user.mk

.PHONY: help build test check check-s3 all flash ota uploadfs check-chirp check-mp3trigger setup setup-wifi clean monitor

help: ## Show available targets
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  %-22s %s\n", $$1, $$2}'

build: ## Compile firmware  (BUILD_ENV=protoArtoo by default)
	pio run -e $(BUILD_ENV)

test: ## Run native unit tests  (required gate before any upload)
	pio test -e native

check: ## Static analysis with cppcheck
	pio check -e protoArtoo

check-s3: ## Static analysis for S3 Mini build  (checks S3-specific GPIO overrides)
	pio check -e protoArtoo_s3

all: test build ## Run tests then build  (full pre-upload validation sequence)

flash: test ## Flash via USB  (ESP32 MUST be unseated — see warning printed below)
	@echo ""
	@echo "WARNING: ESP32 must be UNSEATED from the Artoo PCB before USB flash."
	@echo "         GPIO15 (SBUS receiver) is a strapping pin that blocks bootloader download mode."
	@echo ""
	pio run -e $(BUILD_ENV) -t upload --upload-port $(UPLOAD_PORT)

ota: test ## Flash via OTA  (OTA_IP=10.0.0.22 by default; override on CLI)
	pio run -e $(BUILD_ENV)_ota -t upload --upload-port $(OTA_IP)

uploadfs: ## Upload LittleFS filesystem via OTA  (no test gate — filesystem only)
	pio run -e $(BUILD_ENV)_ota -t uploadfs --upload-port $(OTA_IP)

check-chirp: ## Compile-check CHIRP audio backend  (no flash)
	pio run -e protoArtoo_chirp_check

check-mp3trigger: ## Compile-check MP3Trigger audio backend  (no flash)
	pio run -e protoArtoo_mp3trigger_check

setup: ## Run first-time setup wizard  (writes user.mk)
	python3 tools/configure.py

setup-wifi: ## Configure WiFi credentials securely  (writes src/secrets.h)
	python3 tools/configure.py --wifi


clean: ## Remove PlatformIO build artifacts
	pio run -t clean

monitor: ## Open serial monitor  (does not reset the ESP32)
	python3 tools/serial_monitor.py
