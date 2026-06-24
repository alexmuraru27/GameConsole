# Apps live two levels under the repo root (Apps/<name>/), so point REPO_ROOT up
# two and express every shared path against it (see common.mk).
REPO_ROOT = ../..
include $(REPO_ROOT)/common.mk

######################################
# target
######################################
TARGET = GameXO

#######################################
# paths
#######################################
# Build path (kept at <root>/build/GameXO so the map-file tooling and the VS Code
# launch config find it regardless of where the sources live).
BUILD_DIR = $(REPO_ROOT)/build/GameXO

OPT = -Og

######################################
# source
######################################
# C sources
C_SOURCES =  \
Src/main.c \
Src/game_state_manager.c \
Src/tic_tac_toe_logic.c\
Src/game_assets.c \
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
-I$(REPO_ROOT)/tools/graphics \
-I$(REPO_ROOT)/tools/music_creator


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
# The game's assets are authored with the in-repo tools and committed under
# Assets/{Graphics,Music}; the packer bundles them (per Assets/manifest.yaml)
# into GameXO.pak + a GameXOAssetEnum.h of asset ids the game includes. The
# .pak ships next to GameXO.bin on the SD card and is loaded at runtime.
PACKER     = python3 $(REPO_ROOT)/tools/packer/packer.py
ASSET_DIR  = Assets
MANIFEST   = $(ASSET_DIR)/manifest.yaml
PAK_DIR    = $(ASSET_DIR)/pak
PAK        = $(PAK_DIR)/$(TARGET).pak
ASSET_ENUM = $(PAK_DIR)/$(TARGET)AssetEnum.h
ASSET_BINS = $(wildcard $(ASSET_DIR)/Graphics/*.bin $(ASSET_DIR)/Music/*.bin)

# The enum header is a by-product of the same packer run as the .pak. The .pak
# is re-packed on every build via the FORCE prerequisite — assets are edited in
# place under Assets/, so we never want make to skip the packer just because the
# committed .pak looks newer than its inputs. FORCE has no recipe and no matching
# file, so it is always considered out of date. The packer is cheap; re-packing
# rewrites GameXOAssetEnum.h, so the lone source that includes it recompiles and
# the game relinks each build, but unrelated objects are untouched.
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
	@echo "GameXO — nothing to flash -> .bin loaded at runtime"


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