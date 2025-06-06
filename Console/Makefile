include ../common.mk

######################################
# target
######################################
TARGET = GameConsole

#######################################
# paths
#######################################
# Build path
BUILD_DIR = ../build/Console

######################################
# source
######################################
# C sources
C_SOURCES =  \
Src/main.c \
Src/game_console.c \
Src/stm32f4xx_it.c \
Src/syscalls.c \
Src/Peripherals/sysclock.c \
Src/Peripherals/usart.c \
Src/Peripherals/gpio.c \
Src/Peripherals/dma.c \
Src/Peripherals/spi.c \
Src/Peripherals/timer.c \
Src/Peripherals/adc.c \
Src/Peripherals/sdio.c \
Src/Devices/ILI9341.c \
Src/Devices/joystick.c \
Src/Devices/buzzer.c \
Src/FatFs/diskio.c \
Src/FatFs/diskio_integration.c \
Src/FatFs/ff.c \
Src/FatFs/ffunicode.c \
Src/Test/test_buzzer.c \
Src/Test/test_joystick.c \
Src/Test/test_renderer.c \
Src/Test/test_usart.c \
Src/Test/test_console.c \
Src/Renderer/renderer.c

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
-IInc/Test \
-IInc/Peripherals \
-IInc/Devices \
-IInc/FatFs \
-IInc/Renderer \
-I../Tiles \
-I../Shared/Inc \
-I../Shared/Inc/TileUtils \
-I../Drivers/CMSIS/Device/ST/STM32F4xx/Include \
-I../Drivers/CMSIS/Include


# compile gcc flags
ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections

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
LDSCRIPT = console.ld

# libraries
LIBS = -lc -lm -lnosys 
LIBDIR = 
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# default action: build all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin


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
	$(OPENOCD) $(OPENOCD_CFG) -c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

#######################################
# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)