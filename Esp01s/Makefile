include ../common.mk

# PlatformIO CLI. Prefer the PlatformIO-managed core in its own virtualenv
# (~/.platformio/penv, installed by the VS Code extension / official installer):
# it bundles matching dependencies, so it can't break against system packages the
# way a distro-packaged `pio` on PATH can — e.g. PlatformIO 4.x calling Click's
# removed `resultcallback()`. Fall back to a `pio` on PATH. Override with PIO=<path>.
PIO ?= $(shell test -x $(HOME)/.platformio/penv/bin/pio && echo $(HOME)/.platformio/penv/bin/pio || command -v pio 2>/dev/null)

# PlatformIO build output for the [env:esp01s] environment.
FIRMWARE = .pio/build/esp01s/firmware.bin

# The repo's sub-makes are driven uniformly via `all`; `build` is kept as an
# alias (PlatformIO's vocabulary, and what `make esp` maps to).
all: build

# Build the ESP-01S firmware via PlatformIO.
build:
	$(PIO) run

# Copy the built ESP firmware into the update server as Firmware/ESP01.bin (the
# same category folder as the console OS image). Skips quietly if the firmware
# hasn't been built, so it never breaks `make deploy`.
deploy:
	@if [ -f $(FIRMWARE) ]; then \
		mkdir -p $(UPDATE_SERVER_CONTENT)/Firmware && \
		cp -f $(FIRMWARE) $(UPDATE_SERVER_CONTENT)/Firmware/ESP01.bin && \
		echo "ESP01.bin -> update server $(UPDATE_SERVER_CONTENT)/Firmware"; \
	else \
		echo "skip ESP01.bin: $(FIRMWARE) not built (run 'pio run' in Esp01s/ first)"; \
	fi
