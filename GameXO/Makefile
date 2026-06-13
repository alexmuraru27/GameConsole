include ../common.mk

######################################
# target
######################################
TARGET = GameXO

#######################################
# paths
#######################################
# Build path
BUILD_DIR = ../build/GameXO

OPT = -Og

######################################
# source
######################################
# C sources
C_SOURCES =  \
Src/main.c \
Src/game_state_manager.c \
Src/level_manager.c \
Src/tic_tac_toe_logic.c\
Src/sound.c\
Assets/Src/assets.c \
Src/syscalls.c

# ASM sources
ASM_SOURCES =  \
startup.s

#######################################
# CFLAGS
#######################################
# C defines
C_DEFS =  \
-DSTM32F407xx


# C includes
C_INCLUDES =  \
-IInc \
-IAssets/Inc \
-I../Assets/Tiles \
-I../Shared \
-I../Shared/Api \
-I../Shared/TileUtils


CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

#######################################
# LDFLAGS
#######################################
# link script
LDSCRIPT = ../game.ld

# libraries
LIBS = -lc -lm -lnosys 
LIBDIR = 
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections




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
	mkdir $@


flash: $(BUILD_DIR)/$(TARGET).elf
	@echo "GameXO — nothing to flash -> .bin loaded at runtime"


deploy: $(BUILD_DIR)/$(TARGET).bin
	cp -f $(BUILD_DIR)/$(TARGET).bin $(SD_CARD_PATH)/
	sync
	@echo "$(TARGET).bin copied to SD card $(SD_CARD_PATH)"
#######################################
# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)