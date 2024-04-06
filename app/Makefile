TARGET = App
DEBUG = 1
OPT = -Og -O0
BUILD_DIR = build
LDSCRIPT = app.ld

# binaries
######################################

CC = arm-none-eabi-gcc
AS = arm-none-eabi-gcc -x assembler-with-cpp
CP = arm-none-eabi-objcopy
SZ = arm-none-eabi-size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

# Project Sources
######################################
# C sources
C_SOURCES =  \
src/main.c \
src/stm32/system_stm32f4xx.c  \

# C includes
C_INCLUDES =  \
-Iincludes \
-I../drivers/CMSIS/Device/ST/STM32F4xx/Include \
-I../drivers/CMSIS/Include \
-I../shared_lib/api


# ASM sources
ASM_SOURCES =  \
appStartup.s
 
# CFLAGS
#######################################
# cpu
CPU = -mcpu=cortex-m4

# mcu
MCU = $(CPU) -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# C defines
C_DEFS =  \
-DSTM32F407xx

# compile gcc flags
ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections

CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -MMD -MP -MF"$(@:%.o=%.d)"

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

# LDFLAGS
#######################################
# libraries
LIBS = -lc -lm -lnosys 
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# default action: build all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# build the application
#######################################
# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
# list of ASM program objects
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

# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)

# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)
