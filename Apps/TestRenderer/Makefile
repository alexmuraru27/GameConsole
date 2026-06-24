# Apps live two levels under the repo root (Apps/<name>/), so point REPO_ROOT up
# two and express every shared path against it (see common.mk).
REPO_ROOT = ../..
include $(REPO_ROOT)/common.mk

######################################
# target
######################################
TARGET = TestRenderer

#######################################
# paths
#######################################
# Build path (kept at <root>/build/TestRenderer so the map-file tooling finds it).
BUILD_DIR = $(REPO_ROOT)/build/TestRenderer

OPT = -Og

######################################
# source
######################################
# A renderer benchmark game: its tile graphics ship in TestRenderer.pak and are
# streamed into RAM at init (see the asset pack section below).
C_SOURCES =  \
Src/main.c \
Src/syscalls.c \
$(REPO_ROOT)/Shared/Syscall/console_syscalls.c

# ASM sources
# None: a game has no vector table and no startup.s. The binary header is emitted
# by DECLARE_GAME_HEADER (main.c) and the C-runtime bootstrap lives in the shared
# console_syscalls.c, linked above.
ASM_SOURCES =

#######################################
# CFLAGS
#######################################
# C defines
C_DEFS =  \
-DSTM32F407xx

# C includes
C_INCLUDES =  \
-IInc \
-IAssets/pak \
-I$(REPO_ROOT)/Shared \
-I$(REPO_ROOT)/Shared/Api \
-I$(REPO_ROOT)/Shared/Syscall \
-I$(REPO_ROOT)/tools/graphics

CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

#######################################
# LDFLAGS
#######################################
# link script. The linker scripts live in linker/; app.ld pulls in common.ld via
# an INCLUDE resolved against the linker search path, so -L points ld at linker/.
LDSCRIPT = $(REPO_ROOT)/linker/app.ld

# libraries
LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) -L$(REPO_ROOT)/linker $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

#######################################
# asset pack (.pak)
#######################################
# The tile graphics are GfxAsset .bin files generated from ASCII art by
# Assets/gen_tiles.py (committed, like Pixel Forge output). The packer bundles
# them (per Assets/manifest.yaml) into TestRenderer.pak + a TestRendererAssetEnum.h
# of asset ids the game includes. The .pak ships next to TestRenderer.bin on the SD
# card and is streamed at runtime. Re-packed every build (FORCE) so an edited .bin
# is never skipped; the packer is cheap.
PACKER     = python3 $(REPO_ROOT)/tools/packer/packer.py
ASSET_DIR  = Assets
MANIFEST   = $(ASSET_DIR)/manifest.yaml
PAK_DIR    = $(ASSET_DIR)/pak
PAK        = $(PAK_DIR)/$(TARGET).pak
ASSET_ENUM = $(PAK_DIR)/$(TARGET)AssetEnum.h
ASSET_BINS = $(wildcard $(ASSET_DIR)/Graphics/*.bin)

$(ASSET_ENUM): $(PAK)
$(PAK): $(MANIFEST) $(ASSET_BINS) FORCE
	@mkdir -p $(PAK_DIR)
	$(PACKER) $(MANIFEST) --output-name $(TARGET) --output-dir $(PAK_DIR) --assets-root $(ASSET_DIR)

FORCE:
.PHONY: FORCE

pak: $(PAK)

all: $(BUILD_DIR) $(PAK) $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

#######################################
# build the application
#######################################
# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
# list of ASM program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

######################################
# targets
######################################
$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR) $(ASSET_ENUM)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@
$(BUILD_DIR)/%.o: %.S Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir -p $@


flash: $(BUILD_DIR)/$(TARGET).elf
	@echo "TestRenderer — nothing to flash -> .bin loaded at runtime"


deploy: $(BUILD_DIR)/$(TARGET).bin $(PAK)
	@mkdir -p $(UPDATE_SERVER_CONTENT)/games
	cp -f $(BUILD_DIR)/$(TARGET).bin $(PAK) $(UPDATE_SERVER_CONTENT)/games/
	@echo "$(TARGET).bin + $(TARGET).pak -> update server $(UPDATE_SERVER_CONTENT)/games"
#######################################
# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)
