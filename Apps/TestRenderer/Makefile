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
# A renderer benchmark game: no .pak, it builds its tile art procedurally at init.
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
-I$(REPO_ROOT)/Shared \
-I$(REPO_ROOT)/Shared/Api \
-I$(REPO_ROOT)/Shared/Syscall

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

all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

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
$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
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


deploy: $(BUILD_DIR)/$(TARGET).bin
	@mkdir -p $(UPDATE_SERVER_CONTENT)/games
	cp -f $(BUILD_DIR)/$(TARGET).bin $(UPDATE_SERVER_CONTENT)/games/
	@echo "$(TARGET).bin -> update server $(UPDATE_SERVER_CONTENT)/games"
#######################################
# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)
