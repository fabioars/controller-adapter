# PS3 Xbox Adapter — setup, build and deploy.
#
#   make setup        install ESP-IDF (if missing) + fetch Bluepad32 and libxsm3
#   make build        compile the firmware
#   make deploy       build + flash + open serial monitor (ESP32-S3 auto-detected)
#   make help         list everything
#
# Overrides: make deploy PORT=/dev/cu.usbserial-1410  IDF_PATH=~/esp/esp-idf

IDF_VERSION ?= v5.5.1
IDF_PATH    ?= $(HOME)/esp/esp-idf
TARGET      := esp32s3

# Flash through the board's UART port (USB-serial bridge). The native USB port
# belongs to the PS3 once the firmware runs. Leave PORT empty to auto-detect:
# every USB serial port is probed and the one with an ESP32-S3 is used.
PORT ?=
PORT_FILE := build/.esp32s3-port

SHELL := /bin/bash
# Run idf.py inside the ESP-IDF environment (export.sh must be sourced from bash).
IDF_ENV := . "$(IDF_PATH)/export.sh" >/dev/null 2>&1 &&
IDF := $(IDF_ENV) idf.py

.DEFAULT_GOAL := help
.PHONY: help setup idf bluepad32 target build flash monitor deploy menuconfig test size erase clean fullclean check-port

help: ## Show this help
	@awk 'BEGIN {FS = ":.*## "} /^[a-z0-9-]+:.*## / {printf "  \033[1m%-12s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@echo
	@echo "  Serial port: $(if $(PORT),$(PORT),auto-detected (the port with an ESP32-S3))"

setup: idf bluepad32 target ## Install ESP-IDF if needed, fetch third-party code, set the chip target

idf: ## Install ESP-IDF (v5.5.1 by default) into IDF_PATH, skipped if present
	@if [ -f "$(IDF_PATH)/export.sh" ]; then \
		echo "ESP-IDF found at $(IDF_PATH)"; \
	else \
		echo "Installing ESP-IDF $(IDF_VERSION) into $(IDF_PATH) (takes a few minutes)..."; \
		mkdir -p "$(dir $(IDF_PATH))" && \
		git clone -b $(IDF_VERSION) --recursive --depth 1 --shallow-submodules \
			https://github.com/espressif/esp-idf.git "$(IDF_PATH)"; \
	fi
	@"$(IDF_PATH)/install.sh" $(TARGET) >/dev/null
	@# cmake/ninja are not always on the PATH (e.g. macOS without Homebrew)
	@. "$(IDF_PATH)/export.sh" >/dev/null 2>&1 && python "$(IDF_PATH)/tools/idf_tools.py" install cmake ninja >/dev/null
	@echo "ESP-IDF ready"

bluepad32: ## Fetch Bluepad32 + BTstack and libxsm3 into third_party/
	@./scripts/setup.sh

target: ## Configure the project for the ESP32-S3
	@if ! grep -qs 'CONFIG_IDF_TARGET="$(TARGET)"' sdkconfig; then $(IDF) set-target $(TARGET); fi

build: ## Compile the firmware
	@test -d third_party/bluepad32 || { echo "Run 'make setup' first"; exit 1; }
	@$(IDF) build

# Probes every USB serial port and remembers the one with an ESP32-S3.
check-port:
	@echo "Looking for an ESP32-S3..."
	@mkdir -p build
	@port=$$($(IDF_ENV) ./scripts/find_port.sh "$(PORT)") || exit 1; \
	echo "$$port" > $(PORT_FILE); \
	echo "Using $$port"

flash: check-port build ## Build and flash (ESP32-S3 auto-detected)
	@$(IDF) -p $$(cat $(PORT_FILE)) flash

monitor: check-port ## Open the serial log (Ctrl+] to quit)
	@$(IDF) -p $$(cat $(PORT_FILE)) monitor

deploy: check-port build ## Build, flash, then open the serial monitor
	@$(IDF) -p $$(cat $(PORT_FILE)) flash monitor

menuconfig: ## Change settings (Wi-Fi name/password under "PS3 Xbox Adapter")
	@$(IDF) menuconfig

test: ## Run host unit tests (no board needed)
	@./test/run.sh

size: ## Show firmware memory usage
	@$(IDF) size

erase: check-port ## Erase the whole flash, including paired controllers
	@$(IDF) -p $$(cat $(PORT_FILE)) erase-flash

clean: ## Remove build outputs
	@$(IDF) clean

fullclean: ## Remove build dir and sdkconfig (re-run 'make target' after)
	@rm -rf build sdkconfig sdkconfig.old
