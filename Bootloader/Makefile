include ../common.mk

######################################
# target
######################################
TARGET = Bootloader

#######################################
# paths
#######################################
BUILD_DIR = ../build/Bootloader

# Small and fast: the bootloader must fit sector 0 (16 KB).
OPT = -Os

######################################
# source
######################################
# The bootloader reuses the console's startup (vector table + .data/.bss init; the
# unused peripheral ISRs stay weak-aliased to the default handler) and shares the
# CRC and low-level flash driver with the console — no duplication.
C_SOURCES =  \
Src/main.c \
Src/trace.c \
../Console/Src/Crc/crc.c \
../Console/Src/Flasher/flash_ll.c \
../Console/Src/Logger/logger.c

ASM_SOURCES =  \
../Console/startup.s

#######################################
# CFLAGS
#######################################
C_DEFS = -DSTM32F407xx

C_INCLUDES =  \
-I../Console/Inc/Flasher \
-I../Console/Inc/Crc \
-I../Console/Inc/Logger \
-I../Console/Inc/Swo \
-I../Console/Inc/Peripherals \
-I../Console/Drivers/CMSIS/Device/ST/STM32F4xx/Include \
-I../Console/Drivers/CMSIS/Include

CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

#######################################
# LDFLAGS
#######################################
LDSCRIPT = bootloader.ld
LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

#######################################
# build
#######################################
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
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

# Flash just the bootloader (sector 0) over SWD. Programs only its own sections, so
# it leaves the app and staging regions untouched.
flash: $(BUILD_DIR)/$(TARGET).elf
	$(OPENOCD) $(OPENOCD_CFG) -c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
