include ../common.mk

# PlatformIO CLI. Prefer one on PATH, else fall back to the core the VS Code
# extension installs at ~/.platformio/penv/bin/pio. Override with PIO=<path>.
PIO ?= $(shell command -v pio 2>/dev/null || echo $(HOME)/.platformio/penv/bin/pio)

# PlatformIO build output for the [env:esp01s] environment.
FIRMWARE = .pio/build/esp01s/firmware.bin

# Build the ESP-01S firmware via PlatformIO.
build:
	$(PIO) run

# Copy the built ESP firmware into the update server as wifi/ESP01.bin. Skips
# quietly if the firmware hasn't been built, so it never breaks `make deployesp`.
deploy:
	@if [ -f $(FIRMWARE) ]; then \
		mkdir -p $(UPDATE_SERVER_CONTENT)/wifi && \
		cp -f $(FIRMWARE) $(UPDATE_SERVER_CONTENT)/wifi/ESP01.bin && \
		echo "ESP01.bin -> update server $(UPDATE_SERVER_CONTENT)/wifi"; \
	else \
		echo "skip ESP01.bin: $(FIRMWARE) not built (run 'pio run' in Esp01s/ first)"; \
	fi
