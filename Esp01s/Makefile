include ../common.mk

# PlatformIO CLI. Prefer one on PATH, else fall back to the core the VS Code
# extension installs at ~/.platformio/penv/bin/pio. Override with PIO=<path>.
PIO ?= $(shell command -v pio 2>/dev/null || echo $(HOME)/.platformio/penv/bin/pio)

# PlatformIO build output for the [env:esp01s] environment.
FIRMWARE = .pio/build/esp01s/firmware.bin

# Build the ESP-01S firmware via PlatformIO.
build:
	$(PIO) run

# Copy the built ESP firmware onto the SD card as ESP01.bin for the console's
# "Upgrade WiFi module" flow. Skips quietly if the firmware hasn't been built,
# so it never breaks the top-level `make deploy`.
deploy:
	@if [ -f $(FIRMWARE) ]; then \
		cp -f $(FIRMWARE) $(SD_CARD_PATH)/ESP01.bin && sync && \
		echo "ESP01.bin copied to SD card $(SD_CARD_PATH)"; \
	else \
		echo "skip ESP01.bin: $(FIRMWARE) not built (run 'pio run' in Esp01s/ first)"; \
	fi
