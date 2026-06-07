SHELL := /bin/bash

IDF_EXPORT ?= $(HOME)/esp/v5.5-rc1/esp-idf/export.sh
IDF_TARGET ?= esp32

# Default mapping follows the feature/esp-now README:
# ESP1 = left half on /dev/ttyUSB0, ESP2 = right half on /dev/ttyUSB1.
ESP1_PORT ?= /dev/ttyUSB0
ESP2_PORT ?= /dev/ttyUSB1

ESP1_BUILD_DIR ?= build-esp1
ESP2_BUILD_DIR ?= build-esp2

ESP1_SDKCONFIG ?= sdkconfig.esp1
ESP2_SDKCONFIG ?= sdkconfig.esp2

ESP1_DEFAULTS ?= sdkconfig.defaults.esp1
ESP2_DEFAULTS ?= sdkconfig.defaults.esp2

ESP1_IDF := source "$(IDF_EXPORT)" && idf.py -B "$(ESP1_BUILD_DIR)" -DIDF_TARGET="$(IDF_TARGET)" -DSDKCONFIG="$(ESP1_SDKCONFIG)" -DSDKCONFIG_DEFAULTS="$(ESP1_DEFAULTS)"
ESP2_IDF := source "$(IDF_EXPORT)" && idf.py -B "$(ESP2_BUILD_DIR)" -DIDF_TARGET="$(IDF_TARGET)" -DSDKCONFIG="$(ESP2_SDKCONFIG)" -DSDKCONFIG_DEFAULTS="$(ESP2_DEFAULTS)"

.PHONY: help check-idf esp1 esp2 left right esp1-build esp2-build esp1-flash esp2-flash esp1-monitor esp2-monitor esp1-menuconfig esp2-menuconfig clean-esp1 clean-esp2

help:
	@printf "Targets:\n"
	@printf "  make esp1          Flash + monitor ESP1 (%s)\n" "$(ESP1_PORT)"
	@printf "  make esp2          Flash + monitor ESP2 (%s)\n" "$(ESP2_PORT)"
	@printf "  make left          Alias for esp1\n"
	@printf "  make right         Alias for esp2\n"
	@printf "  make esp1-build    Build ESP1 only\n"
	@printf "  make esp2-build    Build ESP2 only\n"
	@printf "  make esp1-flash    Flash ESP1 without monitor\n"
	@printf "  make esp2-flash    Flash ESP2 without monitor\n"
	@printf "  make esp1-monitor  Monitor ESP1 only\n"
	@printf "  make esp2-monitor  Monitor ESP2 only\n"
	@printf "\nOverride ports if needed: make esp1 ESP1_PORT=/dev/ttyACM0\n"

check-idf:
	@test -f "$(IDF_EXPORT)" || (printf "ESP-IDF export.sh not found: %s\n" "$(IDF_EXPORT)"; exit 1)

esp1: check-idf
	$(ESP1_IDF) -p "$(ESP1_PORT)" flash monitor

esp2: check-idf
	$(ESP2_IDF) -p "$(ESP2_PORT)" flash monitor

left: esp1

right: esp2

esp1-build: check-idf
	$(ESP1_IDF) build

esp2-build: check-idf
	$(ESP2_IDF) build

esp1-flash: check-idf
	$(ESP1_IDF) -p "$(ESP1_PORT)" flash

esp2-flash: check-idf
	$(ESP2_IDF) -p "$(ESP2_PORT)" flash

esp1-monitor: check-idf
	$(ESP1_IDF) -p "$(ESP1_PORT)" monitor

esp2-monitor: check-idf
	$(ESP2_IDF) -p "$(ESP2_PORT)" monitor

esp1-menuconfig: check-idf
	$(ESP1_IDF) menuconfig

esp2-menuconfig: check-idf
	$(ESP2_IDF) menuconfig

clean-esp1: check-idf
	$(ESP1_IDF) fullclean

clean-esp2: check-idf
	$(ESP2_IDF) fullclean
